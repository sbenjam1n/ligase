/* identity_test.c — the engine-identity GATE for the hosted (shim) build of ligase~.
 *
 * Reproduces AUTOMATED_TEST_PROCEDURE.md inside the facade, with no Pd process at all:
 *   Phase 1 (test_auto.pd):     Pd's [noise~] (its exact LCG, first-instance seed) feeds both
 *                               audio inlets; recinput @500 ms, record 1 @600 ms, record 0
 *                               @3600 ms, save @4100 ms — each message lands before the DSP
 *                               tick Pd's scheduler would run it in (settime < next tick time).
 *   Phase 2 (readback):         RMS / MAX / frames of the saved reel, plus an FNV-1a hash that is
 *                               compared byte-for-byte against a native reel when one is given.
 *   Phase 3 (test_playback.pd): a SECOND engine loads the reel (500 ms) and plays it (1000 ms);
 *                               the engine's own "buffer check: avg amplitude L=.. R=.." line is
 *                               captured from the print callback.
 *   Phase 4:                    multi-instance + MIDI/poly + text-dispatch smoke checks.
 *
 * Native baseline (pd 0.54, this repo): RMS 0.372309 / MAX 0.608839 / 132288 frames per channel;
 * playback buffer check L=R=0.330109.
 *
 *   usage: identity_test <scratch_dir> [native_reel.wav]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "ligase_engine.h"

#define SR   44100
#define BLK  64
#define TICK_MS ((double)BLK * 1000.0 / (double)SR)

/* Pd's [noise~]: d_osc.c noise_new/noise_perform, first instance in the process. */
typedef struct { int val; } pdnoise_t;
static void pdnoise_init(pdnoise_t *n) { static int init = 307; n->val = (init *= 1319); }
static void pdnoise_block(pdnoise_t *n, float *out, int len) {
    int val = n->val;
    for (int i = 0; i < len; i++) {
        out[i] = ((float)((val & 0x7fffffff) - 0x40000000)) * (float)(1.0 / 0x40000000);
        val = (int)((unsigned)val * 435898247u + 382842987u);
    }
    n->val = val;
}

/* Pd fires a clock set for time t (ms) before DSP tick k where k = floor(t / TICK_MS)
 * (sched_tick: fire while settime < next_sys_time, then dsp_tick). */
static int tick_for_ms(double ms) { return (int)floor(ms / TICK_MS + 1e-9); }

static char g_lastprint[4096];
static int g_errors = 0;
static double g_bufL = -1, g_bufR = -1;
static void on_print(void *u, int level, const char *text) {
    (void)u;
    if (level) g_errors++;
    strncpy(g_lastprint, text, sizeof(g_lastprint) - 1);
    double l, r;
    if (sscanf(text, "  buffer check: avg amplitude L=%lf R=%lf", &l, &r) == 2) { g_bufL = l; g_bufR = r; }
    if (getenv("LIGASE_TEST_VERBOSE")) fprintf(stderr, "%s%s\n", level ? "[err] " : "", text);
}

static uint64_t fnv_file(const char *path, long *bytes) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    uint64_t h = 1469598103934665603ULL; long n = 0; unsigned char buf[8192]; size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) for (size_t i = 0; i < r; i++) { h ^= buf[i]; h *= 1099511628211ULL; n++; }
    fclose(f); *bytes = n; return h;
}

static int wav_stat(const char *path, double *rms, double *mx, uint32_t *frames) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    unsigned char h[44];
    if (fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4) || memcmp(h + 36, "data", 4)) { fclose(f); return 0; }
    uint16_t channels = (uint16_t)(h[22] | (h[23] << 8));
    uint32_t data_sz = (uint32_t)(h[40] | (h[41] << 8) | (h[42] << 16) | ((uint32_t)h[43] << 24));
    uint32_t nsamp = data_sz / 4, got = 0; double ss = 0, m = 0; float buf[4096]; size_t r;
    while ((r = fread(buf, sizeof(float), 4096, f)) > 0 && got < nsamp)
        for (size_t i = 0; i < r && got < nsamp; i++, got++) { double v = buf[i]; ss += v * v; if (fabs(v) > m) m = fabs(v); }
    fclose(f);
    if (!got) return 0;
    *rms = sqrt(ss / got); *mx = m; *frames = channels ? got / channels : got; return 1;
}

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *native = argc > 2 ? argv[2] : NULL;
    char reel[1024]; snprintf(reel, sizeof reel, "%s/ligase_shim_test.wav", dir);
    int fails = 0;

    /* ---------------- Phase 1: test_auto ---------------- */
    ligase_engine_t *e = ligase_engine_new(SR, BLK, dir);
    if (!e) { fprintf(stderr, "FAIL: engine create\n"); return 1; }
    ligase_engine_set_callbacks(e, on_print, NULL, NULL);
    pdnoise_t noise; pdnoise_init(&noise);
    float in[BLK], outl[BLK], outr[BLK];
    const int t_recinput = tick_for_ms(500), t_rec1 = tick_for_ms(600), t_rec0 = tick_for_ms(3600), t_save = tick_for_ms(4100);
    char savemsg[1100]; snprintf(savemsg, sizeof savemsg, "save %s", reel);
    for (int k = 0; k <= t_save; k++) {
        if (k == t_recinput) ligase_engine_send_text(e, "recinput");
        if (k == t_rec1)     ligase_engine_send_text(e, "record 1");
        if (k == t_rec0)     ligase_engine_send_text(e, "record 0");
        if (k == t_save)     ligase_engine_send_text(e, savemsg);
        pdnoise_block(&noise, in, BLK);
        ligase_engine_process(e, in, in, outl, outr, NULL, NULL);
    }
    printf("phase1: ticks recinput=%d record1=%d record0=%d save=%d\n", t_recinput, t_rec1, t_rec0, t_save);

    /* ---------------- Phase 2: readback ---------------- */
    double rms = 0, mx = 0; uint32_t frames = 0;
    if (!wav_stat(reel, &rms, &mx, &frames)) { fprintf(stderr, "FAIL: no reel written (%s)\n", reel); return 1; }
    long bytes = 0; uint64_t hash = fnv_file(reel, &bytes);
    printf("SHIM-AUTO RMS %.6f  MAX %.6f  frames/ch %u  bytes %ld  fnv1a %016llx\n", rms, mx, frames, bytes, (unsigned long long)hash);
    printf("  native gate: RMS 0.372309  MAX 0.608839  frames 132288\n");
    if (!near(rms, 0.372309, 1e-6)) { fprintf(stderr, "FAIL: RMS %.6f != 0.372309\n", rms); fails++; }
    if (!near(mx, 0.608839, 2e-5))  { fprintf(stderr, "FAIL: MAX %.6f != 0.608839\n", mx); fails++; }
    if (frames != 132288)           { fprintf(stderr, "FAIL: frames %u != 132288\n", frames); fails++; }
    if (native) {
        long nb = 0; uint64_t nh = fnv_file(native, &nb);
        if (nh == hash && nb == bytes) printf("  reel is BYTE-IDENTICAL to %s\n", native);
        else { fprintf(stderr, "FAIL: reel differs from native %s (%016llx/%ld vs %016llx/%ld)\n", native, (unsigned long long)nh, nb, (unsigned long long)hash, bytes); fails++; }
    }

    /* ---------------- Phase 3: test_playback (fresh engine, like a fresh pd process) ---------------- */
    ligase_engine_t *p = ligase_engine_new(SR, BLK, dir);
    ligase_engine_set_callbacks(p, on_print, NULL, NULL);
    char loadmsg[1100]; snprintf(loadmsg, sizeof loadmsg, "load %s", reel);
    const int t_load = tick_for_ms(500), t_play = tick_for_ms(1000), t_end = tick_for_ms(1600);
    double ss = 0; long n = 0;
    for (int k = 0; k <= t_end; k++) {
        if (k == t_load) ligase_engine_send_text(p, loadmsg);
        if (k == t_play) ligase_engine_send_text(p, "play 1");
        pdnoise_block(&noise, in, BLK);
        ligase_engine_process(p, in, in, outl, outr, NULL, NULL);
        if (k > t_play) for (int i = 0; i < BLK; i++) { ss += (double)outl[i] * outl[i]; n++; }
    }
    printf("SHIM-PLAYBACK buffer check L=%.6f R=%.6f (native gate 0.330109); playback out RMS %.4f\n", g_bufL, g_bufR, n ? sqrt(ss / n) : 0.0);
    if (!near(g_bufL, 0.330109, 1e-6) || !near(g_bufR, 0.330109, 1e-6)) { fprintf(stderr, "FAIL: buffer check\n"); fails++; }
    ligase_status_t st; ligase_engine_status(p, &st);
    printf("  status: reel_length=%d splices=%d current=%d playing=%d active_grains=%d sr=%d\n",
           st.reel_length, st.splice_count, st.splice_current, st.playing, st.active_grains, st.sample_rate);
    if (st.reel_length != 132288 || !st.playing) { fprintf(stderr, "FAIL: status readback\n"); fails++; }
    if (n && sqrt(ss / n) < 0.01) { fprintf(stderr, "FAIL: playback produced silence\n"); fails++; }

    /* ---------------- Phase 4: multi-instance + MIDI/poly + dispatch smoke ---------------- */
    /* both engines alive; drive the first one with MIDI while the second keeps playing */
    ligase_engine_send_text(e, "poly 1; pitch_mode 4; midi 60 100 1; midi 64 100 1; midi 67 100 1");
    ligase_engine_status(e, &st);
    if (st.voice_count != 3 || !st.poly || st.pitch_mode != 4) { fprintf(stderr, "FAIL: poly voices %d (want 3) poly=%d mode=%d\n", st.voice_count, st.poly, st.pitch_mode); fails++; }
    ligase_engine_send_text(e, "midi 64 0 1");
    ligase_engine_status(e, &st);
    if (st.voice_count != 2) { fprintf(stderr, "FAIL: note-off left %d voices (want 2)\n", st.voice_count); fails++; }
    ligase_status_t st2; ligase_engine_status(p, &st2);
    if (st2.voice_count != 0 || st2.poly) { fprintf(stderr, "FAIL: instances share voice state\n"); fails++; }
    /* text dispatch: unknown selector and bad args are reported, not crashed */
    int bad = ligase_engine_send_text(e, "no_such_message 1; grainsize foo");
    if (bad != 2) { fprintf(stderr, "FAIL: expected 2 dispatch failures, got %d\n", bad); fails++; }
    /* a handful of real messages through every argument pattern */
    if (ligase_engine_send_text(e, "grainsize 0.2; nbody_G 1 0.5; nbody_pump 1 0.1 20; pitch_rand_type perlin_1d_1; matrix_connect lorenz1 moog_cutoff 500; pattern pitch [ 0 4 7 ]; scope_tap grain; stut")) {
        fprintf(stderr, "FAIL: dispatch of valid messages\n"); fails++; }
    for (int k = 0; k < 200; k++) { pdnoise_block(&noise, in, BLK); ligase_engine_process(e, in, in, outl, outr, NULL, NULL); ligase_engine_process(p, NULL, NULL, outl, outr, NULL, NULL); }
    int selectors = ligase_engine_selector_count();
    char sig1[16], sig2[16];
    snprintf(sig1, sizeof sig1, "%s", ligase_engine_selector_signature(1));
    snprintf(sig2, sizeof sig2, "%s", ligase_engine_selector_signature(selectors - 1));
    printf("  vocabulary: %d selectors registered (e.g. %s/%s, %s/%s)\n", selectors,
           ligase_engine_selector_name(1), sig1, ligase_engine_selector_name(selectors - 1), sig2);
    if (selectors < 200) { fprintf(stderr, "FAIL: selector table too small\n"); fails++; }
    /* sample-rate change keeps the recording (reel_set_sample_rate preserves audio) */
    ligase_engine_set_sample_rate(p, 48000);
    ligase_engine_status(p, &st);
    if (st.sample_rate != 48000 || st.reel_length == 0) { fprintf(stderr, "FAIL: SR change lost the reel\n"); fails++; }
    ligase_engine_free(e);
    ligase_engine_free(p);

    if (fails) { printf("ENGINE-IDENTITY GATE (shim): FAIL (%d)\n", fails); return 1; }
    printf("ENGINE-IDENTITY GATE (shim): PASS (auto RMS 0.372309 / frames 132288 / buffer 0.330109 reproduced by the hosted engine; %d console errors = the 2 deliberate bad messages)\n", g_errors);
    if (g_errors != 2) { printf("WARNING: expected exactly 2 console errors\n"); }
    return 0;
}
