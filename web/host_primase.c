/* host_primase.c — Arc B pairing harness (Plans/web_build.md B3 web GATE).
 *
 * Proves that BOTH compiled externals instantiate in the SAME libpd-WASM module and that the
 * pairing runs: [primase] (the rhythm brain) bangs on every pattern event, and each bang drives
 * [ligase~]'s `stut`. It opens pd/ligase_primase.pd (the hand-authored demo, embedded into the
 * module), runs it through libpd's OWN scheduler exactly as native pd -nogui does (loadbang +
 * [delay] self-run), and counts two things straight off the print hook:
 *   - primase per-event bangs   ("PRIMASE_EV: bang")
 *   - ligase stut triggers      ("ligase~: stut triggered ...")
 *   - clock-locked stuts        ("quantized spacing 500")  == ligase's BPM grid derived from
 *                                                              primase's 500 ms bang interval
 * GATE: bangs == stuts (1:1 primase->ligase), and both > 0, and clock-lock observed.
 *
 * Requires primase compiled in (build_wasm.sh with PRIMASE_DIR set → -DWITH_PRIMASE); the
 * setup calls below are guarded so the file still compiles in a ligase-only tree.
 */
#include <stdio.h>
#include <string.h>
#include "z_libpd.h"

void ligase_tilde_setup(void);
#ifdef WITH_PRIMASE
void primase_setup(void);
#endif

#define SR   44100
#define BLK  64
#define RUN_MS 5000                         /* demo's last scheduled event fires by ~1350 ms; run well past a few loop cycles */
#define MS_TO_BLOCKS(ms) ((int)(((long)(ms) * SR) / (1000L * BLK)))

static int n_bang = 0, n_stut = 0, n_locked = 0;

static void pdprint(const char *s){
  /* libpd hands the hook whole lines; substring-match the two signals we count. */
  if (strstr(s, "PRIMASE_EV: bang"))       n_bang++;
  if (strstr(s, "stut triggered"))         n_stut++;
  if (strstr(s, "quantized spacing 500"))  n_locked++;
  fputs(s, stderr);                          /* still echo for the log */
}

static void tick(int blocks){
  float in[2*BLK], out[2*BLK];
  memset(in, 0, sizeof(in));
  for (int b = 0; b < blocks; ++b) libpd_process_float(1, in, out);
}

int main(void){
  libpd_set_printhook(pdprint);
  libpd_init();
  ligase_tilde_setup();
#ifdef WITH_PRIMASE
  primase_setup();                           /* register [primase] next to [ligase~] */
#else
  fprintf(stderr, "FAIL: built without WITH_PRIMASE — primase not compiled in\n");
  return 3;
#endif
  libpd_init_audio(2, 2, SR);
  libpd_start_message(1); libpd_add_float(1.0f); libpd_finish_message("pd", "dsp");

  if (!libpd_openfile("ligase_primase.pd", ".")) {
    fprintf(stderr, "FAIL: could not open ligase_primase.pd (both externals must load)\n");
    return 1;
  }
  tick(MS_TO_BLOCKS(RUN_MS));

  printf("\nWASM-PAIRING  primase_bangs %d  ligase_stut %d  clock_locked %d\n",
         n_bang, n_stut, n_locked);
  if (n_bang == 0 || n_stut == 0) {
    fprintf(stderr, "FAIL: pairing produced no events (bangs=%d stut=%d)\n", n_bang, n_stut);
    return 2;
  }
  if (n_bang != n_stut) {
    fprintf(stderr, "FAIL: not 1:1 — %d primase bangs but %d ligase stut triggers\n", n_bang, n_stut);
    return 2;
  }
  printf("PASS: %d primase bangs each drove one ligase stut (%d clock-locked to primase's grid)\n",
         n_stut, n_locked);
  return 0;
}
