# web/ — ligase in the browser (Plans/web_build.md, Arc A)

ligase compiled to **WebAssembly via Emscripten**, running libpd with the `ligase~`
external **statically compiled in** (WebPd can't run compiled externals — this is the
same "compiled-in" constraint as the VST path). Served as static files → GitHub Pages,
built by CI. **License-clean:** libpd + pure-data are BSD ("Standard Improved BSD
License"), Emscripten is MIT/NCSA — all GPL-compatible, so the GPL-2 ligase web build is
distributable with no relicense (unlike the GPLv3 plugdata/VST3 path).

## Status — Arc A Steps 1–5 verified headless

- **Step 1 (foundation, Seq 92).** Stock libpd→WASM `test_sine.pd` → RMS 0.141421 / MAX
  0.200000; all 15 `src/*.c` compile under emcc; `[ligase~]` instantiates + processes in WASM.
- **Step 2 (engine identity).** The native AUTOMATED_TEST_PROCEDURE baseline is reproduced
  INSIDE the WASM build via the node harness (`host_identity.c` runs the same self-driving
  patches through libpd's own scheduler, then reads the saved reel back out of MEMFS):
  `test_auto` → **RMS 0.372309 / MAX 0.608858 / 132288 frames-per-channel**, `test_playback`
  → **buffer check L=R=0.330109** — matching the native gate at printed precision. Residual
  per-sample deltas are **≤1 ULP** (max abs 1.19e-7, RMS-of-diff 2.6e-8, ~33% of samples),
  a cross-target x86-64/glibc vs wasm32/musl last-bit difference. Ruled out: FMA/contraction
  (`-ffp-contract=off` changes nothing), pd version (native libpd 0.56.5 == Debian pd 0.54.1
  byte-for-byte), and `noise~` arithmetic (bit-portable in isolation). RMS/frames/buffer are
  gate-invariant. `check_identity.mjs` asserts this and FAILS the build on regression.
- **Step 3 (AudioWorklet + HTML).** Single-threaded worklet (NO SharedArrayBuffer / no
  COOP/COEP) runs `libpd_process_float` on the audio thread; `index.html` starts it on a
  gesture. Verified headless (chromium): the WASM engine instantiates in the worklet and
  produces nonzero audio (self-test peak 1.0).
- **Step 4 (`emit_web.py` GUI + audio-in + reel I/O).** `docs/ui/emit_web.py` reads
  `panel_layout.py` and emits 304 web controls (`ligase_controls.js`) driving the `lgR_`/`lgS_`
  bus. Verified headless: driving `lgR_grainsize 0.73` reads back `lgS_grainsize 0.73`;
  a WAV written to MEMFS loads (11025 samples reported); `save` writes a valid 32-bit-float WAV
  (fmt=3) read back as a Blob; the `getUserMedia`→`[adc~]` graph builds (opt-in, default off).
- **Step 5 (CI → Pages).** `.github/workflows/web-deploy.yml` installs emsdk 6.0.3, builds
  libpd, runs the Step-2 gate as a REQUIRED check, builds the worklet module, runs `emit_web.py`,
  assembles the site, and deploys via `actions/deploy-pages`. Plain-Pages safe (static only).
  **First run is owner-triggered:** enable Pages (Settings → Pages → Source = GitHub Actions).

## Building

Prereqs (set up fresh in CI; the spike used `/opt`):
```bash
# Emscripten
git clone --depth 1 https://github.com/emscripten-core/emsdk /opt/emsdk
/opt/emsdk/emsdk install latest && /opt/emsdk/emsdk activate latest
# libpd (+ pure-data submodule), static WASM archive
git clone --depth 1 --recurse-submodules https://github.com/libpd/libpd /opt/libpd
cd /opt/libpd && emcmake cmake -B build_wasm -DCMAKE_BUILD_TYPE=Release && emmake make -C build_wasm -j4
emar rcs build_wasm/libpd_static.a $(find build_wasm/CMakeFiles/libpd.dir -name '*.o')
```
Then, from the repo (`EMSDK`/`LIBPD_DIR` default to `/opt/emsdk` and `/opt/libpd`):
```bash
TARGET=node     web/build_wasm.sh && node web/host_test.js test_sine.pd 4096 0   # RMS 0.141421
TARGET=identity web/build_wasm.sh && node web/check_identity.mjs                 # Step-2 GATE
TARGET=worklet  web/build_wasm.sh                                                # browser module
python3 docs/ui/emit_web.py                                                      # web controls
web/assemble_site.sh                                                             # -> web/site/
# then serve + open:  (cd web/site && python3 -m http.server 8000)  ->  localhost:8000
```

Headless browser check (chromium/Playwright) drives the real worklet path — DSP-nonzero,
lgR_/lgS_ readback, MEMFS reel load/save, mic-graph wiring — via `test_harness.html`.

## Files (sources tracked; build outputs gitignored)
- `build_wasm.sh` — reproducible build: `TARGET=node|identity|worklet`.
- `host_headless.c` — smoke harness (opens a patch, prints RMS/MAX).
- `host_identity.c` — Step-2 engine-identity harness (self-driving patches → MEMFS reel readback).
- `check_identity.mjs` — asserts the Step-2 baseline; the required CI gate.
- `test_sine.pd` / `test_ligase.pd` — spike patches.
- `test_auto_wasm.pd` / `test_playback_wasm.pd` — the AUTOMATED_TEST_PROCEDURE patches, minus
  `pd quit` (the harness controls block count), for the identity gate.
- `worklet_selftest.pd` — noise→ligase→dac self-source for the Step-3 DSP-nonzero check.
- `ligase-processor.js` — the AudioWorkletProcessor (one WASM engine on the audio thread).
- `ligase-host.js` — main-thread orchestration (Web Audio, port bus, reel/mic).
- `index.html` — the deployable player shell (start / mic / reel load+export + generated GUI).
- `test_harness.html` — headless verification API (test-only; not deployed).
- `assemble_site.sh` — gathers the flat static site into `web/site/`.
- (generated: `ligase_controls.js` from `docs/ui/emit_web.py`; outputs `obj/`, `host_*.js/.wasm`,
  `ligase_wasm.*`, `web/site/` are gitignored.)
