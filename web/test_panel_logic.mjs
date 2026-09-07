/* test_panel_logic.mjs — node tests for the panel brain (web/ligase_panel_logic.js) against the
 * REAL generated descriptors + tables (web/ligase_controls.js, emitted by docs/ui/emit_web.py).
 *
 *   node web/test_panel_logic.mjs
 *
 * A mock bridge records every control()/msg()/note()/host() call; tests assert the exact engine
 * messages the contract (docs/ui/panel_bridge.md) requires. No DOM, no npm. */
import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { execSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..');
const GEN = join(HERE, 'ligase_controls.js');
if (!existsSync(GEN)) execSync('python3 docs/ui/emit_web.py', { cwd: ROOT, stdio: 'inherit' });

const { createPanel, quantizeSnap, fmt } = await import('./ligase_panel_logic.js');
const { CONTROLS, TABLES, INLET_TO_ID, sendDefaults } = await import('./ligase_controls.js');

// ---- mock bridge --------------------------------------------------------------------------
function mockBridge() {
  const b = { calls: [], listeners: {} };
  b.control = (id, value, text) => b.calls.push({ kind: 'control', id, value, text });
  b.msg = (text) => b.calls.push({ kind: 'msg', text });
  b.note = (ch, note, vel) => b.calls.push({ kind: 'note', ch, note, vel });
  b.host = (action) => b.calls.push({ kind: 'host', action });
  b.on = (ev, cb) => { (b.listeners[ev] || (b.listeners[ev] = [])).push(cb); return () => { b.listeners[ev] = b.listeners[ev].filter((f) => f !== cb); }; };
  b.emit = (ev, ...a) => { for (const cb of b.listeners[ev] || []) cb(...a); };
  // every engine message sent so far, one per entry (';'-bundles split), in order
  b.sent = () => b.calls.flatMap((c) => (c.kind === 'control' || c.kind === 'msg') && c.text ? c.text.split(/\s*;\s*/) : []);
  b.reset = () => { b.calls.length = 0; };
  return b;
}
// a handles object that records every visual set()
function mockHandles(ids) {
  const h = {}; const log = [];
  for (const id of ids) h[id] = { set: (v) => log.push([id, v]), flash: () => log.push([id, 'flash']) };
  h.__log = log;
  return h;
}
function panel(controls = CONTROLS) {
  const bridge = mockBridge();
  const surface = createPanel(bridge, controls, TABLES);
  return { bridge, surface };
}
const allSent = [];   // for the vocabulary guard at the end
function sentOf(bridge) { const s = bridge.sent(); allSent.push(...s); return s; }

// ---- helpers ------------------------------------------------------------------------------
test('helpers: quantizeSnap + fmt', () => {
  assert.equal(quantizeSnap(0), null); assert.equal(quantizeSnap(0.3), null);
  assert.equal(quantizeSnap(0.5), 1); assert.equal(quantizeSnap(1), 1); assert.equal(quantizeSnap(3), 4);
  assert.equal(quantizeSnap(6), 8); assert.equal(quantizeSnap(90), 64); assert.equal(quantizeSnap(100), 128);
  assert.equal(quantizeSnap(128), 128); assert.equal(quantizeSnap(1000), 128);
  assert.equal(fmt(0.30000000000000004), '0.3'); assert.equal(fmt(20000), '20000'); assert.equal(fmt(-0.99), '-0.99');
  assert.equal(fmt(1e-9), '0'); assert.equal(fmt(10.05), '10.05');
});

// ---- inlet / msg / msgmap / toggle / bang -------------------------------------------------
test('inlet binds go to bridge.control(id, value, <message twin>) in engine units, clamped', () => {
  const { bridge, surface } = panel();
  surface.set('grainsize', 0.25);
  assert.deepEqual(bridge.calls, [{ kind: 'control', id: 'grainsize', value: 0.25, text: 'grainsize 0.25' }]);
  surface.set('speed', 99);
  assert.equal(surface.getValue('speed'), 4);                 // hi clamp
  assert.equal(bridge.calls[1].value, 4);
  assert.equal(bridge.calls[1].text, 'speed 4');
  assert.equal(INLET_TO_ID[3], 'grainsize');
  bridge.reset();
  surface.set('smr_mix', 0.3); surface.set('midi_note', 67);   // CV-only inlets: no message twin
  assert.deepEqual(bridge.calls.map((c) => c.text), [null, null]);
  bridge.reset();
  surface.set('joy_x', 0.8); surface.set('joy_y', 0.2);         // the joystick is the message cursor
  assert.deepEqual(bridge.calls.map((c) => c.text), ['morph 0.8 0.45', 'morph 0.8 0.2']);
  bridge.reset();
  surface.set('delay_mode', 2); bridge.reset();                 // stut mode remaps the delay knobs like inlets 11/12/13
  surface.set('dly_time', 10); surface.set('dly_feed', 0.5); surface.set('dly_tone', 0.5);
  assert.deepEqual(bridge.calls.map((c) => c.text), ['stut_reps 16', 'stut_reduction 0.5', 'stut_spacing 70.710678']);
  surface.set('delay_mode', 0); bridge.reset();
  surface.set('dly_time', 0.35);
  assert.deepEqual(bridge.calls.map((c) => c.text), ['gdelay_time 0.35']);
});

test('msg binds: "<sel> <v>"; quantize knobs snap to the power-of-two grid (< 0.5 sends nothing)', () => {
  const { bridge, surface } = panel();
  surface.set('smr_freq', 440); surface.set('smr_fdbk', -0.5);
  assert.deepEqual(sentOf(bridge), ['smear_frequency 440', 'smear_feedback -0.5']);
  bridge.reset();
  surface.set('quantize', 0); surface.set('quantize', 0.3); surface.set('delay_quantize', 0.49);
  assert.deepEqual(bridge.calls, []);
  surface.set('quantize', 3); surface.set('quantize', 100); surface.set('quantize', 90); surface.set('delay_quantize', 16); surface.set('quantize', 1);
  assert.deepEqual(sentOf(bridge), ['quantize 4', 'quantize 128', 'quantize 64', 'delay_quantize 16', 'quantize 1']);
  assert.equal(bridge.calls[0].value, 4);                      // control() carries the snapped value
});

test('msgmap: pitch_mode panel positions -> engine modes 0/1/3/4/5; env_type GAUS/EXP now send envelope 3/4', () => {
  const { bridge, surface } = panel();
  for (const i of [0, 1, 2, 3, 4]) surface.set('pitch_mode', i);
  assert.deepEqual(sentOf(bridge), ['pitch_mode 0', 'pitch_mode 1', 'pitch_mode 3', 'pitch_mode 4', 'pitch_mode 5']);
  bridge.reset();
  surface.set('env_type', 3); surface.set('env_type', 4); surface.set('env_type', 0);
  assert.deepEqual(sentOf(bridge), ['envelope 3', 'envelope 4', 'envelope 0']);
  bridge.reset();
  surface.set('playhead', 1); surface.set('spatial_src', 1); surface.set('morph_kernel', 1);
  assert.deepEqual(sentOf(bridge), ['playhead 2', 'spatial nbody', 'morph_interp 1']);
});

test('msgmap None sends nothing', () => {
  const controls = CONTROLS.map((c) => (c.id === 'env_type' ? { ...c, bind: ['msgmap', ['envelope 0', null, 'envelope 2', null, null]] } : c));
  const { bridge, surface } = panel(controls);
  surface.set('env_type', 1); surface.set('env_type', 3); surface.set('env_type', 4);
  assert.deepEqual(bridge.calls, []);
  assert.equal(surface.getValue('env_type'), 4);               // the position is still tracked
  surface.set('env_type', 2);
  assert.deepEqual(sentOf(bridge), ['envelope 2']);
});

test('toggle + bang binds', () => {
  const { bridge, surface } = panel();
  surface.set('loop', 0); surface.set('poly', 1); surface.bang('dist_on');
  surface.bang('stut_bang'); surface.bang('chord'); surface.bang('xp_compare');
  assert.deepEqual(sentOf(bridge), ['loop 0', 'poly 1', 'distortion_enable 0', 'stut', 'chord 60 64 67', 'snapbuf_compare']);
});

// ---- transport ----------------------------------------------------------------------------
test('record arming per recmode: ON -> recinput | recsplice | record 1; OFF -> record 0', () => {
  const { bridge, surface } = panel();
  for (const [mode, arm] of [[0, 'recinput'], [1, 'recsplice'], [2, 'record 1']]) {
    bridge.reset();
    surface.set('recmode', mode);
    assert.deepEqual(bridge.calls, [{ kind: 'control', id: 'recmode', value: mode, text: null }]);   // plugin parameter, no message
    surface.set('record', 1); surface.set('record', 0);
    assert.deepEqual(sentOf(bridge), [arm, 'record 0']);
  }
  bridge.reset();
  surface.bang('record'); surface.bang('record');              // the button latches
  assert.deepEqual(sentOf(bridge), ['record 1', 'record 0']);
});

test('play is a toggle: play 1 / play 0 — never a bare play', () => {
  const { bridge, surface } = panel();
  surface.bang('play'); surface.bang('play'); surface.set('play', 1);
  assert.deepEqual(sentOf(bridge), ['play 1', 'play 0', 'play 1']);
  assert.ok(!sentOf(bridge).includes('play'));
});

test('status drives the transport lamps and re-syncs the latching buttons', () => {
  const { bridge, surface } = panel();
  const h = mockHandles(['play', 'record']); surface.attach(h);
  bridge.emit('status', { playing: 1, recording: 1 });
  assert.deepEqual(h.__log.slice(-2), [['play', 1], ['record', 1]]);
  bridge.emit('status', { playing: 0 });                       // the engine stopped (e.g. one-shot end)
  assert.equal(surface.getValue('play'), 0);
  bridge.reset(); surface.bang('play');
  assert.deepEqual(sentOf(bridge), ['play 1']);                // next press starts again
});

// ---- splice -------------------------------------------------------------------------------
test('splice prev/next/enter; ENTER uses the live status splice', () => {
  const { bridge, surface } = panel();
  const h = mockHandles(['splice_led']); surface.attach(h);
  surface.bang('splice_next'); surface.bang('splice_next'); surface.bang('splice_prev');
  assert.deepEqual(sentOf(bridge), ['shift 1', 'shift 1', 'shift -1']);
  assert.equal(surface.getDisplay('splice_led'), 1);           // panel-side counter (no status yet)
  bridge.reset();
  surface.set('splice_data', 5); surface.bang('splice_enter');
  assert.deepEqual(sentOf(bridge), ['splice_finish_nav 5']);   // no status available: the old Pd semantics
  bridge.reset();
  bridge.emit('status', { splice: 2, splices: 8 });
  assert.equal(surface.getDisplay('splice_led'), 2);           // LED = the engine's 0-based index
  surface.bang('splice_enter');
  assert.deepEqual(sentOf(bridge), ['shift 3']);               // 5 - 2
  bridge.reset();
  bridge.emit('status', { splice: 5, splices: 8 });
  surface.bang('splice_enter');
  assert.deepEqual(bridge.calls, []);                          // already there: nothing
  surface.set('splice_data', 0); surface.bang('splice_enter');
  assert.deepEqual(sentOf(bridge), ['shift -5']);
});

// ---- snapshots ----------------------------------------------------------------------------
test('snapshot slots: panel 1-32 -> engine 0-31; STORE / RECALL / SNAP', () => {
  const { bridge, surface } = panel();
  const h = mockHandles(['snap1', 'snap5', 'snap32']); surface.attach(h);
  surface.bang('snap5');
  assert.deepEqual(bridge.calls, []);                          // selecting a slot sends nothing
  surface.bang('snap_store'); surface.bang('snap_recall');
  assert.deepEqual(sentOf(bridge), ['snapshot 4', 'snapshot_recall 4']);
  bridge.reset();
  surface.bang('snap32'); surface.bang('snap_store');
  assert.deepEqual(sentOf(bridge), ['snapshot 31']);
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap5').slice(-1), [['snap5', 2]]);   // captured, not selected = held
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap32').slice(-1), [['snap32', 1]]); // selected = lit
  bridge.reset();
  const ev = []; surface.watch('morph_snap:placed', (e) => ev.push(e));
  surface.set('joy_x', 0.25); surface.set('joy_y', 0.75); bridge.reset();
  surface.bang('morph_snap');
  assert.deepEqual(sentOf(bridge), ['snapshot 31', 'morph_point 31 0.25 0.75']);
  assert.deepEqual(ev, [{ slot: 31, x: 0.25, y: 0.75, label: '32' }]);
  // SNAP auto-advances to the next FREE slot (wrapping: 31 -> 0) so the next SNAP adds a point
  assert.equal(surface.getValue('snap_slot_sel'), 0);
  assert.deepEqual(surface.morphPlaced(), [31]);
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap32').slice(-1), [['snap32', 2]]);   // held, no longer selected
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap1').slice(-1), [['snap1', 1]]);     // the new selection
  bridge.emit('status', { snapshotMask: Array.from({ length: 64 }, (_, i) => (i === 0 ? 1 : 0)) });
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap1').slice(-1), [['snap1', 1]]);     // selected outranks held
  // remove the placed point: engine point + slot freed, lamp cleared, canvas told
  const gone = []; surface.watch('morph_snap:removed', (e) => gone.push(e));
  bridge.reset(); surface.morphRemove(31);
  assert.deepEqual(sentOf(bridge), ['morph_unplace 31', 'snapshot_clear 31']);
  assert.deepEqual(gone, [{ slot: 31 }]);
  assert.deepEqual(surface.morphPlaced(), []);
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap32').slice(-1), [['snap32', 0]]);
});

// ---- distortion presets -------------------------------------------------------------------
test('dist_preset 4 sends the DIST_PRESETS bundle', () => {
  const { bridge, surface } = panel();
  surface.set('dist_preset', 4);
  assert.deepEqual(sentOf(bridge), ['distortion 0.5', 'dist_waveshaper_mode 3', 'dist_pregain 3', 'dist_curve_blend 0.5']);
  assert.equal(bridge.calls[0].id, 'dist_preset'); assert.equal(bridge.calls[0].value, 4);
  bridge.reset(); surface.set('dist_preset', 7.6);
  assert.equal(sentOf(bridge)[0], 'distortion 1');             // rounds to 8
});

// ---- SOURCE SHAPE -------------------------------------------------------------------------
test('shape routing: FAMILY x INST route RATE/A-D per SHAPE_MEANINGS', () => {
  const { bridge, surface } = panel();
  surface.set('scope_tap', 1); bridge.reset();                 // GRN: no follow traffic in this test
  surface.set('shape_family', 4); surface.set('shape_inst', 1);   // LRNZ, inst 2
  assert.deepEqual(bridge.calls, []);
  surface.set('shape_a', 0.5);
  assert.deepEqual(sentOf(bridge), ['lorenz_sigma 2 10.05']);  // 0.1 + 0.5 * (20 - 0.1)
  bridge.reset(); surface.set('shape_b', 1); surface.set('shape_c', 0);
  assert.deepEqual(sentOf(bridge), ['lorenz_rho 2 56', 'lorenz_beta 2 0.1']);
  bridge.reset(); surface.set('shape_d', 0.7);                 // D has no LRNZ meaning
  assert.deepEqual(bridge.calls, []);
  bridge.reset(); surface.set('shape_rate', 2.5);
  assert.deepEqual(sentOf(bridge), ['noise_freq_2 2.5']);      // suffix form, raw value
  surface.set('shape_family', 8); bridge.reset(); surface.set('shape_a', 0.25);   // FOLW: A = release ms
  assert.deepEqual(sentOf(bridge), ['env_follow_ms 500']);
  bridge.reset(); surface.set('shape_rate', 3);                // FOLW has no RATE meaning
  assert.deepEqual(bridge.calls, []);
  surface.set('shape_family', 0); bridge.reset(); surface.set('shape_c', 0.3);   // SIN: C means nothing
  assert.deepEqual(bridge.calls, []);
  surface.set('shape_a', 0.3); assert.deepEqual(sentOf(bridge), ['waveform_phase 2 0.3']);
  surface.set('shape_family', 3); surface.set('shape_inst', 3); bridge.reset(); surface.set('shape_a', 1);
  assert.deepEqual(sentOf(bridge), ['noise_freq_4 100']);      // PERL A = FREQ (suffix form)
});

test('shape KICK (sphere only), RESET and MODE per family', () => {
  const { bridge, surface } = panel();
  surface.set('scope_tap', 1);
  surface.set('shape_family', 6); surface.set('shape_inst', 0); surface.set('shape_d', 0.4); bridge.reset();
  surface.bang('shape_kick');
  assert.deepEqual(sentOf(bridge), ['sphere_kick 1 2 2 2']);   // s = D * 5
  bridge.reset(); surface.set('shape_family', 4); surface.bang('shape_kick');
  assert.deepEqual(bridge.calls, []);                          // KICK is sphere-only
  surface.set('shape_inst', 1); bridge.reset(); surface.bang('shape_reset');
  assert.deepEqual(sentOf(bridge), ['lorenz_reset 2']);
  bridge.reset(); surface.set('shape_family', 0); surface.bang('shape_reset');
  assert.deepEqual(bridge.calls, []);                          // SIN has no reset
  surface.set('shape_family', 5); bridge.reset(); surface.set('shape_mode', 2);
  assert.deepEqual(sentOf(bridge), ['nbody_mode 2 2']);
  surface.set('shape_family', 6); bridge.reset(); surface.set('shape_mode', 0);
  assert.deepEqual(sentOf(bridge), ['sphere_mode 2 0']);
  surface.set('shape_family', 4); bridge.reset(); surface.set('shape_mode', 1);
  assert.deepEqual(bridge.calls, []);                          // LRNZ has no mode
  for (const [fam, sel] of Object.entries(TABLES.SHAPE_RESET)) {
    surface.set('shape_family', +fam); bridge.reset(); surface.bang('shape_reset');
    assert.deepEqual(sentOf(bridge), [`${sel} 2`]);
  }
});

test('scope_tap FOLW follows FAMILY x INST (the documented gap)', () => {
  const { bridge, surface } = panel();
  surface.set('shape_family', 5); surface.set('shape_inst', 0); bridge.reset();   // NBDY 1 (defaults)
  surface.set('scope_tap', 0);
  assert.deepEqual(sentOf(bridge), ['scope_tap nbody 1']);
  bridge.reset(); surface.set('shape_family', 4); surface.set('shape_inst', 2);
  assert.deepEqual(sentOf(bridge), ['scope_tap lorenz 1', 'scope_tap lorenz 3']);
  bridge.reset(); surface.set('shape_family', 8);
  assert.deepEqual(sentOf(bridge), ['scope_tap folw']);        // FOLW family: no instance
  bridge.reset(); surface.set('scope_tap', 1);
  assert.deepEqual(sentOf(bridge), ['scope_tap grain']);
  bridge.reset(); surface.set('shape_family', 6); surface.set('shape_inst', 1);
  assert.deepEqual(bridge.calls, []);                          // GRN view: family changes are silent
  surface.set('scope_tap', 0);
  assert.deepEqual(sentOf(bridge), ['scope_tap sphere 2']);
});

// ---- MATRIX -------------------------------------------------------------------------------
test('matrix pins: connect with depth x polarity, disconnect on release', () => {
  const { bridge, surface } = panel();
  surface.set('mx_5_4', 1);
  assert.deepEqual(sentOf(bridge), ['matrix_connect lorenz1 moog_cutoff 1']);
  bridge.reset(); surface.set('mx_depth', 2.5); surface.set('mx_pol', 1);
  assert.deepEqual(bridge.calls, []);                          // policy values: panel-side only
  surface.set('mx_0_0', 1);
  assert.deepEqual(sentOf(bridge), ['matrix_connect sine1 gdelay -2.5']);
  bridge.reset(); surface.set('mx_5_4', 0); surface.bang('mx_0_0');
  assert.deepEqual(sentOf(bridge), ['matrix_disconnect lorenz1 moog_cutoff', 'matrix_disconnect sine1 gdelay']);
  bridge.reset(); surface.set('mx_pol', 0); surface.set('mx_depth', 0.5); surface.set('mx_15_21', 1);
  assert.deepEqual(sentOf(bridge), ['matrix_connect env_mono pitch_fine 0.5']);
});

// ---- XPNDR --------------------------------------------------------------------------------
test('xpndr: address -> snapbuf_get, VALUE -> snapbuf_set, band MIN/MAX scaled by XPNDR_BAND_RANGES', () => {
  const { bridge, surface } = panel();
  const h = mockHandles(['xp_led', 'xp_value_led', 'xp_min', 'xp_max', 'xp_value', 'xp_source', 'xp_inst', 'xp_slew', 'xp_enabled', 'xp_invert']);
  surface.attach(h);
  surface.set('xp_page', 3); surface.set('xp_param', 0);       // FILTR / 1 = moog_cutoff
  assert.deepEqual(sentOf(bridge), ['snapbuf_get moog_cutoff', 'snapbuf_get moog_cutoff_range', 'snapbuf_get moog_cutoff', 'snapbuf_get moog_cutoff_range']);
  bridge.reset(); surface.set('xp_value', 500);
  assert.deepEqual(sentOf(bridge), ['snapbuf_set moog_cutoff 500']);
  assert.equal(surface.getDisplay('xp_value_led'), '500');
  bridge.reset(); surface.set('xp_min', 0.5); surface.set('xp_max', 1); surface.set('xp_min', 0);
  assert.deepEqual(sentOf(bridge), ['snapbuf_set moog_cutoff_range min 10010', 'snapbuf_set moog_cutoff_range max 20000', 'snapbuf_set moog_cutoff_range min 20']);
  bridge.reset(); surface.set('xp_slew', 0.25); surface.set('xp_enabled', 0); surface.set('xp_invert', 1); surface.set('xp_inst', 3);
  assert.deepEqual(sentOf(bridge), ['snapbuf_set moog_cutoff_range slew 0.25', 'snapbuf_set moog_cutoff_range enabled 0',
    'snapbuf_set moog_cutoff_range invert 1', 'snapbuf_set moog_cutoff_range rand_instance 3']);
  bridge.reset(); for (const i of [0, 1, 2, 3, 4, 5, 6]) surface.set('xp_source', i);
  assert.deepEqual(sentOf(bridge).map((s) => +s.split(' ').pop()), TABLES.XPNDR_SOURCE_CODES);   // OFF PERL LRNZ NBDY SPHR RAND PAT
  // a field with no band: MIN sends nothing; a band with no value field: VALUE sends nothing
  bridge.reset(); surface.set('xp_page', 0); surface.set('xp_param', 5);   // GRAIN / quant
  assert.deepEqual(sentOf(bridge), ['snapbuf_get grainsize', 'snapbuf_get grainsize_range', 'snapbuf_get quant']);
  bridge.reset(); surface.set('xp_min', 0.5);
  assert.deepEqual(bridge.calls, []);
  surface.set('xp_param', 3); bridge.reset(); surface.set('xp_min', 0.5);       // iot_range: [0.0005, 2]
  assert.deepEqual(sentOf(bridge), ['snapbuf_set iot_range min 1.00025']);
  surface.set('xp_page', 3); surface.set('xp_param', 3); bridge.reset();        // FILTR / 4 = (None, distortion_range)
  surface.set('xp_value', 0.3); assert.deepEqual(bridge.calls, []);
  surface.set('xp_max', 0.5); assert.deepEqual(sentOf(bridge), ['snapbuf_set distortion_range max 0.5']);   // default [0,1]
});

test('xpndr: LOAD / FROM LIVE / STORE / ASSIGN / AUDITION + the snapbuf readbacks', () => {
  const { bridge, surface } = panel();
  const h = mockHandles(['xp_led', 'xp_value_led', 'xp_min', 'xp_max', 'xp_value', 'xp_source', 'xp_inst', 'xp_slew', 'xp_enabled', 'xp_invert', 'xp_audition']);
  surface.attach(h);
  surface.set('xp_page', 3); surface.set('xp_param', 0); surface.set('xp_slot', 7); bridge.reset();
  surface.bang('xp_load');
  assert.deepEqual(sentOf(bridge), ['snapbuf_load 7', 'snapbuf_get moog_cutoff', 'snapbuf_get moog_cutoff_range']);
  assert.equal(surface.getDisplay('xp_led'), 7);
  bridge.reset(); surface.bang('xp_fromlive');
  assert.equal(sentOf(bridge)[0], 'snapbuf_from_live');
  bridge.reset(); surface.bang('xp_store'); surface.bang('xp_assign'); surface.bang('xp_audition'); surface.bang('xp_audition');
  assert.deepEqual(sentOf(bridge), ['snapbuf_store 7', 'snapbuf_apply', 'snapbuf_audition 1', 'snapbuf_audition 0']);
  // readbacks: the addressed value field lights the VALUE LED; a whole-band report re-seats the cluster
  bridge.emit('out9', 'snapbuf', ['moog_cutoff', 1234.5]);
  assert.equal(surface.getDisplay('xp_value_led'), '1235');   // >= 1000: whole numbers (half-up)
  assert.equal(surface.getValue('xp_value'), 1234.5);
  bridge.emit('out9', 'snapbuf', ['speed', 3]);                // not the addressed field: ignored
  assert.equal(surface.getDisplay('xp_value_led'), '1235');
  bridge.reset();
  bridge.emit('out9', 'snapbuf', ['moog_cutoff_range', 20, 10010, 1, 4, 2, 0, 0.5, 1]);   // min max enabled rand_type rand_instance base slew invert
  assert.equal(surface.getValue('xp_min'), 0); assert.equal(surface.getValue('xp_max'), 0.5);
  assert.equal(surface.getValue('xp_enabled'), 1); assert.equal(surface.getValue('xp_source'), 2);   // LRNZ
  assert.equal(surface.getValue('xp_inst'), 2); assert.equal(surface.getValue('xp_slew'), 0.5); assert.equal(surface.getValue('xp_invert'), 1);
  assert.deepEqual(bridge.calls, []);                          // readbacks never re-send
  bridge.emit('out9', 'snapbuf', ['moog_cutoff_range', 'max', 5015]);
  assert.equal(surface.getValue('xp_max'), 0.25);
});

// ---- SEQ / SCALE --------------------------------------------------------------------------
test('seq: tone ring compose + APPLY, routed by DEST', () => {
  const { bridge, surface } = panel();
  surface.set('seq_ring_7', 1); surface.set('seq_ring_0', 1); surface.set('seq_ring_4', 1);
  assert.deepEqual(bridge.calls, []);                          // the ring edits COLD
  surface.bang('seq_apply');                                   // DEST default = BOTH
  assert.deepEqual(sentOf(bridge), ['pitch_scale 0 4 7', 'smear_pitch_scale 0 4 7', 'scale_root 0', 'smear_scale_root 0', 'scale_rotate 0', 'smear_scale_rotate 0']);
  assert.equal(surface.getDisplay('seq_readout'), 'pitch_scale 0 4 7');
  bridge.reset(); surface.set('seq_dest', 0); surface.set('seq_root', 3); surface.set('seq_mode', 2); surface.bang('seq_apply');
  assert.deepEqual(sentOf(bridge), ['pitch_scale 0 4 7', 'scale_root 3', 'scale_rotate 2']);
  bridge.reset(); surface.set('seq_dest', 1); surface.bang('seq_apply');
  assert.deepEqual(sentOf(bridge), ['smear_pitch_scale 0 4 7', 'smear_scale_root 3', 'smear_scale_rotate 2']);
  bridge.reset(); surface.set('seq_dest', 2);
  for (let i = 0; i < 12; i++) surface.set(`seq_ring_${i}`, 0);
  surface.bang('seq_apply');
  assert.deepEqual(sentOf(bridge), ['scale_root 3', 'smear_scale_root 3', 'scale_rotate 2', 'smear_scale_rotate 2']);   // empty ring: no scale commit
});

test('seq: PRESET lights the ring, slots A-P select the live slot', () => {
  const { bridge, surface } = panel();
  const h = mockHandles(Array.from({ length: 12 }, (_, i) => `seq_ring_${i}`).concat(['seq_slot_A', 'seq_slot_C'])); surface.attach(h);
  surface.set('seq_preset', 0);                                // MAJ
  assert.deepEqual(Array.from({ length: 12 }, (_, i) => surface.getValue(`seq_ring_${i}`)), [1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1]);
  assert.deepEqual(bridge.calls, []);
  surface.set('seq_preset', 3); bridge.reset(); surface.bang('seq_apply');
  assert.equal(sentOf(bridge)[0], 'pitch_scale 0 2 4 6 8 10');   // W-T
  bridge.reset(); surface.bang('seq_slot_C');
  assert.deepEqual(sentOf(bridge), ['pitch_scale_slot 2', 'smear_pitch_scale_slot 2']);
  assert.deepEqual(h.__log.filter(([id]) => id === 'seq_slot_C').slice(-1), [['seq_slot_C', 1]]);
  bridge.reset(); surface.set('seq_dest', 0); surface.bang('seq_slot_P');
  assert.deepEqual(sentOf(bridge), ['pitch_scale_slot 15']);
});

test('seq: AXIS -> SLOTS with REV and ALT', () => {
  const { bridge, surface } = panel();
  surface.set('seq_preset', 0); surface.set('seq_axis', 2); bridge.reset();   // axis 3 (Giant Steps)
  surface.bang('seq_axis_slots');
  assert.deepEqual(sentOf(bridge), ['pitch_scale_to 0 0 2 4 5 7 9 11', 'smear_pitch_scale_to 0 0 2 4 5 7 9 11',
    'pattern scale_root [ 0 4 8 ]', 'pattern smear_scale_root [ 0 4 8 ]']);
  assert.equal(surface.getDisplay('seq_readout'), 'pattern scale_root [ 0 4 8 ]');
  bridge.reset(); surface.set('seq_rev', 1); surface.set('seq_dest', 0); surface.bang('seq_axis_slots');
  assert.deepEqual(sentOf(bridge), ['pitch_scale_to 0 0 2 4 5 7 9 11', 'pattern scale_root [ 8 4 0 ]']);
  bridge.reset(); surface.set('seq_alt', 2); surface.bang('seq_axis_slots');
  assert.deepEqual(sentOf(bridge).pop(), 'pattern scale_root < 0 4 8 >');   // ALT > 0 wins over REV
  bridge.reset(); surface.set('seq_alt', 0); surface.set('seq_rev', 0); surface.set('seq_axis', 3); surface.set('seq_dest', 1);
  for (let i = 0; i < 12; i++) surface.set(`seq_ring_${i}`, 0);
  surface.bang('seq_axis_slots');
  assert.deepEqual(sentOf(bridge), ['pattern smear_scale_root [ 0 3 6 9 ]']);   // empty ring: no pitch_scale_to
});

test('seq: TIME circle euclid token for (3,8) and the default fallback, TARGET routing', () => {
  const { bridge, surface } = panel();
  surface.set('seq_time_target', 2); surface.set('seq_rot', 3); surface.set('seq_time_slot', 2);
  assert.deepEqual(bridge.calls, []);                          // nothing armed yet
  surface.set('seq_time_target', 0);
  surface.set('seq_k', 3);                                     // K/N compose + arm (N default 8)
  assert.deepEqual(sentOf(bridge), ['pattern event grain 1(3,8)']);
  bridge.reset(); surface.set('seq_n', 16); surface.set('seq_k', 7);
  assert.deepEqual(sentOf(bridge), ['pattern event grain 1(3,16)', 'pattern event grain 1(7,16)']);
  bridge.reset(); surface.set('seq_n', 5);                     // (7,5) is no preset -> default (3,8)
  assert.deepEqual(sentOf(bridge), ['pattern event grain 1(3,8)']);
  bridge.reset(); surface.set('seq_time_target', 2); surface.set('seq_time_target', 1); surface.set('seq_time_target', 3);
  assert.deepEqual(sentOf(bridge), ['pattern pitch 1(3,8)', 'pattern moog_cutoff 1(3,8)', 'pattern smear_pitch 1(3,8)']);
  bridge.reset(); surface.set('seq_rot', 1);                   // ROT re-arms the stored token (display-only value)
  assert.deepEqual(sentOf(bridge), ['pattern smear_pitch 1(3,8)']);
  assert.equal(surface.getDisplay('seq_readout'), 'pattern smear_pitch 1(3,8)');
});

test('seq: pattern grid rows -> pattern <field> <16 values> at the VALUE level', () => {
  const { bridge, surface } = panel();
  surface.set('seq_grid_page', 3); surface.set('seq_grid_param', 0); surface.set('seq_grid_value', 0.5);
  assert.deepEqual(bridge.calls, []);                          // no pins yet: VALUE re-sends nothing
  surface.set('seq_grid_2_3', 1);
  assert.deepEqual(sentOf(bridge), ['pattern moog_cutoff 0 0 0 0.5 0 0 0 0 0 0 0 0 0 0 0 0']);
  assert.equal(surface.getDisplay('seq_grid_readout'), 'pattern moog_cutoff 0 0 0 0.5 0 0 0 0 0 0 0 0 0 0 0 0');
  bridge.reset(); surface.set('seq_grid_2_15', 1); surface.set('seq_grid_value', 0.8);
  assert.deepEqual(sentOf(bridge), ['pattern moog_cutoff 0 0 0 0.5 0 0 0 0 0 0 0 0 0 0 0 0.5', 'pattern moog_cutoff 0 0 0 0.8 0 0 0 0 0 0 0 0 0 0 0 0.8']);
  bridge.reset(); surface.set('seq_grid_2_3', 0); surface.set('seq_grid_2_15', 0);
  assert.deepEqual(sentOf(bridge).pop(), 'pattern moog_cutoff 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0');
  bridge.reset(); surface.set('seq_grid_param', 3);            // FILTR / 4 has no value field
  surface.set('seq_grid_0_0', 1);
  assert.deepEqual(bridge.calls, []);
  assert.match(surface.getDisplay('seq_grid_readout'), /no value field/);
  surface.set('seq_grid_page', 0); surface.set('seq_grid_param', 0); bridge.reset(); surface.set('seq_grid_0_1', 1);
  assert.deepEqual(sentOf(bridge), ['pattern grainsize 0.8 0.8 0 0 0 0 0 0 0 0 0 0 0 0 0 0']);
});

// ---- surface plumbing ---------------------------------------------------------------------
test('surface.msg / note / host actions / master / scope_view', () => {
  const { bridge, surface } = panel();
  surface.msg('morph_rate 1.5; morph_run 1'); surface.note(1, 60, 100);
  surface.bang('reel_load'); surface.bang('reel_save');
  surface.set('master', 0.5); surface.set('scope_view', 1);
  assert.deepEqual(bridge.calls, [
    { kind: 'msg', text: 'morph_rate 1.5; morph_run 1' }, { kind: 'note', ch: 1, note: 60, vel: 100 },
    { kind: 'host', action: 'load_reel' }, { kind: 'host', action: 'save_reel' },
    { kind: 'control', id: 'master', value: 0.5, text: null }]);
  assert.equal(surface.getValue('scope_view'), 1);
});

test("bridge 'control' (host automation) updates the widget + value without re-sending", () => {
  const { bridge, surface } = panel();
  const h = mockHandles(['grainsize']); surface.attach(h);
  bridge.reset(); bridge.emit('control', 'grainsize', 0.42);
  assert.deepEqual(bridge.calls, []);
  assert.equal(surface.getValue('grainsize'), 0.42);
  assert.deepEqual(h.__log.slice(-1), [['grainsize', 0.42]]);
});

test('attach renders the current values + displays; watch() fires on changes', () => {
  const { surface } = panel();
  const h = mockHandles(['grainsize', 'recmode', 'splice_led', 'xp_value_led', 'seq_readout', 'snap1', 'loop']);
  surface.attach(h);
  const got = Object.fromEntries(h.__log);
  assert.equal(got.grainsize, 0.1); assert.equal(got.recmode, 2); assert.equal(got.loop, 1);
  assert.equal(got.splice_led, 0); assert.equal(got.xp_value_led, '--'); assert.equal(got.seq_readout, ''); assert.equal(got.snap1, 1);
  const seen = []; const off = surface.watch('cutoff', (v, id) => seen.push([id, v]));
  surface.set('cutoff', 1000); off(); surface.set('cutoff', 2000);
  assert.deepEqual(seen, [['cutoff', 1000]]);
});

test('getPanelState / setPanelState round-trip is side-effect free (resend opt-in)', () => {
  const a = panel();
  a.surface.set('recmode', 0); a.surface.bang('snap7'); a.surface.set('mx_depth', 3); a.surface.set('mx_pol', 1);
  a.surface.set('mx_5_4', 1); a.surface.set('shape_family', 6); a.surface.set('shape_a', 0.9);
  a.surface.set('xp_page', 4); a.surface.set('xp_param', 2); a.surface.set('xp_slot', 9);
  a.surface.set('seq_ring_0', 1); a.surface.set('seq_ring_5', 1); a.surface.set('seq_root', -5); a.surface.set('seq_axis', 1);
  a.surface.set('seq_dest', 0); a.surface.set('seq_rev', 1); a.surface.set('seq_alt', 3); a.surface.set('seq_k', 5);
  a.surface.set('seq_grid_1_2', 1); a.surface.set('seq_grid_value', 0.25); a.surface.set('scope_view', 1); a.surface.bang('seq_slot_D');
  const state = a.surface.getPanelState();
  assert.equal(JSON.parse(JSON.stringify(state)).controls.recmode, 0);   // JSON-able
  assert.equal(state.snapSlot, 6); assert.deepEqual(state.matrix, { '5_4': -3 });
  assert.equal(state.controls.shape_family, 6); assert.equal(state.controls.xp_slot, 9);
  assert.deepEqual(state.ring, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]); assert.equal(state.grid[1][2], 1);
  assert.equal(state.controls.scope_view, 1); assert.equal(state.seqSlot, 3);
  assert.ok(!('grainsize' in state.controls) && !('record' in state.controls) && !('play' in state.controls));   // engine params live elsewhere
  const b = panel();
  const h = mockHandles(['mx_5_4', 'recmode', 'seq_ring_5', 'snap7']); b.surface.attach(h);
  b.surface.setPanelState(JSON.parse(JSON.stringify(state)));
  assert.deepEqual(b.bridge.calls, []);                        // no engine traffic
  for (const id of ['recmode', 'mx_depth', 'mx_pol', 'shape_family', 'shape_a', 'xp_page', 'xp_param', 'xp_slot', 'seq_root', 'seq_axis', 'seq_dest', 'seq_rev', 'seq_alt', 'seq_k', 'seq_grid_value', 'scope_view', 'seq_ring_0', 'seq_grid_1_2', 'mx_5_4'])
    assert.equal(b.surface.getValue(id), a.surface.getValue(id), id);
  assert.deepEqual(b.surface.getPanelState(), state);
  assert.deepEqual(h.__log.filter(([id]) => id === 'mx_5_4').slice(-1), [['mx_5_4', 1]]);
  assert.deepEqual(h.__log.filter(([id]) => id === 'snap7').slice(-1), [['snap7', 1]]);
  b.bridge.reset(); b.surface.bang('snap_store');
  assert.deepEqual(sentOf(b.bridge), ['snapshot 6']);          // restored slot selection is live
  const c = panel(); c.surface.setPanelState(state, { resend: true });
  assert.deepEqual(sentOf(c.bridge), ['matrix_connect lorenz1 moog_cutoff -3']);
});

test('sendDefaults pushes every initSend default through the surface (patch loadbang twin)', () => {
  const { bridge, surface } = panel();
  sendDefaults(surface);
  const sent = sentOf(bridge);
  const inletCalls = bridge.calls.filter((c) => c.kind === 'control' && Object.values(INLET_TO_ID).includes(c.id));
  assert.equal(inletCalls.length, 22);                         // 20 CV knobs + the two joystick axes
  assert.equal(inletCalls.filter((c) => c.text === null).length, 2);   // smear mix + MIDI note stay CV inlets
  assert.ok(inletCalls.some((c) => c.id === 'grainsize' && c.value === 0.1 && c.text === 'grainsize 0.1'));
  assert.ok(inletCalls.some((c) => c.id === 'joy_x' && c.value === 0.55 && /^morph 0\.55 /.test(c.text)));
  for (const m of ['envelope 2', 'playhead 2', 'delay_mode 0', 'smear_mode 0', 'pitch_mode 4', 'dist_emphasis_mode 1', 'pan_mode 2', 'spatial sphere', 'morph_interp 0', 'loop 1', 'poly 1', 'distortion_enable 1'])
    assert.ok(sent.includes(m), m);
  assert.ok(!sent.some((m) => /^(snapshot|shift|record|play|recinput|recsplice|stut|scope_tap|pattern)\b/.test(m)), 'no side-effect messages at load');
});

test('every selector the brain emitted is a registered ligase~ method (src/ligase~.c)', (t) => {
  const src = join(ROOT, 'src', 'ligase~.c');
  if (!existsSync(src)) return t.skip('engine source not present');
  const vocab = new Set([...readFileSync(src, 'utf8').matchAll(/class_addmethod\([^,]+,[^,]+,\s*gensym\("([^"]+)"\)/g)].map((m) => m[1]));
  const bad = new Set();
  for (const m of allSent) { const sel = m.trim().split(/\s+/)[0]; if (sel && !vocab.has(sel)) bad.add(sel); }
  assert.deepEqual([...bad], []);
  assert.ok(allSent.length > 150);
});

// ---- the browser bridge (web/ligase-host.js webBridge) on a fake engine ------------------
const { webBridge, tokenize } = await import('./ligase-host.js');
function fakeEngine() {
  const e = { floats: [], msgs: [], gain: null, prints: [], msgCbs: [], watchers: {}, vu: null, scope: null };
  let readyRes; e.ready = new Promise((r) => { readyRes = r; }); e.resolveReady = () => readyRes({});
  e.setFloat = (recv, v) => e.floats.push([recv, v]);
  e.sendMsg = (recv, atoms) => e.msgs.push([recv, atoms]);
  e.setGain = (v) => { e.gain = v; };
  e.onPrint = (cb) => e.prints.push(cb);
  e.onMessage = (cb) => e.msgCbs.push(cb);
  e.onVU = (cb) => { e.vu = cb; }; e.onScopeXY = (cb) => { e.scope = cb; };
  e.watch = (send, cb) => { (e.watchers[send] || (e.watchers[send] = [])).push(cb); };
  e.echo = (send, v) => { for (const cb of e.watchers[send] || []) cb(v); };
  e.out9 = (sel, atoms) => { for (const cb of e.msgCbs) cb('lg_state9', sel, atoms); };
  return e;
}

test('webBridge: tokenizer + control/msg/note routing', () => {
  assert.deepEqual(tokenize('pattern scale_root [ 0 4 8 ]'), ['pattern', 'scale_root', '[', 0, 4, 8, ']']);
  assert.deepEqual(tokenize('pattern event grain 1(3,8)'), ['pattern', 'event', 'grain', '1(3,8)']);
  assert.deepEqual(tokenize(' matrix_connect lorenz1 moog_cutoff -2.5 '), ['matrix_connect', 'lorenz1', 'moog_cutoff', -2.5]);
  assert.deepEqual(tokenize('x 1e3 .5 -0'), ['x', 1000, 0.5, -0]);
  const e = fakeEngine(); const b = webBridge(e, CONTROLS, { poll: false });
  b.control('grainsize', 0.25, null); b.control('master', 0.5, null); b.control('recmode', 1, null); b.control('env_type', 3, 'envelope 3');
  b.control('seq_apply', 1, 'pitch_scale 0 4 7; scale_root 3'); b.msg('snapbuf_get moog_cutoff'); b.note(2, 64, 90);
  assert.deepEqual(e.floats, [['lgR_grainsize', 0.25]]);
  assert.equal(e.gain, 0.5);
  assert.deepEqual(e.msgs, [['lg_engine', ['envelope', 3]], ['lg_engine', ['pitch_scale', 0, 4, 7]], ['lg_engine', ['scale_root', 3]],
    ['lg_engine', ['snapbuf_get', 'moog_cutoff']], ['lg_engine', ['midi', 64, 90, 2]]]);
});

test('webBridge: outlet-9 -> out9 events + the assembled status (frame ends at rec_mode)', () => {
  const e = fakeEngine(); const b = webBridge(e, CONTROLS, { poll: false });
  const out9 = [], statuses = []; b.on('out9', (sel, a) => out9.push([sel, a])); b.on('status', (s) => statuses.push(s));
  e.out9('grainsize', [0.1]); e.out9('bpm', [120]); e.out9('snapbuf', ['moog_cutoff', 500]);
  assert.equal(statuses.length, 0);                            // no frame end yet
  e.out9('splice', [2, 8]); e.out9('reel', [44100, 44100]); e.out9('playing', [1]); e.out9('recording', [0]); e.out9('rec_mode', [1]);
  assert.equal(statuses.length, 1);
  const s = statuses[0];
  assert.equal(s.splice, 2); assert.equal(s.splices, 8); assert.equal(s.reelLen, 44100); assert.equal(s.reelSr, 44100);
  assert.equal(s.playing, 1); assert.equal(s.recording, 0); assert.equal(s.recMode, 1); assert.equal(s.bpm, 120); assert.equal(s.params.grainsize, 0.1);
  assert.deepEqual(out9[2], ['snapbuf', ['moog_cutoff', 500]]);
  assert.equal(out9.length, 8);
});

test('webBridge: own lgS_ echoes are swallowed, foreign ones become control events; poll after ready', async () => {
  const e = fakeEngine(); const b = webBridge(e, CONTROLS, { pollMs: 5 });
  const ctl = []; b.on('control', (id, v) => ctl.push([id, v]));
  b.control('cutoff', 1000, null); b.control('cutoff', 2000, null);
  e.echo('lgS_cutoff', 1000.1); e.echo('lgS_cutoff', 2000.2);   // the hsl echoes (quantized)
  assert.deepEqual(ctl, []);
  e.echo('lgS_cutoff', 5000);                                  // a scripted / patch-side change
  assert.deepEqual(ctl, [['cutoff', 5000]]);
  assert.equal(e.msgs.filter((m) => m[1][0] === 'get_params').length, 0);
  e.resolveReady(); await new Promise((r) => setTimeout(r, 40)); b.stopPoll();
  assert.ok(e.msgs.filter((m) => m[1][0] === 'get_params').length >= 3, 'get_params polled after ready');
  const prints = []; b.on('print', (t, isErr) => prints.push([t, isErr]));
  for (const cb of e.prints) cb('ligase~: quantize note must be 1, 2, 4 ... error');
  assert.deepEqual(prints, [['ligase~: quantize note must be 1, 2, 4 ... error', true]]);
});
