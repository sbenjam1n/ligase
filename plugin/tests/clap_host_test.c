/* clap_host_test.c — headless end-to-end smoke test of the BUILT plugin binary (CLAP format).
 *
 * Loads bin/ligase.clap, instantiates the plugin through the CLAP ABI, and drives it like a
 * DAW: parameter events arm recording (REC MODE = INPUT), noise is recorded for ~1.5 s, then
 * PLAY is engaged and MIDI notes are sent while the granular output is measured. Also checks
 * the latency extension (0 for 64-frame host blocks, 64 for 100-frame blocks), the parameter
 * table, and a state save/load round trip (a reel + voice restore into a second instance).
 *
 *   usage: clap_host_test <path/to/ligase.clap>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/process.h"
#include "clap/events.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/latency.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/note-ports.h"

/* ---- minimal host ---- */
static const void *host_get_extension(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_noop(const clap_host_t *h) { (void)h; }
static const clap_host_t g_host = {
    CLAP_VERSION_INIT, NULL, "clap_host_test", "ligase", "https://github.com/sbenjam1n/ligase", "0.1",
    host_get_extension, host_noop, host_noop, host_noop };

/* ---- event lists ---- */
typedef struct { const clap_event_header_t *ev[64]; uint32_t n; } evlist_t;
static uint32_t ev_size(const clap_input_events_t *l) { return ((const evlist_t *)l->ctx)->n; }
static const clap_event_header_t *ev_get(const clap_input_events_t *l, uint32_t i) { return ((const evlist_t *)l->ctx)->ev[i]; }
static bool ev_push(const clap_output_events_t *l, const clap_event_header_t *e) { (void)l; (void)e; return true; }

/* ---- memory stream for state ---- */
typedef struct { unsigned char *buf; size_t len, cap, pos; } mem_t;
static int64_t mem_write(const clap_ostream_t *s, const void *d, uint64_t n) {
    mem_t *m = (mem_t *)s->ctx;
    if (m->len + n > m->cap) { m->cap = (m->len + n) * 2 + 4096; m->buf = realloc(m->buf, m->cap); }
    memcpy(m->buf + m->len, d, n); m->len += n; return (int64_t)n;
}
static int64_t mem_read(const clap_istream_t *s, void *d, uint64_t n) {
    mem_t *m = (mem_t *)s->ctx;
    size_t left = m->len - m->pos; if (n > left) n = left;
    memcpy(d, m->buf + m->pos, n); m->pos += n; return (int64_t)n;
}

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr, "\n"); g_fail++; } } while (0)

static uint32_t param_id_of(const clap_plugin_t *p, const clap_plugin_params_t *params, const char *name, double *min, double *max) {
    uint32_t n = params->count(p);
    for (uint32_t i = 0; i < n; i++) {
        clap_param_info_t info; if (!params->get_info(p, i, &info)) continue;
        if (!strcmp(info.name, name)) { if (min) *min = info.min_value; if (max) *max = info.max_value; return info.id; }
    }
    return 0xFFFFFFFFu;
}

static clap_event_param_value_t make_param(uint32_t id, double v) {
    clap_event_param_value_t e; memset(&e, 0, sizeof e);
    e.header.size = sizeof e; e.header.time = 0; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v; return e;
}
static clap_event_midi_t make_midi(unsigned char a, unsigned char b, unsigned char c) {
    clap_event_midi_t e; memset(&e, 0, sizeof e);
    e.header.size = sizeof e; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_MIDI;
    e.port_index = 0; e.data[0] = a; e.data[1] = b; e.data[2] = c; return e;
}

static unsigned g_seed = 12345;
static float noise(void) { g_seed = g_seed * 1103515245u + 12345u; return ((float)((g_seed >> 9) & 0x7fffff) / 0x400000 - 1.0f) * 0.5f; }

typedef struct { const clap_plugin_t *p; float inL[512], inR[512], outL[512], outR[512]; uint32_t frames; int64_t t; } ctx_t;

static double run_blocks(ctx_t *c, int blocks, int feed_noise, evlist_t *first_events) {
    double ss = 0; long n = 0;
    for (int b = 0; b < blocks; b++) {
        for (uint32_t i = 0; i < c->frames; i++) { float v = feed_noise ? noise() : 0.0f; c->inL[i] = v; c->inR[i] = v; }
        float *ins[2] = { c->inL, c->inR }, *outs[2] = { c->outL, c->outR };
        clap_audio_buffer_t in = { ins, NULL, 2, 0, 0 }, out = { outs, NULL, 2, 0, 0 };
        evlist_t empty = { {0}, 0 };
        evlist_t *el = (b == 0 && first_events) ? first_events : &empty;
        clap_input_events_t inev = { el, ev_size, ev_get };
        clap_output_events_t outev = { NULL, ev_push };
        clap_process_t pr; memset(&pr, 0, sizeof pr);
        pr.steady_time = c->t; pr.frames_count = c->frames; pr.transport = NULL;
        pr.audio_inputs = &in; pr.audio_outputs = &out; pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.in_events = &inev; pr.out_events = &outev;
        clap_process_status st = c->p->process(c->p, &pr);
        CHECK(st != CLAP_PROCESS_ERROR, "process returned error");
        for (uint32_t i = 0; i < c->frames; i++) { ss += (double)c->outL[i] * c->outL[i]; n++; }
        c->t += c->frames;
    }
    return n ? sqrt(ss / n) : 0.0;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../bin/ligase.clap/ligase.clap";
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return 1; }
    const clap_plugin_entry_t *entry = (const clap_plugin_entry_t *)dlsym(lib, "clap_entry");
    if (!entry) { fprintf(stderr, "no clap_entry\n"); return 1; }
    CHECK(entry->init(path), "entry init");
    const clap_plugin_factory_t *factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory) { fprintf(stderr, "no factory\n"); return 1; }
    const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, 0);
    printf("plugin: %s (%s) v%s by %s\n", desc->name, desc->id, desc->version, desc->vendor);
    const clap_plugin_t *p = factory->create_plugin(factory, &g_host, desc->id);
    if (!p) { fprintf(stderr, "create failed\n"); return 1; }
    CHECK(p->init(p), "plugin init");

    const clap_plugin_params_t *params = (const clap_plugin_params_t *)p->get_extension(p, CLAP_EXT_PARAMS);
    const clap_plugin_state_t *state = (const clap_plugin_state_t *)p->get_extension(p, CLAP_EXT_STATE);
    const clap_plugin_latency_t *lat = (const clap_plugin_latency_t *)p->get_extension(p, CLAP_EXT_LATENCY);
    const clap_plugin_audio_ports_t *aports = (const clap_plugin_audio_ports_t *)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    const clap_plugin_note_ports_t *nports = (const clap_plugin_note_ports_t *)p->get_extension(p, CLAP_EXT_NOTE_PORTS);
    CHECK(params && state && lat && aports && nports, "missing extension (params %p state %p latency %p audio %p notes %p)",
          (void*)params, (void*)state, (void*)lat, (void*)aports, (void*)nports);
    if (!params || !state || !lat) return 1;
    printf("params: %u, audio in/out ports: %u/%u, note in ports: %u\n", params->count(p),
           aports ? aports->count(p, true) : 0, aports ? aports->count(p, false) : 0, nports ? nports->count(p, true) : 0);
    CHECK(params->count(p) >= 70, "expected >= 70 parameters");
    CHECK(nports && nports->count(p, true) >= 1, "no MIDI/note input port");

    uint32_t id_record = param_id_of(p, params, "RECORD", NULL, NULL), id_recmode = param_id_of(p, params, "REC MODE", NULL, NULL);
    uint32_t id_play = param_id_of(p, params, "PLAY", NULL, NULL), id_level = param_id_of(p, params, "LEVEL", NULL, NULL);
    uint32_t id_pitch = param_id_of(p, params, "PITCH MODE", NULL, NULL), id_poly = param_id_of(p, params, "POLY \xc3\x97" "8", NULL, NULL);
    uint32_t id_clock = param_id_of(p, params, "CLOCK SOURCE", NULL, NULL);
    CHECK(id_record != 0xFFFFFFFFu && id_recmode != 0xFFFFFFFFu && id_play != 0xFFFFFFFFu && id_level != 0xFFFFFFFFu, "core params by name");
    double cut_min = 0, cut_max = 0; uint32_t id_cut = param_id_of(p, params, "CUTOFF", &cut_min, &cut_max);
    CHECK(id_cut != 0xFFFFFFFFu && cut_min == 20.0 && cut_max == 20000.0, "CUTOFF range is engine units (got %g..%g)", cut_min, cut_max);

    /* ---- 64-frame blocks: zero latency ---- */
    CHECK(p->activate(p, 44100.0, 64, 64), "activate 64");
    CHECK(p->start_processing(p), "start_processing");
    CHECK(lat->get(p) == 0, "latency at 64-frame blocks should be 0 (got %u)", lat->get(p));
    ctx_t c; memset(&c, 0, sizeof c); c.p = p; c.frames = 64;

    /* arm: REC MODE = INPUT (0), RECORD = 1, then feed noise for ~1.5 s */
    clap_event_param_value_t e1 = make_param(id_recmode, 0.0), e2 = make_param(id_record, 1.0), e3 = make_param(id_clock, 0.0);
    evlist_t arm = { { &e1.header, &e2.header, &e3.header }, 3 };
    double rms_rec = run_blocks(&c, 1000, 1, &arm);
    printf("recording pass: output RMS %.4f (input monitor while recording)\n", rms_rec);
    clap_event_param_value_t e4 = make_param(id_record, 0.0);
    evlist_t disarm = { { &e4.header }, 1 };
    run_blocks(&c, 10, 0, &disarm);

    /* play: PLAY = 1, MIDI mode + poly, then notes */
    clap_event_param_value_t e5 = make_param(id_play, 1.0), e6 = make_param(id_pitch, 3.0);
    clap_event_param_value_t e7 = make_param(id_poly, 1.0);
    evlist_t play = { { &e5.header, &e6.header, &e7.header }, 3 };
    double rms_play = run_blocks(&c, 400, 0, &play);
    printf("playback pass (no MIDI yet): output RMS %.4f\n", rms_play);
    CHECK(rms_play > 0.01, "granular playback is silent (RMS %.5f)", rms_play);

    clap_event_midi_t n1 = make_midi(0x90, 67, 100), n2 = make_midi(0x90, 72, 100);
    evlist_t notes = { { &n1.header, &n2.header }, 2 };
    double rms_midi = run_blocks(&c, 400, 0, &notes);
    printf("MIDI notes 67+72 held: output RMS %.4f\n", rms_midi);
    CHECK(rms_midi > 0.01, "silent with MIDI notes");
    clap_event_midi_t off1 = make_midi(0x80, 67, 0), off2 = make_midi(0x80, 72, 0);
    clap_event_midi_t cc = make_midi(0xB0, 74, 20);   /* CC74 -> CUTOFF (default map) */
    evlist_t offs = { { &off1.header, &off2.header, &cc.header }, 3 };
    run_blocks(&c, 50, 0, &offs);
    double cutoff_now = 0; params->get_value(p, id_cut, &cutoff_now);
    printf("after CC74=20: CUTOFF parameter = %.1f Hz\n", cutoff_now);
    CHECK(cutoff_now < 2000.0, "CC74 did not move CUTOFF (%.1f)", cutoff_now);

    /* level to 0 -> output must go quiet (parameter -> signal inlet path) */
    clap_event_param_value_t e8 = make_param(id_level, 0.0);
    evlist_t lvl = { { &e8.header }, 1 };
    run_blocks(&c, 60, 0, &lvl);   /* let the glide + grains in flight finish */
    double rms_quiet = run_blocks(&c, 200, 0, NULL);
    printf("LEVEL=0: output RMS %.5f\n", rms_quiet);
    CHECK(rms_quiet < 0.01, "LEVEL 0 did not silence the grains");

    /* ---- state round trip into a fresh instance ---- */
    mem_t mem = { NULL, 0, 0, 0 };
    clap_ostream_t os = { &mem, mem_write };
    CHECK(state->save(p, &os), "state save");
    printf("state chunk: %zu bytes\n", mem.len);
    CHECK(mem.len > 1000, "state chunk suspiciously small");
    p->stop_processing(p); p->deactivate(p);

    const clap_plugin_t *q = factory->create_plugin(factory, &g_host, desc->id);
    CHECK(q && q->init(q), "second instance");
    const clap_plugin_state_t *state2 = (const clap_plugin_state_t *)q->get_extension(q, CLAP_EXT_STATE);
    const clap_plugin_params_t *params2 = (const clap_plugin_params_t *)q->get_extension(q, CLAP_EXT_PARAMS);
    clap_istream_t is = { &mem, mem_read };
    CHECK(state2->load(q, &is), "state load");
    double lvl2 = 1; params2->get_value(q, id_level, &lvl2);
    CHECK(lvl2 == 0.0, "LEVEL not restored (%.3f)", lvl2);
    /* 100-frame blocks: one inner block of latency, then the restored reel must play */
    CHECK(q->activate(q, 44100.0, 100, 100), "activate 100");
    CHECK(q->start_processing(q), "start 2");
    ctx_t c2; memset(&c2, 0, sizeof c2); c2.p = q; c2.frames = 100;
    clap_event_param_value_t r1 = make_param(id_level, 1.0), r2 = make_param(id_play, 1.0);
    evlist_t restore_play = { { &r1.header, &r2.header }, 2 };
    double rms2 = run_blocks(&c2, 300, 0, &restore_play);
    const clap_plugin_latency_t *lat2 = (const clap_plugin_latency_t *)q->get_extension(q, CLAP_EXT_LATENCY);
    printf("restored instance @100-frame blocks: latency %u, playback RMS %.4f\n", lat2 ? lat2->get(q) : 999, rms2);
    CHECK(lat2 && lat2->get(q) == 64, "latency at 100-frame blocks should be 64");
    CHECK(rms2 > 0.01, "restored reel did not play (RMS %.5f)", rms2);
    q->stop_processing(q); q->deactivate(q); q->destroy(q);
    p->destroy(p);
    entry->deinit();
    free(mem.buf);
    if (g_fail) { printf("CLAP HOST TEST: FAIL (%d)\n", g_fail); return 1; }
    printf("CLAP HOST TEST: PASS\n");
    return 0;
}
