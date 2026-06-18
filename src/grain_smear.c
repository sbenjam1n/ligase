// @region:ligase_pd.core.grain.smear Allpass Smear Effect Implementation

#include "grain_smear.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GRAIN_SMEAR_MAX_STAGES 48   // up to 96 poles (cf. Smear's 98)
#define SMEAR_MAGIC 0x5EA12EF0

struct grain_smear {
    unsigned int magic;
    int sample_rate;

    // Controls
    float mix;          // 0..1
    float freq_hz;      // allpass center frequency
    float resonance;    // pole radius 0..0.999
    int   stages;       // active sections 0..MAX
    float feedback;     // global loop feedback -0.99..0.99

    // Pre-computed 2nd-order allpass coefficients (shared by all stages):
    //   H(z) = (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2)
    //   a1 = -2 r cos(w0),  a2 = r^2
    float a1, a2;

    // Per-stage, per-channel state (Direct Form I): x[n-1], x[n-2], y[n-1], y[n-2]
    float xL1[GRAIN_SMEAR_MAX_STAGES], xL2[GRAIN_SMEAR_MAX_STAGES];
    float yL1[GRAIN_SMEAR_MAX_STAGES], yL2[GRAIN_SMEAR_MAX_STAGES];
    float xR1[GRAIN_SMEAR_MAX_STAGES], xR2[GRAIN_SMEAR_MAX_STAGES];
    float yR1[GRAIN_SMEAR_MAX_STAGES], yR2[GRAIN_SMEAR_MAX_STAGES];

    // Global feedback memory (previous wet output per channel)
    float fb_l, fb_r;
};

static inline float smear_flush(float v) {
    // Kill subnormals/NaN so a decaying tail can't tank CPU or self-sustain garbage.
    if (!isfinite(v) || fabsf(v) < 1e-20f) return 0.0f;
    return v;
}

static void smear_update_coeffs(grain_smear_t *s) {
    float sr = (s->sample_rate > 0) ? (float)s->sample_rate : 48000.0f;
    float f = s->freq_hz;
    if (f < 20.0f) f = 20.0f;
    if (f > 0.45f * sr) f = 0.45f * sr;
    float w0 = 2.0f * (float)M_PI * f / sr;
    float r = s->resonance;
    if (r < 0.0f) r = 0.0f;
    if (r > 0.999f) r = 0.999f;
    s->a1 = -2.0f * r * cosf(w0);
    s->a2 = r * r;
}

grain_smear_t *grain_smear_create(int sample_rate) {
    grain_smear_t *s = (grain_smear_t *)calloc(1, sizeof(grain_smear_t));
    if (!s) return NULL;
    s->magic = SMEAR_MAGIC;
    s->sample_rate = (sample_rate > 0) ? sample_rate : 48000;
    s->mix = 0.0f;          // dry by default (driven by inlet)
    s->freq_hz = 800.0f;
    s->resonance = 0.7f;
    s->stages = 12;
    s->feedback = 0.0f;
    smear_update_coeffs(s);
    return s;
}

void grain_smear_destroy(grain_smear_t *s) {
    if (s) free(s);
}

void grain_smear_set_sample_rate(grain_smear_t *s, int sample_rate) {
    if (!s || s->magic != SMEAR_MAGIC || sample_rate <= 0) return;
    s->sample_rate = sample_rate;
    smear_update_coeffs(s);
}

void grain_smear_set_mix(grain_smear_t *s, float mix) {
    if (!s || s->magic != SMEAR_MAGIC) return;
    s->mix = fmaxf(0.0f, fminf(1.0f, mix));
}

void grain_smear_set_frequency(grain_smear_t *s, float hz) {
    if (!s || s->magic != SMEAR_MAGIC) return;
    s->freq_hz = hz;
    smear_update_coeffs(s);
}

void grain_smear_set_resonance(grain_smear_t *s, float r) {
    if (!s || s->magic != SMEAR_MAGIC) return;
    s->resonance = fmaxf(0.0f, fminf(0.999f, r));
    smear_update_coeffs(s);
}

void grain_smear_set_stages(grain_smear_t *s, int stages) {
    if (!s || s->magic != SMEAR_MAGIC) return;
    if (stages < 0) stages = 0;
    if (stages > GRAIN_SMEAR_MAX_STAGES) stages = GRAIN_SMEAR_MAX_STAGES;
    s->stages = stages;
}

void grain_smear_set_feedback(grain_smear_t *s, float fb) {
    if (!s || s->magic != SMEAR_MAGIC) return;
    s->feedback = fmaxf(-0.99f, fminf(0.99f, fb));
}

// One 2nd-order allpass section, Direct Form I, with denormal-flushed state.
static inline float smear_allpass(float x, const float a1, const float a2,
                                  float *x1, float *x2, float *y1, float *y2) {
    float y = a2 * x + a1 * (*x1) + (*x2) - a1 * (*y1) - a2 * (*y2);
    *x2 = *x1; *x1 = x;
    *y2 = smear_flush(*y1); *y1 = smear_flush(y);
    return y;
}

void grain_smear_process(grain_smear_t *s, float *left, float *right, int n) {
    if (!s || s->magic != SMEAR_MAGIC) return;

    // Fully dry: leave the signal untouched (and let state decay/flush).
    if (s->mix < 0.0001f || s->stages <= 0) {
        return;
    }

    const float a1 = s->a1, a2 = s->a2;
    const int stages = s->stages;
    const float fb = s->feedback;
    const float wet = s->mix;
    const float dry = 1.0f - s->mix;

    for (int i = 0; i < n; i++) {
        // ---- Left ----
        float dl = left[i];
        float xl = dl + fb * s->fb_l;
        for (int st = 0; st < stages; st++) {
            xl = smear_allpass(xl, a1, a2, &s->xL1[st], &s->xL2[st], &s->yL1[st], &s->yL2[st]);
        }
        s->fb_l = smear_flush(xl);
        left[i] = dry * dl + wet * xl;

        // ---- Right ----
        float dr = right[i];
        float xr = dr + fb * s->fb_r;
        for (int st = 0; st < stages; st++) {
            xr = smear_allpass(xr, a1, a2, &s->xR1[st], &s->xR2[st], &s->yR1[st], &s->yR2[st]);
        }
        s->fb_r = smear_flush(xr);
        right[i] = dry * dr + wet * xr;
    }
}
