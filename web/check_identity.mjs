#!/usr/bin/env node
/* check_identity.mjs — the Step-2 engine-identity GATE as a required CI check
 * (Plans/web_build.md Arc A). Runs the WASM node harness (web/host_identity.js) and asserts
 * the engine reproduces the native AUTOMATED_TEST_PROCEDURE baseline. Exits non-zero (fails
 * the build) if the baseline regresses.
 *
 *   native baseline (pd 0.54.1 / native libpd 0.56.5, byte-identical):
 *     test_auto     -> RMS 0.372309, MAX 0.608858 (sox displays 0.608839), 132288 frames/ch
 *     test_playback -> buffer check L=R=0.330109
 *   The WASM build matches RMS/frames/buffer exactly at this precision; residual per-sample
 *   deltas are <=1 ULP (cross-target x86-64/glibc vs wasm32/musl), invisible here.
 *
 *   usage: node web/check_identity.mjs [path/to/host_identity.js]
 */
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const harness = process.argv[2] || join(HERE, 'host_identity.js');
if (!existsSync(harness)) {
  console.error(`check_identity: harness not found: ${harness}\n  build it: EMSDK=.. LIBPD_DIR=.. TARGET=identity web/build_wasm.sh`);
  process.exit(2);
}

// Tolerances: the gate metrics are bit-exact at printed precision; allow a hair for rounding.
const RMS_REF = 0.372309, RMS_TOL = 1e-4;
const BUF_REF = 0.330109, BUF_TOL = 1e-4;
const FRAMES_REF = 132288;

// host_identity prints the RMS line to stdout and the buffer-check line to stderr — capture both.
const res = spawnSync('node', [harness], { encoding: 'utf8' });
const combined = (res.stdout || '') + (res.stderr || '');
process.stdout.write(combined);

const rmsM = combined.match(/WASM-AUTO RMS ([\d.]+)\s+MAX ([\d.]+)\s+frames\/ch (\d+)/);
const bufM = combined.match(/buffer check: avg amplitude L=([\d.]+) R=([\d.]+)/);

const fails = [];
if (!rmsM) fails.push('no WASM-AUTO line (harness did not produce a reel)');
else {
  const rms = +rmsM[1], frames = +rmsM[3];
  if (Math.abs(rms - RMS_REF) > RMS_TOL) fails.push(`auto RMS ${rms} != ${RMS_REF} (tol ${RMS_TOL})`);
  if (frames !== FRAMES_REF) fails.push(`auto frames ${frames} != ${FRAMES_REF}`);
}
if (!bufM) fails.push('no buffer-check line (playback did not load the reel)');
else {
  const l = +bufM[1], r = +bufM[2];
  if (Math.abs(l - BUF_REF) > BUF_TOL) fails.push(`playback L ${l} != ${BUF_REF} (tol ${BUF_TOL})`);
  if (Math.abs(r - BUF_REF) > BUF_TOL) fails.push(`playback R ${r} != ${BUF_REF} (tol ${BUF_TOL})`);
}

if (fails.length) {
  console.error('\nENGINE-IDENTITY GATE: FAIL');
  for (const f of fails) console.error('  - ' + f);
  process.exit(1);
}
console.error('\nENGINE-IDENTITY GATE: PASS (auto RMS 0.372309 / frames 132288 / buffer 0.330109 reproduced in WASM)');
