/* host_headless.c — headless libpd+ligase harness for node (WASM engine-identity test).
 * Registers the compiled-in ligase~ external, opens a patch, runs N 64-frame blocks of
 * libpd_process_float, and prints RMS/MAX of the stereo output. No browser/AudioWorklet.
 * usage: node host_test.js <patch.pd> <blocks> [noise]   (noise=1 feeds white noise to inlets) */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "z_libpd.h"
void ligase_tilde_setup(void);
static void pdprint(const char *s){ fprintf(stderr, "%s", s); }
int main(int argc, char **argv){
  const char *patch = argc > 1 ? argv[1] : "test_sine.pd";
  int blocks = argc > 2 ? atoi(argv[2]) : 4096;
  int noise  = argc > 3 ? atoi(argv[3]) : 0;
  libpd_set_printhook(pdprint);
  libpd_init();
  ligase_tilde_setup();                 /* register the statically compiled-in external */
  libpd_init_audio(2, 2, 48000);
  libpd_start_message(1); libpd_add_float(1.0f); libpd_finish_message("pd", "dsp");
  if (!libpd_openfile(patch, ".")) { fprintf(stderr, "FAIL: open %s\n", patch); return 1; }
  float in[128], out[128];
  double ss = 0.0; long n = 0; float mx = 0.0f;
  unsigned int seed = 12345;
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < 128; ++i) { seed = seed*1103515245u+12345u; in[i] = noise ? ((float)((seed>>9)&0x7fffff)/0x400000-1.0f) : 0.0f; }
    libpd_process_float(1, in, out);
    for (int i = 0; i < 128; ++i) { ss += (double)out[i]*out[i]; float a = fabsf(out[i]); if (a>mx) mx=a; n++; }
  }
  printf("RMS %.6f  MAX %.6f  samples %ld  patch %s\n", sqrt(ss/n), mx, n, patch);
  return 0;
}
