/* ligase_panel_logic.js — the panel BRAIN: one hand-written ES module that gives the generated
 * control surface (docs/ui/emit_web.py -> web/ligase_controls.js) its behaviour.
 *
 * Contract: docs/ui/panel_bridge.md. The brain implements EVERY bind kind of panel_layout.py
 * (inlet / msg / msgmap / toggle / bang / special / None) and every `special` ported from the
 * Pure Data wiring in docs/ui/emit_pd.py (build_wiring, build_shape, build_matrix_logic,
 * build_xpndr_body, build_seq_body, build_displays), talking to an engine only through the tiny
 * bridge object:
 *
 *   bridge.control(id, value, text)   a BOUND control changed (text = composed message(s), ';'
 *                                     separated). For an inlet bind the text is the knob's MESSAGE
 *                                     TWIN (`grainsize 0.25`, panel_layout.INLET_SELECTORS; the
 *                                     joystick composes `morph <x> <y>`): a software host delivers
 *                                     it as a message (engine headless 1, inlets unpatched) so
 *                                     snapshots / the metasurface / XPNDR ASSIGN can move the knob;
 *                                     null = a CV-only inlet (smear mix, MIDI note) or the two
 *                                     plugin-parameter policy controls recmode/master
 *   bridge.msg(text)                  free-form engine message(s) (';' separated)
 *   bridge.note(ch, note, vel)        optional MIDI note
 *   bridge.host(action)               optional host action ('load_reel' | 'save_reel')
 *   bridge.on(event, cb)              'status' | 'out9' | 'control' | 'vu' | 'scope' | 'print'
 *
 * NO DOM access: this file runs under node for web/test_panel_logic.mjs. Widgets are reached only
 * through the `handles` object handed to surface.attach(handles): handles[id].set(value) is a
 * VISUAL-ONLY setter (never re-enters the brain).
 *
 *   import { createPanel } from './ligase_panel_logic.js';
 *   const surface = createPanel(bridge, CONTROLS, TABLES);
 *   const handles = buildControls(surface, container, { svg });
 *   surface.attach(handles);
 *   sendDefaults(surface);
 */

// The engine's quantize / delay_quantize grid vocabulary (src/ligase~.c ligase_quantize).
export const QUANT_NOTES = [1, 2, 4, 8, 16, 32, 64, 128];

// scope_tap family tokens in SHAPE_FAMILIES order (SIN SAW SQR PERL LRNZ NBDY SPHR RAND FOLW).
export const SCOPE_FAMILY_TOKENS = ['sine', 'saw', 'square', 'perlin', 'lorenz', 'nbody', 'sphere', 'rand', 'folw'];

// Whole-band `snapbuf <range_field> v0..v7` export order (src/ligase~.c morph_range_subs).
export const BAND_SUBS = ['min', 'max', 'enabled', 'rand_type', 'rand_instance', 'base_value', 'slew', 'invert'];

/* Snap a 0..128 knob value onto the engine's power-of-two grid. Values below 0.5 mean "leave the
 * grid alone" and return null (send nothing). Rounding is geometric (log2), so 3 -> 4, 6 -> 8,
 * 90 -> 64, 100 -> 128. */
export function quantizeSnap(v) {
  v = +v;
  if (!(v >= 0.5)) return null;
  const k = Math.max(0, Math.min(7, Math.round(Math.log2(v))));
  return QUANT_NOTES[k];
}

/* Number -> message text: plain decimal, no exponent (both bridges tokenize this), <= 6 decimals. */
export function fmt(v) {
  v = +v;
  if (!Number.isFinite(v)) return '0';
  if (Number.isInteger(v)) return String(v);
  const s = v.toFixed(6).replace(/0+$/, '').replace(/\.$/, '');
  return s === '-0' ? '0' : s;
}

/* Text for the amber VALUE LED (a handful of characters). */
export function fmtLed(v) {
  v = +v;
  if (!Number.isFinite(v)) return '--';
  if (Number.isInteger(v)) return String(v);
  if (Math.abs(v) >= 1000) return v.toFixed(0);
  if (Math.abs(v) >= 100) return v.toFixed(1);
  return String(parseFloat(v.toFixed(4)));
}

const isNum = (v) => typeof v === 'number' && Number.isFinite(v);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const round = (v) => Math.round(+v || 0);

// Tables arrive as JSON (int keys became strings); look them up tolerantly.
function tget(obj, key) {
  if (!obj) return undefined;
  if (Object.prototype.hasOwnProperty.call(obj, key)) return obj[key];
  return obj[String(key)];
}

export function createPanel(bridge, CONTROLS, TABLES = {}) {
  bridge = bridge || {};
  const T = TABLES || {};
  const byId = new Map();
  for (const c of CONTROLS || []) if (c && c.id) byId.set(c.id, c);
  const bindOf = (c) => (Array.isArray(c.bind) && c.bind.length ? c.bind : null);
  const bindKind = (c) => { const b = bindOf(c); return b ? b[0] : null; };
  const bindArg = (c) => { const b = bindOf(c); return b ? b[1] : null; };

  // ---- state --------------------------------------------------------------------------------
  const values = new Map();
  for (const c of byId.values()) {
    if (c.kind === 'joypad' || c.kind === 'led' || c.kind === 'readout') continue;
    values.set(c.id, isNum(c.default) ? c.default : 0);
  }
  const st = {
    captured: new Set(),        // snapshot slots we captured (or the status mask says are held)
    pins: new Map(),            // "i_j" -> signed depth sent with matrix_connect
    spliceLocal: 0,             // panel-side splice counter (fallback when status has no splice)
    haveSplice: false,
    euclidArmed: false,
    euclidToken: null,
    seqSlot: 0,
    xpLed: '',
    xpValueLed: '--',
    placed: new Set(),          // snapshot slots placed as metasurface points (this panel's doing)
  };
  const displays = {};          // id -> last rendered display value
  let handles = null;
  let lastStatus = null;
  const watchers = new Map();   // id -> Set(cb)
  const unsubs = [];

  // ---- helpers -----------------------------------------------------------------------------
  const v = (id) => values.get(id);
  const vr = (id) => round(values.get(id));
  const notify = (id, value, extra) => {
    const run = (set) => { if (set) for (const cb of [...set]) { try { cb(value, id, extra); } catch (e) { console.error('[panel] watcher', id, e); } } };
    run(watchers.get(id)); run(watchers.get('*'));
  };
  const render = (id, value) => {
    const h = handles && handles[id];
    if (h && typeof h.set === 'function') { try { h.set(value); } catch (e) { console.error('[panel] render', id, e); } }
  };
  const display = (id, value) => { displays[id] = value; render(id, value); notify(id, value); };
  const control = (id, value, text) => {
    if (text != null) noteFollow(text);
    if (typeof bridge.control === 'function') bridge.control(id, value, text == null ? null : text);
    else if (text != null && typeof bridge.msg === 'function') bridge.msg(text);
  };
  const msg = (text) => { noteFollow(text); if (typeof bridge.msg === 'function') bridge.msg(text); };

  function coerce(c, value) {
    let x = +value;
    if (!Number.isFinite(x)) x = 0;
    switch (c.kind) {
      case 'switch': return clamp(Math.round(x), isNum(c.lo) ? c.lo : 0, isNum(c.hi) ? c.hi : 0);
      case 'toggle': case 'pin': case 'mxpin': case 'button': return x ? 1 : 0;
      default:
        if (isNum(c.lo) && isNum(c.hi) && c.hi >= c.lo) x = clamp(x, c.lo, c.hi);
        return x;
    }
  }
  const isLatching = (c) => c.kind === 'button' && (bindKind(c) === 'toggle' || (bindKind(c) === 'special' && (bindArg(c) === 'record' || bindArg(c) === 'xp_audition')));

  // ---- SOURCE SHAPE ------------------------------------------------------------------------
  const shapeFamily = () => vr('shape_family');
  const shapeInst = () => vr('shape_inst') + 1;
  const scopeFollowText = () => {
    const fam = clamp(shapeFamily(), 0, SCOPE_FAMILY_TOKENS.length - 1);
    const tok = SCOPE_FAMILY_TOKENS[fam];
    return tok === 'folw' ? 'scope_tap folw' : `scope_tap ${tok} ${shapeInst()}`;
  };
  const scopeFollowing = () => vr('scope_tap') === 0 && byId.has('scope_tap');
  function shapeKnobText(id, value) {
    const meanings = tget(T.SHAPE_MEANINGS, id);
    const m = tget(meanings, shapeFamily());
    if (!m) return null;                                   // no meaning for this family: send nothing
    const [sel, form, lo, hi, tail] = m;
    const val = (lo == null || hi == null) ? value : lo + value * (hi - lo);
    const extra = Array.isArray(tail) && tail.length ? ' ' + tail.map(fmt).join(' ') : '';   // constant trailing args
    if (form === 'global') return `${sel} ${fmt(val)}${extra}`;
    if (form === 'suffix') return `${sel}_${shapeInst()} ${fmt(val)}${extra}`;
    return `${sel} ${shapeInst()} ${fmt(val)}${extra}`;   // inst2
  }

  // ---- MATRIX --------------------------------------------------------------------------------
  const mxDepthSigned = () => (v('mx_depth') ?? 1) * (1 - 2 * (vr('mx_pol') ? 1 : 0));
  function matrixText(id, on) {
    const m = /^mx_(\d+)_(\d+)$/.exec(id);
    if (!m) return null;
    const src = (T.MATRIX_SRCS || [])[+m[1]], dst = (T.MATRIX_DSTS || [])[+m[2]];
    if (!src || !dst) return null;
    const key = m[1] + '_' + m[2];
    if (on) { const d = mxDepthSigned(); st.pins.set(key, d); return `matrix_connect ${src[1]} ${dst[1]} ${fmt(d)}`; }
    st.pins.delete(key);
    return `matrix_disconnect ${src[1]} ${dst[1]}`;
  }

  // ---- XPNDR ---------------------------------------------------------------------------------
  function xpAddress(pageId = 'xp_page', paramId = 'xp_param') {
    const pages = T.XPNDR_PAGES || [];
    const page = pages[clamp(vr(pageId), 0, Math.max(0, pages.length - 1))];
    const row = tget(T.XPNDR_FIELDS, page) || [];
    const ent = row[clamp(vr(paramId), 0, Math.max(0, row.length - 1))] || [null, null];
    return { vf: ent[0] || null, bf: ent[1] || null };
  }
  const bandRange = (bf) => tget(T.XPNDR_BAND_RANGES, bf) || [0, 1];
  const bandScale = (bf, x) => { const [lo, hi] = bandRange(bf); return lo + clamp(+x, 0, 1) * (hi - lo); };
  const bandUnscale = (bf, val) => { const [lo, hi] = bandRange(bf); return hi === lo ? 0 : clamp((val - lo) / (hi - lo), 0, 1); };
  function xpQueryText() {
    const { vf, bf } = xpAddress();
    const q = [];
    if (vf) q.push(`snapbuf_get ${vf}`);
    if (bf) q.push(`snapbuf_get ${bf}`);
    return q.length ? q.join('; ') : null;
  }
  function xpBandText(sub, value) {
    const { bf } = xpAddress();
    if (!bf) return null;
    return `snapbuf_set ${bf} ${sub} ${fmt(value)}`;
  }

  // ---- SEQ / SCALE ---------------------------------------------------------------------------
  const seqDegrees = () => { const out = []; for (let i = 0; i < 12; i++) if (vr(`seq_ring_${i}`)) out.push(i); return out; };
  const seqGates = () => { const d = vr('seq_dest'); return { g: d !== 1, s: d !== 0 }; };
  function euclidToken() {
    const k = vr('seq_k'), n = vr('seq_n');
    const presets = T.SEQ_EUCLID_PRESETS || [];
    const hit = presets.find((p) => p[0] === k && p[1] === n);
    const [dk, dn] = hit || T.SEQ_EUCLID_DEFAULT || [3, 8];
    return `1(${dk},${dn})`;
  }
  function euclidArm(id, value) {
    st.euclidToken = euclidToken(); st.euclidArmed = true;
    const targets = T.SEQ_TIME_TARGETS || [];
    const tgt = targets[clamp(vr('seq_time_target'), 0, Math.max(0, targets.length - 1))];
    if (!tgt) return;
    const text = `pattern ${tgt} ${st.euclidToken}`;
    control(id, value, text); display('seq_readout', text);
  }
  const gridField = () => xpAddress('seq_grid_page', 'seq_grid_param').vf;
  function gridRowText(r) {
    const field = gridField();
    if (!field) return null;
    const gv = v('seq_grid_value') ?? 0;
    const cells = [];
    for (let c = 0; c < 16; c++) cells.push(vr(`seq_grid_${r}_${c}`) ? fmt(gv) : '0');
    return `pattern ${field} ${cells.join(' ')}`;
  }
  const gridRowHasPins = (r) => { for (let c = 0; c < 16; c++) if (vr(`seq_grid_${r}_${c}`)) return true; return false; };
  function gridSend(id, value, r) {
    const text = gridRowText(r);
    if (!text) { display('seq_grid_readout', '— no value field at PAGE × PARAM —'); return; }
    control(id, value, text); display('seq_grid_readout', text);
  }

  // ---- SNAPSHOT lamps ------------------------------------------------------------------------
  function renderSnapLamps() {
    const sel = vr('snap_slot_sel');
    for (let i = 0; i < 32; i++) render(`snap${i + 1}`, i === sel ? 1 : (st.captured.has(i) ? 2 : 0));
  }
  values.set('snap_slot_sel', 0);
  function nextFreeSlot(from) {
    for (let k = 1; k < 32; k++) { const s = (from + k) % 32; if (!st.captured.has(s)) return s; }
    return from;
  }
  // Remove a metasurface point: the engine drops the point AND frees the snapshot slot
  // (morph_unplace + snapshot_clear), the lamp clears, the canvas drops the marker.
  function morphRemove(slot) {
    slot = clamp(round(slot), 0, 31);
    msg(`morph_unplace ${slot}; snapshot_clear ${slot}`);
    st.captured.delete(slot); st.placed.delete(slot);
    renderSnapLamps();
    notify('morph_snap:removed', { slot });
  }
  function renderSeqSlotLamps() { for (let i = 0; i < 16; i++) render(`seq_slot_${String.fromCharCode(65 + i)}`, i === st.seqSlot ? 1 : 0); }

  // ---- special handlers: name -> (id, value, c) ---------------------------------------------
  const SPECIAL = {
    adc_l() {}, adc_r() {}, dac_l() {}, dac_r() {},
    recmode(id, value) { control(id, value, null); },
    record(id, value) {
      if (value) { const mode = vr('recmode'); control(id, 1, mode === 0 ? 'recinput' : mode === 1 ? 'recsplice' : 'record 1'); }
      else control(id, 0, 'record 0');
    },
    openpanel_load() { if (typeof bridge.host === 'function') bridge.host('load_reel'); },
    savepanel_save() { if (typeof bridge.host === 'function') bridge.host('save_reel'); },
    splice_led() {},
    splice_data() {},
    splice_prev(id) { spliceShift(id, -1); },
    splice_next(id) { spliceShift(id, 1); },
    splice_enter(id) {
      const n = vr('splice_data');
      if (st.haveSplice && lastStatus && isNum(lastStatus.splice)) {
        const delta = n - Math.round(lastStatus.splice);
        if (delta !== 0) control(id, n, `shift ${delta}`);
      } else {
        control(id, n, `splice_finish_nav ${n}`);   // the old Pd panel semantics (no status available)
        st.spliceLocal = n; display('splice_led', st.spliceLocal);
      }
    },
    snap_store(id) { const slot = vr('snap_slot_sel'); control(id, slot, `snapshot ${slot}`); st.captured.add(slot); renderSnapLamps(); },
    snap_recall(id) { const slot = vr('snap_slot_sel'); control(id, slot, `snapshot_recall ${slot}`); },
    morph_snap(id) {
      const slot = vr('snap_slot_sel'), x = v('joy_x') ?? 0.5, y = v('joy_y') ?? 0.5;
      control(id, slot, `snapshot ${slot}; morph_point ${slot} ${fmt(x)} ${fmt(y)}`);
      st.captured.add(slot); st.placed.add(slot);
      notify('morph_snap:placed', { slot, x, y, label: String(slot + 1) });   // the metasurface canvas places the point
      // auto-advance to the next FREE slot (wrapping) so the next SNAP adds a point instead of
      // overwriting this one; a full bank keeps the selection where it is
      const next = nextFreeSlot(slot);
      if (next !== slot) { values.set('snap_slot_sel', next); notify('snap_slot_sel', next); }
      renderSnapLamps();
    },
    dist_preset(id, value) {
      const k = clamp(round(value), 1, 8);
      const bundle = tget(T.DIST_PRESETS, k);
      if (bundle && bundle.length) control(id, k, bundle.join('; '));
    },
    shape_family(id, value) { if (scopeFollowing()) control(id, value, scopeFollowText()); },
    shape_inst(id, value) { if (scopeFollowing()) control(id, value, scopeFollowText()); },
    shape_rate(id, value) { const t = shapeKnobText(id, value); if (t) control(id, value, t); },
    shape_a(id, value) { const t = shapeKnobText(id, value); if (t) control(id, value, t); },
    shape_b(id, value) { const t = shapeKnobText(id, value); if (t) control(id, value, t); },
    shape_c(id, value) { const t = shapeKnobText(id, value); if (t) control(id, value, t); },
    shape_d(id, value) { const t = shapeKnobText(id, value); if (t) control(id, value, t); },
    shape_kick(id) {
      if (shapeFamily() !== (isNum(T.SHAPE_KICK_FAMILY) ? T.SHAPE_KICK_FAMILY : 6)) return;
      const s = (v('shape_d') ?? 0) * (isNum(T.SHAPE_KICK_SCALE) ? T.SHAPE_KICK_SCALE : 5);
      control(id, s, `sphere_kick ${shapeInst()} ${fmt(s)} ${fmt(s)} ${fmt(s)}`);
    },
    shape_reset(id) { const sel = tget(T.SHAPE_RESET, shapeFamily()); if (sel) control(id, 1, `${sel} ${shapeInst()}`); },
    shape_mode(id, value) { const sel = tget(T.SHAPE_MODE, shapeFamily()); if (sel) control(id, value, `${sel} ${shapeInst()} ${round(value)}`); },
    mx_depth() {}, mx_pol() {},
    matrix(id, value) { const t = matrixText(id, !!value); if (t) control(id, value, t); },
    xp_led() {}, xp_value_led() {},
    xp_slot() {},
    xp_load(id) {
      const slot = vr('xp_slot');
      const q = xpQueryText();
      control(id, slot, `snapbuf_load ${slot}` + (q ? '; ' + q : ''));
      st.xpLed = String(slot); display('xp_led', slot);
    },
    xp_fromlive(id) {
      const q = xpQueryText();
      control(id, 1, 'snapbuf_from_live' + (q ? '; ' + q : ''));
      st.xpLed = '--'; display('xp_led', '--');
    },
    xp_page(id, value) { const q = xpQueryText(); if (q) control(id, value, q); },
    xp_param(id, value) { const q = xpQueryText(); if (q) control(id, value, q); },
    xp_value(id, value) {
      const { vf } = xpAddress();
      if (!vf) return;
      control(id, value, `snapbuf_set ${vf} ${fmt(value)}`);
      display('xp_value_led', fmtLed(value));
    },
    xp_min(id, value) { const { bf } = xpAddress(); const t = bf && xpBandText('min', bandScale(bf, value)); if (t) control(id, value, t); },
    xp_max(id, value) { const { bf } = xpAddress(); const t = bf && xpBandText('max', bandScale(bf, value)); if (t) control(id, value, t); },
    xp_slew(id, value) { const t = xpBandText('slew', value); if (t) control(id, value, t); },
    xp_enabled(id, value) { const t = xpBandText('enabled', value ? 1 : 0); if (t) control(id, value, t); },
    xp_invert(id, value) { const t = xpBandText('invert', value ? 1 : 0); if (t) control(id, value, t); },
    xp_inst(id, value) { const t = xpBandText('rand_instance', round(value)); if (t) control(id, value, t); },
    xp_source(id, value) {
      const codes = T.XPNDR_SOURCE_CODES || [];
      const code = codes[clamp(round(value), 0, Math.max(0, codes.length - 1))];
      if (!isNum(code)) return;
      const t = xpBandText('rand_type', code); if (t) control(id, value, t);
    },
    xp_store(id) { const slot = vr('xp_slot'); control(id, slot, `snapbuf_store ${slot}`); },
    xp_assign(id) { control(id, 1, 'snapbuf_apply'); },
    xp_audition(id, value) { control(id, value, `snapbuf_audition ${value ? 1 : 0}`); },
    seq_ring() {},
    seq_ring_order(id, value) { notify('seq_ring_order:layout', value); },
    seq_root() {}, seq_mode() {}, seq_axis() {},
    seq_preset(id, value) {
      const pcs = tget(T.SEQ_PRESET_PCS, clamp(round(value), 0, 5));
      if (!pcs) return;
      const on = new Set(pcs);
      for (let i = 0; i < 12; i++) setSilent(`seq_ring_${i}`, on.has(i) ? 1 : 0);
    },
    seq_slot(id) {
      const i = id.charCodeAt(id.length - 1) - 65;
      st.seqSlot = i; renderSeqSlotLamps();
      const { g, s } = seqGates(); const m = [];
      if (g) m.push(`pitch_scale_slot ${i}`);
      if (s) m.push(`smear_pitch_scale_slot ${i}`);
      if (m.length) { control(id, i, m.join('; ')); display('seq_readout', m[m.length - 1]); }
    },
    seq_axis_slots(id) {
      const axes = T.SEQ_AXES || [1, 2, 3, 4, 6];
      const a = axes[clamp(vr('seq_axis'), 0, axes.length - 1)];
      const shifts = tget(T.SEQ_AXIS_SHIFTS, a) || [0];
      const alt = (v('seq_alt') ?? 0) > 0, rev = !!vr('seq_rev');
      const body = alt ? `< ${shifts.join(' ')} >` : `[ ${(rev ? [...shifts].reverse() : shifts).join(' ')} ]`;
      const degs = seqDegrees(); const { g, s } = seqGates(); const m = [];
      if (degs.length) {
        if (g) m.push(`pitch_scale_to 0 ${degs.join(' ')}`);
        if (s) m.push(`smear_pitch_scale_to 0 ${degs.join(' ')}`);
      }
      if (g) m.push(`pattern scale_root ${body}`);
      if (s) m.push(`pattern smear_scale_root ${body}`);
      if (m.length) { control(id, 1, m.join('; ')); display('seq_readout', g ? `pattern scale_root ${body}` : m[m.length - 1]); }
    },
    seq_readout() {}, seq_grid_readout() {},
    seq_dest() {},
    seq_apply(id) {
      const degs = seqDegrees(); const { g, s } = seqGates(); const m = [];
      if (degs.length) {
        if (g) m.push(`pitch_scale ${degs.join(' ')}`);
        if (s) m.push(`smear_pitch_scale ${degs.join(' ')}`);
      }
      const root = vr('seq_root'), mode = vr('seq_mode');
      if (g) m.push(`scale_root ${root}`);
      if (s) m.push(`smear_scale_root ${root}`);
      if (g) m.push(`scale_rotate ${mode}`);
      if (s) m.push(`smear_scale_rotate ${mode}`);
      if (m.length) { control(id, 1, m.join('; ')); display('seq_readout', m[0]); }
    },
    seq_rev() {}, seq_alt() {},
    seq_time_target(id, value) { if (st.euclidArmed) euclidArm(id, value); },
    seq_k(id, value) { euclidArm(id, value); },
    seq_n(id, value) { euclidArm(id, value); },
    seq_rot(id, value) { if (st.euclidArmed) euclidArm(id, value); },
    seq_time_slot(id, value) { if (st.euclidArmed) euclidArm(id, value); },
    seq_grid(id, value) { const m = /^seq_grid_(\d+)_(\d+)$/.exec(id); if (m) gridSend(id, value, +m[1]); },
    seq_grid_page() {}, seq_grid_param() {},
    seq_grid_value(id, value) { for (let r = 0; r < 8; r++) if (gridRowHasPins(r)) gridSend(id, value, r); },
  };

  function spliceShift(id, delta) {
    control(id, delta, `shift ${delta}`);
    const count = lastStatus && isNum(lastStatus.splices) ? Math.round(lastStatus.splices) : 0;
    st.spliceLocal = count > 0 ? (((st.spliceLocal + delta) % count) + count) % count : Math.max(0, st.spliceLocal + delta);
    if (!st.haveSplice) display('splice_led', st.spliceLocal);
  }

  function specialName(c) {
    const name = bindArg(c) || '';
    if (/^snap_slot\d+$/.test(name)) return 'snap_slot';
    return name;
  }

  // ---- inlet knobs: the message twin (software-host delivery) --------------------------------
  // A CV knob's `msg` (panel_layout.INLET_SELECTORS) sets the same base the signal inlet would
  // drive. In stut mode (delay MODE 2) the three delay knobs remap exactly like the engine remaps
  // signal inlets 11/12/13: TIME 0-10 -> stut_reps 1-16, FDBK 0-1 -> stut_reduction, TONE 0-1 ->
  // stut_spacing 1..5000 ms (exponential). The joystick composes the message cursor `morph x y`.
  // null = a CV-only inlet (no message twin): the bridge drives the signal inlet instead.
  const STUT_MODE = 2;
  function inletText(c, value) {
    if (c.id === 'joy_x' || c.id === 'joy_y') {
      const x = c.id === 'joy_x' ? value : (v('joy_x') ?? 0.5), y = c.id === 'joy_y' ? value : (v('joy_y') ?? 0.5);
      return `morph ${fmt(x)} ${fmt(y)}`;
    }
    if (!c.msg) return null;
    if (c.stutMsg && vr('delay_mode') === STUT_MODE) {
      if (c.stutMsg === 'stut_reps') return `stut_reps ${1 + Math.round((clamp(+value, 0, 10) / 10) * 15)}`;
      if (c.stutMsg === 'stut_spacing') return `stut_spacing ${fmt(Math.pow(5000, clamp(+value, 0, 1)))}`;
      return `${c.stutMsg} ${fmt(value)}`;
    }
    return `${c.msg} ${fmt(value)}`;
  }

  // ---- knob follow -----------------------------------------------------------------------------
  // While the ENGINE moves the scalar bases (a metasurface blend, a running route, a snapshot
  // recall, XPNDR ASSIGN / AUDITION / A-B, a loaded surface) the get_params readback on outlet 9
  // re-seats every knob whose message twin matches, so the panel shows what the engine plays.
  // Outside such a window the readback is ignored (a modulation band or matrix routing makes the
  // readback the MODULATED value, not the base the knob holds). A knob touched within FOLLOW_MS
  // keeps its own value, so a drag never fights a stale poll.
  const FOLLOW_MS = 600, FOLLOW_WINDOW_MS = 1500;
  const READBACK_ALIAS = { gdelay: 'gdelay_time' };            // get_params name -> selector
  const followBySel = new Map();
  for (const c of byId.values()) if (c.msg) followBySel.set(c.msg, c);
  const touched = new Map();                                    // id -> time of our last send
  const now = () => (typeof performance !== 'undefined' && performance.now ? performance.now() : Date.now());
  let followUntil = 0, followRun = false;
  const followActive = () => followRun || now() < followUntil;
  function noteFollow(text) {
    const t = String(text || '');
    if (/(^|;)\s*morph_run\s+1\b/.test(t)) followRun = true;
    else if (/(^|;)\s*(morph_stop|morph_pause|morph_run\s+0)\b/.test(t)) { followRun = false; followUntil = now() + FOLLOW_WINDOW_MS; }
    if (/(^|;)\s*(morph|morph_x|morph_y|snapshot_recall|snapbuf_apply|snapbuf_audition|snapbuf_compare|morph_import|morph_load|load)\b/.test(t)) followUntil = now() + FOLLOW_WINDOW_MS;
  }
  function followParam(sel, val) {
    if (!followActive()) return;
    const c = followBySel.get(READBACK_ALIAS[sel] || sel);
    if (!c || !isNum(val)) return;
    if (c.stutMsg && vr('delay_mode') === STUT_MODE) return;           // stut mode: this readback is not this knob
    if (c.id === 'level' && (v('midi_vel_amp') || 0) > 0) return;      // velocity scales what the engine sees
    const t = touched.get(c.id);
    if (t != null && now() - t < FOLLOW_MS) return;
    const cur = values.get(c.id);
    if (isNum(cur) && Math.abs(cur - val) <= 1e-6 * Math.max(1, Math.abs(val))) return;
    setSilent(c.id, val);
  }

  // ---- dispatch ------------------------------------------------------------------------------
  function dispatch(id, value, c) {
    const kind = bindKind(c);
    if (!kind) {
      if (id === 'master') control(id, value, null);    // display-side gain: host applies it
      return;                                            // scope_view etc.: panel-side only
    }
    switch (kind) {
      case 'inlet': touched.set(id, now()); control(id, value, inletText(c, value)); break;
      case 'msg': {
        const sel = bindArg(c);
        if (id === 'quantize' || id === 'delay_quantize') {
          const q = quantizeSnap(value);
          if (q != null) control(id, q, `${sel} ${q}`);
          break;
        }
        control(id, value, `${sel} ${fmt(value)}`); break;
      }
      case 'msgmap': {
        const map = bindArg(c) || [];
        const i = clamp(round(value - (isNum(c.lo) ? c.lo : 0)), 0, Math.max(0, map.length - 1));
        if (id === 'scope_tap' && i === 0) { control(id, value, scopeFollowText()); break; }   // FOLW = follow FAMILY x INST
        const m = map[i];
        if (m != null) control(id, value, m);
        break;
      }
      case 'toggle': control(id, value, `${bindArg(c)} ${value ? 1 : 0}`); break;
      case 'bang': if (value) control(id, 1, bindArg(c)); break;
      case 'special': {
        const name = specialName(c);
        if (name === 'snap_slot') { const n = +bindArg(c).slice(9); values.set('snap_slot_sel', n); renderSnapLamps(); notify('snap_slot_sel', n); break; }
        const fn = SPECIAL[name];
        if (fn) fn(id, value, c);
        else console.warn('[panel] unimplemented special', name, 'for', id);
        break;
      }
      default: break;
    }
  }

  function setSilent(id, value) {
    const c = byId.get(id); if (!c) return;
    const x = coerce(c, value); values.set(id, x); render(id, x); notify(id, x);
  }

  function set(id, value, opts) {
    const c = byId.get(id);
    if (!c) { console.warn('[panel] unknown control', id); return; }
    if (c.kind === 'led' || c.kind === 'readout') { display(id, value); return; }
    if (c.kind === 'button' && !isLatching(c)) { if (value) bang(id); return; }   // momentary: set(x, 1) == bang
    const x = coerce(c, value);
    values.set(id, x);
    render(id, x);
    notify(id, x);
    if (!(opts && opts.silent)) dispatch(id, x, c);
  }

  function bang(id) {
    const c = byId.get(id);
    if (!c) { console.warn('[panel] unknown control', id); return; }
    if (isLatching(c) || c.kind === 'toggle' || c.kind === 'pin' || c.kind === 'mxpin') { set(id, values.get(id) ? 0 : 1); return; }
    if (c.kind !== 'button') { set(id, values.get(id)); return; }     // re-send the current value
    const h = handles && handles[id];
    if (h && typeof h.flash === 'function') { try { h.flash(); } catch (_) {} }
    notify(id, 1);
    dispatch(id, 1, c);
  }

  // ---- bridge events -------------------------------------------------------------------------
  function onStatus(s) {
    if (!s || typeof s !== 'object') return;
    lastStatus = Object.assign({}, lastStatus || {}, s);
    if (isNum(s.splice)) { st.haveSplice = true; st.spliceLocal = Math.round(s.splice); display('splice_led', Math.round(s.splice)); }
    if (isNum(s.playing)) { const p = s.playing ? 1 : 0; if (byId.has('play')) { values.set('play', p); render('play', p); } }
    if (isNum(s.recording)) { const r = s.recording ? 1 : 0; if (byId.has('record')) { values.set('record', r); render('record', r); } }
    if (Array.isArray(s.snapshotMask)) {
      st.captured = new Set(); s.snapshotMask.forEach((b, i) => { if (b && i < 32) st.captured.add(i); });
      renderSnapLamps();
    }
    notify('status', lastStatus);
  }
  function onOut9(sel, args) {
    args = Array.isArray(args) ? args : [];
    if (args.length === 1 && isNum(args[0])) { followParam(sel, args[0]); return; }   // a get_params scalar line
    if (sel !== 'snapbuf' || !args.length) return;
    const field = args[0];
    const { vf, bf } = xpAddress();
    if (field === vf && args.length === 2 && isNum(args[1])) {
      st.xpValueLed = fmtLed(args[1]); display('xp_value_led', st.xpValueLed);
      if (byId.has('xp_value')) setSilent('xp_value', args[1]);
      return;
    }
    if (field && field === bf) {
      if (args.length === 1 + BAND_SUBS.length && args.slice(1).every(isNum)) {
        const band = {}; BAND_SUBS.forEach((k, i) => { band[k] = args[1 + i]; });
        applyBand(bf, band);
      } else if (args.length === 3 && typeof args[1] === 'string' && isNum(args[2])) {
        applyBand(bf, { [args[1]]: args[2] });
      }
    }
  }
  function applyBand(bf, band) {
    if ('min' in band) setSilent('xp_min', bandUnscale(bf, band.min));
    if ('max' in band) setSilent('xp_max', bandUnscale(bf, band.max));
    if ('slew' in band) setSilent('xp_slew', band.slew);
    if ('enabled' in band) setSilent('xp_enabled', band.enabled ? 1 : 0);
    if ('invert' in band) setSilent('xp_invert', band.invert ? 1 : 0);
    if ('rand_instance' in band) setSilent('xp_inst', band.rand_instance);
    if ('rand_type' in band) { const i = (T.XPNDR_SOURCE_CODES || []).indexOf(Math.round(band.rand_type)); if (i >= 0) setSilent('xp_source', i); }
  }
  function onHostControl(id, value) { if (byId.has(id)) set(id, value, { silent: true }); }

  function sub(event, cb) {
    if (typeof bridge.on !== 'function') return () => {};
    const r = bridge.on(event, cb);
    const off = typeof r === 'function' ? r : (typeof bridge.off === 'function' ? () => bridge.off(event, cb) : () => {});
    unsubs.push(off);
    return off;
  }
  sub('status', onStatus);
  sub('out9', onOut9);
  sub('control', onHostControl);

  // ---- attach / panel state ----------------------------------------------------------------
  function attach(h) {
    handles = h || {};
    for (const [id, val] of values) render(id, val);
    display('splice_led', st.haveSplice && lastStatus ? Math.round(lastStatus.splice) : st.spliceLocal);
    display('xp_led', st.xpLed);
    display('xp_value_led', st.xpValueLed);
    display('seq_readout', displays.seq_readout || '');
    display('seq_grid_readout', displays.seq_grid_readout || '');
    renderSnapLamps(); renderSeqSlotLamps();
  }

  // panel-side values (NOT engine parameters): the specials and the unbound controls, minus the
  // momentary buttons (no state), the matrix / ring / grid pins (exported as maps below) and displays
  const PANEL_IDS = (() => {
    const out = [];
    for (const c of byId.values()) {
      const k = bindKind(c);
      if (c.kind === 'led' || c.kind === 'readout' || c.kind === 'joypad' || c.kind === 'mxpin') continue;
      if (c.kind === 'button' && !isLatching(c)) continue;
      if (/^seq_(ring|grid)_\d/.test(c.id)) continue;
      if (k === 'special' || k === null) out.push(c.id);
    }
    return out;
  })();
  function getPanelState() {
    const ctl = {};
    for (const id of PANEL_IDS) { if (id === 'record' || id === 'play') continue; ctl[id] = values.get(id); }
    const pins = {}; for (const [k, d] of st.pins) pins[k] = d;
    const grid = []; for (let r = 0; r < 8; r++) { const row = []; for (let c = 0; c < 16; c++) row.push(vr(`seq_grid_${r}_${c}`)); grid.push(row); }
    const ring = []; for (let i = 0; i < 12; i++) ring.push(vr(`seq_ring_${i}`));
    const morph = handles && handles.morph && typeof handles.morph.getState === 'function' ? handles.morph.getState() : null;
    return {
      version: 1,
      controls: ctl,
      snapSlot: vr('snap_slot_sel'),
      captured: [...st.captured].sort((a, b) => a - b),
      seqSlot: st.seqSlot,
      splice: { data: vr('splice_data'), local: st.spliceLocal },
      matrix: pins,
      ring, grid,
      euclid: { armed: st.euclidArmed, token: st.euclidToken },
      xpLed: st.xpLed, xpValueLed: st.xpValueLed,
      readouts: { seq_readout: displays.seq_readout || '', seq_grid_readout: displays.seq_grid_readout || '' },
      morph,
    };
  }
  function setPanelState(obj, opts) {
    if (!obj || typeof obj !== 'object') return;
    const resend = !!(opts && opts.resend);
    if (obj.controls) for (const [id, val] of Object.entries(obj.controls)) if (byId.has(id) && isNum(+val)) setSilent(id, +val);
    if (Array.isArray(obj.ring)) obj.ring.forEach((b, i) => { if (i < 12) setSilent(`seq_ring_${i}`, b ? 1 : 0); });
    if (Array.isArray(obj.grid)) obj.grid.forEach((row, r) => { if (Array.isArray(row) && r < 8) row.forEach((b, c) => { if (c < 16) setSilent(`seq_grid_${r}_${c}`, b ? 1 : 0); }); });
    if (isNum(obj.snapSlot)) { values.set('snap_slot_sel', clamp(Math.round(obj.snapSlot), 0, 31)); notify('snap_slot_sel', values.get('snap_slot_sel')); }
    if (Array.isArray(obj.captured)) st.captured = new Set(obj.captured.filter(isNum));
    if (isNum(obj.seqSlot)) st.seqSlot = clamp(Math.round(obj.seqSlot), 0, 15);
    if (obj.splice) { if (isNum(obj.splice.data)) setSilent('splice_data', obj.splice.data); if (isNum(obj.splice.local)) st.spliceLocal = obj.splice.local; }
    if (obj.matrix && typeof obj.matrix === 'object') {
      const keep = new Set(Object.keys(obj.matrix));
      for (const k of [...st.pins.keys()]) if (!keep.has(k)) { st.pins.delete(k); setSilent(`mx_${k}`, 0); }
      for (const [k, d] of Object.entries(obj.matrix)) {
        if (!byId.has(`mx_${k}`)) continue;
        st.pins.set(k, +d); setSilent(`mx_${k}`, 1);
        if (resend) { const m = /^(\d+)_(\d+)$/.exec(k); const src = (T.MATRIX_SRCS || [])[+m[1]], dst = (T.MATRIX_DSTS || [])[+m[2]]; if (src && dst) msg(`matrix_connect ${src[1]} ${dst[1]} ${fmt(+d)}`); }
      }
    }
    if (obj.euclid) { st.euclidArmed = !!obj.euclid.armed; st.euclidToken = obj.euclid.token || null; }
    if (typeof obj.xpLed === 'string') { st.xpLed = obj.xpLed; display('xp_led', st.xpLed); }
    if (typeof obj.xpValueLed === 'string') { st.xpValueLed = obj.xpValueLed; display('xp_value_led', st.xpValueLed); }
    if (obj.readouts) { for (const k of ['seq_readout', 'seq_grid_readout']) if (typeof obj.readouts[k] === 'string') display(k, obj.readouts[k]); }
    if (obj.morph && handles && handles.morph && typeof handles.morph.setState === 'function') handles.morph.setState(obj.morph);
    if (!st.haveSplice) display('splice_led', st.spliceLocal);
    renderSnapLamps(); renderSeqSlotLamps();
    notify('seq_ring_order:layout', vr('seq_ring_order'));
  }

  function watch(id, cb) {
    if (!watchers.has(id)) watchers.set(id, new Set());
    watchers.get(id).add(cb);
    return () => { const s = watchers.get(id); if (s) s.delete(cb); };
  }
  function dispose() { for (const off of unsubs.splice(0)) { try { off(); } catch (_) {} } watchers.clear(); handles = null; }

  const surface = {
    set, bang, watch, attach, dispose,
    getValue: (id) => values.get(id),
    getDisplay: (id) => displays[id],
    has: (id) => byId.has(id),
    describe: (id) => byId.get(id) || null,
    msg: (text) => msg(text),
    morphRemove,
    morphPlaced: () => [...st.placed].sort((a, b) => a - b),
    note: (ch, note, vel) => { if (typeof bridge.note === 'function') bridge.note(ch, note, vel); },
    on: (event, cb) => sub(event, cb),
    display: {
      set: display,
      get: (id) => displays[id],
      lamp: (id, on) => render(id, on ? 1 : 0),
    },
    getPanelState, setPanelState,
    get status() { return lastStatus; },
    get handles() { return handles; },
    bridge, controls: CONTROLS, tables: T,
  };
  return surface;
}
