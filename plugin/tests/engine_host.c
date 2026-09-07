/* engine_host.c — a line-protocol host around the ligase engine facade, so the JavaScript
 * panel brain (web/ligase_panel_logic.js) can be driven against the REAL engine from node
 * (web/test_panel_engine.mjs). Every reply is a line; each command ends with "ok".
 *
 *   msg <text>            engine message(s), text form (';' separates several)
 *   cv <inlet0> <value>   signal inlet (0-based engine index 2..23) held at value (no glide)
 *   run <blocks> [noise]  process N inner blocks; noise=1 feeds white noise to the audio inputs
 *   status                -> "status {json}"
 *   selectors             -> "selector <name> <sig>" lines
 *   quit
 * Replies: "out9 <sel> <args>", "print <text>", "err <text>", "status {...}", "ok".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ligase_engine.h"

static void on_print(void *u, int level, const char *text) { (void)u; printf("%s %s\n", level ? "err" : "print", text); }
static void on_outlet(void *u, int outlet, const char *sel, int argc, const ligase_atom_t *argv) {
    (void)u;
    if (outlet != 8) { if (outlet >= 4 && outlet <= 7 && argc) printf("modout %d %.6g\n", outlet - 3, (double)argv[0].f); return; }
    printf("out9 %s", sel);
    for (int i = 0; i < argc; i++) {
        if (argv[i].type == LIGASE_ATOM_FLOAT) printf(" %.9g", (double)argv[i].f); else printf(" %s", argv[i].s ? argv[i].s : "?");
    }
    printf("\n");
}
static unsigned g_seed = 4242;
static float noise(void) { g_seed = g_seed * 1103515245u + 12345u; return ((float)((g_seed >> 9) & 0x7fffff) / 0x400000 - 1.0f) * 0.5f; }

int main(int argc, char **argv) {
    int sr = argc > 1 ? atoi(argv[1]) : 44100;
    ligase_engine_t *e = ligase_engine_new(sr, 64, argc > 2 ? argv[2] : NULL);
    if (!e) { fprintf(stderr, "engine create failed\n"); return 1; }
    ligase_engine_set_callbacks(e, on_print, on_outlet, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);
    char line[8192];
    float in[64], ol[64], orr[64];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line); while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (!strncmp(line, "msg ", 4)) { ligase_engine_send_text(e, line + 4); }
        else if (!strncmp(line, "cv ", 3)) { int i = 0; float v = 0; if (sscanf(line + 3, "%d %f", &i, &v) == 2) ligase_engine_set_inlet(e, i, v, 0.0f); }
        else if (!strncmp(line, "run", 3)) {
            int blocks = 1, nz = 0; sscanf(line + 3, "%d %d", &blocks, &nz);
            double ss = 0; long cnt = 0;
            for (int b = 0; b < blocks; b++) {
                for (int i = 0; i < 64; i++) in[i] = nz ? noise() : 0.0f;
                ligase_engine_process(e, in, in, ol, orr, NULL, NULL);
                for (int i = 0; i < 64; i++) ss += (double)ol[i] * ol[i];
                cnt += 64;
            }
            printf("rms %.6f\n", cnt ? sqrt(ss / (double)cnt) : 0.0);   /* over the WHOLE run */
        }
        else if (!strcmp(line, "status")) {
            ligase_status_t st; ligase_engine_status(e, &st);
            printf("status {\"splice\":%d,\"splices\":%d,\"spliceStart\":%d,\"spliceEnd\":%d,\"playing\":%d,\"triggering\":%d,\"recording\":%d,"
                   "\"recMode\":%d,\"bpm\":%.4f,\"reelLen\":%d,\"reelSr\":%d,\"voices\":%d,\"activeGrains\":%d,\"poly\":%d,\"pitchMode\":%d,"
                   "\"midiNote\":%d,\"maxGrains\":%d,\"delayMode\":%d,\"smearMode\":%d,\"playheadMode\":%d,\"snapbufHas\":%d,\"snapbufAudition\":%d,"
                   "\"headless\":%d,\"clockRunning\":%d,\"morph\":{\"x\":%.4f,\"y\":%.4f,\"points\":%d,\"route\":%d,\"running\":%d},"
                   "\"snapshotMaskLo\":%u,\"snapshotMaskHi\":%u}\n",
                   st.splice_current, st.splice_count, st.splice_start, st.splice_end, st.playing, st.triggering, st.recording, st.record_mode,
                   (double)st.bpm, st.reel_length, st.sample_rate, st.voice_count, st.active_grains, st.poly, st.pitch_mode, st.midi_note,
                   st.max_grains, st.delay_mode, st.smear_mode, st.playhead_mode, st.snapbuf_has, st.snapbuf_audition, st.headless, st.clock_running,
                   (double)st.morph_cursor_x, (double)st.morph_cursor_y, st.morph_points, st.morph_route_len, st.morph_running,
                   (unsigned)(st.snapshot_mask & 0xFFFFFFFFu), (unsigned)(st.snapshot_mask >> 32));
        }
        else if (!strcmp(line, "selectors")) { int c = ligase_engine_selector_count(); for (int i = 0; i < c; i++) printf("selector %s %s\n", ligase_engine_selector_name(i), ligase_engine_selector_signature(i)); }
        else if (!strcmp(line, "quit")) break;
        printf("ok\n");
    }
    ligase_engine_free(e);
    return 0;
}
