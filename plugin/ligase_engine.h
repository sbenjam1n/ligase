/* ligase_engine.h — host-facing facade around the ligase~ engine (src/ligase~.c hosted by the
 * Pd shim in pdshim.c). One instance = one ligase~ object with its own reel, scheduler, effects.
 *
 * Threading contract (matches Pd's single-threaded scheduler): every call that touches an
 * instance — messages, inlet writes, process, status — must be serialized by the caller.
 * The plugin does this by running messages on the audio thread between blocks (a queue) and
 * by holding a lock during the few heavy operations (reel load/save) while the audio thread
 * outputs silence. Different instances never share mutable state.
 */
#ifndef LIGASE_ENGINE_H
#define LIGASE_ENGINE_H

#include <stddef.h>
#include "ligase_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ligase_engine ligase_engine_t;

#define LIGASE_ENGINE_INLETS   24   /* signal inlets: 0/1 audio L/R, 2..23 CV (see docs/ligase_manual.md INLETS) */
#define LIGASE_ENGINE_OUTLETS  11   /* 0/1 audio, 2/3 bangs, 4-7 modout floats, 8 state list, 9/10 scope X/Y */

typedef enum { LIGASE_ATOM_FLOAT = 0, LIGASE_ATOM_SYMBOL = 1 } ligase_atom_type_t;
typedef struct { ligase_atom_type_t type; float f; const char *s; } ligase_atom_t;

/* level 0 = post, 1 = error */
typedef void (*ligase_print_fn)(void *user, int level, const char *text);
/* Non-signal outlet output. outlet index per LIGASE_ENGINE_OUTLETS; sel is "bang", "float",
 * "list" or the message selector (outlet 8 carries e.g. "speed 1.0", "snapbuf ...", "splice 2 5"). */
typedef void (*ligase_outlet_fn)(void *user, int outlet, const char *sel, int argc, const ligase_atom_t *argv);

/* Create an engine running at `sample_rate` with an inner DSP block of `block` frames (64 =
 * Pd's block; the value the engine's per-block modulation was designed for). `resource_dir`
 * anchors relative file names in load/save/morph_* messages (NULL = process cwd). */
ligase_engine_t *ligase_engine_new(int sample_rate, int block, const char *resource_dir);
void             ligase_engine_free(ligase_engine_t *e);

void ligase_engine_set_callbacks(ligase_engine_t *e, ligase_print_fn print, ligase_outlet_fn outlet, void *user);
void ligase_engine_set_resource_dir(ligase_engine_t *e, const char *dir);

int  ligase_engine_sample_rate(const ligase_engine_t *e);
int  ligase_engine_block(const ligase_engine_t *e);
/* Re-runs the engine's dsp method at the new rate (reallocates the reel/delay lines, like a
 * Pd sample-rate change). Recorded audio survives (reel_set_sample_rate keeps it). */
int  ligase_engine_set_sample_rate(ligase_engine_t *e, int sample_rate);

/* ---- control ---------------------------------------------------------------------------- */
/* Typed message to the main inlet. Returns 0 ok, -1 unknown selector, -2 bad args. */
int  ligase_engine_send(ligase_engine_t *e, const char *selector, int argc, const ligase_atom_t *argv);
/* Text form: "grainsize 0.25", "matrix_connect lorenz1 moog_cutoff 2000", "pattern pitch [ 0 2 4 ]".
 * Tokens that parse fully as numbers become floats, everything else symbols. Several messages
 * may be joined with ';' or newlines. Returns the number of messages that failed. */
int  ligase_engine_send_text(ligase_engine_t *e, const char *text);
/* A bang on the main inlet = the tempo clock tick. `time_ms` (>= 0) stamps the bang with an
 * exact logical time in milliseconds (host beat position) so BPM detection is exact; < 0 uses
 * the current block time. */
void ligase_engine_bang(ligase_engine_t *e, double time_ms);

/* Quiet mode: while set, the engine's informational post() lines are dropped (errors are still
 * delivered). Hosts use it around bursts of per-block smoothing messages. */
void ligase_engine_set_quiet(ligase_engine_t *e, int quiet);

/* Signal inlets 2..23 as control values, held for the whole block (Pd's [sig~]) with an
 * optional linear glide of `glide_ms` (the panel's [line~] 20 ms). Inlets 0/1 are audio. */
void  ligase_engine_set_inlet(ligase_engine_t *e, int inlet, float value, float glide_ms);
float ligase_engine_get_inlet(const ligase_engine_t *e, int inlet);   /* current (smoothed) value */

/* ---- audio ------------------------------------------------------------------------------ */
/* Process exactly one inner block. Any pointer may be NULL (silent input / discarded output). */
void ligase_engine_process(ligase_engine_t *e, const float *in_l, const float *in_r,
                           float *out_l, float *out_r, float *scope_x, float *scope_y);

/* ---- readback --------------------------------------------------------------------------- */
void   ligase_engine_status(ligase_engine_t *e, ligase_status_t *st);
double ligase_engine_logical_ms(const ligase_engine_t *e);   /* logical time at the next block start */
void  *ligase_engine_object(ligase_engine_t *e);             /* the underlying ligase~ object */

/* Engine vocabulary: iterate the message selectors registered by ligase_tilde_setup(). */
int         ligase_engine_selector_count(void);
const char *ligase_engine_selector_name(int i);
/* Argument signature of selector i as a string: "" (none), "f", "ff", "s", "gimme", "cant". */
const char *ligase_engine_selector_signature(int i);

#ifdef __cplusplus
}
#endif
#endif
