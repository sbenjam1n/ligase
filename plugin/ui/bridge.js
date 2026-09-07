/* bridge.js — the PLUGIN side of docs/ui/panel_bridge.md (classic script, no module syntax).
 *
 * DPF's web view injects setParameterValue/editParameter/setState/sendNote into the page and
 * calls parameterChanged/stateChanged/sampleRateChanged on it. LigaseUI.cpp adds the live
 * channels (out9 / log / status / scope / panel / hello) as base64 "state" updates.
 */
(function () {
  const listeners = {};
  const emit = (ev, ...args) => {
    const l = listeners[ev]; if (!l) return;
    for (const cb of l) { try { cb(...args); } catch (e) { console.error('bridge', ev, e); } }
  };
  const b64 = (s) => {
    try { return decodeURIComponent(escape(atob(s))); } catch (e) { try { return atob(s); } catch (_) { return ''; } }
  };
  let byId = {}, byIndex = {};
  const hostSet = (i, v) => { if (typeof setParameterValue === 'function') setParameterValue(i, v); };
  const hostEdit = (i, on) => { if (typeof editParameter === 'function') editParameter(i, on); };
  const hostState = (k, v) => { if (typeof setState === 'function') setState(k, v); };

  // control value (engine units / switch index) <-> host parameter value
  const toParam = (p, value) => {
    if (p.pow2) { const v = +value; return v < 0.5 ? 0 : Math.max(1, Math.min(8, Math.round(Math.log2(v)) + 1)); }
    return +value;
  };
  const fromParam = (p, v) => (p.pow2 ? (v < 0.5 ? 0 : (1 << (Math.round(v) - 1))) : v);

  const bridge = {
    params: [],
    init(params) {
      bridge.params = params; byId = {}; byIndex = {};
      for (const p of params) { byId[p.id] = p; byIndex[p.index] = p; }
    },
    paramOf(id) { return byId[id] || null; },
    msg(text) { if (text) hostState('cmd', String(text).replace(/\n/g, ';')); },
    control(id, value, text) {
      const p = byId[id];
      if (p && p.kind !== 'output') { hostEdit(p.index, true); hostSet(p.index, toParam(p, value)); hostEdit(p.index, false); }
      else if (text) bridge.msg(text);
    },
    note(ch, note, vel) {
      if (typeof sendNote === 'function') sendNote(Math.max(0, (ch | 0) - 1), note | 0, vel | 0);
      else bridge.msg('midi ' + (note | 0) + ' ' + (vel | 0) + ' ' + (ch | 0));
    },
    on(ev, cb) { (listeners[ev] = listeners[ev] || []).push(cb); },
    savePanel(obj) { try { hostState('panel', JSON.stringify(obj)); } catch (e) {} },
    saveMidiMap(text) { hostState('midimap', text); },
  };

  window.parameterChanged = function (index, value) {
    const p = byIndex[index]; if (!p) return;
    if (p.kind === 'output') { emit('output', p.id, value); return; }
    emit('control', p.id, fromParam(p, value));
  };
  window.stateChanged = function (key, value) {
    value = String(value == null ? '' : value);
    switch (key) {
      case 'out9':
        for (const line of b64(value).split('\n')) {
          if (!line) continue;
          const t = line.split(' '); const sel = t.shift();
          emit('out9', sel, t.map((x) => (x !== '' && !isNaN(x) ? +x : x)));
        }
        break;
      case 'log':
        for (const line of b64(value).split('\n')) if (line) emit('print', line.replace(/^!/, ''), line[0] === '!');
        break;
      case 'status': {
        let st; try { st = JSON.parse(b64(value)); } catch (e) { return; }
        const mask = [];
        for (let i = 0; i < 32; i++) mask.push((st.snapshotMaskLo >>> i) & 1);
        for (let i = 0; i < 32; i++) mask.push((st.snapshotMaskHi >>> i) & 1);
        st.snapshotMask = mask;
        if (st.vu) emit('vu', st.vu[0], st.vu[1]);
        emit('status', st);
        break;
      }
      case 'scope': {
        const pts = b64(value).split(';');
        const x = new Float32Array(pts.length), y = new Float32Array(pts.length); let k = 0;
        for (const pt of pts) { if (!pt) continue; const c = pt.indexOf(','); x[k] = +pt.slice(0, c); y[k] = +pt.slice(c + 1); k++; }
        emit('scope', x.subarray(0, k), y.subarray(0, k));
        break;
      }
      case 'panel': {
        const raw = value.charAt(0) === '{' ? value : b64(value);
        let obj = null; try { obj = JSON.parse(raw); } catch (e) {}
        if (obj) emit('panel', obj);
        break;
      }
      case 'hello': emit('hello', b64(value)); break;
      case 'midimap': emit('midimap', value); break;
      default: break;
    }
  };
  window.sampleRateChanged = function (sr) { emit('samplerate', sr); };
  window.LigaseBridge = bridge;
})();
