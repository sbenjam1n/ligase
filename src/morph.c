// Morph / Metasurface layer — pure (ligase_t-independent) helpers.
//
// Only the kernel math, curve functions, and state init live here. The
// x-coupled capture/apply/handlers live in ligase~.c (struct _ligase is
// private to that translation unit). See Plans/morph_metasurface.md.

#include "morph.h"
#include <math.h>
#include <string.h>

void morph_state_init(morph_state_t *m) {
    memset(m, 0, sizeof(*m));
    m->interp_kind = MORPH_INTERP_IDW;
    m->idw_power   = 2.0f;
    m->cursor_x    = 0.5f;
    m->cursor_y    = 0.5f;
    // selection tree defaults to ALL fields included
    for (int i = 0; i < MORPH_INCLUDE_COUNT; i++) m->included[i] = 1;
}

float morph_curve(float t, int kind) {
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    switch (kind) {
        case MORPH_CURVE_EASE_IN:     return t * t;
        case MORPH_CURVE_EASE_OUT:    return 1.0f - (1.0f - t) * (1.0f - t);
        case MORPH_CURVE_EASE_IN_OUT: return t * t * (3.0f - 2.0f * t);   // smoothstep
        case MORPH_CURVE_HOLD:        return (t >= 1.0f) ? 1.0f : 0.0f;
        case MORPH_CURVE_LINEAR:
        default:                      return t;
    }
}

int morph_compute_weights(const morph_state_t *m, float cx, float cy,
                          float *w, int *exact_idx) {
    int n = m->point_count;
    if (n <= 0) return 0;

    const float EPS = 1e-6f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float dx = cx - m->points[i].x;
        float dy = cy - m->points[i].y;
        float d  = hypotf(dx, dy);
        if (d < EPS) {                 // cursor sits on a point -> exact reproduction
            if (exact_idx) *exact_idx = i;
            return -1;
        }
        float wi = 1.0f / powf(d, m->idw_power);
        w[i] = wi;
        sum += wi;
    }
    if (sum > 0.0f) {
        for (int i = 0; i < n; i++) w[i] /= sum;
    }
    return n;
}
