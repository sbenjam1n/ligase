#!/usr/bin/env bash
# build_wasm.sh — build the ligase WASM engine (Plans/web_build.md Arc A).
# Compiles libpd (static) + ligase's 15 sources + a host into a WASM module.
#   TARGET=node    -> host_test.js  (headless engine-identity harness, runs under node)
#   TARGET=worklet -> ligase_wasm.js (MODULARIZE for the browser AudioWorklet — Step 3+)
# Env: EMSDK (default /opt/emsdk), LIBPD_DIR (default /opt/libpd), TARGET (default node).
# CI clones emsdk + libpd fresh; locally they were set up during the Seq-92 spike.
set -euo pipefail
EMSDK="${EMSDK:-/opt/emsdk}"
LIBPD_DIR="${LIBPD_DIR:-/opt/libpd}"
TARGET="${TARGET:-node}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
EMCC="$EMSDK/upstream/emscripten/emcc"
EMAR="$EMSDK/upstream/emscripten/emar"
PD_SRC="$LIBPD_DIR/pure-data/src"
PD_WRAP="$LIBPD_DIR/libpd_wrapper"

# 1. libpd static archive (from the emcmake object build, archived statically)
LIBPD_A="$LIBPD_DIR/build_wasm/libpd_static.a"
if [ ! -f "$LIBPD_A" ]; then
  echo "[build] libpd static archive missing at $LIBPD_A"
  echo "[build] run: cd $LIBPD_DIR && emcmake cmake -B build_wasm -DCMAKE_BUILD_TYPE=Release && emmake make -C build_wasm -j4"
  echo "[build] then: emar rcs $LIBPD_A \$(find $LIBPD_DIR/build_wasm/CMakeFiles/libpd.dir -name '*.o')"
  exit 1
fi

# 2. compile ligase sources -> WASM objects
mkdir -p "$HERE/obj"
SRCS="ligase~ envelope grain grain_delay grain_delay_stut grain_delay_bencina grain_distortion grain_moogladder grain_smear grain_smear_bank reel splice perlin sphere morph"
for s in $SRCS; do
  "$EMCC" -O2 -DPD -I"$PD_SRC" -c "$ROOT/src/$s.c" -o "$HERE/obj/$s.o"
done

# 3. link
COMMON=(-I"$PD_SRC" -I"$PD_WRAP" "$HERE/obj/"*.o "$LIBPD_A" -lm
        -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_STACK=4MB)
if [ "$TARGET" = node ]; then
  "$EMCC" "$HERE/host_headless.c" "${COMMON[@]}" \
    --embed-file "$HERE/test_sine.pd" --embed-file "$HERE/test_ligase.pd" \
    -o "$HERE/host_test.js"
  echo "[build] wrote host_test.js (node harness)"
else
  # browser module: MODULARIZE, export libpd API for the AudioWorklet glue (Step 3+)
  "$EMCC" "${COMMON[@]}" -O3 \
    -s MODULARIZE=1 -s EXPORT_NAME=createLigaseModule -s EXPORT_ES6=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
    -o "$HERE/ligase_wasm.js"
  echo "[build] wrote ligase_wasm.js (browser module)"
fi
