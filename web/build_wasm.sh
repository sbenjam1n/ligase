#!/usr/bin/env bash
# build_wasm.sh — build the ligase WASM engine (Plans/web_build.md Arc A).
# Compiles libpd (static) + ligase's 15 sources + a host into a WASM module.
#   TARGET=node     -> host_test.js     (headless smoke harness: sine/ligase RMS, runs under node)
#   TARGET=identity -> host_identity.js  (Step-2 engine-identity gate: reproduces the native
#                                         AUTOMATED_TEST_PROCEDURE baseline in WASM, runs under node)
#   TARGET=worklet  -> ligase_wasm.js    (MODULARIZE for the browser AudioWorklet — Step 3+)
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
elif [ "$TARGET" = identity ]; then
  # Step-2 engine-identity gate. Embeds the two self-running procedure patches; reads the
  # reel back out of MEMFS. No -ffast-math (bit-faithful IEEE-754), matching the native build.
  "$EMCC" "$HERE/host_identity.c" "${COMMON[@]}" \
    --embed-file "$HERE/test_auto_wasm.pd@test_auto_wasm.pd" \
    --embed-file "$HERE/test_playback_wasm.pd@test_playback_wasm.pd" \
    -o "$HERE/host_identity.js"
  echo "[build] wrote host_identity.js (engine-identity harness)"
else
  # Browser module for the single-threaded AudioWorklet (Step 3+). Plain-Pages safe:
  #   - NO pthreads / NO SharedArrayBuffer  -> works without COOP/COEP headers.
  #   - MODULARIZE (classic, NOT ES6)       -> defines a global createLigaseModule factory; the
  #                                            host concatenates this glue + the processor into one
  #                                            Blob for audioWorklet.addModule (worklets can't
  #                                            import/fetch). Also usable from a <script> tag.
  #   - ENVIRONMENT=web,worker              -> no node/shell glue paths.
  #   - WASM_ASYNC_COMPILATION=0            -> synchronous instantiate from a passed wasmBinary
  #                                            (AudioWorkletGlobalScope has no fetch()).
  #   - ALLOW_TABLE_GROWTH + addFunction    -> register JS print/float hooks as C callbacks.
  # The full libpd C API is exported so the worklet drives the engine (process, messages,
  # the lgR_/lgS_ bus, reel MEMFS I/O).
  LIGASE_EXPORTS='["_libpd_init","_libpd_clear_search_path","_libpd_add_to_search_path","_libpd_init_audio","_libpd_process_float","_libpd_openfile","_libpd_closefile","_libpd_start_message","_libpd_add_float","_libpd_add_symbol","_libpd_finish_message","_libpd_finish_list","_libpd_float","_libpd_symbol","_libpd_bang","_libpd_bind","_libpd_unbind","_libpd_set_printhook","_libpd_set_floathook","_libpd_blocksize","_ligase_tilde_setup","_malloc","_free"]'
  "$EMCC" "${COMMON[@]}" -O3 \
    -s MODULARIZE=1 -s EXPORT_NAME=createLigaseModule \
    -s ENVIRONMENT=web,worker -s WASM_ASYNC_COMPILATION=0 \
    -s ALLOW_TABLE_GROWTH=1 \
    -s EXPORTED_FUNCTIONS="$LIGASE_EXPORTS" \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","addFunction","removeFunction","FS","HEAPF32","UTF8ToString","stringToUTF8","lengthBytesUTF8","getValue","setValue"]' \
    -o "$HERE/ligase_wasm.js"
  echo "[build] wrote ligase_wasm.js (browser AudioWorklet module)"
fi
