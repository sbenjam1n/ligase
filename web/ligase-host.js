/* ligase-host.js — main-thread orchestration for the ligase WASM AudioWorklet
 * (Plans/web_build.md Arc A, Step 3/4) + the browser BRIDGE for the panel brain
 * (docs/ui/panel_bridge.md). ES module.
 *
 * SINGLE-THREADED, plain-Pages safe: no SharedArrayBuffer, no COOP/COEP. One WASM engine
 * instance lives in the worklet; this side just wires Web Audio, ships it the .wasm bytes +
 * the patch, and relays control/reel messages over the port.
 *
 *   const eng = new LigaseEngine({ base: './' });
 *   await eng.start({ patch: '<pd text>' });     // must be called from a user gesture
 *   eng.setFloat('lgR_grainsize', 0.5);          // drive the lgR_ bus
 *   eng.watch('lgS_grainsize', v => ...);        // read echoes / state back
 *   eng.onMessage((recv, sel, atoms) => ...);    // outlet-9 replies (bound lg_state9)
 *   await eng.loadReel(file);                     // <input type=file> / drag -> MEMFS -> load
 *   const blob = await eng.saveReel();            // save -> MEMFS -> Blob (download)
 *   await eng.enableMic();                         // getUserMedia -> adc~ (opt-in)
 *
 *   const bridge = webBridge(eng, CONTROLS);      // the brain's bridge (control/msg/note/on)
 *   const surface = createPanel(bridge, CONTROLS, TABLES);
 */
export class LigaseEngine {
  constructor(opts = {}) {
    this.base = opts.base || './';
    this.ctx = null;
    this.node = null;
    this.gain = null;
    this.hooks = false;        // message/list hooks available in the loaded WASM (outlet 9)
    this._gainValue = 1;
    this._watchers = {};       // recv -> [cb...]
    this._prints = [];         // print listeners
    this._msgs = [];           // message/list listeners (recv, sel, atoms)
    this._saveResolvers = [];  // pending saveReel() promises
    this._preStart = [];       // control messages issued before start() — flushed on start
    this._readyResolve = null;
    this.ready = new Promise((res) => { this._readyResolve = res; });
  }

  onPrint(cb) { this._prints.push(cb); }
  onMessage(cb) { this._msgs.push(cb); }
  onScope(cb) { this._scope = cb; }
  onVU(cb) { this._vu = cb; }
  onScopeXY(cb) { this._scopexy = cb; }

  async _fetchText(url) { const r = await fetch(url); if (!r.ok) throw new Error('fetch ' + url); return r.text(); }
  async _fetchBytes(url) { const r = await fetch(url); if (!r.ok) throw new Error('fetch ' + url); return r.arrayBuffer(); }

  /* Build the AudioContext + worklet. `ctxClass` lets tests pass OfflineAudioContext. */
  async start({ patch, conf, ctxClass, contextOptions } = {}) {
    if (!patch) throw new Error('start() needs a patch');
    const Ctx = ctxClass || (window.AudioContext || window.webkitAudioContext);
    this.ctx = contextOptions ? new Ctx(contextOptions) : new Ctx();

    // The worklet cannot fetch/import, so hand it one Blob = emscripten glue + processor,
    // and the .wasm bytes via processorOptions.
    const [glue, proc, wasmBinary] = await Promise.all([
      this._fetchText(this.base + 'ligase_wasm.js'),
      this._fetchText(this.base + 'ligase-processor.js'),
      this._fetchBytes(this.base + 'ligase_wasm.wasm'),
    ]);
    const blobUrl = URL.createObjectURL(new Blob([glue + '\n' + proc], { type: 'application/javascript' }));
    await this.ctx.audioWorklet.addModule(blobUrl);
    URL.revokeObjectURL(blobUrl);

    this.node = new AudioWorkletNode(this.ctx, 'ligase', {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: { wasmBinary, patch, conf },
    });
    this.node.port.onmessage = (e) => this._onmsg(e.data);
    // MASTER: a display-side output gain (the panel's MASTER knob) between the engine and the DAC
    this.gain = this.ctx.createGain();
    this.gain.gain.value = this._gainValue;
    this.node.connect(this.gain);
    this.gain.connect(this.ctx.destination);
    // (re)register every watch and replay any control messages issued before start()
    for (const send of Object.keys(this._watchers)) this.node.port.postMessage({ type: 'watch', send });
    for (const msg of this._preStart) this.node.port.postMessage(msg);
    this._preStart = [];
    if (this.ctx.resume) { try { await this.ctx.resume(); } catch (_) {} }
    return this.ready;
  }

  _onmsg(m) {
    switch (m.type) {
      case 'ready': this.hooks = !!m.hooks; this._readyResolve && this._readyResolve(m); break;
      case 'print': for (const cb of this._prints) cb(m.text); break;
      case 'value': {
        const cbs = this._watchers[m.recv];
        if (cbs) for (const cb of cbs) cb(m.value);
        break;
      }
      case 'msg': for (const cb of this._msgs) cb(m.recv, m.sel, m.atoms); break;
      case 'reelBytes': {
        const r = this._saveResolvers.shift();
        if (r) r(new Blob([m.bytes], { type: 'audio/wav' }));
        break;
      }
      case 'scope': if (this._scope) this._scope(m.peak); break;
      case 'vu': if (this._vu) this._vu(m.l, m.r); break;
      case 'scopexy': if (this._scopexy) this._scopexy(m.x, m.y); break;
      case 'error': for (const cb of this._prints) cb('[worklet error] ' + m.text); break;
      default: break;
    }
  }

  /* --- control surface (lgR_ bus + ligase messages) ---
   * Messages issued before start() are buffered and replayed once the worklet exists, so the
   * panel can render and accept input before audio is armed. */
  _ctl(msg) { if (this.node) this.node.port.postMessage(msg); else this._preStart.push(msg); }
  setFloat(recv, value) { this._ctl({ type: 'float', recv, value: +value }); }
  sendBang(recv) { this._ctl({ type: 'bang', recv }); }
  sendSymbol(recv, value) { this._ctl({ type: 'symbol', recv, value: String(value) }); }
  sendMsg(recv, atoms) { this._ctl({ type: 'msg', recv, atoms }); }
  watch(send, cb) {
    (this._watchers[send] || (this._watchers[send] = [])).push(cb);
    if (this.node) this.node.port.postMessage({ type: 'watch', send });
  }
  /* output gain 0..2 (MASTER) — applied in the host, not the engine */
  setGain(v) { this._gainValue = Math.max(0, Math.min(2, +v || 0)); if (this.gain) this.gain.gain.value = this._gainValue; }

  /* --- reel import: File/Blob/ArrayBuffer -> worklet MEMFS -> `load` --- */
  async loadReel(fileOrBytes, path = '/tmp/reel.wav') {
    let bytes;
    if (fileOrBytes instanceof ArrayBuffer) bytes = fileOrBytes;
    else if (fileOrBytes.arrayBuffer) bytes = await fileOrBytes.arrayBuffer();
    else bytes = fileOrBytes.buffer || fileOrBytes;
    this.node.port.postMessage({ type: 'loadReel', path, bytes }, [bytes]);
  }

  /* --- reel export: `save` -> worklet MEMFS -> Blob --- */
  saveReel(path = '/tmp/reel_out.wav') {
    const p = new Promise((res) => this._saveResolvers.push(res));
    this.node.port.postMessage({ type: 'saveReel', path });
    return p;
  }

  /* --- audio-in (opt-in, default off): getUserMedia -> adc~ --- */
  async enableMic() {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    this.micStream = stream;
    this.micSource = this.ctx.createMediaStreamSource(stream);
    this.micSource.connect(this.node);   // feeds the worklet input -> [adc~]
    return true;
  }
  disableMic() {
    if (this.micSource) { this.micSource.disconnect(); this.micSource = null; }
    if (this.micStream) { for (const t of this.micStream.getTracks()) t.stop(); this.micStream = null; }
  }
}

/* Message text -> pd atoms: numbers become floats, everything else a symbol ('[' ']' '<' '>' and
 * euclid tokens like 1(3,8) included). */
const NUM_RE = /^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/;
export function tokenize(text) {
  return String(text).trim().split(/\s+/).filter(Boolean).map((t) => (NUM_RE.test(t) ? parseFloat(t) : t));
}

/* get_params lines that carry ONE float (src/ligase~.c ligase_get_params) — collected into status.params */
const PARAM_LINES = new Set(['speed', 'grainsize', 'grainstart', 'organize', 'scanrate', 'sos', 'iot', 'maxgrains',
  'gdelay', 'gdelay_feed', 'stut_reduction', 'gdelay_tone', 'stut_spacing', 'gdelay_mix', 'smear', 'moog_cutoff',
  'moog_resonance', 'moog_mix', 'midi', 'env_skew', 'amplitude', 'pan', 'bpm']);

/* webBridge(engine, CONTROLS, opts) — the bridge object the panel brain talks to
 * (docs/ui/panel_bridge.md), on top of LigaseEngine:
 *   control(id, value, text)  text != null -> msg(text) (a knob's MESSAGE TWIN: the software host
 *                             delivers knobs as messages, see panel_layout.INLET_SELECTORS);
 *                             a CV-only inlet (text null) -> engine.setFloat('lgR_' + id) (the
 *                             patch's line~ chain); 'master' -> host output gain
 *   arm()                     call once after engine.start(): headless 1 + message cursor, and
 *                             every message-delivered CV chain parked at 0 (= unpatched), so the
 *                             patch's own loadbang defaults never out-rank the knob messages
 *   msg(text)                 ';'-separated messages -> engine.sendMsg('lg_engine', atoms)
 *   note(ch, note, vel)       -> `midi <note> <vel> <ch>`
 *   host(action)              -> opts.host(action)  ('load_reel' | 'save_reel')
 *   on(event, cb)             'status' (assembled from a 10 Hz get_params poll), 'out9' (every
 *                             outlet-9 line), 'control' (an lgS_ echo NOT caused by our own send —
 *                             a scripted / patch-side change), 'vu', 'scope', 'print'; returns off()
 * opts: { pollMs = 100, poll = true, host }. The poll starts once the engine reports ready. */
export function webBridge(engine, CONTROLS, opts = {}) {
  const inletIds = new Set((CONTROLS || []).filter((c) => Array.isArray(c.bind) && c.bind[0] === 'inlet').map((c) => c.id));
  const msgInletIds = new Set((CONTROLS || []).filter((c) => Array.isArray(c.bind) && c.bind[0] === 'inlet' && (c.msg || c.id === 'joy_x' || c.id === 'joy_y')).map((c) => c.id));
  const listeners = {};
  const emit = (ev, ...a) => { for (const cb of listeners[ev] || []) { try { cb(...a); } catch (e) { console.error('[bridge]', ev, e); } } };
  const on = (ev, cb) => { (listeners[ev] || (listeners[ev] = [])).push(cb); return () => off(ev, cb); };
  const off = (ev, cb) => { if (listeners[ev]) listeners[ev] = listeners[ev].filter((f) => f !== cb); };

  const msg = (text) => {
    for (const part of String(text == null ? '' : text).split(';')) {
      const atoms = tokenize(part);
      if (atoms.length) engine.sendMsg('lg_engine', atoms);
    }
  };
  const pending = {};                  // id -> our own sends not yet echoed on lgS_<id>
  const control = (id, value, text) => {
    if (id === 'master') { engine.setGain(value); return; }
    if (text != null) { msg(text); return; }
    if (inletIds.has(id)) { pending[id] = (pending[id] || 0) + 1; engine.setFloat('lgR_' + id, value); }
  };
  // software-host knob contract: the engine's inlets stay unpatched (headless 1) and the panel's
  // message cursor drives the metasurface; the message-delivered CV chains are parked at 0
  const arm = () => {
    msg('headless 1; morph_cursor 0');
    for (const id of msgInletIds) { pending[id] = (pending[id] || 0) + 1; engine.setFloat('lgR_' + id, 0); }
  };
  const note = (ch, n, vel) => engine.sendMsg('lg_engine', ['midi', Math.round(n), Math.round(vel), Math.round(ch || 1)]);
  const host = (action) => { if (typeof opts.host === 'function') return opts.host(action); };

  // --- outlet 9 -> 'out9' + the assembled 'status' object ---
  const status = { params: {} };
  let dirty = false;
  const flush = () => { if (!dirty) return; dirty = false; emit('status', Object.assign({}, status, { params: Object.assign({}, status.params) })); };
  engine.onMessage((recv, sel, atoms) => {
    if (recv !== 'lg_state9') return;
    atoms = Array.isArray(atoms) ? atoms : [];
    emit('out9', sel, atoms);
    const a0 = atoms[0], a1 = atoms[1];
    switch (sel) {
      case 'splice': status.splice = a0; status.splices = a1; dirty = true; break;
      case 'reel': status.reelLen = a0; status.reelSr = a1; dirty = true; break;
      case 'playing': status.playing = a0 ? 1 : 0; dirty = true; break;
      case 'recording': status.recording = a0 ? 1 : 0; dirty = true; break;
      case 'rec_mode': status.recMode = a0; dirty = true; flush(); break;   // last line of a get_params frame
      default:
        if (PARAM_LINES.has(sel) && typeof a0 === 'number') { status.params[sel] = a0; if (sel === 'bpm') status.bpm = a0; dirty = true; }
        break;
    }
  });
  let timer = null;
  const stopPoll = () => { if (timer) { clearInterval(timer); timer = null; } };
  const startPoll = (ms) => {
    stopPoll();
    timer = setInterval(() => { flush(); engine.sendMsg('lg_engine', ['get_params']); }, ms || opts.pollMs || 100);
  };
  if (opts.poll !== false) engine.ready.then(() => startPoll());

  // --- lgS_ echoes: our own sends drain `pending`; anything else is a host/patch-side change ---
  for (const id of inletIds) {
    engine.watch('lgS_' + id, (v) => {
      if (pending[id] > 0) { pending[id]--; return; }
      emit('control', id, v);
    });
  }
  engine.onPrint((t) => emit('print', t, /error|failed|cannot/i.test(t)));
  engine.onVU((l, r) => emit('vu', l, r));
  engine.onScopeXY((x, y) => emit('scope', x, y));

  return { control, msg, note, host, arm, on, off, startPoll, stopPoll, tokenize, engine,
           get status() { return status; }, dispose: stopPoll };
}
