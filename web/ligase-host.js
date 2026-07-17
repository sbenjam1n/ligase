/* ligase-host.js — main-thread orchestration for the ligase WASM AudioWorklet
 * (Plans/web_build.md Arc A, Step 3/4). ES module.
 *
 * SINGLE-THREADED, plain-Pages safe: no SharedArrayBuffer, no COOP/COEP. One WASM engine
 * instance lives in the worklet; this side just wires Web Audio, ships it the .wasm bytes +
 * the patch, and relays control/reel messages over the port.
 *
 *   const eng = new LigaseEngine({ base: './' });
 *   await eng.start({ patch: '<pd text>' });     // must be called from a user gesture
 *   eng.setFloat('lgR_grainsize', 0.5);          // drive the lgR_ bus
 *   eng.watch('lgS_grainsize', v => ...);        // read echoes / state back
 *   await eng.loadReel(file);                     // <input type=file> / drag -> MEMFS -> load
 *   const blob = await eng.saveReel();            // save -> MEMFS -> Blob (download)
 *   await eng.enableMic();                         // getUserMedia -> adc~ (opt-in)
 */
export class LigaseEngine {
  constructor(opts = {}) {
    this.base = opts.base || './';
    this.ctx = null;
    this.node = null;
    this._watchers = {};       // recv -> [cb...]
    this._prints = [];         // print listeners
    this._saveResolvers = [];  // pending saveReel() promises
    this._preStart = [];       // control messages issued before start() — flushed on start
    this._readyResolve = null;
    this.ready = new Promise((res) => { this._readyResolve = res; });
  }

  onPrint(cb) { this._prints.push(cb); }
  onScope(cb) { this._scope = cb; }

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
    this.node.connect(this.ctx.destination);
    // (re)register every watch and replay any control messages issued before start()
    for (const send of Object.keys(this._watchers)) this.node.port.postMessage({ type: 'watch', send });
    for (const msg of this._preStart) this.node.port.postMessage(msg);
    this._preStart = [];
    if (this.ctx.resume) { try { await this.ctx.resume(); } catch (_) {} }
    return this.ready;
  }

  _onmsg(m) {
    switch (m.type) {
      case 'ready': this._readyResolve && this._readyResolve(m); break;
      case 'print': for (const cb of this._prints) cb(m.text); break;
      case 'value': {
        const cbs = this._watchers[m.recv];
        if (cbs) for (const cb of cbs) cb(m.value);
        break;
      }
      case 'reelBytes': {
        const r = this._saveResolvers.shift();
        if (r) r(new Blob([m.bytes], { type: 'audio/wav' }));
        break;
      }
      case 'scope': if (this._scope) this._scope(m.peak); break;
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
