// @region:ligase_pd.core.grain.moogladder Grain Moogladder Filter Implementation

#include "grain_moogladder.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Denormal flush macro - forces very small floats to zero
// Prevents CPU performance degradation from subnormal arithmetic
#define FLUSH_DENORMAL(x) do { \
    if (fabsf(x) < 1e-15f) { \
        x = 0.0f; \
    } \
} while(0)

// NaN/Inf sanitization macro - replaces invalid floats with zero
// Prevents filter state corruption from upstream errors
#define SANITIZE(x) do { \
    if (!isfinite(x)) { \
        x = 0.0f; \
    } \
} while(0)

// Soft limiter for feedback - prevents runaway oscillation
// threshold: hard clip threshold (1.0-5.0)
// saturation: saturation coefficient (0.1-2.0)
static inline float soft_limit(float x, float threshold, float saturation) {
    if (x > threshold) x = threshold;
    if (x < -threshold) x = -threshold;
    float ax = fabsf(x);
    return x / (1.0f + ax * saturation);
}

// Hard clamp with headroom for natural Moog saturation
static inline float clamp_stage(float x) {
    if (x > 10.0f) return 10.0f;
    if (x < -10.0f) return -10.0f;
    return x;
}

// @region:ligase_pd.core.grain.moogladder.create Filter Creation

grain_moogladder_t* grain_moogladder_create(int sample_rate) {
    grain_moogladder_t *filter = (grain_moogladder_t*)calloc(1, sizeof(grain_moogladder_t));
    if (!filter) return NULL;

    filter->sample_rate = sample_rate;

    // Initialize with safe defaults
    filter->cutoff = 1000.0f;      // 1kHz cutoff
    filter->resonance = 0.0f;      // No resonance
    filter->mix = 0.0f;            // 0% wet (effect off)
    filter->enabled = 1;           // Enabled by default
    filter->fb_threshold = 2.0f;   // Default hard clip threshold
    filter->fb_saturation = 0.5f;  // Default saturation coefficient

    // Initialize filter state to zero (calloc does this, but explicit for clarity)
    memset(filter->stage, 0, sizeof(filter->stage));

    // Compute initial coefficients
    grain_moogladder_set_cutoff(filter, filter->cutoff);
    grain_moogladder_set_resonance(filter, filter->resonance);

    // Initialize smoothed coefficients to target values (no smoothing on startup)
    filter->f_smooth = filter->f;
    filter->fb_smooth = filter->fb;

    return filter;
}

void grain_moogladder_destroy(grain_moogladder_t *filter) {
    if (filter) {
        free(filter);
    }
}

// @endregion:ligase_pd.core.grain.moogladder.create

// @region:ligase_pd.core.grain.moogladder.parameters Parameter Control

void grain_moogladder_set_cutoff(grain_moogladder_t *filter, float cutoff_hz) {
    if (!filter) return;

    // Clamp cutoff to valid range (20Hz - 20kHz)
    if (cutoff_hz < 20.0f) cutoff_hz = 20.0f;
    if (cutoff_hz > 20000.0f) cutoff_hz = 20000.0f;

    // Additional safety: clamp to Nyquist (sample_rate / 2)
    float nyquist = filter->sample_rate * 0.5f;
    if (cutoff_hz > nyquist) cutoff_hz = nyquist;

    filter->cutoff = cutoff_hz;

    // Compute cutoff coefficient using normalized frequency
    float fc_norm = cutoff_hz / filter->sample_rate;
    filter->f = 2.0f * sinf(M_PI * fc_norm);

    // Clamp f to [0, 0.95] for stability with high resonance
    if (filter->f < 0.0f) filter->f = 0.0f;
    if (filter->f > 0.95f) filter->f = 0.95f;
}

void grain_moogladder_set_resonance(grain_moogladder_t *filter, float resonance) {
    if (!filter) return;

    // Clamp resonance to safe range
    // 0.0 = no resonance, 4.0 = maximum (self-oscillation around 3.5+)
    if (resonance < 0.0f) resonance = 0.0f;
    if (resonance > 4.0f) resonance = 4.0f;

    filter->resonance = resonance;

    // Compute feedback coefficient with nonlinear compensation
    // This formula prevents resonance from dropping at high cutoff frequencies
    // Based on Musicdsp.org Moog filter implementation
    float f5 = filter->f * filter->f * filter->f * filter->f * filter->f;
    filter->fb = resonance * (1.0f + 0.5f * f5);
}

void grain_moogladder_set_mix(grain_moogladder_t *filter, float mix) {
    if (!filter) return;

    // Clamp mix to [0, 1]
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    filter->mix = mix;
}

void grain_moogladder_set_enabled(grain_moogladder_t *filter, int enabled) {
    if (!filter) return;

    // If transitioning from disabled to enabled, clear filter state
    // This prevents clicks from stale filter memory
    if (!filter->enabled && enabled) {
        memset(filter->stage, 0, sizeof(filter->stage));
    }

    filter->enabled = enabled;
}

void grain_moogladder_set_fb_threshold(grain_moogladder_t *filter, float threshold) {
    if (!filter) return;

    // Clamp threshold to safe range (1.0 - 5.0)
    // Lower values = more aggressive clipping, higher = more headroom
    if (threshold < 1.0f) threshold = 1.0f;
    if (threshold > 5.0f) threshold = 5.0f;

    filter->fb_threshold = threshold;
}

void grain_moogladder_set_fb_saturation(grain_moogladder_t *filter, float saturation) {
    if (!filter) return;

    // Clamp saturation coefficient to safe range (0.1 - 2.0)
    // Lower values = gentler saturation curve, higher = harder limiting
    if (saturation < 0.1f) saturation = 0.1f;
    if (saturation > 2.0f) saturation = 2.0f;

    filter->fb_saturation = saturation;
}

// @endregion:ligase_pd.core.grain.moogladder.parameters

// @region:ligase_pd.core.grain.moogladder.process Filter Processing

void grain_moogladder_process(grain_moogladder_t *filter, float *left, float *right, int blocksize) {
    if (!filter || !left || !right) return;

    // Parameter smoothing: block-rate instead of per-sample for efficiency
    // Keep filter warm even when bypassed to prevent clicks on re-enable
    const float smooth_coeff = 0.001f;
    for (int s = 0; s < blocksize; s++) {
        filter->f_smooth += smooth_coeff * (filter->f - filter->f_smooth);
        filter->fb_smooth += smooth_coeff * (filter->fb - filter->fb_smooth);
    }
    FLUSH_DENORMAL(filter->f_smooth);
    FLUSH_DENORMAL(filter->fb_smooth);

    // Bypass mode: output dry signal but keep processing to maintain warm state
    int bypass = (!filter->enabled || filter->mix <= 0.0f);

    for (int i = 0; i < blocksize; i++) {

        // Process left channel
        {
            float input = left[i];
            SANITIZE(input);

            // Feedback from last stage with soft limiting
            float fb_signal = soft_limit(filter->fb_smooth * filter->stage[3][0],
                                        filter->fb_threshold, filter->fb_saturation);
            float in_fb = input - fb_signal;

            // 4-pole cascade with clamping for natural saturation
            filter->stage[0][0] += filter->f_smooth * (in_fb - filter->stage[0][0]);
            filter->stage[0][0] = clamp_stage(filter->stage[0][0]);
            FLUSH_DENORMAL(filter->stage[0][0]);
            SANITIZE(filter->stage[0][0]);

            filter->stage[1][0] += filter->f_smooth * (filter->stage[0][0] - filter->stage[1][0]);
            filter->stage[1][0] = clamp_stage(filter->stage[1][0]);
            FLUSH_DENORMAL(filter->stage[1][0]);
            SANITIZE(filter->stage[1][0]);

            filter->stage[2][0] += filter->f_smooth * (filter->stage[1][0] - filter->stage[2][0]);
            filter->stage[2][0] = clamp_stage(filter->stage[2][0]);
            FLUSH_DENORMAL(filter->stage[2][0]);
            SANITIZE(filter->stage[2][0]);

            filter->stage[3][0] += filter->f_smooth * (filter->stage[2][0] - filter->stage[3][0]);
            filter->stage[3][0] = clamp_stage(filter->stage[3][0]);
            FLUSH_DENORMAL(filter->stage[3][0]);
            SANITIZE(filter->stage[3][0]);

            float output = filter->stage[3][0];

            // Bypass: output dry, but filter stays warm
            if (bypass) {
                left[i] = input;
            } else {
                left[i] = input * (1.0f - filter->mix) + output * filter->mix;
            }
        }

        // Process right channel
        {
            float input = right[i];
            SANITIZE(input);

            float fb_signal = soft_limit(filter->fb_smooth * filter->stage[3][1],
                                        filter->fb_threshold, filter->fb_saturation);
            float in_fb = input - fb_signal;

            filter->stage[0][1] += filter->f_smooth * (in_fb - filter->stage[0][1]);
            filter->stage[0][1] = clamp_stage(filter->stage[0][1]);
            FLUSH_DENORMAL(filter->stage[0][1]);
            SANITIZE(filter->stage[0][1]);

            filter->stage[1][1] += filter->f_smooth * (filter->stage[0][1] - filter->stage[1][1]);
            filter->stage[1][1] = clamp_stage(filter->stage[1][1]);
            FLUSH_DENORMAL(filter->stage[1][1]);
            SANITIZE(filter->stage[1][1]);

            filter->stage[2][1] += filter->f_smooth * (filter->stage[1][1] - filter->stage[2][1]);
            filter->stage[2][1] = clamp_stage(filter->stage[2][1]);
            FLUSH_DENORMAL(filter->stage[2][1]);
            SANITIZE(filter->stage[2][1]);

            filter->stage[3][1] += filter->f_smooth * (filter->stage[2][1] - filter->stage[3][1]);
            filter->stage[3][1] = clamp_stage(filter->stage[3][1]);
            FLUSH_DENORMAL(filter->stage[3][1]);
            SANITIZE(filter->stage[3][1]);

            float output = filter->stage[3][1];

            if (bypass) {
                right[i] = input;
            } else {
                right[i] = input * (1.0f - filter->mix) + output * filter->mix;
            }
        }
    }
}

// @endregion:ligase_pd.core.grain.moogladder.process

// @endregion:ligase_pd.core.grain.moogladder
