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
#define MORPH_SCALAR_COUNT   64   // continuous scalar bases (fixed upper bound; capture table <= this)
#define MORPH_DISCRETE_COUNT 32   // discrete modes/enums/channels (fixed upper bound)

#define MORPH_INCLUDE_COUNT (MORPH_RANGE_COUNT + MORPH_SCALAR_COUNT + MORPH_DISCRETE_COUNT)

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
typedef struct {
    int   in_use;
    morph_range_slot_t ranges[MORPH_RANGE_COUNT];      // (a) modulatable ranges
    float scalars[MORPH_SCALAR_COUNT];                 // (b) continuous scalars (lerp)
    int   discretes[MORPH_DISCRETE_COUNT];             // (c) discrete ints (argmax step)
    float pitch_scale[MAX_SCALE_NOTES];        int pitch_scale_count;
    float smear_pitch_scale[MAX_SCALE_NOTES];  int smear_pitch_scale_count;
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
