/* pdshim.h — a minimal Pure Data API implementation ("shim") for hosting ligase~ outside Pd.
 *
 * ligase~ is a Pd external: src/ligase~.c talks to Pd through m_pd.h (class/method table,
 * inlets/outlets, dsp_add, post/pd_error, gensym, canvas path helpers, logical time). Every
 * other engine source is Pd-free. This shim implements exactly that subset, so ligase~.c is
 * compiled UNMODIFIED into the plugin and the class_addmethod table becomes the plugin's
 * control vocabulary (the "message-string dispatch IS the parameter API" design in
 * Plans/vst_plugin.md v2).
 *
 * Model: Pd is single-threaded with process globals; here every ligase instance owns a
 * pdshim_instance_t and the facade marks it "current" (thread-local) around every call into
 * the engine — messages, the dsp method, and the perform routine — so shim callbacks
 * (outlets, prints, dsp_add, canvas_getcurrent, clock_getlogicaltime) resolve to the right
 * instance. The symbol table and the class table are process-wide and read-only after
 * setup (lookups are lock-free; creation takes a mutex).
 */
#ifndef LIGASE_PDSHIM_H
#define LIGASE_PDSHIM_H

#include "m_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PDSHIM_MAX_PERFARGS 64

typedef void (*pdshim_print_fn)(void *user, int level, const char *text);          /* level 0 = post, 1 = error */
typedef void (*pdshim_outlet_fn)(void *user, int outlet, t_symbol *sel, int argc, t_atom *argv);

typedef struct pdshim_instance {
    void            *user;
    pdshim_print_fn  print;
    pdshim_outlet_fn outlet;
    char             dir[MAXPDSTRING];   /* "canvas" directory for relative load/save paths */
    double           logical_time;       /* Pd time units (TIMEUNITPERMSEC = 14112 per ms) */
    float            sample_rate;
    /* dsp_add capture: perform routine + its argument vector (w[0] = routine) */
    t_perfroutine    perf;
    t_int            w[PDSHIM_MAX_PERFARGS + 1];
    int              wn;
    /* the object this instance hosts */
    t_pd            *obj;
    /* the object's canvas (returned by canvas_getcurrent while this instance is current) */
    struct _glist   *canvas;
} pdshim_instance_t;

/* Process-wide init (idempotent, thread-safe): builtin symbols. Called by everything below. */
void pdshim_init(void);

/* Make an instance current for the calling thread (NULL to clear). */
void pdshim_set_current(pdshim_instance_t *inst);
pdshim_instance_t *pdshim_current(void);

/* Attach/detach the per-instance canvas (allocated by the shim, freed by pdshim_instance_release). */
void pdshim_instance_init(pdshim_instance_t *inst, const char *dir);
void pdshim_instance_release(pdshim_instance_t *inst);

/* Class registry (populated by class_new). */
t_class *pdshim_class_find(const char *name);
void    *pdshim_class_instantiate(t_class *c);        /* calls the constructor (no creation args) */
void     pdshim_object_free(t_pd *obj);               /* free method + inlets/outlets + memory */

/* Message dispatch (Pd's pd_typedmess for the supported argument patterns).
 *   0 = ok, -1 = no such method, -2 = bad arguments, -3 = not message-callable (A_CANT) */
int pdshim_typedmess(t_pd *obj, t_symbol *sel, int argc, t_atom *argv);
/* Call the A_CANT "dsp" method with a signal array. Returns 0 on success. */
int pdshim_call_dsp(t_pd *obj, t_signal **sp);

/* Introspection: the method table (for host UIs / docs). */
int pdshim_method_count(t_class *c);
const char *pdshim_method_name(t_class *c, int i);
int pdshim_method_argc(t_class *c, int i, t_atomtype *types, int max);   /* returns arg count; A_GIMME => -1 */

/* Outlet/inlet metadata */
int pdshim_outlet_count(t_pd *obj);
int pdshim_inlet_count(t_pd *obj);                    /* counts inlet_new'd inlets (main inlet excluded) */
t_symbol *pdshim_outlet_type(t_pd *obj, int index);

#ifdef __cplusplus
}
#endif
#endif
