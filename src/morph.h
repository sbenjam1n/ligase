#ifndef LIGASE_MORPH_H
#define LIGASE_MORPH_H

// Morph / Metasurface layer — snapshot interpolation across a 2D surface.
//
// This header holds ONLY the data structures + the pure (ligase_t-independent)
// helpers: the snapshot buffers, the surface point list, the route, the IDW
// kernel and the curve functions. Everything that touches `struct _ligase`
// fields (capture, apply/blend, the message handlers, the per-block stepper)
// lives in ligase~.c, where that struct is visible. See Plans/morph_metasurface.md.

#include "types.h"   // MAX_SCALE_NOTES

// ── Caps ────────────────────────────────────────────────────────────────────
#define MORPH_MAX_SNAPSHOTS  64   // surface stays cheap; IDW is O(snapshots)
#define MORPH_MAX_WAYPOINTS  64
#define MORPH_RANGE_COUNT    45   // get_param_range_by_name (41) + saw_cycles + saw_depth
                                  //   + pitch.semitone_range + smear_pitch.semitone_range
#define MORPH_SCALAR_COUNT   96   // continuous scalar bases (fixed upper bound; capture table <= this).
                                  //   Grown 64->96 for the schema-v4 SOURCE SHAPE scalars (61 used + 28
                                  //   new = 89 > 64). CAUTION: the included[] index layout is (ranges,
                                  //   then SCALAR_COUNT scalars, then discretes), so this growth SHIFTS
                                  //   the discrete include-indices in exported files: v1-v3 exports wrote
                                  //   discrete indices from base 45+64=109; v4 writes them from 45+96=141.
                                  //   morph_import remaps old-layout exclude indices by file version
                                  //   (see MORPH_SCALAR_COUNT_V3 and the "exclude" handler in ligase~.c).
#define MORPH_SCALAR_COUNT_V3 64  // the pre-v4 scalar capacity — the OLD included[] layout stride,
                                  //   kept only for the import-time exclude-index remap
#define MORPH_DISCRETE_COUNT 48   // discrete modes/enums/channels (fixed upper bound; grown 32->48 for
                                  //   the schema-v3 generator discretes)

#define MORPH_INCLUDE_COUNT (MORPH_RANGE_COUNT + MORPH_SCALAR_COUNT + MORPH_DISCRETE_COUNT)

// Logical field counts actually populated by capture — the schema for the TEXT export
// (morph_export/morph_import). MUST match the capture + the shared field walker in ligase~.c:
//   v1/v2 scalars   = morph_collect_scalars() (21) + MORPH_FX_SCALARS (11)          = 32
//   v1/v2 discretes = morph_collect_discretes() (28) + 2 enums (playhead, pitch mode) = 30
//   v3 appends the GENERATOR ("sources") params: scalars +29 (noise_freq_1..4,
//   env_follow_ms, sphere_damping/elasticity x4, nbody G/damping/epsilon/pump x4),
//   discretes +12 (nbody pump_interval/mode x4, sphere mode x4).
//   v4 appends the SOURCE SHAPE params: scalars +28 (waveform_phase_1..4,
//   square_pw_1..4, saw_skew_1..4, lorenz sigma/rho/beta x4, sphere_spin_1..4).
// Bump MORPH_TEXT_VERSION whenever this schema changes. Import still accepts older
// versions: fields with a newer `since` tag in the walker keep their current values.
//   v5 appends the HARMONIC LAYER params: scalars +2 (scale_root, smear_scale_root),
//   discretes +6 (active slot x2, root quant x2, rotate x2), plus 32 SCALE-SLOT list
//   fields (16 slots x 2 destinations; empty slots written count-only in the text export).
//   NOTE: v5 grew NEITHER MORPH_SCALAR_COUNT (91 used <= 96) NOR MORPH_DISCRETE_COUNT
//   (48 used == 48), so the included[] index layout is UNCHANGED — no v4->v5 exclude-index
//   remap is needed (unlike the v3->v4 MORPH_SCALAR_COUNT_V3 remap above).
#define MORPH_SCALAR_USED_V2   32   // v1/v2 file layout (scalars written before v3)
#define MORPH_DISCRETE_USED_V2 30   // v1/v2 file layout (discretes written before v3)
#define MORPH_SCALAR_USED_V3   61   // v3 file layout (scalars written before v4)
#define MORPH_SCALAR_USED_V4   89   // v4 file layout (scalars written before v5)
#define MORPH_DISCRETE_USED_V4 42   // v3/v4 file layout (discretes written before v5)
#define MORPH_SCALAR_USED   91      // v5: 32 + 29 generator + 28 source-shape + 2 harmonic scalars
#define MORPH_DISCRETE_USED 48      // v5: 30 + 12 generator + 6 harmonic discretes
#define MORPH_SCALE_SLOT_FIELDS (2 * 16)  // v5: the 32 scale-slot list fields (walker kind MF_SCALE_SLOT)
#define MORPH_TEXT_VERSION  5   /* v5 adds the HARMONIC LAYER (slots/root/rotate); v1-v4 still import */

// Route-leg easing curves
enum {
    MORPH_CURVE_LINEAR = 0,
    MORPH_CURVE_EASE_IN,
    MORPH_CURVE_EASE_OUT,
    MORPH_CURVE_EASE_IN_OUT,
    MORPH_CURVE_HOLD
};

// Interpolation kernels (pluggable; the API is kernel-agnostic)
enum {
    MORPH_INTERP_IDW = 0,   // Shepard inverse-distance weighting (v1)
    MORPH_INTERP_NN  = 1     // natural-neighbour / Sibson (v1.x, reserved)
};

// One captured modulation BAND (the snapshot-worthy subset of param_range_t).
// The scalar BASE that pairs with each range is captured separately in scalars[]
// (band and base are independent fields — see the capture notes in morph_metasurface.md).
typedef struct {
    float min, max;      // modulation band (continuous -> lerp)
    int   enabled;       // band active? (discrete -> step)
    int   rand_type;     // generator (categorical -> step, not lerp)
    int   rand_instance; // generator instance (categorical -> step)
    float base_value;    // PERLIN_2D Y base (continuous -> lerp)
    float slew;          // smoothing coeff (continuous -> lerp)
    int   invert;        // categorical -> step
} morph_range_slot_t;

// Shadow mirror of the opaque-FX scalar bases (the FX objects expose no readback). Each FX
// setter mirror-writes its value here; capture reads it, restore re-applies via the FX setter.
// v1 covers the playable FX (moog / smear / gdelay); distortion-enhancement + stut/bencina
// scalar bases are a documented later-completeness item (their modulation bands already morph).
#define MORPH_FX_SCALARS 11
typedef struct {
    float moog_cutoff, moog_resonance, moog_mix;
    float smear_frequency, smear_resonance, smear_stages, smear_feedback;
    float gdelay_time, gdelay_feedback, gdelay_tone, gdelay_mix;
} morph_fx_shadow_t;

// A full snapshot — three field classes (continuous lerp vs discrete step) + the scale lists.
// Schema v5 adds the 16 scale SLOTS per destination (argmax step on blend, like the active
// scale lists; ~16.5 KB per snapshot — accepted in Plans/harmonic_layer.md; the text export
// writes empty slots count-only so files stay compact).
typedef struct {
    int   in_use;
    morph_range_slot_t ranges[MORPH_RANGE_COUNT];      // (a) modulatable ranges
    float scalars[MORPH_SCALAR_COUNT];                 // (b) continuous scalars (lerp)
    int   discretes[MORPH_DISCRETE_COUNT];             // (c) discrete ints (argmax step)
    float pitch_scale[MAX_SCALE_NOTES];        int pitch_scale_count;
    float smear_pitch_scale[MAX_SCALE_NOTES];  int smear_pitch_scale_count;
    // (d) HARMONIC LAYER scale slots (schema v5) — 16 per destination, A-P
    float pitch_scale_slots[16][MAX_SCALE_NOTES];        int pitch_scale_slot_counts[16];
    float smear_pitch_scale_slots[16][MAX_SCALE_NOTES];  int smear_pitch_scale_slot_counts[16];
} morph_snapshot_t;

// A placed snapshot on the surface.
typedef struct { int snap_id; float x, y; int in_use; } morph_point_t;

// A route waypoint (the value-add over AudioMulch's bare X/Y automation).
typedef struct {
    float x, y;     // target cursor coordinate for this leg
    float rate;     // seconds to traverse this leg (transition rate)
    int   curve;    // MORPH_CURVE_*
} morph_waypoint_t;

// The surface, route, and engine state (owned by struct _ligase as x->morph).
typedef struct {
    morph_snapshot_t snaps[MORPH_MAX_SNAPSHOTS];
    morph_point_t    points[MORPH_MAX_SNAPSHOTS];   // compact: points[0..point_count-1]
    int              point_count;

    float cursor_x, cursor_y;       // live cursor (also written by morph_x/morph_y / signal inlets)
    int   interp_kind;              // MORPH_INTERP_*
    float idw_power;                // Shepard exponent p (default 2.0)
    int   cursor_is_signal;         // CV cursor engaged (signal inlets drive the cursor) — GATE F

    int   included[MORPH_INCLUDE_COUNT];  // selection tree (1 = morph this field)

    // route playback
    morph_waypoint_t route[MORPH_MAX_WAYPOINTS];
    int   route_len;
    int   route_active;             // morph_run engaged
    int   route_leg;                // current leg index
    float route_leg_t;              // 0..1 progress within the current leg
    float route_from_x, route_from_y; // leg start coordinate
    int   route_loop;               // loop the path?

    // GLOBAL BASE RATE — a relative multiplier on EVERY route leg's rate: the per-leg rates
    // keep their ratios, base_rate scales them all (higher = faster). LFO/chaos-modulatable
    // via rate_range (`param_range morph_rate <min> <max>` + a generator), exactly like every
    // other modulatable param. base_rate is the value used while rate_range is disabled.
    float         base_rate;        // default 1.0
    param_range_t rate_range;       // optional modulation band on the base rate
} morph_state_t;

// ── Pure helpers (no struct _ligase access; implemented in morph.c) ──────────

// Zero + default a freshly-allocated morph_state_t (selection tree = all-on,
// cursor centred, IDW kernel, p=2).
void  morph_state_init(morph_state_t *m);

// Ease a normalized leg progress t in [0,1] by the given MORPH_CURVE_* kind.
float morph_curve(float t, int kind);

// Compute normalized IDW (Shepard) weights for the placed points at cursor (cx,cy).
// Writes point_count weights into w[] and returns point_count, OR:
//   returns  0 if no points are placed (w untouched);
//   returns -1 on an exact hit (cursor sits on a point) and sets *exact_idx to
//            that point index — the caller uses that snapshot with weight 1
//            (Bencina's exact-reproduction / no-overshoot property).
int   morph_compute_weights(const morph_state_t *m, float cx, float cy,
                            float *w, int *exact_idx);

#endif // LIGASE_MORPH_H
