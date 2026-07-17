/* host_identity.c — WASM engine-identity harness (Plans/web_build.md Arc A, Step 2).
 *
 * Reproduces the native AUTOMATED_TEST_PROCEDURE baseline INSIDE the WASM build, in node.
 * It runs the SAME patches (test_auto_wasm.pd / test_playback_wasm.pd — byte-identical
 * wiring to test_auto.pd / test_playback.pd minus the `pd quit` message) through libpd's
 * OWN scheduler, so pd's deterministic noise~ and ligase~'s recorder produce the same
 * samples they do natively. It drives no messages by hand: the patches self-run via
 * loadbang+delay, exactly as native pd -nogui does.
 *
 *   Phase 1 (auto):     open test_auto_wasm.pd, tick libpd until ligase~ has recorded 3 s
 *                       of noise~ and `save /tmp/ligase_test.wav` has fired into MEMFS.
 *   Phase 2 (readback): parse the WAV back out of MEMFS, compute RMS/MAX over all
 *                       interleaved samples (== `sox -n stat`), print them.
 *   Phase 3 (playback): open test_playback_wasm.pd, tick until `load` fires; ligase~'s own
 *                       "buffer check: avg amplitude L=.. R=.." line is captured via the
 *                       print hook (stderr) — the native playback gate.
 *
 * Native baseline (pd 0.54.1, this repo): auto RMS 0.372309 / MAX 0.608839 (132288/ch @ 44100);
 * playback buffer check L=R=0.330109. GATE: match these in WASM (or characterize the delta).
 *
 * Sample rate MUST be 44100 (pd's default; the reel is written at the DSP rate) — NOT 48000.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "z_libpd.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* Optional: with LIGASE_DUMP_REEL=<realpath> set, copy the MEMFS reel out to the real
 * filesystem (node only) so it can be byte-compared against the native reel. Diagnostic. */
static void dump_reel(const char *memfs_path){
#ifdef __EMSCRIPTEN__
  EM_ASM({
    var out = (typeof process !== 'undefined' && process.env && process.env.LIGASE_DUMP_REEL);
    if (!out) return;
    try {
      var d = FS.readFile(UTF8ToString($0));
      require('fs').writeFileSync(out, Buffer.from(d.buffer, d.byteOffset, d.length));
      console.error('dump_reel: wrote ' + out + ' (' + d.length + ' bytes)');
    } catch (e) { console.error('dump_reel: ' + e); }
  }, memfs_path);
#else
  (void)memfs_path;
#endif
}

void ligase_tilde_setup(void);
static void pdprint(const char *s){ fputs(s, stderr); }

#define SR        44100
#define BLK       64          /* libpd's fixed DSP block */
/* run each phase long enough for its last scheduled message to fire, no `pd quit` needed */
#define AUTO_MS   4600        /* save fires at 4100 ms; extra ticks don't change the file */
#define PLAY_MS   1600        /* load fires at 500 ms, play at 1000 ms */
#define MS_TO_BLOCKS(ms) ((int)(((long)(ms) * SR) / (1000L * BLK)))

/* Parse the ligase reel WAV (RIFF/WAVE, 32-bit IEEE float, stereo, fixed 44-byte header
 * written by reel_save_wav) straight out of MEMFS and reduce it the way sox stat does:
 * RMS = sqrt(mean(x^2)) and MAX = max|x| over ALL interleaved samples. */
/* FNV-1a 64-bit over the whole file — a definitive bit-identity check against the native reel. */
static int wav_hash(const char *path, uint64_t *hash, long *bytes){
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  uint64_t h = 1469598103934665603ULL; long n = 0; int c;
  unsigned char buf[8192]; size_t r;
  while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
    for (size_t i = 0; i < r; ++i) { h ^= buf[i]; h *= 1099511628211ULL; n++; }
  (void)c; fclose(f);
  *hash = h; *bytes = n; return 1;
}

static int wav_stat(const char *path, double *rms, double *maxabs, uint32_t *frames){
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "readback: cannot open %s\n", path); return 0; }
  unsigned char h[44];
  if (fread(h, 1, 44, f) != 44 ||
      memcmp(h, "RIFF", 4) != 0 || memcmp(h+8, "WAVE", 4) != 0 ||
      memcmp(h+36, "data", 4) != 0) {
    fprintf(stderr, "readback: %s is not the expected RIFF/WAVE/data layout\n", path);
    fclose(f); return 0;
  }
  uint16_t fmt      = (uint16_t)(h[20] | (h[21]<<8));
  uint16_t channels = (uint16_t)(h[22] | (h[23]<<8));
  uint16_t bits     = (uint16_t)(h[34] | (h[35]<<8));
  uint32_t data_sz  = (uint32_t)(h[40] | (h[41]<<8) | (h[42]<<16) | ((uint32_t)h[43]<<24));
  if (fmt != 3 || bits != 32) {
    fprintf(stderr, "readback: expected 32-bit float (fmt=3), got fmt=%u bits=%u\n", fmt, bits);
    fclose(f); return 0;
  }
  uint32_t nsamp = data_sz / 4;               /* total interleaved float samples */
  fprintf(stderr, "header44:");
  for (int i = 0; i < 44; ++i) fprintf(stderr, " %02x", h[i]);
  fprintf(stderr, "\n");
  double ss = 0.0, mx = 0.0; uint32_t got = 0;
  uint64_t dhash = 1469598103934665603ULL;    /* FNV-1a over the PCM data region only */
  float buf[4096];
  size_t r;
  while ((r = fread(buf, sizeof(float), 4096, f)) > 0 && got < nsamp) {
    for (size_t i = 0; i < r && got < nsamp; ++i, ++got) {
      double v = buf[i]; ss += v*v;
      double a = fabs(v); if (a > mx) mx = a;
      unsigned char *pb = (unsigned char*)&buf[i];
      for (int k = 0; k < 4; ++k) { dhash ^= pb[k]; dhash *= 1099511628211ULL; }
    }
  }
  fprintf(stderr, "data-fnv1a %016llx  data-samples %u\n", (unsigned long long)dhash, got);
  fclose(f);
  if (got == 0) { fprintf(stderr, "readback: no samples in %s\n", path); return 0; }
  *rms = sqrt(ss / got);
  *maxabs = mx;
  *frames = channels ? got / channels : got;
  return 1;
}

static void tick(int blocks){
  float in[2*BLK], out[2*BLK];
  memset(in, 0, sizeof(in));                  /* dac feed unused; noise~ is internal */
  for (int b = 0; b < blocks; ++b) libpd_process_float(1, in, out);
}

int main(void){
  libpd_set_printhook(pdprint);
  libpd_init();
  ligase_tilde_setup();                       /* statically compiled-in external */
  libpd_init_audio(2, 2, SR);
  libpd_start_message(1); libpd_add_float(1.0f); libpd_finish_message("pd", "dsp");

  /* ---- Phase 1: record noise~ -> ligase~ -> save WAV to MEMFS ---- */
  void *p_auto = libpd_openfile("test_auto_wasm.pd", ".");
  if (!p_auto) { fprintf(stderr, "FAIL: open test_auto_wasm.pd\n"); return 1; }
  tick(MS_TO_BLOCKS(AUTO_MS));

  /* ---- Phase 2: read the saved reel back out of MEMFS ---- */
  double rms = 0, mx = 0; uint32_t frames = 0;
  if (!wav_stat("/tmp/ligase_test.wav", &rms, &mx, &frames)) return 2;
  uint64_t whash = 0; long wbytes = 0; wav_hash("/tmp/ligase_test.wav", &whash, &wbytes);
  printf("WASM-AUTO RMS %.6f  MAX %.6f  frames/ch %u  bytes %ld  fnv1a %016llx\n",
         rms, mx, frames, wbytes, (unsigned long long)whash);
  printf("  native gate: RMS 0.372309  MAX 0.608858  frames 132288  (sox displays MAX as 0.608839)\n");
  dump_reel("/tmp/ligase_test.wav");
  libpd_closefile(p_auto);

  /* ---- Phase 3: load the reel, capture ligase~'s buffer-check print ---- */
  void *p_play = libpd_openfile("test_playback_wasm.pd", ".");
  if (!p_play) { fprintf(stderr, "FAIL: open test_playback_wasm.pd\n"); return 1; }
  fprintf(stderr, "WASM-PLAYBACK (native gate: buffer check L=R=0.330109):\n");
  tick(MS_TO_BLOCKS(PLAY_MS));
  libpd_closefile(p_play);
  return 0;
}
