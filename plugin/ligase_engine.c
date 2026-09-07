/* ligase_engine.c — see ligase_engine.h. */
#include "ligase_engine.h"
#include "pdshim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define TIMEUNITPERMSEC 14112.0      /* Pd's logical clock: 32 * 441 units per millisecond */
#define NSIG (LIGASE_ENGINE_INLETS + 4)   /* 24 inlet vectors + out L/R + scope X/Y */

void ligase_tilde_setup(void);       /* src/ligase~.c — the Pd class constructor */

struct ligase_engine {
    pdshim_instance_t sh;
    t_pd     *obj;
    t_class  *cls;
    int       sr, block;
    float    *vec[NSIG];
    t_signal  sig[NSIG];
    t_signal *sp[NSIG];
    /* CV inlets (2..23): per-block held values with linear glide */
    float     cv_cur[LIGASE_ENGINE_INLETS];
    float     cv_target[LIGASE_ENGINE_INLETS];
    float     cv_step[LIGASE_ENGINE_INLETS];
    int       cv_ramp[LIGASE_ENGINE_INLETS];
    ligase_print_fn  print;
    ligase_outlet_fn outlet;
    void            *user;
    double    block_units;            /* logical time advance per block */
};

static atomic_int g_setup_done = 0;

static void ensure_setup(void) {
    if (atomic_load(&g_setup_done)) return;
    pdshim_init();
    /* class creation is idempotent-guarded by the shim's class list: only one thread must run
     * the setup; a plain compare-exchange serializes construction */
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_setup_done, &expected, 2)) {
        ligase_tilde_setup();
        atomic_store(&g_setup_done, 1);
    } else {
        while (atomic_load(&g_setup_done) != 1) { /* spin: setup is microseconds */ }
    }
}

/* --- shim callbacks -> host callbacks ------------------------------------------------------ */
static void on_print(void *user, int level, const char *text) {
    ligase_engine_t *e = (ligase_engine_t *)user;
    if (e->print) e->print(e->user, level, text);
}

static void on_outlet(void *user, int outlet, t_symbol *sel, int argc, t_atom *argv) {
    ligase_engine_t *e = (ligase_engine_t *)user;
    if (!e->outlet) return;
    ligase_atom_t stack[32];
    ligase_atom_t *a = stack;
    if (argc > 32) a = (ligase_atom_t *)calloc((size_t)argc, sizeof(ligase_atom_t));
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type == A_FLOAT) { a[i].type = LIGASE_ATOM_FLOAT; a[i].f = argv[i].a_w.w_float; a[i].s = NULL; }
        else if (argv[i].a_type == A_SYMBOL) { a[i].type = LIGASE_ATOM_SYMBOL; a[i].f = 0; a[i].s = argv[i].a_w.w_symbol->s_name; }
        else { a[i].type = LIGASE_ATOM_SYMBOL; a[i].f = 0; a[i].s = "?"; }
    }
    e->outlet(e->user, outlet, sel ? sel->s_name : "bang", argc, a);
    if (a != stack) free(a);
}

/* --- construction -------------------------------------------------------------------------- */
static int run_dsp(ligase_engine_t *e) {
    for (int i = 0; i < NSIG; i++) {
        memset(&e->sig[i], 0, sizeof(t_signal));
        e->sig[i].s_n = e->block;
        e->sig[i].s_vec = e->vec[i];
        e->sig[i].s_sr = (t_float)e->sr;
        e->sig[i].s_nchans = 1;
        e->sp[i] = &e->sig[i];
    }
    e->sh.perf = NULL; e->sh.wn = 0;
    e->sh.sample_rate = (float)e->sr;
    pdshim_set_current(&e->sh);
    int rc = pdshim_call_dsp(e->obj, e->sp);
    pdshim_set_current(NULL);
    if (rc != 0 || !e->sh.perf) return -1;
    return 0;
}

ligase_engine_t *ligase_engine_new(int sample_rate, int block, const char *resource_dir) {
    if (sample_rate <= 0) sample_rate = 44100;
    if (block <= 0) block = 64;
    if (block > 8192) block = 8192;
    ensure_setup();
    ligase_engine_t *e = (ligase_engine_t *)calloc(1, sizeof(ligase_engine_t));
    if (!e) return NULL;
    e->sr = sample_rate; e->block = block;
    e->block_units = (double)block * (TIMEUNITPERMSEC * 1000.0) / (double)sample_rate;
    pdshim_instance_init(&e->sh, resource_dir);
    e->sh.user = e; e->sh.print = on_print; e->sh.outlet = on_outlet;
    e->sh.sample_rate = (float)sample_rate;
    for (int i = 0; i < NSIG; i++) {
        e->vec[i] = (float *)calloc((size_t)block, sizeof(float));
        if (!e->vec[i]) { ligase_engine_free(e); return NULL; }
    }
    e->cls = pdshim_class_find("ligase~");
    if (!e->cls) { ligase_engine_free(e); return NULL; }
    pdshim_set_current(&e->sh);
    e->obj = (t_pd *)pdshim_class_instantiate(e->cls);
    pdshim_set_current(NULL);
    if (!e->obj) { ligase_engine_free(e); return NULL; }
    e->sh.obj = e->obj;
    if (run_dsp(e) != 0) { ligase_engine_free(e); return NULL; }
    return e;
}

void ligase_engine_free(ligase_engine_t *e) {
    if (!e) return;
    if (e->obj) { pdshim_set_current(&e->sh); pdshim_object_free(e->obj); pdshim_set_current(NULL); e->obj = NULL; }
    for (int i = 0; i < NSIG; i++) free(e->vec[i]);
    pdshim_instance_release(&e->sh);
    free(e);
}

void ligase_engine_set_callbacks(ligase_engine_t *e, ligase_print_fn print, ligase_outlet_fn outlet, void *user) {
    e->print = print; e->outlet = outlet; e->user = user;
}
void ligase_engine_set_resource_dir(ligase_engine_t *e, const char *dir) {
    strncpy(e->sh.dir, dir ? dir : "", MAXPDSTRING - 1); e->sh.dir[MAXPDSTRING - 1] = 0;
    if (e->sh.canvas) { strncpy(((char *)e->sh.canvas), e->sh.dir, MAXPDSTRING - 1); }
}
int ligase_engine_sample_rate(const ligase_engine_t *e) { return e->sr; }
int ligase_engine_block(const ligase_engine_t *e) { return e->block; }

int ligase_engine_set_sample_rate(ligase_engine_t *e, int sample_rate) {
    if (sample_rate <= 0 || sample_rate == e->sr) return 0;
    e->sr = sample_rate;
    e->block_units = (double)e->block * (TIMEUNITPERMSEC * 1000.0) / (double)sample_rate;
    return run_dsp(e);
}

/* --- control ------------------------------------------------------------------------------- */
int ligase_engine_send(ligase_engine_t *e, const char *selector, int argc, const ligase_atom_t *argv) {
    if (!e || !selector || !*selector) return -1;
    t_atom stack[32];
    t_atom *a = stack;
    if (argc > 32) a = (t_atom *)calloc((size_t)argc, sizeof(t_atom));
    for (int i = 0; i < argc; i++) {
        if (argv[i].type == LIGASE_ATOM_FLOAT) SETFLOAT(&a[i], argv[i].f);
        else SETSYMBOL(&a[i], gensym(argv[i].s ? argv[i].s : ""));
    }
    pdshim_set_current(&e->sh);
    int rc = pdshim_typedmess(e->obj, gensym(selector), argc, a);
    pdshim_set_current(NULL);
    if (a != stack) free(a);
    return rc;
}

static int token_is_number(const char *tok, float *out) {
    char *end = NULL;
    double v = strtod(tok, &end);
    if (end == tok || *end != 0) return 0;
    *out = (float)v;
    return 1;
}

int ligase_engine_send_text(ligase_engine_t *e, const char *text) {
    if (!e || !text) return 1;
    int failures = 0;
    const char *p = text;
    while (*p) {
        /* one message: up to ';' or newline */
        const char *end = p;
        while (*end && *end != ';' && *end != '\n') end++;
        size_t len = (size_t)(end - p);
        char *buf = (char *)malloc(len + 1);
        memcpy(buf, p, len); buf[len] = 0;
        /* tokenize */
        ligase_atom_t atoms[128];
        int n = 0; char *sel = NULL;
        char *cur = buf;
        for (;;) {
            while (*cur == ' ' || *cur == '\t' || *cur == '\r') cur++;
            if (!*cur) break;
            char *tok = cur;
            while (*cur && *cur != ' ' && *cur != '\t' && *cur != '\r') cur++;
            if (*cur) *cur++ = 0;
            if (!sel) { sel = tok; continue; }
            if (n >= 128) break;
            float f;
            if (token_is_number(tok, &f)) { atoms[n].type = LIGASE_ATOM_FLOAT; atoms[n].f = f; atoms[n].s = NULL; }
            else { atoms[n].type = LIGASE_ATOM_SYMBOL; atoms[n].f = 0; atoms[n].s = tok; }
            n++;
        }
        if (sel) {
            float f;
            int rc;
            if (token_is_number(sel, &f)) {         /* bare number = float on the main inlet */
                ligase_atom_t a = { LIGASE_ATOM_FLOAT, f, NULL };
                rc = ligase_engine_send(e, "float", 1, &a);
            } else {
                rc = ligase_engine_send(e, sel, n, atoms);
            }
            if (rc != 0) {
                failures++;
                if (e->print) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "ligase~: %s: %s", rc == -1 ? "no method for" : "bad arguments for", sel);
                    e->print(e->user, 1, msg);
                }
            }
        }
        free(buf);
        p = (*end) ? end + 1 : end;
    }
    return failures;
}

void ligase_engine_bang(ligase_engine_t *e, double time_ms) {
    double saved = e->sh.logical_time;
    if (time_ms >= 0.0) e->sh.logical_time = time_ms * TIMEUNITPERMSEC;
    pdshim_set_current(&e->sh);
    pdshim_typedmess(e->obj, &s_bang, 0, NULL);
    pdshim_set_current(NULL);
    if (time_ms >= 0.0) e->sh.logical_time = saved;
}

void ligase_engine_set_inlet(ligase_engine_t *e, int inlet, float value, float glide_ms) {
    if (inlet < 2 || inlet >= LIGASE_ENGINE_INLETS) return;
    if (!isfinite(value)) return;
    e->cv_target[inlet] = value;
    int blocks = (glide_ms > 0.0f) ? (int)lrintf(glide_ms * 0.001f * (float)e->sr / (float)e->block) : 0;
    if (blocks < 1) { e->cv_cur[inlet] = value; e->cv_ramp[inlet] = 0; e->cv_step[inlet] = 0.0f; return; }
    e->cv_ramp[inlet] = blocks;
    e->cv_step[inlet] = (value - e->cv_cur[inlet]) / (float)blocks;
}

float ligase_engine_get_inlet(const ligase_engine_t *e, int inlet) {
    if (inlet < 2 || inlet >= LIGASE_ENGINE_INLETS) return 0.0f;
    return e->cv_cur[inlet];
}

/* --- audio --------------------------------------------------------------------------------- */
static void fill(float *v, int n, float x) { for (int i = 0; i < n; i++) v[i] = x; }

void ligase_engine_process(ligase_engine_t *e, const float *in_l, const float *in_r,
                           float *out_l, float *out_r, float *scope_x, float *scope_y) {
    const int n = e->block;
    if (in_l) memcpy(e->vec[0], in_l, sizeof(float) * (size_t)n); else memset(e->vec[0], 0, sizeof(float) * (size_t)n);
    if (in_r) memcpy(e->vec[1], in_r, sizeof(float) * (size_t)n); else memset(e->vec[1], 0, sizeof(float) * (size_t)n);
    for (int i = 2; i < LIGASE_ENGINE_INLETS; i++) {
        if (e->cv_ramp[i] > 0) {
            e->cv_cur[i] += e->cv_step[i];
            if (--e->cv_ramp[i] == 0) e->cv_cur[i] = e->cv_target[i];
        }
        fill(e->vec[i], n, e->cv_cur[i]);
    }
    pdshim_set_current(&e->sh);
    if (e->sh.perf) (*e->sh.perf)(e->sh.w);
    e->sh.logical_time += e->block_units;
    pdshim_set_current(NULL);
    if (out_l)   memcpy(out_l,   e->vec[LIGASE_ENGINE_INLETS + 0], sizeof(float) * (size_t)n);
    if (out_r)   memcpy(out_r,   e->vec[LIGASE_ENGINE_INLETS + 1], sizeof(float) * (size_t)n);
    if (scope_x) memcpy(scope_x, e->vec[LIGASE_ENGINE_INLETS + 2], sizeof(float) * (size_t)n);
    if (scope_y) memcpy(scope_y, e->vec[LIGASE_ENGINE_INLETS + 3], sizeof(float) * (size_t)n);
}

/* --- readback ------------------------------------------------------------------------------ */
void ligase_engine_status(ligase_engine_t *e, ligase_status_t *st) { ligase_status(e->obj, st); }
double ligase_engine_logical_ms(const ligase_engine_t *e) { return e->sh.logical_time / TIMEUNITPERMSEC; }
void *ligase_engine_object(ligase_engine_t *e) { return e->obj; }

int ligase_engine_selector_count(void) { ensure_setup(); return pdshim_method_count(pdshim_class_find("ligase~")); }
const char *ligase_engine_selector_name(int i) { ensure_setup(); return pdshim_method_name(pdshim_class_find("ligase~"), i); }
const char *ligase_engine_selector_signature(int i) {
    static _Thread_local char sig[16];
    ensure_setup();
    t_atomtype types[8];
    int n = pdshim_method_argc(pdshim_class_find("ligase~"), i, types, 8);
    if (n == -1) return "gimme";
    if (n == -2) return "cant";
    int k = 0;
    for (int j = 0; j < n && k < 15; j++) sig[k++] = (types[j] == A_SYMBOL || types[j] == A_DEFSYM) ? 's' : 'f';
    sig[k] = 0;
    return sig;
}
