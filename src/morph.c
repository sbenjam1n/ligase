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

// Discrete (sampled) natural-neighbour weights — approximate Sibson coordinates without a mesh.
// Sample the Voronoi assignment on a fixed grid: for each cell, if the cursor X is nearer than the
// nearest existing point, X "steals" that cell from that point. w_P = (cells stolen from P), so each
// weight is the fraction of X's would-be Voronoi cell overlapping P's original cell. Local,
// C0-continuous, exact at the data points — the Bencina Metasurface kernel, mesh-free.
#define MORPH_NN_GRID 48
static int morph_nn_weights(const morph_state_t *m, float cx, float cy,
                            float *w, int *exact_idx) {
    int n = m->point_count;
    if (n <= 0) return 0;

    const float EPS = 1e-6f;
    for (int i = 0; i < n; i++) {
        float dx = cx - m->points[i].x, dy = cy - m->points[i].y;
        if (hypotf(dx, dy) < EPS) { if (exact_idx) *exact_idx = i; return -1; }
        w[i] = 0.0f;
    }

    long stolen = 0;
    const float step = 1.0f / (float)MORPH_NN_GRID;
    for (int gy = 0; gy < MORPH_NN_GRID; gy++) {
        float py = ((float)gy + 0.5f) * step;
        for (int gx = 0; gx < MORPH_NN_GRID; gx++) {
            float px = ((float)gx + 0.5f) * step;
            int nearest = 0; float bestd = 1e30f;
            for (int i = 0; i < n; i++) {
                float dx = px - m->points[i].x, dy = py - m->points[i].y;
                float d = dx * dx + dy * dy;
                if (d < bestd) { bestd = d; nearest = i; }
            }
            float dxx = px - cx, dyy = py - cy;
            if (dxx * dxx + dyy * dyy < bestd) { w[nearest] += 1.0f; stolen++; }  // X steals this cell
        }
    }
    if (stolen > 0) {
        for (int i = 0; i < n; i++) w[i] /= (float)stolen;
    } else {                            // X stole nothing on this grid -> fall back to the nearest point
        int nearest = 0; float bestd = 1e30f;
        for (int i = 0; i < n; i++) {
            float dx = cx - m->points[i].x, dy = cy - m->points[i].y;
            float d = dx * dx + dy * dy;
            if (d < bestd) { bestd = d; nearest = i; }
        }
        w[nearest] = 1.0f;
    }
    return n;
}

int morph_compute_weights(const morph_state_t *m, float cx, float cy,
                          float *w, int *exact_idx) {
    int n = m->point_count;
    if (n <= 0) return 0;

    if (m->interp_kind == MORPH_INTERP_NN)
        return morph_nn_weights(m, cx, cy, w, exact_idx);

    const float EPS = 1e-6f;            // default: IDW / Shepard
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
