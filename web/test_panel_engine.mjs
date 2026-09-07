#!/usr/bin/env node
/* test_panel_engine.mjs — EVERY panel control driven through the panel brain into the REAL
 * engine (plugin/tests/engine_host, the hosted src/ engine), asserting the engine's own state
 * afterwards: get_params / snapbuf_get / morph_state / matrix_dump / the status struct / the
 * console. "The brain emitted a message" is not enough — the engine must accept it and show the
 * promised effect. Any engine error line during a case fails that case.
 *
 *   make -C plugin/tests host && node web/test_panel_engine.mjs
 */
import { spawn, execSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { createInterface } from 'node:readline';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..');
const HOST = process.env.LIGASE_ENGINE_HOST || join(ROOT, 'plugin', 'tests', 'build', 'engine_host');
if (!existsSync(join(HERE, 'ligase_controls.js'))) execSync('python3 docs/ui/emit_web.py', { cwd: ROOT, stdio: 'ignore' });
if (!existsSync(HOST)) execSync('make -C plugin/tests host', { cwd: ROOT, stdio: 'inherit' });
const { createPanel } = await import('./ligase_panel_logic.js');
const { CONTROLS, TABLES, CONTROL_BY_ID, sendDefaults } = await import('./ligase_controls.js');

// ---- the engine host (line protocol) ---------------------------------------------------------
class Engine {
  constructor() {
    this.p = spawn(HOST, ['44100', ROOT], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.rl = createInterface({ input: this.p.stdout });
    this.buf = []; this.waiters = []; this.listeners = { out9: [], print: [], status: [] };
    this.errs = []; this.prints = []; this.out9 = [];
    this.rl.on('line', (line) => {
      if (line === 'ok') { const w = this.waiters.shift(); const b = this.buf; this.buf = []; if (w) w(b); return; }
      this.buf.push(line);
      if (line.startsWith('err ')) { this.errs.push(line.slice(4)); this.prints.push(line.slice(4)); for (const cb of this.listeners.print) cb(line.slice(4), true); }
      else if (line.startsWith('print ')) { this.prints.push(line.slice(6)); for (const cb of this.listeners.print) cb(line.slice(6), false); }
      else if (line.startsWith('out9 ')) {
        const t = line.slice(5).split(' '); const sel = t.shift();
        const args = t.map((x) => (x !== '' && !isNaN(x) ? +x : x));
        this.out9.push([sel, args]); for (const cb of this.listeners.out9) cb(sel, args);
      }
    });
  }
  cmd(text) { return new Promise((res) => { this.waiters.push(res); this.p.stdin.write(text + '\n'); }); }
  async msg(text) { return this.cmd('msg ' + text.replace(/\n/g, ';')); }
  async cv(i, v) { return this.cmd(`cv ${i} ${v}`); }
  async run(n = 8, noise = 0) { return this.cmd(`run ${n} ${noise}`); }
  async status() { const l = await this.cmd('status'); const s = l.find((x) => x.startsWith('status ')); const st = JSON.parse(s.slice(7));
    const mask = []; for (let i = 0; i < 32; i++) mask.push((st.snapshotMaskLo >>> i) & 1); for (let i = 0; i < 32; i++) mask.push((st.snapshotMaskHi >>> i) & 1);
    st.snapshotMask = mask; for (const cb of this.listeners.status) cb(st); return st; }
  async params() { this.out9 = []; await this.msg('get_params'); const m = {}; for (const [s, a] of this.out9) m[s] = a.length === 1 ? a[0] : a; return m; }
  async snap(field) { this.out9 = []; await this.msg(`snapbuf_from_live; snapbuf_get ${field}`); const r = this.out9.find((o) => o[0] === 'snapbuf' && o[1][0] === field); return r ? r[1].slice(1) : null; }
  async snapbuf(field) { this.out9 = []; await this.msg(`snapbuf_get ${field}`); const r = this.out9.find((o) => o[0] === 'snapbuf' && o[1][0] === field); return r ? r[1].slice(1) : null; }
  async morphState() { this.out9 = []; await this.msg('morph_state'); return this.out9.slice(); }
  async matrixDump() { const n0 = this.prints.length; await this.msg('matrix_dump'); return this.prints.slice(n0); }
  quit() { this.p.stdin.write('quit\n'); }
}

// ---- bridge: the brain's control/msg calls are queued and flushed into the engine ----------
const eng = new Engine();
const queue = [];
const bridge = {
  control(id, value, text) {
    // the software-host contract: a knob's message twin is delivered as a message; a CV-only
    // inlet (text null) drives its signal inlet
    const c = CONTROL_BY_ID[id];
    if (text) queue.push(['msg', text]);
    else if (c && Array.isArray(c.bind) && c.bind[0] === 'inlet') queue.push(['cv', c.bind[1] - 1, value]);
  },
  msg(text) { queue.push(['msg', text]); },
  note(ch, note, vel) { queue.push(['msg', `midi ${note} ${vel} ${ch}`]); },
  on(ev, cb) { if (eng.listeners[ev]) eng.listeners[ev].push(cb); return () => {}; },
};
async function flush() { while (queue.length) { const q = queue.shift(); if (q[0] === 'cv') await eng.cv(q[1], q[2]); else await eng.msg(q[1]); } }
const surface = createPanel(bridge, CONTROLS, TABLES);
const set = async (id, v) => { surface.set(id, v); await flush(); };
const bang = async (id) => { surface.bang(id); await flush(); };
const near = (a, b, tol = 1e-3) => Math.abs(a - b) <= tol * Math.max(1, Math.abs(b));

// ---- runner ------------------------------------------------------------------------------------
const results = []; let section = '';
const sec = (name) => { section = name; };
async function it(name, fn) {
  const e0 = eng.errs.length;
  try {
    const info = await fn();
    const errs = eng.errs.slice(e0);
    if (errs.length) results.push(['FAIL', section, name, 'engine error: ' + errs.join(' | ')]);
    else results.push(['PASS', section, name, info || '']);
  } catch (err) { results.push(['FAIL', section, name, String(err.message || err)]); }
}
const expect = (cond, msg) => { if (!cond) throw new Error(msg); };

// =============================================================================================
sec('0 load contract');
await it('defaults broadcast accepted by the engine (no errors)', async () => {
  await eng.msg('headless 1; scope_tap lorenz 1; morph_cursor 0');   // plugin/web applyDefaults contract
  sendDefaults(surface); await flush(); await eng.run(4);
  return 'sent ' + CONTROLS.filter((c) => c.initSend && !['button', 'led', 'readout', 'joypad'].includes(c.kind)).length + ' defaults';
});

sec('A granular engine (inlets)');
const INLET_CASES = [['grainsize', 0.25, 'grainsize'], ['start', 0.3, 'grainstart'], ['speed', 1.5, 'speed'], ['density', 0.2, 'iot'],
  ['voices', 40, 'maxgrains'], ['level', 0.8, 'amplitude'], ['sos', 0.4, 'sos'], ['scan', 2.5, 'scanrate'],
  ['dly_time', 0.35, 'gdelay'], ['dly_feed', 0.45, 'gdelay_feed'], ['dly_tone', 0.7, 'gdelay_tone'], ['dly_mix', 0.3, 'gdelay_mix'],
  ['smr_mix', 0.2, 'smear'], ['cutoff', 1500, 'moog_cutoff'], ['resonance', 1.2, 'moog_resonance'], ['flt_mix', 0.6, 'moog_mix'],
  ['skew', 0.3, 'env_skew'], ['pan', 0.7, 'pan'], ['midi_note', 67, 'midi']];
for (const [id, val, sel] of INLET_CASES) {
  await it(`${id} -> inlet -> get_params ${sel} = ${val}`, async () => {
    await set(id, val); await eng.run(30); const p = await eng.params();
    expect(sel in p, `no ${sel} in get_params`); expect(near(p[sel], val, 1e-3), `${sel} = ${p[sel]}`);
  });
}
await it('organize -> `organize 0.6` accepted (splice navigation; the readback is inlet-only)', async () => { await set('organize', 0.6); });
await it('joy_x / joy_y -> morph cursor (`morph x y`, the message cursor)', async () => {
  await set('joy_x', 0.8); await set('joy_y', 0.2); await eng.run(4); const st = await eng.status();
  expect(near(st.morph.x, 0.8, 1e-2) && near(st.morph.y, 0.2, 1e-2), `cursor ${st.morph.x},${st.morph.y}`);
});

sec('B tape reel / record');
await it('REC MODE INPUT + RECORD on -> recinput (recording, mode INPUT_ONLY)', async () => {
  await set('recmode', 0); await set('record', 1); await eng.run(40, 1); const st = await eng.status();
  expect(st.recording === 1 && st.recMode === 2, `recording=${st.recording} mode=${st.recMode}`);
});
await it('RECORD off -> record 0, reel holds the take, a splice exists', async () => {
  await set('record', 0); await eng.run(2); const st = await eng.status();
  expect(st.recording === 0 && st.reelLen > 2000 && st.splices >= 1, JSON.stringify(st)); return `reel ${st.reelLen} samples, ${st.splices} splice(s)`;
});
await it('REC MODE SPLICE + RECORD -> recsplice (NEW_SPLICE), second take -> 2 splices', async () => {
  await set('recmode', 1); await set('record', 1); await eng.run(40, 1); const on = await eng.status();
  await set('record', 0); await eng.run(2); const st = await eng.status();
  expect(on.recMode === 1 && st.splices >= 2, `mode ${on.recMode} splices ${st.splices}`); return `${st.splices} splices`;
});
await it('REC MODE OVRDUB + RECORD -> record 1 (OVERDUB)', async () => {
  await set('recmode', 2); await set('record', 1); await eng.run(10, 1); const on = await eng.status(); await set('record', 0);
  expect(on.recMode === 0 && on.recording === 1, `mode ${on.recMode}`);
});
await it('PLAY toggle -> play 1 / play 0', async () => {
  await set('play', 1); await eng.run(2); const a = await eng.status(); await set('play', 0); await eng.run(2); const b = await eng.status();
  expect(a.playing === 1 && b.playing === 0, `${a.playing}/${b.playing}`);
});
await it('LOOP toggle -> loop 0 / loop 1 accepted', async () => { await set('loop', 0); await set('loop', 1); });
await it('playback audible after PLAY 1 (grains from the recorded reel)', async () => {
  // density 0.2 s and 0.25 s grains: measure over ~0.6 s after a settle, not a single block
  await set('play', 1); await set('level', 1.0); await set('dist_on', 0); await eng.run(200);
  const lines = await eng.run(400); const rms = +(lines.find((l) => l.startsWith('rms')) || 'rms 0').split(' ')[1];
  expect(rms > 0.005, `rms ${rms}`); return `rms ${rms.toFixed(3)}`;
});

sec('C playhead / splice select');
for (const [pos, mode] of [[0, 0], [1, 1], [2, 2]]) {
  await it(`MODE switch ${pos} -> playhead ${pos + 1} (engine mode ${mode})`, async () => { await set('playhead', pos); await eng.run(2); const st = await eng.status(); expect(st.playheadMode === mode, `mode ${st.playheadMode}`); });
}
await it('GRID knob 37 -> quantize 32 (power-of-two snap)', async () => { await set('quantize', 37); const v = await eng.snap('quantize'); expect(v && v[0] === 32, `quantize ${v}`); });
await it('GRID knob 0.2 -> sends nothing (grid untouched)', async () => { const q0 = eng.prints.length; await set('quantize', 0.2); expect(eng.prints.length === q0, 'something was sent'); });
await it('AMOUNT -> quant', async () => { await set('quant', 0.35); const v = await eng.snap('quant'); expect(v && near(v[0], 0.35), `quant ${v}`); });
await it('CLK-ADV QUANT toggle -> clock_advance_quant', async () => { await set('clkadv', 1); const v = await eng.snap('clock_advance_quant'); expect(v && v[0] === 1, `${v}`); await set('clkadv', 0); });
await it('splice ▶ / ◀ -> shift ±1 moves the current splice', async () => {
  const s0 = (await eng.status()).splice; await bang('splice_next'); await eng.run(2); const s1 = (await eng.status()).splice;
  await bang('splice_prev'); await eng.run(2); const s2 = (await eng.status()).splice;
  expect(s1 !== s0 && s2 === s0, `${s0} -> ${s1} -> ${s2}`); return `${s0} -> ${s1} -> ${s2}`;
});
await it('DATA + ENTER -> jump to splice N (via the live index)', async () => {
  const st = await eng.status(); const target = (st.splice + 1) % st.splices;
  await set('splice_data', target); await bang('splice_enter'); await eng.run(2); const s = await eng.status();
  expect(s.splice === target, `splice ${s.splice} want ${target}`); expect(surface.getDisplay('splice_led') === target, `LED ${surface.getDisplay('splice_led')}`);
});

sec('D grain delay');
for (const [pos] of [[1], [2], [0]]) await it(`delay MODE ${pos} -> delay_mode ${pos}`, async () => { await set('delay_mode', pos); await eng.run(2); const st = await eng.status(); expect(st.delayMode === pos, `${st.delayMode}`); });
await it('delay GRID 90 -> delay_quantize 64', async () => { await set('delay_quantize', 90); const v = await eng.snap('delay_quantize'); expect(v && v[0] === 64, `${v}`); });
await it('delay AMOUNT -> delay_quant', async () => { await set('delay_quant', 0.55); const v = await eng.snap('delay_quant'); expect(v && near(v[0], 0.55), `${v}`); });
await it('GLIDE -> delay_glide accepted', async () => { await set('delay_glide', 120); });
await it('STUT! -> stut trigger accepted (stut mode)', async () => { await set('delay_mode', 2); await bang('stut_bang'); await eng.run(4); await set('delay_mode', 0); });

sec('E ladder filter + smear');
await it('FREQ -> smear_frequency', async () => { await set('smr_freq', 880); const v = await eng.snap('smear_frequency'); expect(v && near(v[0], 880), `${v}`); });
await it('RESON -> smear_resonance', async () => { await set('smr_res', 0.5); const v = await eng.snap('smear_resonance'); expect(v && near(v[0], 0.5), `${v}`); });
await it('STAGES -> smear_stages', async () => { await set('smr_stages', 16); const v = await eng.snap('smear_stages'); expect(v && near(v[0], 16), `${v}`); });
await it('FDBK -> smear_feedback', async () => { await set('smr_fdbk', -0.3); const v = await eng.snap('smear_feedback'); expect(v && near(v[0], -0.3), `${v}`); });
await it('smear MODE -> smear_mode 1 / 0', async () => { await set('smr_mode', 1); await eng.run(2); const a = await eng.status(); await set('smr_mode', 0); expect(a.smearMode === 1, `${a.smearMode}`); });
await it('BANK MIX -> smear_bank_mix accepted', async () => { await set('bank_mix', 0.4); });

sec('F envelope + pitch/midi');
for (let i = 0; i < 5; i++) await it(`TYPE ${i} -> envelope ${i} accepted`, async () => { const n0 = eng.prints.length; await set('env_type', i); expect(eng.prints.slice(n0).some((p) => /envelope set to/.test(p)), 'no confirmation'); });
await it('SAW CYC / SAW DEP -> saw_cycles / saw_depth', async () => { await set('saw_cycles', 8); await set('saw_depth', 0.6); const a = await eng.snap('saw_cycles'), b = await eng.snap('saw_depth'); expect(a && a[0] === 8 && b && near(b[0], 0.6), `${a} ${b}`); });
for (const [pos, mode] of [[0, 0], [1, 1], [2, 3], [4, 5], [3, 4]]) await it(`PITCH MODE ${pos} -> pitch_mode ${mode}`, async () => { await set('pitch_mode', pos); const st = await eng.status(); expect(st.pitchMode === mode, `${st.pitchMode}`); });
await it('FINE 25 -> pitch_fine 0.25 semitone', async () => { await set('pitch_fine', 25); const v = await eng.snap('pitch_fine'); expect(v && near(v[0], 0.25), `${v}`); });
await it('POLY toggle -> poly', async () => { await set('poly', 0); const a = await eng.status(); await set('poly', 1); const b = await eng.status(); expect(a.poly === 0 && b.poly === 1, `${a.poly}/${b.poly}`); });
await it('CHORD button -> a real triad in the voice pool (chord 60 64 67)', async () => { await bang('chord'); const st = await eng.status(); expect(st.voices === 3, `voices ${st.voices}`); return `voices ${st.voices}`; });
await it('note on/off through the bridge -> voice pool', async () => { surface.note(1, 72, 100); await flush(); const a = await eng.status(); surface.note(1, 72, 0); await flush(); const b = await eng.status(); expect(a.voices === 4 && b.voices === 3, `${a.voices}/${b.voices}`); });

sec('G distortion + output/space');
await it('DIST ON/OFF toggle', async () => { await set('dist_on', 1); await set('dist_on', 0); });
await it('EMPHASIS switch', async () => { await set('dist_emph', 0); await set('dist_emph', 1); });
for (let k = 1; k <= 8; k++) await it(`PRESET ${k} -> bundle accepted`, async () => { await set('dist_preset', k); });
for (const [pos] of [[0], [1], [2]]) await it(`PAN MODE ${pos} -> pan_mode`, async () => { await set('pan_mode', pos); const v = await eng.snap('pan_mode'); expect(v && v[0] === pos, `${v}`); });
await it('WIDTH -> spatial_width accepted', async () => { await set('spatial_width', 0.5); });
await it('SOURCE SPHERE / NBODY -> spatial', async () => { await set('spatial_src', 1); await set('spatial_src', 0); });

sec('H presets / snapshots');
await it('slot 3 + STORE -> snapshot 2 held (engine mask)', async () => { await bang('snap3'); await bang('snap_store'); const st = await eng.status(); expect(st.snapshotMask[2] === 1, 'mask bit 2 clear'); });
await it('slot 3 + RECALL -> snapshot_recall 2 accepted', async () => { await bang('snap_recall'); });
await it('slot 12 + STORE -> mask bit 11', async () => { await bang('snap12'); await bang('snap_store'); const st = await eng.status(); expect(st.snapshotMask[11] === 1 && st.snapshotMask[2] === 1, 'mask'); });

sec('MATRIX');
await it('pin on (LRNZ1 -> CUTOFF) -> matrix_connect depth 1', async () => { await set('mx_5_4', 1); const d = await eng.matrixDump(); expect(d.some((l) => /lorenz1 -> moog_cutoff depth 1\.0/.test(l)), d.join('|')); });
await it('DEPTH 2 + POL - then pin on (SIN1 -> DLY TIME) -> depth -2', async () => { await set('mx_depth', 2); await set('mx_pol', 1); await set('mx_0_0', 1); const d = await eng.matrixDump(); expect(d.some((l) => /sine1 -> gdelay depth -2\.0/.test(l)), d.join('|')); await set('mx_pol', 0); await set('mx_depth', 1); });
await it('pin off -> matrix_disconnect removes the routing', async () => { await set('mx_5_4', 0); const d = await eng.matrixDump(); expect(!d.some((l) => /lorenz1 -> moog_cutoff/.test(l) && !/disabled|inert/.test(l)) || d.some((l) => /lorenz1 -> moog_cutoff.*(disabled|inert)/.test(l)), d.join('|')); });
await it('every source x destination name is accepted by the engine (352 pins on/off)', async () => {
  let n = 0;
  for (const c of CONTROLS) if (c.kind === 'mxpin') { await set(c.id, 1); await set(c.id, 0); n++; }
  return `${n} pins`;
});

sec('SOURCE SHAPE');
const shapeReads = [
  ['LRNZ inst 2: A -> lorenz_sigma_2', 4, 1, 'shape_a', 0.5, 'lorenz_sigma_2', 0.1 + 0.5 * 19.9],
  ['LRNZ inst 2: B -> lorenz_rho_2', 4, 1, 'shape_b', 0.25, 'lorenz_rho_2', 0.1 + 0.25 * 55.9],
  ['LRNZ inst 2: C -> lorenz_beta_2', 4, 1, 'shape_c', 0.5, 'lorenz_beta_2', 0.1 + 0.5 * 7.9],
  ['NBDY inst 1: A -> nbody_G_1', 5, 0, 'shape_a', 0.2, 'nbody_G_1', 0.01 + 0.2 * 4.99],
  ['NBDY inst 1: B -> nbody_damping_1', 5, 0, 'shape_b', 0.3, 'nbody_damping_1', 0.3],
  ['NBDY inst 1: C -> nbody_epsilon_1', 5, 0, 'shape_c', 0.5, 'nbody_epsilon_1', 0.01 + 0.5 * 0.99],
  ['NBDY inst 1: D -> nbody_pump_1 (amount 0-0.01, interval 10)', 5, 0, 'shape_d', 0.4, 'nbody_pump_1', 0.004],
  ['SPHR inst 3: A -> sphere_damping_3', 6, 2, 'shape_a', 0.6, 'sphere_damping_3', 0.6],
  ['SPHR inst 3: B -> sphere_elasticity_3', 6, 2, 'shape_b', 0.7, 'sphere_elasticity_3', 0.7],
  ['SPHR inst 3: C -> sphere_spin_3', 6, 2, 'shape_c', 0.5, 'sphere_spin_3', 5],
  ['SIN inst 4: A -> waveform_phase_4', 0, 3, 'shape_a', 0.25, 'waveform_phase_4', 0.25],
  ['SAW inst 1: B -> saw_skew_1', 1, 0, 'shape_b', 0.75, 'saw_skew_1', 0.75],
  ['SQR inst 2: B -> square_pw_2', 2, 1, 'shape_b', 0.5, 'square_pw_2', 0.05 + 0.5 * 0.9],
  ['PERL inst 2: A -> noise_freq_2', 3, 1, 'shape_a', 0.5, 'noise_freq_2', 0.01 + 0.5 * 99.99],
  ['FOLW: A -> env_follow_ms', 8, 0, 'shape_a', 0.1, 'env_follow_ms', 200],
  ['RAND inst 1: RATE -> noise_freq_1', 7, 0, 'shape_rate', 4, 'noise_freq_1', 4],
];
for (const [name, fam, inst, knob, val, field, want] of shapeReads) {
  await it(name, async () => { await set('shape_family', fam); await set('shape_inst', inst); await set(knob, val); const v = await eng.snap(field); expect(v && near(v[0], want, 1e-3), `${field} = ${v} want ${want}`); });
}
await it('knob with no meaning for the family sends nothing (SIN: C)', async () => { await set('shape_family', 0); const n0 = eng.prints.length; await set('shape_c', 0.3); expect(eng.prints.length === n0, 'sent something'); });
await it('KICK in SPHR -> sphere_kick; ignored elsewhere', async () => { await set('shape_family', 6); await set('shape_d', 0.5); await bang('shape_kick'); await set('shape_family', 4); const n0 = eng.prints.length; await bang('shape_kick'); expect(eng.prints.length === n0, 'kick sent outside SPHR'); });
for (const [fam, name] of [[3, 'PERL'], [4, 'LRNZ'], [5, 'NBDY'], [6, 'SPHR']]) await it(`RESET in ${name} -> ${name.toLowerCase()} reset accepted`, async () => { await set('shape_family', fam); await bang('shape_reset'); });
await it('MODE in NBDY -> nbody_mode; in SPHR -> sphere_mode', async () => { await set('shape_family', 5); await set('shape_inst', 0); await set('shape_mode', 2); const a = await eng.snap('nbody_mode_1'); await set('shape_family', 6); await set('shape_mode', 1); const b = await eng.snap('sphere_mode_1'); expect(a && a[0] === 2 && b && b[0] === 1, `${a} ${b}`); });
await it('scope TAP=FOLW follows FAMILY x INST (scope_tap <family> <inst>)', async () => { await set('scope_tap', 0); const n0 = eng.prints.length; await set('shape_family', 4); await set('shape_inst', 1); expect(eng.prints.slice(n0).some((p) => /scope/.test(p) && /lorenz/.test(p)), eng.prints.slice(n0).join('|')); });
await it('scope TAP=GRN -> scope_tap grain', async () => { const n0 = eng.prints.length; await set('scope_tap', 1); expect(eng.prints.slice(n0).some((p) => /scope/.test(p) && /grain/.test(p)), eng.prints.slice(n0).join('|')); });

sec('MORPH metasurface');
await it('POWER -> morph_power', async () => { await set('morph_power', 3); const m = await eng.morphState(); expect(m.some(([s, a]) => s === 'morph_power' && near(a[0], 3)), JSON.stringify(m)); });
await it('KERNEL switch -> morph_interp 1 / 0 accepted', async () => { await set('morph_kernel', 1); await set('morph_kernel', 0); });
await it('SNAP at the cursor -> snapshot <slot> + morph_point (engine point placed)', async () => {
  await bang('snap5'); await set('joy_x', 0.3); await set('joy_y', 0.6); await bang('morph_snap'); const m = await eng.morphState();
  const pt = m.find(([s, a]) => s === 'morph_point' && a[0] === 4); expect(pt && near(pt[1][1], 0.3, 1e-2) && near(pt[1][2], 0.6, 1e-2), JSON.stringify(m));
  const st = await eng.status(); expect(st.snapshotMask[4] === 1, 'snapshot 4 not held');
});
await it('SNAP again auto-advances to the next free slot (a new point, not an overwrite)', async () => {
  await set('joy_x', 0.7); await bang('morph_snap'); const m = await eng.morphState(); const pts = m.filter(([s]) => s === 'morph_point');
  expect(pts.length >= 2, `points ${pts.length}`); return `${pts.length} points`;
});
await it('remove a point -> morph_unplace + snapshot_clear (engine point gone, slot freed)', async () => {
  expect(typeof surface.morphRemove === 'function', 'surface.morphRemove missing');
  surface.morphRemove(4); await flush(); const m = await eng.morphState(); const st = await eng.status();
  expect(!m.some(([s, a]) => s === 'morph_point' && a[0] === 4), 'point 4 still placed'); expect(st.snapshotMask[4] === 0, 'snapshot 4 still held');
});
await it('ROUTE RUN / STOP / PAUSE -> morph_route legs + morph_run, engine runs and stops', async () => {
  surface.msg('morph_route_clear; morph_route 0.2 0.2 1 3; morph_route 0.8 0.8 1 3; morph_run 1'); await flush(); await eng.run(4); const a = await eng.status();
  surface.msg('morph_pause'); await flush(); surface.msg('morph_stop'); await flush(); const b = await eng.status();
  expect(a.morph.route === 2 && a.morph.running === 1 && b.morph.running === 0, JSON.stringify([a.morph, b.morph]));
});
await it('BASE rate -> morph_rate accepted', async () => { surface.msg('morph_rate 2'); await flush(); });

sec('SEQ / SCALE');
await it('ring 0,4,7 + APPLY (BOTH) -> pitch_scale 0 4 7 in the engine (+ smear twin)', async () => {
  await set('seq_dest', 2); for (let i = 0; i < 12; i++) await set(`seq_ring_${i}`, [0, 4, 7].includes(i) ? 1 : 0);
  await set('seq_root', 2); await set('seq_mode', 1); await bang('seq_apply');
  const sc = await eng.snap('pitch_scale'), sm = await eng.snap('smear_pitch_scale'), root = await eng.snap('scale_root'), rot = await eng.snap('scale_rotate');
  expect(sc && sc.length === 3 && sc[0] === 0 && sc[1] === 4 && sc[2] === 7, `pitch_scale ${sc}`); expect(sm && sm.length === 3, `smear ${sm}`);   // snapbuf replies list the degrees
  expect(root && root[0] === 2 && rot && rot[0] === 1, `root ${root} rot ${rot}`);
});
await it('PRESET MAJ lights the ring; APPLY (GRAIN only) -> 7 degrees, smear untouched', async () => {
  await set('seq_preset', 0); await set('seq_dest', 0); await set('seq_root', 0); await set('seq_mode', 0); await bang('seq_apply');
  const sc = await eng.snap('pitch_scale'), sm = await eng.snap('smear_pitch_scale');
  expect(sc && sc.length === 7 && sc[3] === 5, `pitch_scale ${sc}`); expect(sm && sm.length === 3, `smear changed: ${sm}`);
});
await it('SLOT C -> pitch_scale_slot 2', async () => { await set('seq_dest', 2); await bang('seq_slot_C'); const v = await eng.snap('pitch_scale_slot'); expect(v && v[0] === 2, `${v}`); await bang('seq_slot_A'); });
await it('AXIS->SLOTS (axis 3) -> pitch_scale_to 0 + pattern scale_root [ 0 4 8 ] accepted', async () => { await set('seq_axis', 2); await bang('seq_axis_slots'); expect(/pattern scale_root \[ 0 4 8 \]/.test(surface.getDisplay('seq_readout')), surface.getDisplay('seq_readout')); });
await it('AXIS->SLOTS with REV and with ALT accepted', async () => { await set('seq_rev', 1); await bang('seq_axis_slots'); await set('seq_rev', 0); await set('seq_alt', 2); await bang('seq_axis_slots'); await set('seq_alt', 0); });
await it('pattern scale_root actually steps the root (cycle clock running)', async () => {
  await set('seq_alt', 0); await set('seq_rev', 0); await bang('seq_axis_slots');
  surface.msg('pattern_cycle 1/4'); await flush();
  // the cycle clock needs a tempo: two clock bangs 100 blocks apart (~145 ms -> ~413 BPM)
  await eng.msg('bang'); await eng.run(100); await eng.msg('bang');
  const seen = new Set();
  for (let k = 0; k < 40; k++) { await eng.run(20); const r = await eng.snap('scale_root'); if (r) seen.add(Math.round(r[0])); }
  expect(seen.size >= 2, `roots seen: ${[...seen]}`); return `roots ${[...seen].join(',')}`;
});
for (const [t, name] of [[0, 'EVNT (event grain)'], [1, 'MOD (moog_cutoff)'], [2, 'PTCH (pitch)'], [3, 'SMR (smear_pitch)']]) {
  await it(`time circle K=3 N=8 TARGET ${name} -> pattern ... 1(3,8) accepted`, async () => { await set('seq_time_target', t); await set('seq_k', 3); await set('seq_n', 8); expect(/1\(3,8\)/.test(surface.getDisplay('seq_readout')), surface.getDisplay('seq_readout')); });
}
await it('non-preset K,N falls back to the default token', async () => { await set('seq_time_target', 1); await set('seq_k', 11); await set('seq_n', 13); expect(/1\(3,8\)/.test(surface.getDisplay('seq_readout')), surface.getDisplay('seq_readout')); });
await it('pattern grid: FILTR/1 (moog_cutoff) pins -> pattern moog_cutoff <16 values> accepted', async () => {
  await set('seq_grid_page', 3); await set('seq_grid_param', 0); await set('seq_grid_value', 0.7); await set('seq_grid_0_0', 1); await set('seq_grid_0_4', 1);
  expect(/^pattern moog_cutoff 0\.7 0 0 0 0\.7/.test(surface.getDisplay('seq_grid_readout')), surface.getDisplay('seq_grid_readout'));
});
await it('pattern grid: a PAGE x PARAM without a value field sends nothing and says so', async () => { await set('seq_grid_param', 3); const n0 = eng.prints.length; await set('seq_grid_1_0', 1); expect(eng.prints.length === n0 && /no value field/.test(surface.getDisplay('seq_grid_readout')), surface.getDisplay('seq_grid_readout')); await set('seq_grid_1_0', 0); });
await it('pattern grid: clearing the row -> pattern_clear moog_cutoff (the live cutoff is the base again)', async () => {
  await set('seq_grid_param', 0); await set('seq_grid_0_0', 0); await set('seq_grid_0_4', 0);
  surface.msg('pattern_clear moog_cutoff'); await flush(); await set('cutoff', 1500); await eng.run(4);
  const p = await eng.params(); expect(near(p.moog_cutoff, 1500, 1e-2), `live ${p.moog_cutoff}`);
});

sec('XPNDR snapshot expander');
await it('FROM LIVE -> snapbuf_from_live + address query; VALUE LED shows the field', async () => { await set('xp_page', 0); await set('xp_param', 0); await bang('xp_fromlive'); await eng.run(1); const st = await eng.status(); expect(st.snapbufHas === 1, 'no buffer'); expect(surface.getDisplay('xp_value_led') !== '--', `LED ${surface.getDisplay('xp_value_led')}`); return `LED ${surface.getDisplay('xp_value_led')}`; });
await it('PAGE FILTR / PARAM 1 -> snapbuf_get moog_cutoff + band; widgets re-seated from the reply', async () => { await set('xp_page', 3); await set('xp_param', 0); expect(near(+surface.getDisplay('xp_value_led'), 1500, 1e-2), `LED ${surface.getDisplay('xp_value_led')}`); });
await it('VALUE -> snapbuf_set moog_cutoff 5000 (buffer, not live)', async () => { await set('xp_value', 5000); const b = await eng.snapbuf('moog_cutoff'); const p = await eng.params(); expect(b && near(b[0], 5000), `buf ${b}`); expect(near(p.moog_cutoff, 1500, 1e-2), `live changed: ${p.moog_cutoff}`); });
await it('band MIN 0.5 -> snapbuf_set moog_cutoff_range min <scaled Hz>', async () => { await set('xp_min', 0.5); const b = await eng.snapbuf('moog_cutoff_range'); expect(b && near(b[0], 20 + 0.5 * 19980, 1e-2), `band ${b}`); });
await it('band MAX / SLEW / ENABLED / INVERT / INST / SOURCE PERL -> band fields', async () => {
  await set('xp_max', 0.75); await set('xp_slew', 0.3); await set('xp_enabled', 1); await set('xp_invert', 1); await set('xp_inst', 2); await set('xp_source', 1);
  const b = await eng.snapbuf('moog_cutoff_range');   // min max enabled rand_type rand_instance base_value slew invert
  expect(b && near(b[1], 20 + 0.75 * 19980, 1e-2) && b[2] === 1 && b[3] === 2 && b[4] === 2 && near(b[6], 0.3) && b[7] === 1, `band ${b}`);
});
await it('STORE slot 4 -> snapbuf_store 4 (engine holds it); LOAD 4 re-reads it', async () => { await set('xp_slot', 4); await bang('xp_store'); const st = await eng.status(); expect(st.snapshotMask[4] === 1, 'not stored'); await bang('xp_load'); expect(surface.getDisplay('xp_led') === 4, `LED ${surface.getDisplay('xp_led')}`); });
await it('ASSIGN -> snapbuf_apply: live cutoff becomes 5000 (band disabled first, else the readback is the modulated value)', async () => {
  await set('xp_enabled', 0); await bang('xp_assign'); await eng.run(4); const p = await eng.params(); expect(near(p.moog_cutoff, 5000, 1e-2), `live ${p.moog_cutoff}`);
  // knob follow: the brain re-seats CUTOFF from the get_params readback inside the 1.5 s window
  // after snapbuf_apply, once the 600 ms guard since the knob was last touched has passed
  await new Promise((r) => setTimeout(r, 650)); await eng.params();
  expect(near(surface.getValue('cutoff'), 5000, 1e-2), `knob did not follow: ${surface.getValue('cutoff')}`);
});
await it('AUDITION on/off -> snapbuf_audition; A/B -> snapbuf_compare', async () => { await set('xp_audition', 1); const a = await eng.status(); await set('xp_audition', 0); const b = await eng.status(); await bang('xp_compare'); await bang('xp_compare'); expect(a.snapbufAudition === 1 && b.snapbufAudition === 0, `${a.snapbufAudition}/${b.snapbufAudition}`); });

sec('SWEEP every bound control');
await it('every knob / switch / toggle / button / pin gesture is accepted (no engine errors)', async () => {
  // RECALL / LOAD of an EMPTY slot is a legitimate engine error (not a facade): hold something in
  // the slots the sweep's default positions address before pressing them
  await bang('snap_store'); await set('xp_slot', CONTROL_BY_ID.xp_slot.default); await bang('xp_fromlive'); await bang('xp_store');
  let n = 0; const skip = new Set(['reel_load', 'reel_save', 'record', 'play']);
  for (const c of CONTROLS) {
    if (!Array.isArray(c.bind) || skip.has(c.id) || c.kind === 'mxpin' || c.kind === 'joypad' || c.kind === 'led' || c.kind === 'readout') continue;
    const e0 = eng.errs.length;
    if (c.kind === 'knob' || c.kind === 'virtual') { await set(c.id, c.lo + 0.5 * (c.hi - c.lo)); await set(c.id, c.default); }
    else if (c.kind === 'switch') { for (let i = 0; i < c.labels.length; i++) await set(c.id, c.lo + i); await set(c.id, c.default); }
    else if (c.kind === 'toggle' || c.kind === 'pin') { await set(c.id, 1); await set(c.id, 0); }
    else if (c.kind === 'button') { await bang(c.id); }
    if (eng.errs.length > e0) throw new Error(`${c.id}: ${eng.errs.slice(e0).join(' | ')}`);
    n++;
  }
  return `${n} controls`;
});

eng.quit();
const fails = results.filter((r) => r[0] === 'FAIL');
let cur = '';
for (const [st, s, name, info] of results) { if (s !== cur) { cur = s; console.log(`\n== ${s} ==`); } console.log(`${st}  ${name}${info ? '   — ' + info : ''}`); }
console.log(`\nPANEL x ENGINE: ${results.length - fails.length}/${results.length} pass`);
process.exit(fails.length ? 1 : 0);
