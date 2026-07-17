/* ligase-processor.js — the AudioWorkletProcessor that runs the ligase WASM engine on the
 * audio thread (Plans/web_build.md Arc A, Step 3).
 *
 * SINGLE-THREADED, no SharedArrayBuffer: one WASM instance lives here, in the
 * AudioWorkletGlobalScope. Because worklets can neither fetch() nor import scripts, the host
 * concatenates the emscripten glue (classic MODULARIZE -> global createLigaseModule) ahead of
 * this file into one Blob for audioWorklet.addModule(), and passes the .wasm bytes in
 * processorOptions.wasmBinary. Instantiation is synchronous (WASM_ASYNC_COMPILATION=0).
 *
 * libpd runs in fixed 64-frame blocks; a render quantum is 128 frames => 2 ticks. Audio is
 * interleaved per frame across channels for libpd_process_float.
 *
 * Control surface (all over the existing lgR_/lgS_ bus + ligase messages), via port messages:
 *   {type:'float',  recv, value}         -> libpd_float(recv, value)         (drive lgR_*)
 *   {type:'msg',    recv, atoms:[...]}   -> start/add/finish_list(recv)      (e.g. ['grainsize',0.5])
 *   {type:'watch',  send}                -> libpd_bind(send)  (echo lgS_* / state back to host)
 *   {type:'loadReel', path, bytes}       -> FS.writeFile(path,bytes); float? no -> msg 'load'
 *   {type:'saveReel', path}              -> msg 'save'; then read FS -> post {type:'reelBytes'}
 *   {type:'micEnable', on}               -> (host wires the graph; this just notes state)
 * Outbound to host:
 *   {type:'ready', sampleRate, blocksize}
 *   {type:'print', text}
 *   {type:'value', recv, value}          (a watched lgS_/state float)
 *   {type:'reelBytes', path, bytes}      (transferable ArrayBuffer)
 */

class LigaseProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.ticks = 0;
    this.watched = {};                 // send-symbol -> bound ptr (kept alive)
    const opts = options.processorOptions || {};
    this.port.onmessage = (e) => this._onmsg(e.data);

    // createLigaseModule is defined by the glue prepended to this file.
    // eslint-disable-next-line no-undef
    createLigaseModule({ wasmBinary: opts.wasmBinary })
      .then((mod) => this._boot(mod, opts))
      .catch((err) => this.port.postMessage({ type: 'error', text: String(err) }));
  }

  _boot(mod, opts) {
    this.mod = mod;
    const c = (name, ret, args) => mod.cwrap(name, ret, args);
    this._init        = c('libpd_init', 'number', []);
    this._initAudio   = c('libpd_init_audio', 'number', ['number', 'number', 'number']);
    this._process     = c('libpd_process_float', 'number', ['number', 'number', 'number']);
    this._openfile    = c('libpd_openfile', 'number', ['string', 'string']);
    this._startMsg    = c('libpd_start_message', 'number', ['number']);
    this._addFloat    = c('libpd_add_float', null, ['number']);
    this._addSymbol   = c('libpd_add_symbol', null, ['string']);
    this._finishList  = c('libpd_finish_list', 'number', ['string']);
    this._finishMsg   = c('libpd_finish_message', 'number', ['string', 'string']);
    this._float       = c('libpd_float', 'number', ['string', 'number']);
    this._bang        = c('libpd_bang', 'number', ['string']);
    this._symbol      = c('libpd_symbol', 'number', ['string', 'string']);
    this._bind        = c('libpd_bind', 'number', ['string']);
    this._setPrint    = c('libpd_set_printhook', null, ['number']);
    this._setFloat    = c('libpd_set_floathook', null, ['number']);
    this._setupLigase = c('ligase_tilde_setup', null, []);

    // Register print + float hooks as C callbacks (ALLOW_TABLE_GROWTH + addFunction).
    const printHook = mod.addFunction((sPtr) => {
      this.port.postMessage({ type: 'print', text: mod.UTF8ToString(sPtr) });
    }, 'vi');
    const floatHook = mod.addFunction((recvPtr, val) => {
      this.port.postMessage({ type: 'value', recv: mod.UTF8ToString(recvPtr), value: val });
    }, 'vif');
    this._setPrint(printHook);
    this._setFloat(floatHook);

    this._init();
    this._setupLigase();                              // register the compiled-in external
    // Arc B (Plans/web_build.md B3): if primase was compiled into this WASM module,
    // register it too, right after ligase — harmless-absent when built without PRIMASE_DIR.
    if (mod._primase_setup) { c('primase_setup', null, [])(); }
    // AudioWorklet's sampleRate global is the graph rate; libpd adapts its 64-block to it.
    this._initAudio(2, 2, sampleRate);                // 2 in, 2 out

    // Write the patch into MEMFS and open it.
    if (opts.patch) {
      this.mod.FS.writeFile('/patch.pd', opts.patch);
      // ligase.conf (max_grains) is optional; write if provided so the engine reads its cap.
      if (opts.conf) this.mod.FS.writeFile('/ligase.conf', opts.conf);
      const h = this._openfile('patch.pd', '/');
      if (!h) this.port.postMessage({ type: 'error', text: 'libpd_openfile failed' });
    }
    // DSP on.
    this._startMsg(1); this._addFloat(1); this._finishMsg('pd', 'dsp');

    // Interleaved scratch buffers: 128 frames (2 ticks * 64) * 2 channels.
    this.blocksize = 64;
    this.quantum = 128;
    this.nch = 2;
    const n = this.quantum * this.nch;
    this._inPtr  = this.mod._malloc(n * 4);
    this._outPtr = this.mod._malloc(n * 4);
    this._n = n;
    this.ticks = this.quantum / this.blocksize;       // 2

    this.ready = true;
    this.port.postMessage({ type: 'ready', sampleRate, blocksize: this.blocksize });
    // Flush any queued control messages that arrived before boot.
    if (this._queue) { for (const m of this._queue) this._apply(m); this._queue = null; }
  }

  _onmsg(m) {
    if (!this.ready) { (this._queue || (this._queue = [])).push(m); return; }
    this._apply(m);
  }

  _apply(m) {
    switch (m.type) {
      case 'float':
        this._float(m.recv, m.value);
        break;
      case 'bang':
        this._bang(m.recv);
        break;
      case 'symbol':
        this._symbol(m.recv, m.value);
        break;
      case 'msg': {
        // If the first atom is a symbol, send a TYPED message (selector = atom0, rest = args) —
        // this is how ligase's methods (load/save/grainsize/...) are dispatched, matching the
        // patch's [msg <sel> $1] boxes. All-float payloads go as a plain list.
        const atoms = m.atoms;
        if (atoms.length && typeof atoms[0] === 'string') {
          this._startMsg(atoms.length - 1);
          for (let i = 1; i < atoms.length; i++) {
            if (typeof atoms[i] === 'number') this._addFloat(atoms[i]); else this._addSymbol(atoms[i]);
          }
          this._finishMsg(m.recv, atoms[0]);
        } else {
          this._startMsg(atoms.length);
          for (const a of atoms) { if (typeof a === 'number') this._addFloat(a); else this._addSymbol(a); }
          this._finishList(m.recv);
        }
        break;
      }
      case 'watch': {
        if (!this.watched[m.send]) this.watched[m.send] = this._bind(m.send);
        break;
      }
      case 'loadReel': {
        // Import: WAV bytes -> MEMFS -> typed `load <path>` on the engine bus [r lg_engine].
        this.mod.FS.writeFile(m.path, new Uint8Array(m.bytes));
        this._startMsg(1); this._addSymbol(m.path); this._finishMsg(m.ctl || 'lg_engine', 'load');
        break;
      }
      case 'saveReel': {
        // Export: typed `save <path>` -> engine writes WAV to MEMFS; read it back next block.
        this._startMsg(1); this._addSymbol(m.path); this._finishMsg(m.ctl || 'lg_engine', 'save');
        this._pendingSave = m.path;
        this._pendingSaveWait = 3;   // give the write a few blocks before reading back
        break;
      }
      default:
        break;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.ready) { for (const ch of out) ch.fill(0); return true; }

    const input = inputs[0];
    const HEAP = this.mod.HEAPF32;
    const inBase = this._inPtr >> 2;
    const outBase = this._outPtr >> 2;
    const L = out.length > 0 ? out[0].length : this.quantum;
    const frames = Math.min(L, this.quantum);

    // Interleave inputs (adc~). Silent when no mic connected.
    const inL = (input && input[0]) ? input[0] : null;
    const inR = (input && input[1]) ? input[1] : inL;
    for (let i = 0; i < frames; i++) {
      HEAP[inBase + i * 2]     = inL ? inL[i] : 0;
      HEAP[inBase + i * 2 + 1] = inR ? inR[i] : 0;
    }

    this._process(this.ticks, this._inPtr, this._outPtr);

    // De-interleave to outputs (dac~), tracking a peak for the scope tap.
    const o0 = out[0], o1 = out.length > 1 ? out[1] : null;
    let peak = this._peak || 0;
    for (let i = 0; i < frames; i++) {
      const l = HEAP[outBase + i * 2];
      o0[i] = l;
      const a = l < 0 ? -l : l; if (a > peak) peak = a;
      if (o1) o1[i] = HEAP[outBase + i * 2 + 1];
    }
    // Scope: post the running output peak roughly every ~85 ms so a headless host can confirm
    // DSP is producing audio without an output device.
    this._peak = peak;
    this._scopeN = (this._scopeN || 0) + 1;
    if (this._scopeN >= 32) { this.port.postMessage({ type: 'scope', peak }); this._scopeN = 0; this._peak = 0; }

    // Deferred reel save readback (engine wrote to MEMFS on a previous block).
    if (this._pendingSave && (this._pendingSaveWait = (this._pendingSaveWait || 0) - 1) <= 0) {
      try {
        const bytes = this.mod.FS.readFile(this._pendingSave); // Uint8Array
        const buf = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length);
        this.port.postMessage({ type: 'reelBytes', path: this._pendingSave, bytes: buf }, [buf]);
      } catch (e) {
        this.port.postMessage({ type: 'error', text: 'saveReel readback: ' + e });
      }
      this._pendingSave = null;
    }
    return true;
  }
}

registerProcessor('ligase', LigaseProcessor);
