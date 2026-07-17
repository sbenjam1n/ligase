# web/ — ligase in the browser (Plans/web_build.md, Arc A)

ligase compiled to **WebAssembly via Emscripten**, running libpd with the `ligase~`
external **statically compiled in** (WebPd can't run compiled externals — this is the
same "compiled-in" constraint as the VST path). Served as static files → GitHub Pages,
built by CI. **License-clean:** libpd + pure-data are BSD ("Standard Improved BSD
License"), Emscripten is MIT/NCSA — all GPL-compatible, so the GPL-2 ligase web build is
distributable with no relicense (unlike the GPLv3 plugdata/VST3 path).

## Status (Seq 92 — foundation proven)

Toolchain spike + engine-compile-in are **verified headless in node**:
- Stock libpd→WASM: `test_sine.pd` → **RMS 0.141421 / MAX 0.200000** (a 0.2-amp 440 Hz sine).
- **ligase compiled in**: all 15 `src/*.c` compile under emcc; `[ligase~]` instantiates
  (no "couldn't create") and processes audio in WASM.

Remaining (Steps 3–5): browser AudioWorklet host + HTML, `emit_web.py` GUI from
`panel_layout.py` driving the `lgR_*` bus, `getUserMedia` audio-in, MEMFS reel import +
Blob export, the rigorous exact-baseline engine-identity test, and the GitHub Actions →
Pages workflow.

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
Then, from the repo:
```bash
EMSDK=/opt/emsdk LIBPD_DIR=/opt/libpd TARGET=node    web/build_wasm.sh   # node harness
EMSDK=/opt/emsdk LIBPD_DIR=/opt/libpd TARGET=worklet web/build_wasm.sh   # browser module
node web/host_test.js test_sine.pd 4096 0     # -> RMS 0.141421 (stock libpd proof)
node web/host_test.js test_ligase.pd 4096 1   # -> ligase~ loads + processes in WASM
```

## Files
- `build_wasm.sh` — reproducible build (libpd static + ligase objects → WASM module).
- `host_headless.c` — the node engine-identity harness (registers `ligase_tilde_setup`,
  opens a patch, runs blocks, prints RMS/MAX).
- `test_sine.pd` / `test_ligase.pd` — spike patches.
- (build outputs `obj/`, `host_test.*`, `ligase_wasm.*` are gitignored.)
