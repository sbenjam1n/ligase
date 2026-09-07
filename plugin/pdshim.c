/* pdshim.c — minimal Pure Data API implementation for hosting ligase~ outside Pd. See pdshim.h. */
#include "pdshim.h"
/* m_pd.h wraps these in same-named function-like macros (cast helpers); we define the real functions */
#undef class_addbang
#undef class_addpointer
#undef class_addfloat
#undef class_addsymbol
#undef class_addlist
#undef class_addanything
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
# include <io.h>
# define pdshim_open(p) _open((p), _O_RDONLY | _O_BINARY)
# define pdshim_close(fd) _close(fd)
# define PDSHIM_TLS __declspec(thread)
#else
# include <unistd.h>
# include <pthread.h>
# define pdshim_open(p) open((p), O_RDONLY)
# define pdshim_close(fd) close(fd)
# define PDSHIM_TLS _Thread_local
#endif

/* ------------------------------------------------------------------------------------------
 * Locking (creation paths only — never on the audio path)
 * ------------------------------------------------------------------------------------------ */
#ifdef _WIN32
# include <windows.h>
static CRITICAL_SECTION g_lock; static LONG g_lock_init = 0;
static void lock_init(void) { if (InterlockedCompareExchange(&g_lock_init, 1, 0) == 0) InitializeCriticalSection(&g_lock); else Sleep(0); }
static void lock(void)   { EnterCriticalSection(&g_lock); }
static void unlock(void) { LeaveCriticalSection(&g_lock); }
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock_init(void) {}
static void lock(void)   { pthread_mutex_lock(&g_lock); }
static void unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

/* ------------------------------------------------------------------------------------------
 * Builtin symbols (declared EXTERN in m_pd.h)
 * ------------------------------------------------------------------------------------------ */
t_symbol s_pointer  = { "pointer",  0, 0 };
t_symbol s_float    = { "float",    0, 0 };
t_symbol s_symbol   = { "symbol",   0, 0 };
t_symbol s_bang     = { "bang",     0, 0 };
t_symbol s_list     = { "list",     0, 0 };
t_symbol s_anything = { "anything", 0, 0 };
t_symbol s_signal   = { "signal",   0, 0 };
t_symbol s__N       = { "#N",       0, 0 };
t_symbol s__X       = { "#X",       0, 0 };
t_symbol s_x        = { "x",        0, 0 };
t_symbol s_y        = { "y",        0, 0 };
t_symbol s_         = { "",         0, 0 };

/* ------------------------------------------------------------------------------------------
 * Symbol table: open addressing, lock-free lookups (atomic publish), locked inserts.
 * Symbols are never freed, so a reader can never observe a torn entry.
 * ------------------------------------------------------------------------------------------ */
#define SYMTAB_BITS 14
#define SYMTAB_SIZE (1 << SYMTAB_BITS)
static _Atomic(t_symbol *) g_symtab[SYMTAB_SIZE];
static atomic_int g_initialized = 0;

static unsigned symhash(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static t_symbol *symtab_insert(t_symbol *sym) {
    unsigned i = symhash(sym->s_name) & (SYMTAB_SIZE - 1);
    for (unsigned n = 0; n < SYMTAB_SIZE; n++, i = (i + 1) & (SYMTAB_SIZE - 1)) {
        t_symbol *cur = atomic_load_explicit(&g_symtab[i], memory_order_acquire);
        if (!cur) { atomic_store_explicit(&g_symtab[i], sym, memory_order_release); return sym; }
        if (!strcmp(cur->s_name, sym->s_name)) return cur;
    }
    return sym; /* table full: unreachable at this size for any sane plugin */
}

void pdshim_init(void) {
    if (atomic_load(&g_initialized)) return;
    lock_init();
    lock();
    if (!atomic_load(&g_initialized)) {
        t_symbol *builtins[] = { &s_pointer, &s_float, &s_symbol, &s_bang, &s_list, &s_anything,
                                 &s_signal, &s__N, &s__X, &s_x, &s_y, &s_ };
        for (size_t k = 0; k < sizeof(builtins) / sizeof(builtins[0]); k++) symtab_insert(builtins[k]);
        atomic_store(&g_initialized, 1);
    }
    unlock();
}

t_symbol *gensym(const char *s) {
    if (!atomic_load(&g_initialized)) pdshim_init();
    unsigned i = symhash(s) & (SYMTAB_SIZE - 1);
    for (unsigned n = 0; n < SYMTAB_SIZE; n++, i = (i + 1) & (SYMTAB_SIZE - 1)) {
        t_symbol *cur = atomic_load_explicit(&g_symtab[i], memory_order_acquire);
        if (!cur) break;
        if (!strcmp(cur->s_name, s)) return cur;
    }
    lock();
    t_symbol *sym = (t_symbol *)calloc(1, sizeof(t_symbol));
    size_t len = strlen(s);
    char *name = (char *)malloc(len + 1);
    memcpy(name, s, len + 1);
    sym->s_name = name;
    t_symbol *got = symtab_insert(sym);
    if (got != sym) { free(name); free(sym); }
    unlock();
    return got;
}

/* ------------------------------------------------------------------------------------------
 * Instances / current-instance tracking
 * ------------------------------------------------------------------------------------------ */
struct _glist { char dir[MAXPDSTRING]; pdshim_instance_t *inst; };

static PDSHIM_TLS pdshim_instance_t *g_current = NULL;

void pdshim_set_current(pdshim_instance_t *inst) { g_current = inst; }
pdshim_instance_t *pdshim_current(void) { return g_current; }

void pdshim_instance_init(pdshim_instance_t *inst, const char *dir) {
    pdshim_init();
    memset(inst, 0, sizeof(*inst));
    if (dir) { strncpy(inst->dir, dir, MAXPDSTRING - 1); inst->dir[MAXPDSTRING - 1] = 0; }
    inst->canvas = (struct _glist *)calloc(1, sizeof(struct _glist));
    strncpy(inst->canvas->dir, inst->dir, MAXPDSTRING - 1);
    inst->canvas->inst = inst;
    inst->sample_rate = 44100.0f;
}

void pdshim_instance_release(pdshim_instance_t *inst) {
    if (inst->canvas) { free(inst->canvas); inst->canvas = NULL; }
}

/* ------------------------------------------------------------------------------------------
 * Console
 * ------------------------------------------------------------------------------------------ */
static void emit(int level, const char *text) {
    pdshim_instance_t *inst = g_current;
    if (inst && inst->print) inst->print(inst->user, level, text);
    else { fputs(text, stderr); fputc('\n', stderr); }
}
static void vemit(int level, const char *fmt, va_list ap) {
    char buf[MAXPDSTRING * 2];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    emit(level, buf);
}
void post(const char *fmt, ...)        { va_list ap; va_start(ap, fmt); vemit(0, fmt, ap); va_end(ap); }
void startpost(const char *fmt, ...)   { va_list ap; va_start(ap, fmt); vemit(0, fmt, ap); va_end(ap); }
void poststring(const char *s)         { emit(0, s); }
void endpost(void)                     {}
void pd_error(const void *obj, const char *fmt, ...) { (void)obj; va_list ap; va_start(ap, fmt); vemit(1, fmt, ap); va_end(ap); }
void logpost(const void *obj, int level, const char *fmt, ...) { (void)obj; va_list ap; va_start(ap, fmt); vemit(level <= 1 ? 1 : 0, fmt, ap); va_end(ap); }
void verbose(int level, const char *fmt, ...) { (void)level; va_list ap; va_start(ap, fmt); vemit(0, fmt, ap); va_end(ap); }
void bug(const char *fmt, ...)         { va_list ap; va_start(ap, fmt); vemit(1, fmt, ap); va_end(ap); }
void error(const char *fmt, ...)       { va_list ap; va_start(ap, fmt); vemit(1, fmt, ap); va_end(ap); }

/* ------------------------------------------------------------------------------------------
 * Memory
 * ------------------------------------------------------------------------------------------ */
void *getbytes(size_t n)                    { return calloc(1, n ? n : 1); }
void *getzbytes(size_t n)                   { return calloc(1, n ? n : 1); }
void *copybytes(const void *src, size_t n)  { void *p = malloc(n ? n : 1); if (p && n) memcpy(p, src, n); return p; }
void  freebytes(void *p, size_t n)          { (void)n; free(p); }
void *resizebytes(void *p, size_t o, size_t n) { (void)o; return realloc(p, n ? n : 1); }

/* ------------------------------------------------------------------------------------------
 * Atoms
 * ------------------------------------------------------------------------------------------ */
t_float   atom_getfloat(const t_atom *a)  { return a->a_type == A_FLOAT ? a->a_w.w_float : 0; }
t_int     atom_getint(const t_atom *a)    { return (t_int)atom_getfloat(a); }
t_symbol *atom_getsymbol(const t_atom *a) { return a->a_type == A_SYMBOL ? a->a_w.w_symbol : &s_symbol; }
t_float   atom_getfloatarg(int which, int argc, const t_atom *argv) {
    if (which < 0 || which >= argc) return 0; return atom_getfloat(argv + which); }
t_int     atom_getintarg(int which, int argc, const t_atom *argv) { return (t_int)atom_getfloatarg(which, argc, argv); }
t_symbol *atom_getsymbolarg(int which, int argc, const t_atom *argv) {
    if (which < 0 || which >= argc) return &s_; return atom_getsymbol(argv + which); }
void atom_string(const t_atom *a, char *buf, unsigned int bufsize) {
    switch (a->a_type) {
        case A_FLOAT:  snprintf(buf, bufsize, "%g", (double)a->a_w.w_float); break;
        case A_SYMBOL: snprintf(buf, bufsize, "%s", a->a_w.w_symbol->s_name); break;
        default:       snprintf(buf, bufsize, "?"); break;
    }
}

/* ------------------------------------------------------------------------------------------
 * Classes and objects
 * ------------------------------------------------------------------------------------------ */
#define PDSHIM_MAX_METHOD_ARGS 8

typedef struct {
    t_symbol  *sel;
    t_method   fn;
    int        nargs;                          /* -1 = A_GIMME, -2 = A_CANT */
    t_atomtype args[PDSHIM_MAX_METHOD_ARGS];
} pdshim_method_t;

struct _class {
    t_symbol        *c_name;
    t_newmethod      c_new;
    t_method         c_free;
    size_t           c_size;
    int              c_flags;
    int              c_floatsignalin;          /* byte offset of the CLASS_MAINSIGNALIN float, -1 none */
    t_method         c_bang;
    t_method         c_float;
    pdshim_method_t *c_methods;
    int              c_nmethods, c_cap;
    struct _class   *c_next;
};

struct _inlet  { t_object *i_owner; t_symbol *i_sym1, *i_sym2; int i_index; struct _inlet *i_next; };
struct _outlet { t_object *o_owner; t_symbol *o_sym; int o_index; struct _outlet *o_next; };

static struct _class *g_classes = NULL;

static struct _class *class_new_impl(t_symbol *name, t_newmethod newmethod, t_method freemethod, size_t size, int flags) {
    pdshim_init();
    struct _class *c = (struct _class *)calloc(1, sizeof(struct _class));
    c->c_name = name; c->c_new = newmethod; c->c_free = freemethod; c->c_size = size;
    c->c_flags = flags; c->c_floatsignalin = -1;
    lock();
    c->c_next = g_classes; g_classes = c;
    unlock();
    return c;
}

t_class *class_new(t_symbol *name, t_newmethod newmethod, t_method freemethod, size_t size, int flags, t_atomtype arg1, ...) {
    (void)arg1;   /* ligase~ takes no creation arguments */
    return class_new_impl(name, newmethod, freemethod, size, flags);
}
t_class *class_new64(t_symbol *name, t_newmethod newmethod, t_method freemethod, size_t size, int flags, t_atomtype arg1, ...) {
    (void)arg1;
    return class_new_impl(name, newmethod, freemethod, size, flags);
}
void class_free(t_class *c) { (void)c; }

static pdshim_method_t *method_slot(t_class *c) {
    if (c->c_nmethods == c->c_cap) {
        int ncap = c->c_cap ? c->c_cap * 2 : 64;
        c->c_methods = (pdshim_method_t *)realloc(c->c_methods, (size_t)ncap * sizeof(pdshim_method_t));
        c->c_cap = ncap;
    }
    pdshim_method_t *m = &c->c_methods[c->c_nmethods++];
    memset(m, 0, sizeof(*m));
    return m;
}

void class_addmethod(t_class *c, t_method fn, t_symbol *sel, t_atomtype arg1, ...) {
    pdshim_method_t *m = method_slot(c);
    m->sel = sel; m->fn = fn; m->nargs = 0;
    va_list ap; va_start(ap, arg1);
    t_atomtype t = arg1;
    while (t != A_NULL) {
        if (t == A_GIMME) { m->nargs = -1; break; }
        if (t == A_CANT)  { m->nargs = -2; break; }
        if (m->nargs < PDSHIM_MAX_METHOD_ARGS) m->args[m->nargs++] = t;
        t = (t_atomtype)va_arg(ap, int);
    }
    va_end(ap);
}
void class_addbang(t_class *c, t_method fn)     { c->c_bang = fn; }
void class_doaddfloat(t_class *c, t_method fn)  { c->c_float = fn; }
void class_addpointer(t_class *c, t_method fn)  { (void)c; (void)fn; }
void class_addsymbol(t_class *c, t_method fn)   { (void)c; (void)fn; }
void class_addlist(t_class *c, t_method fn)     { (void)c; (void)fn; }
void class_addanything(t_class *c, t_method fn) { (void)c; (void)fn; }
void class_domainsignalin(t_class *c, int onset) { c->c_floatsignalin = onset; }
void class_sethelpsymbol(t_class *c, t_symbol *s) { (void)c; (void)s; }
const char *class_getname(const t_class *c) { return c->c_name->s_name; }

t_pd *pd_new(t_class *c) {
    t_pd *x = (t_pd *)calloc(1, c->c_size > sizeof(t_pd) ? c->c_size : sizeof(t_pd));
    *x = c;
    return x;
}

t_class *pdshim_class_find(const char *name) {
    for (struct _class *c = g_classes; c; c = c->c_next)
        if (!strcmp(c->c_name->s_name, name)) return c;
    return NULL;
}

void *pdshim_class_instantiate(t_class *c) {
    if (!c || !c->c_new) return NULL;
    void *(*ctor)(void) = (void *(*)(void))c->c_new;
    return ctor();
}

void pdshim_object_free(t_pd *obj) {
    if (!obj) return;
    t_class *c = *obj;
    if (c && c->c_free) ((void (*)(void *))c->c_free)(obj);
    if (c && (c->c_flags & CLASS_PATCHABLE) == CLASS_PATCHABLE) {
        t_object *o = (t_object *)obj;
        for (t_inlet *i = o->te_inlet; i;) { t_inlet *n = i->i_next; free(i); i = n; }
        for (t_outlet *ol = o->te_outlet; ol;) { t_outlet *n = ol->o_next; free(ol); ol = n; }
    }
    free(obj);
}

/* --- dispatch ---------------------------------------------------------------------------- */
typedef void (*fn_v)(void *);
typedef void (*fn_f)(void *, t_floatarg);
typedef void (*fn_ff)(void *, t_floatarg, t_floatarg);
typedef void (*fn_fff)(void *, t_floatarg, t_floatarg, t_floatarg);
typedef void (*fn_ffff)(void *, t_floatarg, t_floatarg, t_floatarg, t_floatarg);
typedef void (*fn_fffff)(void *, t_floatarg, t_floatarg, t_floatarg, t_floatarg, t_floatarg);
typedef void (*fn_s)(void *, t_symbol *);
typedef void (*fn_sf)(void *, t_symbol *, t_floatarg);
typedef void (*fn_ss)(void *, t_symbol *, t_symbol *);
typedef void (*fn_gimme)(void *, t_symbol *, int, t_atom *);

static pdshim_method_t *find_method(t_class *c, t_symbol *sel) {
    for (int i = 0; i < c->c_nmethods; i++) if (c->c_methods[i].sel == sel) return &c->c_methods[i];
    return NULL;
}

int pdshim_typedmess(t_pd *obj, t_symbol *sel, int argc, t_atom *argv) {
    if (!obj || !sel) return -1;
    t_class *c = *obj;
    if (sel == &s_bang && argc == 0) {
        if (c->c_bang) { ((fn_v)c->c_bang)(obj); return 0; }
    }
    if (sel == &s_float) {
        if (c->c_float) { ((fn_f)c->c_float)(obj, atom_getfloatarg(0, argc, argv)); return 0; }
        if (c->c_floatsignalin >= 0) { *(t_float *)((char *)obj + c->c_floatsignalin) = atom_getfloatarg(0, argc, argv); return 0; }
    }
    pdshim_method_t *m = find_method(c, sel);
    if (!m) {
        /* A list whose selector is a float-typed first atom, or a bare number: hand to float */
        return -1;
    }
    if (m->nargs == -2) return -3;
    if (m->nargs == -1) { ((fn_gimme)m->fn)(obj, sel, argc, argv); return 0; }

    /* Marshal typed arguments (Pd's rules: A_DEF* default when missing; type mismatch = error) */
    t_floatarg f[PDSHIM_MAX_METHOD_ARGS] = {0};
    t_symbol *s[PDSHIM_MAX_METHOD_ARGS] = {0};
    int nf = 0, ns = 0;
    char pattern[PDSHIM_MAX_METHOD_ARGS + 1] = {0};
    for (int i = 0; i < m->nargs; i++) {
        t_atomtype t = m->args[i];
        int have = i < argc;
        switch (t) {
            case A_FLOAT:
                if (!have || argv[i].a_type != A_FLOAT) return -2;
                f[nf++] = argv[i].a_w.w_float; pattern[i] = 'f'; break;
            case A_DEFFLOAT:
                if (have && argv[i].a_type != A_FLOAT) return -2;
                f[nf++] = have ? argv[i].a_w.w_float : 0; pattern[i] = 'f'; break;
            case A_SYMBOL:
                if (!have || argv[i].a_type != A_SYMBOL) return -2;
                s[ns++] = argv[i].a_w.w_symbol; pattern[i] = 's'; break;
            case A_DEFSYM:
                if (have && argv[i].a_type != A_SYMBOL) return -2;
                s[ns++] = have ? argv[i].a_w.w_symbol : &s_; pattern[i] = 's'; break;
            default: return -2;
        }
    }
    if (!strcmp(pattern, ""))      { ((fn_v)m->fn)(obj); return 0; }
    if (!strcmp(pattern, "f"))     { ((fn_f)m->fn)(obj, f[0]); return 0; }
    if (!strcmp(pattern, "ff"))    { ((fn_ff)m->fn)(obj, f[0], f[1]); return 0; }
    if (!strcmp(pattern, "fff"))   { ((fn_fff)m->fn)(obj, f[0], f[1], f[2]); return 0; }
    if (!strcmp(pattern, "ffff"))  { ((fn_ffff)m->fn)(obj, f[0], f[1], f[2], f[3]); return 0; }
    if (!strcmp(pattern, "fffff")) { ((fn_fffff)m->fn)(obj, f[0], f[1], f[2], f[3], f[4]); return 0; }
    if (!strcmp(pattern, "s"))     { ((fn_s)m->fn)(obj, s[0]); return 0; }
    if (!strcmp(pattern, "sf"))    { ((fn_sf)m->fn)(obj, s[0], f[0]); return 0; }
    if (!strcmp(pattern, "ss"))    { ((fn_ss)m->fn)(obj, s[0], s[1]); return 0; }
    return -2;
}

int pdshim_call_dsp(t_pd *obj, t_signal **sp) {
    t_class *c = *obj;
    pdshim_method_t *m = find_method(c, gensym("dsp"));
    if (!m) return -1;
    ((void (*)(void *, t_signal **))m->fn)(obj, sp);
    return 0;
}

int pdshim_method_count(t_class *c) { return c ? c->c_nmethods : 0; }
const char *pdshim_method_name(t_class *c, int i) { return (c && i >= 0 && i < c->c_nmethods) ? c->c_methods[i].sel->s_name : NULL; }
int pdshim_method_argc(t_class *c, int i, t_atomtype *types, int max) {
    if (!c || i < 0 || i >= c->c_nmethods) return 0;
    pdshim_method_t *m = &c->c_methods[i];
    if (m->nargs < 0) return m->nargs;
    for (int k = 0; k < m->nargs && k < max; k++) types[k] = m->args[k];
    return m->nargs;
}

/* --- inlets / outlets ------------------------------------------------------------------- */
t_inlet *inlet_new(t_object *owner, t_pd *dest, t_symbol *s1, t_symbol *s2) {
    (void)dest;
    t_inlet *in = (t_inlet *)calloc(1, sizeof(t_inlet));
    in->i_owner = owner; in->i_sym1 = s1; in->i_sym2 = s2;
    int idx = 1;
    if (!owner->te_inlet) owner->te_inlet = in;
    else { t_inlet *p = owner->te_inlet; idx++; while (p->i_next) { p = p->i_next; idx++; } p->i_next = in; }
    in->i_index = idx;
    return in;
}
t_inlet *signalinlet_new(t_object *owner, t_float f) { (void)f; return inlet_new(owner, &owner->ob_pd, &s_signal, &s_signal); }
t_inlet *floatinlet_new(t_object *owner, t_float *fp) { (void)fp; return inlet_new(owner, &owner->ob_pd, &s_float, &s_float); }
t_inlet *symbolinlet_new(t_object *owner, t_symbol **sp) { (void)sp; return inlet_new(owner, &owner->ob_pd, &s_symbol, &s_symbol); }
void inlet_free(t_inlet *x) { (void)x; }

t_outlet *outlet_new(t_object *owner, t_symbol *s) {
    t_outlet *o = (t_outlet *)calloc(1, sizeof(t_outlet));
    o->o_owner = owner; o->o_sym = s;
    int idx = 0;
    if (!owner->te_outlet) owner->te_outlet = o;
    else { t_outlet *p = owner->te_outlet; idx++; while (p->o_next) { p = p->o_next; idx++; } p->o_next = o; }
    o->o_index = idx;
    return o;
}
void outlet_free(t_outlet *x) { (void)x; }

static void deliver(t_outlet *o, t_symbol *sel, int argc, t_atom *argv) {
    pdshim_instance_t *inst = g_current;
    if (!o || !inst || !inst->outlet) return;
    inst->outlet(inst->user, o->o_index, sel, argc, argv);
}
void outlet_bang(t_outlet *x)                 { deliver(x, &s_bang, 0, NULL); }
void outlet_float(t_outlet *x, t_float f)     { t_atom a; SETFLOAT(&a, f); deliver(x, &s_float, 1, &a); }
void outlet_symbol(t_outlet *x, t_symbol *s)  { t_atom a; SETSYMBOL(&a, s); deliver(x, &s_symbol, 1, &a); }
void outlet_list(t_outlet *x, t_symbol *s, int argc, t_atom *argv) { (void)s; deliver(x, &s_list, argc, argv); }
void outlet_anything(t_outlet *x, t_symbol *s, int argc, t_atom *argv) { deliver(x, s, argc, argv); }
void outlet_pointer(t_outlet *x, t_gpointer *gp) { (void)x; (void)gp; }

int pdshim_outlet_count(t_pd *obj) { int n = 0; for (t_outlet *o = ((t_object *)obj)->te_outlet; o; o = o->o_next) n++; return n; }
int pdshim_inlet_count(t_pd *obj)  { int n = 0; for (t_inlet *i = ((t_object *)obj)->te_inlet; i; i = i->i_next) n++; return n; }
t_symbol *pdshim_outlet_type(t_pd *obj, int index) {
    for (t_outlet *o = ((t_object *)obj)->te_outlet; o; o = o->o_next) if (o->o_index == index) return o->o_sym;
    return NULL;
}

/* ------------------------------------------------------------------------------------------
 * DSP hookup
 * ------------------------------------------------------------------------------------------ */
void dsp_add(t_perfroutine f, int n, ...) {
    pdshim_instance_t *inst = g_current;
    if (!inst) return;
    if (n > PDSHIM_MAX_PERFARGS) n = PDSHIM_MAX_PERFARGS;
    inst->perf = f; inst->wn = n;
    inst->w[0] = (t_int)f;
    va_list ap; va_start(ap, n);
    for (int i = 1; i <= n; i++) inst->w[i] = va_arg(ap, t_int);
    va_end(ap);
}
void dsp_addv(t_perfroutine f, int n, t_int *vec) {
    pdshim_instance_t *inst = g_current;
    if (!inst) return;
    if (n > PDSHIM_MAX_PERFARGS) n = PDSHIM_MAX_PERFARGS;
    inst->perf = f; inst->wn = n; inst->w[0] = (t_int)f;
    for (int i = 1; i <= n; i++) inst->w[i] = vec[i - 1];
}
t_float sys_getsr(void) { return g_current ? g_current->sample_rate : 44100.0f; }
int sys_getblksize(void) { return 64; }
double clock_getlogicaltime(void) { return g_current ? g_current->logical_time : 0.0; }
double clock_gettimesince(double prevsystime) { return (clock_getlogicaltime() - prevsystime) / 14112.0; }

/* ------------------------------------------------------------------------------------------
 * Canvas / file resolution (the reel/morph load & save path helpers)
 * ------------------------------------------------------------------------------------------ */
static int is_absolute(const char *p) {
    if (!p || !*p) return 0;
    if (p[0] == '/' || p[0] == '\\') return 1;
    if (((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':') return 1;
    return 0;
}

t_glist *canvas_getcurrent(void) { return g_current ? g_current->canvas : NULL; }
t_symbol *canvas_getcurrentdir(void) { return gensym(g_current ? g_current->dir : "."); }
t_symbol *canvas_getdir(const t_glist *x) { return gensym(x && x->dir[0] ? x->dir : "."); }
void canvas_getargs(int *argcp, t_atom **argvp) { *argcp = 0; *argvp = NULL; }
t_float canvas_getsr(t_canvas *x) { (void)x; return sys_getsr(); }
int canvas_getsignallength(t_canvas *x) { (void)x; return 64; }

void canvas_makefilename(const t_glist *c, const char *file, char *result, int resultsize) {
    const char *dir = (c && c->dir[0]) ? c->dir : NULL;
    if (is_absolute(file) || !dir) snprintf(result, (size_t)resultsize, "%s", file);
    else snprintf(result, (size_t)resultsize, "%s/%s", dir, file);
    result[resultsize - 1] = 0;
}

/* Pd contract: on success dirresult holds the directory, *nameresult points INTO dirresult
 * at the file name (with extension), and the returned fd is open (caller closes it). */
static int try_open_split(const char *full, char *dirresult, char **nameresult, unsigned size) {
    int fd = pdshim_open(full);
    if (fd < 0) return -1;
    size_t len = strlen(full);
    if (len + 1 > size) { pdshim_close(fd); return -1; }
    memcpy(dirresult, full, len + 1);
    char *slash = strrchr(dirresult, '/');
#ifdef _WIN32
    char *bslash = strrchr(dirresult, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash) { *slash = 0; *nameresult = slash + 1; }
    else { /* no directory component: dir = "." */
        memmove(dirresult + 2, dirresult, len + 1); dirresult[0] = '.'; dirresult[1] = 0; *nameresult = dirresult + 2; }
    return fd;
}

int canvas_open(const t_canvas *x, const char *name, const char *ext, char *dirresult, char **nameresult, unsigned int size, int bin) {
    (void)bin;
    char full[MAXPDSTRING * 2];
    const char *dir = (x && x->dir[0]) ? x->dir : NULL;
    const char *exts[2] = { ext ? ext : "", "" };
    for (int e = 0; e < 2; e++) {
        if (e == 1 && !exts[0][0]) break;
        if (is_absolute(name) || !dir) snprintf(full, sizeof(full), "%s%s", name, exts[e]);
        else snprintf(full, sizeof(full), "%s/%s%s", dir, name, exts[e]);
        int fd = try_open_split(full, dirresult, nameresult, size);
        if (fd >= 0) return fd;
        if (dir && !is_absolute(name)) {   /* also try the bare name (cwd-relative), like Pd's search path */
            snprintf(full, sizeof(full), "%s%s", name, exts[e]);
            fd = try_open_split(full, dirresult, nameresult, size);
            if (fd >= 0) return fd;
        }
    }
    return -1;
}
int sys_close(int fd) { return pdshim_close(fd); }
int sys_isabsolutepath(const char *dir) { return is_absolute(dir); }
