// @region:ligase_pd.core.grain.envelope Envelope Generators

#include "types.h"
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// @region:ligase_pd.core.grain.envelope.parabolic Parabolic Envelope (Welch Window)

void envelope_parabolic_generate(float *table, int length, float skew) {
    if (length == 0) return;
    if (length == 1) {
        table[0] = 1.0f;
        return;
    }
    // Welch window with skew: parabolic approximation with adjustable attack/decay
    // skew = 0.5: symmetric (peak at center)
    // skew < 0.5: shorter attack, longer decay (peak shifts left)
    // skew > 0.5: longer attack, shorter decay (peak shifts right)

    // Clamp skew to valid range
    if (skew < 0.0f) skew = 0.0f;
    if (skew > 1.0f) skew = 1.0f;

    for (int i = 0; i < length; i++) {
        float x = (float)i / (float)(length - 1);

        // Apply skew: remap x based on which side of the peak we're on
        float skewed_x;
        if (x <= skew) {
            // Attack phase: map [0, skew] to [0, 0.5]
            skewed_x = (x / skew) * 0.5f;
        } else {
            // Decay phase: map [skew, 1] to [0.5, 1]
            skewed_x = 0.5f + ((x - skew) / (1.0f - skew)) * 0.5f;
        }

        // Parabolic formula: 1 - 4(x - 0.5)^2
        table[i] = 1.0f - 4.0f * (skewed_x - 0.5f) * (skewed_x - 0.5f);
    }
}

// @endregion:ligase_pd.core.grain.envelope.parabolic

// @region:ligase_pd.core.grain.envelope.trapezoidal Trapezoidal Envelope

void envelope_trapezoidal_generate(float *table, int length, float attack_pct, float release_pct, float skew) {
    // Apply skew to adjust attack/release balance
    // skew = 0.5: use original attack/release percentages
    // skew < 0.5: decrease attack, increase release
    // skew > 0.5: increase attack, decrease release

    // Clamp skew to valid range
    if (skew < 0.0f) skew = 0.0f;
    if (skew > 1.0f) skew = 1.0f;

    // Calculate skewed attack/release percentages
    // Total ramp percentage (attack + release)
    float total_ramp_pct = attack_pct + release_pct;

    // Redistribute based on skew
    float skewed_attack_pct = total_ramp_pct * skew;
    float skewed_release_pct = total_ramp_pct * (1.0f - skew);

    int attack_samples = (int)(length * skewed_attack_pct);
    int release_samples = (int)(length * skewed_release_pct);
    int sustain_samples = length - attack_samples - release_samples;

    // Ensure we don't have negative sustain
    if (sustain_samples < 0) {
        sustain_samples = 0;
        attack_samples = (int)(length * skew);
        release_samples = length - attack_samples;
    }

    for (int i = 0; i < length; i++) {
        if (i < attack_samples) {
            if (attack_samples > 0) {
                table[i] = (float)i / (float)attack_samples;
            } else {
                table[i] = 1.0f;
            }
        } else if (i < attack_samples + sustain_samples) {
            table[i] = 1.0f;
        } else {
            int release_idx = i - attack_samples - sustain_samples;
            if (release_samples > 0) {
                table[i] = 1.0f - ((float)release_idx / (float)release_samples);
            } else {
                table[i] = 0.0f;
            }
        }
    }
}

// @endregion:ligase_pd.core.grain.envelope.trapezoidal

// @region:ligase_pd.core.grain.envelope.cosine Raised Cosine Bell Envelope (Hann Window)

void envelope_cosine_generate(float *table, int length, float skew) {
    if (length == 0) return;
    if (length == 1) {
        table[0] = 0.0f;
        return;
    }
    // Hann window with skew: smoother than basic raised cosine with adjustable attack/decay
    // skew = 0.5: symmetric (peak at center)
    // skew < 0.5: shorter attack, longer decay (peak shifts left)
    // skew > 0.5: longer attack, shorter decay (peak shifts right)

    // Clamp skew to valid range
    if (skew < 0.0f) skew = 0.0f;
    if (skew > 1.0f) skew = 1.0f;

    for (int i = 0; i < length; i++) {
        float x = (float)i / (float)(length - 1);

        // Apply skew: remap x based on which side of the peak we're on
        float skewed_phase;
        if (x <= skew) {
            // Attack phase: map [0, skew] to [0, 0.5]
            skewed_phase = (x / skew) * 0.5f;
        } else {
            // Decay phase: map [skew, 1] to [0.5, 1]
            skewed_phase = 0.5f + ((x - skew) / (1.0f - skew)) * 0.5f;
        }

        // Hann window formula: 0.5 * (1 - cos(2π * phase))
        table[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * skewed_phase));
    }
}

// @endregion:ligase_pd.core.grain.envelope.cosine

// @region:ligase_pd.core.grain.envelope.gaussian Gaussian Bell Curve Envelope

void envelope_gaussian_generate(float *table, int length, float skew, float sigma) {
    if (length == 0) return;
    if (length == 1) {
        table[0] = 1.0f;
        return;
    }
    // Gaussian (bell curve) envelope: optimal time-frequency localization
    // Smoother than Hann, minimal spectral artifacts, ideal for high-overlap transparent synthesis
    // sigma controls width: lower = narrower spike, higher = wider bell
    // skew = 0.5: symmetric peak at center
    // skew < 0.5: shorter attack, longer decay (peak shifts left)
    // skew > 0.5: longer attack, shorter decay (peak shifts right)

    // Clamp parameters to valid ranges
    if (skew < 0.0f) skew = 0.0f;
    if (skew > 1.0f) skew = 1.0f;
    if (sigma < 0.05f) sigma = 0.05f;  // Prevent extreme narrowness
    if (sigma > 0.5f) sigma = 0.5f;    // Prevent rectangle-like shape

    // Calculate edge value for normalization (value at t=0 and t=1)
    float edge_centered = -0.5f;
    float edge_val = expf(-(edge_centered * edge_centered) / (2.0f * sigma * sigma));

    for (int i = 0; i < length; i++) {
        float x = (float)i / (float)(length - 1);

        // Apply skew: remap x based on which side of the peak we're on
        float skewed_x;
        if (x <= skew) {
            // Attack phase: map [0, skew] to [0, 0.5]
            skewed_x = (x / skew) * 0.5f;
        } else {
            // Decay phase: map [skew, 1] to [0.5, 1]
            skewed_x = 0.5f + ((x - skew) / (1.0f - skew)) * 0.5f;
        }

        // Center the time around 0 (range: -0.5 to 0.5)
        float centered_t = skewed_x - 0.5f;

        // Gaussian formula: exp(-(t-0.5)^2 / (2*sigma^2))
        float exponent = -(centered_t * centered_t) / (2.0f * sigma * sigma);
        float env_val = expf(exponent);

        // Edge correction: subtract edge value and normalize to [0, 1]
        // This ensures envelope truly starts and ends at 0.0
        env_val = (env_val - edge_val) / (1.0f - edge_val);

        // Clamp to prevent negative values from floating-point errors
        if (env_val < 0.0f) env_val = 0.0f;

        table[i] = env_val;
    }
}

// @endregion:ligase_pd.core.grain.envelope.gaussian

// @region:ligase_pd.core.grain.envelope.exponential Exponential FOF-Style Envelope

void envelope_exponential_generate(float *table, int length, float skew, float alpha) {
    if (length == 0) return;
    if (length == 1) {
        table[0] = 1.0f;
        return;
    }
    // Exponential FOF (Formant Wave Function) style envelope
    // Creates "plucked" or "ringing" character with natural decay
    // Formula: t * exp(-alpha * t) - creates asymmetric rise and exponential fall
    // alpha controls decay sharpness: low = long ring, high = short click
    // skew = 0.5: symmetric application
    // skew < 0.5: faster attack (percussive)
    // skew > 0.5: slower attack (swelling)

    // Clamp parameters to valid ranges
    if (skew < 0.0f) skew = 0.0f;
    if (skew > 1.0f) skew = 1.0f;
    if (alpha < 1.0f) alpha = 1.0f;    // Prevent too slow decay
    if (alpha > 30.0f) alpha = 30.0f;  // Prevent extreme click

    // Peak occurs at t = 1/alpha, max value is (1/alpha * e^-1)
    // Normalization factor to make peak = 1.0
    float scale = alpha * 2.71828f;  // alpha * e

    for (int i = 0; i < length; i++) {
        float x = (float)i / (float)(length - 1);

        // Apply skew: remap x based on which side of the peak we're on
        float skewed_x;
        if (x <= skew) {
            // Attack phase: map [0, skew] to [0, 0.5]
            skewed_x = (x / skew) * 0.5f;
        } else {
            // Decay phase: map [skew, 1] to [0.5, 1]
            skewed_x = 0.5f + ((x - skew) / (1.0f - skew)) * 0.5f;
        }

        // Exponential formula: t * exp(-alpha * t)
        float raw_env = skewed_x * expf(-alpha * skewed_x);

        // Normalize so peak is 1.0
        float env_val = raw_env * scale;

        // Quick fade at very end to ensure clean ending
        if (skewed_x > 0.95f) {
            float fade = (1.0f - skewed_x) * 20.0f;  // Linear fade over last 5%
            if (fade < 1.0f) env_val *= fade;
        }

        // Clamp to [0, 1]
        if (env_val < 0.0f) env_val = 0.0f;
        if (env_val > 1.0f) env_val = 1.0f;

        table[i] = env_val;
    }
}

// @endregion:ligase_pd.core.grain.envelope.exponential

envelope_t* envelope_create(envelope_type_t type, int length) {
    envelope_t *env = (envelope_t*)malloc(sizeof(envelope_t));
    if (!env) return NULL;  // Memory allocation failed

    env->type = type;
    env->length = length;
    env->skew = 0.5f;  // Default: symmetric (50% attack, 50% decay)
    env->sigma = 0.15f;  // Default Gaussian width (moderate smoothness)
    env->alpha = 10.0f;  // Default Exponential decay (moderate ring)
    env->table = (float*)malloc(length * sizeof(float));

    if (!env->table) {
        free(env);
        return NULL;  // Table allocation failed
    }

    switch(type) {
        case ENVELOPE_PARABOLIC:
            envelope_parabolic_generate(env->table, length, env->skew);
            break;
        case ENVELOPE_TRAPEZOIDAL:
            envelope_trapezoidal_generate(env->table, length, 0.1f, 0.1f, env->skew);
            break;
        case ENVELOPE_COSINE:
            envelope_cosine_generate(env->table, length, env->skew);
            break;
        case ENVELOPE_GAUSSIAN:
            envelope_gaussian_generate(env->table, length, env->skew, env->sigma);
            break;
        case ENVELOPE_EXPONENTIAL:
            envelope_exponential_generate(env->table, length, env->skew, env->alpha);
            break;
    }

    return env;
}

void envelope_set_skew(envelope_t *env, float skew) {
    if (!env) return;

    // Clamp skew to valid range
    if (skew < 0.0f) skew = 0.0f;
    if (skew > 1.0f) skew = 1.0f;

    env->skew = skew;

    // Regenerate envelope table with new skew
    switch(env->type) {
        case ENVELOPE_PARABOLIC:
            envelope_parabolic_generate(env->table, env->length, env->skew);
            break;
        case ENVELOPE_TRAPEZOIDAL:
            envelope_trapezoidal_generate(env->table, env->length, 0.1f, 0.1f, env->skew);
            break;
        case ENVELOPE_COSINE:
            envelope_cosine_generate(env->table, env->length, env->skew);
            break;
        case ENVELOPE_GAUSSIAN:
            envelope_gaussian_generate(env->table, env->length, env->skew, env->sigma);
            break;
        case ENVELOPE_EXPONENTIAL:
            envelope_exponential_generate(env->table, env->length, env->skew, env->alpha);
            break;
    }
}

void envelope_set_type(envelope_t *env, envelope_type_t type) {
    if (!env || !env->table) return;

    env->type = type;

    // Regenerate the table IN PLACE for the new type (same struct, same allocation,
    // same length) — exactly like envelope_set_skew. This is critical: other objects
    // (the scheduler and the Bencina delay) cache this envelope POINTER, so the struct
    // must never be freed/recreated underneath them. Skew/sigma/alpha are preserved.
    switch(env->type) {
        case ENVELOPE_PARABOLIC:
            envelope_parabolic_generate(env->table, env->length, env->skew);
            break;
        case ENVELOPE_TRAPEZOIDAL:
            envelope_trapezoidal_generate(env->table, env->length, 0.1f, 0.1f, env->skew);
            break;
        case ENVELOPE_COSINE:
            envelope_cosine_generate(env->table, env->length, env->skew);
            break;
        case ENVELOPE_GAUSSIAN:
            envelope_gaussian_generate(env->table, env->length, env->skew, env->sigma);
            break;
        case ENVELOPE_EXPONENTIAL:
            envelope_exponential_generate(env->table, env->length, env->skew, env->alpha);
            break;
    }
}

void envelope_set_sigma(envelope_t *env, float sigma) {
    if (!env) return;

    // Clamp sigma to valid range
    if (sigma < 0.05f) sigma = 0.05f;  // Prevent extreme narrowness
    if (sigma > 0.5f) sigma = 0.5f;    // Prevent rectangle-like shape

    env->sigma = sigma;

    // Regenerate envelope table with new sigma (only applies to Gaussian)
    if (env->type == ENVELOPE_GAUSSIAN) {
        envelope_gaussian_generate(env->table, env->length, env->skew, env->sigma);
    }
}

void envelope_set_alpha(envelope_t *env, float alpha) {
    if (!env) return;

    // Clamp alpha to valid range
    if (alpha < 1.0f) alpha = 1.0f;    // Prevent too slow decay
    if (alpha > 30.0f) alpha = 30.0f;  // Prevent extreme click

    env->alpha = alpha;

    // Regenerate envelope table with new alpha (only applies to Exponential)
    if (env->type == ENVELOPE_EXPONENTIAL) {
        envelope_exponential_generate(env->table, env->length, env->skew, env->alpha);
    }
}

// Sample envelope at a given phase (0.0-1.0)
float envelope_sample(envelope_t *env, float phase) {
    if (!env || !env->table) return 0.0f;
    if (env->length == 0) return 0.0f;

    // Clamp phase to valid range
    if (phase < 0.0f) phase = 0.0f;
    if (phase > 1.0f) phase = 1.0f;

    // Calculate index in table
    float idx_f = phase * (env->length - 1);
    int idx = (int)idx_f;

    // Bounds check
    if (idx >= env->length - 1) return env->table[env->length - 1];

    // Linear interpolation between samples
    float frac = idx_f - idx;
    return env->table[idx] * (1.0f - frac) + env->table[idx + 1] * frac;
}

void envelope_destroy(envelope_t *env) {
    if (env) {
        if (env->table) free(env->table);
        free(env);
    }
}

// @endregion:ligase_pd.core.grain.envelope
