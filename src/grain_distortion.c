// @region:ligase_pd.core.grain.distortion Grain Distortion with Multi-Mode Waveshaping

#include "grain_distortion.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// @region:ligase_pd.core.grain.distortion.constants Constants

#define DRIVE_MIN 1.0f
#define DRIVE_MAX 20.0f
#define DRIVE_POWER 2.0f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// @endregion:ligase_pd.core.grain.distortion.constants

// @region:ligase_pd.core.grain.distortion.filter_coeff Filter Coefficient Calculation

// Compute 1-pole highpass filter coefficient (6dB/octave) [OUTER FILTER]
static void update_highpass_coeffs(grain_distortion_t *dist) {
    float freq = dist->pre_hp_freq;
    float omega = 2.0f * M_PI * freq / (float)dist->sample_rate;
    float tan_omega = tanf(omega / 2.0f);

    //  Prevent division by zero
    float denominator = 1.0f + tan_omega;
    if (fabsf(denominator) < 1e-10f) {
        dist->pre_hp_coeff = 0.999f;  // Near-unity passthrough
        return;
    }

    dist->pre_hp_coeff = (1.0f - tan_omega) / denominator;
}

// Compute 2-pole Butterworth lowpass filter coefficients (12dB/octave) [OUTER FILTER]
static void update_lowpass_coeffs(grain_distortion_t *dist) {
    float freq = dist->post_lp_freq;
    float omega = 2.0f * M_PI * freq / (float)dist->sample_rate;
    float omega_cos = cosf(omega);
    float omega_sin = sinf(omega);
    float alpha = omega_sin / (2.0f * 0.707f); // Q = 0.707 for Butterworth

    float a0 = 1.0f + alpha;

    //  Prevent division by zero
    if (fabsf(a0) < 1e-10f) {
        // Degenerate case - use passthrough
        dist->post_lp_a1 = 0.0f;
        dist->post_lp_a2 = 0.0f;
        dist->post_lp_b0 = 1.0f;
        dist->post_lp_b1 = 0.0f;
        dist->post_lp_b2 = 0.0f;
        return;
    }

    dist->post_lp_a1 = (-2.0f * omega_cos) / a0;
    dist->post_lp_a2 = (1.0f - alpha) / a0;
    dist->post_lp_b0 = ((1.0f - omega_cos) / 2.0f) / a0;
    dist->post_lp_b1 = (1.0f - omega_cos) / a0;
    dist->post_lp_b2 = dist->post_lp_b0;
}

// Compute notch/bandstop filter coefficients [OUTER FILTER]
static void update_notch_coeffs(grain_distortion_t *dist) {
    float freq = dist->notch_freq;
    float bandwidth = dist->notch_bandwidth;
    float omega = 2.0f * M_PI * freq / (float)dist->sample_rate;
    float omega_cos = cosf(omega);
    float omega_sin = sinf(omega);

    //  Prevent division by zero when omega_sin ≈ 0 (freq near 0 Hz or Nyquist)
    float alpha;
    if (fabsf(omega_sin) < 1e-10f) {
        // Frequency too close to 0 or Nyquist - use minimal notch effect
        alpha = 0.001f;
    } else {
        alpha = omega_sin * sinhf(logf(2.0f) / 2.0f * bandwidth * omega / omega_sin);
    }

    float a0 = 1.0f + alpha;

    //  Prevent division by zero for a0
    if (fabsf(a0) < 1e-10f) {
        // Degenerate case - use passthrough (no notch effect)
        dist->notch_a1 = 0.0f;
        dist->notch_a2 = 0.0f;
        dist->notch_b0 = 1.0f;
        dist->notch_b1 = 0.0f;
        dist->notch_b2 = 0.0f;
        return;
    }

    dist->notch_a1 = (-2.0f * omega_cos) / a0;
    dist->notch_a2 = (1.0f - alpha) / a0;
    dist->notch_b0 = 1.0f / a0;
    dist->notch_b1 = (-2.0f * omega_cos) / a0;
    dist->notch_b2 = 1.0f / a0;
}

// Compute pre-emphasis and de-emphasis filter coefficients [INNER FILTERS]
static void update_emphasis_coeffs(grain_distortion_t *dist) {
    float freq = dist->emphasis_freq;
    float omega = 2.0f * M_PI * freq / (float)dist->sample_rate;
    float tan_omega = tanf(omega / 2.0f);

    //  Prevent division by zero when (1 + tan_omega) ≈ 0
    float denominator = 1.0f + tan_omega;
    if (fabsf(denominator) < 1e-10f) {
        // Degenerate case - use passthrough coefficients
        dist->preemph_coeff = 1.0f;
        dist->deemph_coeff = 1.0f;
        dist->emphasis_gain_makeup = 1.0f;
        return;
    }

    // Pre-emphasis coefficient (HP or LP depending on mode)
    if (dist->emphasis_mode == EMPHASIS_MODE_HP) {
        // High-pass: boost highs before distortion
        dist->preemph_coeff = (1.0f - tan_omega) / denominator;
        // De-emphasis is inverse: low-pass to restore balance
        dist->deemph_coeff = tan_omega / denominator;
        // Makeup gain to compensate for HP attenuation of lows
        dist->emphasis_gain_makeup = 1.0f + (freq / 1000.0f) * 0.5f;
    } else {
        // Low-pass: boost lows before distortion
        dist->preemph_coeff = tan_omega / denominator;
        // De-emphasis is inverse: high-pass to restore balance
        dist->deemph_coeff = (1.0f - tan_omega) / denominator;
        // Makeup gain to compensate for LP attenuation of highs
        dist->emphasis_gain_makeup = 1.0f + (1.0f - freq / 5000.0f) * 0.5f;
    }

    //  Clamp makeup gain to reasonable range
    if (dist->emphasis_gain_makeup < 0.5f) dist->emphasis_gain_makeup = 0.5f;
    if (dist->emphasis_gain_makeup > 3.0f) dist->emphasis_gain_makeup = 3.0f;
}

// @endregion:ligase_pd.core.grain.distortion.filter_coeff

// @region:ligase_pd.core.grain.distortion.antialiasing Anti-Aliasing Filter

// Compute 6-pole Butterworth lowpass for anti-aliasing after upsampling
// Cutoff: sample_rate / (2 * oversample_factor)
// This prevents upsampled harmonics from aliasing on downsample
static void update_antialias_coeffs(grain_distortion_t *dist) {
    if (dist->oversample_factor <= 1) {
        // No oversampling = no anti-aliasing needed
        return;
    }

    // Anti-alias cutoff at original Nyquist frequency
    float cutoff = (float)dist->sample_rate / (2.0f * (float)dist->oversample_factor);
    float omega = 2.0f * M_PI * cutoff / (float)dist->sample_rate;
    float omega_cos = cosf(omega);
    float omega_sin = sinf(omega);
    float alpha = omega_sin / (2.0f * 0.707f); // Q = 0.707 for Butterworth

    float a0 = 1.0f + alpha;

    //  Prevent division by zero
    if (fabsf(a0) < 1e-10f) {
        // Degenerate case - use passthrough
        dist->antialias_a1 = 0.0f;
        dist->antialias_a2 = 0.0f;
        dist->antialias_b0 = 1.0f;
        dist->antialias_b1 = 0.0f;
        dist->antialias_b2 = 0.0f;
        return;
    }

    dist->antialias_a1 = (-2.0f * omega_cos) / a0;
    dist->antialias_a2 = (1.0f - alpha) / a0;
    dist->antialias_b0 = ((1.0f - omega_cos) / 2.0f) / a0;
    dist->antialias_b1 = (1.0f - omega_cos) / a0;
    dist->antialias_b2 = dist->antialias_b0;
}

// @endregion:ligase_pd.core.grain.distortion.antialiasing

// @region:ligase_pd.core.grain.distortion.oversampling Upsampling/Downsampling

// Upsample a single sample using linear interpolation
// Input: one sample, Output: oversample_factor samples
static inline void upsample_sample(float input, float *output, int oversample_factor) {
    // First upsampled sample is the input
    output[0] = input;

    // Fill intermediate samples with linear interpolation
    // For 4x: [input, 0.75*input, 0.5*input, 0.25*input]
    // This is a simple zero-order hold that will be smoothed by anti-alias filter
    for (int i = 1; i < oversample_factor; i++) {
        output[i] = input * (1.0f - ((float)i / (float)oversample_factor));
    }
}

// Downsample using simple averaging
// Input: oversample_factor samples, Output: one sample
static inline float downsample_samples(float *input, int oversample_factor) {
    float sum = 0.0f;
    for (int i = 0; i < oversample_factor; i++) {
        sum += input[i];
    }
    return sum / (float)oversample_factor;
}

// Apply 2-pole Butterworth lowpass anti-aliasing filter
static inline float apply_antialias_filter(
    grain_distortion_t *dist,
    float input,
    int channel
) {
    // Select channel-specific state
    float *z1, *z2, *y1, *y2;
    if (channel == 0) {
        z1 = &dist->antialias_z1_left;
        z2 = &dist->antialias_z2_left;
        y1 = &dist->antialias_y1_left;
        y2 = &dist->antialias_y2_left;
    } else {
        z1 = &dist->antialias_z1_right;
        z2 = &dist->antialias_z2_right;
        y1 = &dist->antialias_y1_right;
        y2 = &dist->antialias_y2_right;
    }

    // 2-pole Butterworth lowpass (Direct Form II)
    float filtered = dist->antialias_b0 * input +
                    dist->antialias_b1 * (*z1) +
                    dist->antialias_b2 * (*z2) -
                    dist->antialias_a1 * (*y1) -
                    dist->antialias_a2 * (*y2);

    // Update state
    *z2 = *z1;
    *z1 = input;
    *y2 = *y1;
    *y1 = filtered;

    return filtered;
}

// @endregion:ligase_pd.core.grain.distortion.oversampling

// @region:ligase_pd.core.grain.distortion.update Update Parameters

static void update_drive_params(grain_distortion_t *dist, float intensity) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    dist->intensity_index = intensity;

    float drive_range = DRIVE_MAX - DRIVE_MIN;
    dist->current_drive = DRIVE_MIN + powf(intensity, DRIVE_POWER) * drive_range;
}

// @endregion:ligase_pd.core.grain.distortion.update

// @region:ligase_pd.core.grain.distortion.waveshaper Waveshaper Functions

// Pure tanh waveshaper
static inline float waveshape_tanh(float input, float drive) {
    return tanhf(drive * input);
}

// Pure arctan waveshaper (smoother, creamier)
static inline float waveshape_arctan(float input, float drive) {
    return (2.0f / M_PI) * atanf(drive * input);
}

// Blend between tanh and arctan
static inline float waveshape_blend(float input, float drive, float blend) {
    float tanh_out = tanhf(drive * input);
    float arctan_out = (2.0f / M_PI) * atanf(drive * input);
    return tanh_out * (1.0f - blend) + arctan_out * blend;
}

// Asymmetric waveshaper (different drive for pos/neg cycles → even harmonics)
static inline float waveshape_asymmetric(float input, float drive_pos, float drive_neg,
                                          waveshaper_mode_t base_mode) {
    if (input >= 0.0f) {
        if (base_mode == WAVESHAPER_MODE_ARCTAN) {
            return (2.0f / M_PI) * atanf(drive_pos * input);
        } else {
            return tanhf(drive_pos * input);
        }
    } else {
        if (base_mode == WAVESHAPER_MODE_ARCTAN) {
            return (2.0f / M_PI) * atanf(drive_neg * input);
        } else {
            return tanhf(drive_neg * input);
        }
    }
}

// Polynomial waveshaper (user-defined coefficients)
static inline float waveshape_polynomial(float input, float c1, float c2, float c3) {
    float x = input;
    float x2 = x * x;
    float x3 = x2 * x;

    float output = c1 * x + c2 * x2 + c3 * x3;

    // Soft clip to prevent explosion
    if (output > 1.0f) output = 1.0f;
    if (output < -1.0f) output = -1.0f;

    return output;
}

// @endregion:ligase_pd.core.grain.distortion.waveshaper

// @region:ligase_pd.core.grain.distortion.process Distortion Processing

// Process block of samples with oversampling (post-mix mode)
void grain_distortion_process_block(
    grain_distortion_t *dist,
    float *in_left,
    float *in_right,
    float *out_left,
    float *out_right,
    int blocksize
) {
    //  Validate inputs
    if (!dist) {
        fprintf(stderr, "grain_distortion_process_block: ERROR - NULL dist pointer\n");
        // Copy input to output (passthrough)
        if (in_left && out_left) memcpy(out_left, in_left, blocksize * sizeof(float));
        if (in_right && out_right) memcpy(out_right, in_right, blocksize * sizeof(float));
        return;
    }

    //  Check magic number to detect use-after-free
    if (dist->magic != 0xD157BEEF) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "grain_distortion_process_block: CRITICAL - use-after-free detected! magic=0x%08X\n", dist->magic);
            warn_count++;
        }
        // Passthrough
        if (in_left && out_left) memcpy(out_left, in_left, blocksize * sizeof(float));
        if (in_right && out_right) memcpy(out_right, in_right, blocksize * sizeof(float));
        return;
    }

    // Check if enabled - early return with passthrough
    if (!dist->enabled) {
        if (in_left && out_left) memcpy(out_left, in_left, blocksize * sizeof(float));
        if (in_right && out_right) memcpy(out_right, in_right, blocksize * sizeof(float));
        return;
    }

    //  Validate buffer pointers
    if (!in_left || !in_right || !out_left || !out_right) {
        fprintf(stderr, "grain_distortion_process_block: ERROR - NULL buffer pointer\n");
        return;
    }

    //  Validate blocksize
    if (blocksize <= 0 || blocksize > 8192) {
        fprintf(stderr, "grain_distortion_process_block: ERROR - invalid blocksize %d\n", blocksize);
        memcpy(out_left, in_left, blocksize * sizeof(float));
        memcpy(out_right, in_right, blocksize * sizeof(float));
        return;
    }

    //  Validate oversample_factor
    if (dist->oversample_factor != 1 && dist->oversample_factor != 2 &&
        dist->oversample_factor != 4 && dist->oversample_factor != 8) {
        fprintf(stderr, "grain_distortion_process_block: ERROR - invalid oversample_factor %d\n",
                dist->oversample_factor);
        // Clamp to valid value
        dist->oversample_factor = 4;
    }

    // If oversampling factor is 1, process directly without upsampling
    if (dist->oversample_factor == 1) {
        for (int i = 0; i < blocksize; i++) {
            out_left[i] = grain_distortion_process_sample(dist, in_left[i], 0);
            out_right[i] = grain_distortion_process_sample(dist, in_right[i], 1);
        }
        return;
    }

    // Verify pre-allocated upsample buffers are large enough
    // (buffers are allocated in grain_distortion_create for max size 8192*8)
    int required_size = blocksize * dist->oversample_factor;
    if (!dist->upsample_buffer_left || !dist->upsample_buffer_right ||
        dist->upsample_buffer_size < required_size) {
        // Fallback: process without oversampling (no allocation on audio thread)
        for (int i = 0; i < blocksize; i++) {
            out_left[i] = grain_distortion_process_sample(dist, in_left[i], 0);
            out_right[i] = grain_distortion_process_sample(dist, in_right[i], 1);
        }
        return;
    }

    // Temporary arrays for upsampled samples
    float upsampled_left[8];   // Max oversample_factor is 8
    float upsampled_right[8];

    // Process each sample in block
    for (int i = 0; i < blocksize; i++) {
        //  NaN check on input
        if (!isfinite(in_left[i]) || !isfinite(in_right[i])) {
            out_left[i] = 0.0f;
            out_right[i] = 0.0f;
            continue;
        }

        // 1. Upsample input sample
        upsample_sample(in_left[i], upsampled_left, dist->oversample_factor);
        upsample_sample(in_right[i], upsampled_right, dist->oversample_factor);

        // 2. Process each upsampled sample through distortion
        for (int j = 0; j < dist->oversample_factor; j++) {
            upsampled_left[j] = grain_distortion_process_sample(dist, upsampled_left[j], 0);
            upsampled_right[j] = grain_distortion_process_sample(dist, upsampled_right[j], 1);

            // 3. Apply anti-aliasing filter after distortion
            upsampled_left[j] = apply_antialias_filter(dist, upsampled_left[j], 0);
            upsampled_right[j] = apply_antialias_filter(dist, upsampled_right[j], 1);
        }

        // 4. Downsample back to original rate
        out_left[i] = downsample_samples(upsampled_left, dist->oversample_factor);
        out_right[i] = downsample_samples(upsampled_right, dist->oversample_factor);

        //  Final NaN check on output
        if (!isfinite(out_left[i]) || !isfinite(out_right[i])) {
            out_left[i] = in_left[i];  // Passthrough original on NaN
            out_right[i] = in_right[i];
        }
    }
}

float grain_distortion_process_sample(grain_distortion_t *dist, float input, int channel) {
    //  Validate input first (can check without dereferencing)
    if (!isfinite(input)) {
        return 0.0f;
    }

    //  Validate channel parameter (can check without dereferencing)
    if (channel < 0 || channel > 1) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "grain_distortion: ERROR - invalid channel %d (must be 0 or 1)\n", channel);
            warn_count++;
        }
        return input;
    }

    //  NULL pointer check before ANY dereferencing
    if (!dist) {
        return input;
    }

    //  Check magic number to detect use-after-free
    if (dist->magic != 0xD157BEEF) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "grain_distortion: CRITICAL - use-after-free detected! magic=0x%08X (expected 0xD157BEEF)\n", dist->magic);
            warn_count++;
        }
        return input;
    }

    //  Check if enabled FIRST (simple int, less likely corrupted)
    if (!dist->enabled) {
        return input;
    }

    //  Validate sample_rate hasn't been corrupted
    if (dist->sample_rate <= 0 || dist->sample_rate > 384000) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "grain_distortion: ERROR - corrupted sample_rate %d detected in process\n", dist->sample_rate);
            warn_count++;
        }
        return input;
    }

    float signal = input;
    float dry = input;

    // ========== OUTER FILTER 1: Pre-HP Filter (user-controlled) ==========
    if (dist->pre_hp_mix > 0.0f) {
        float *z1 = (channel == 0) ? &dist->pre_hp_z1_left : &dist->pre_hp_z1_right;
        float *y1 = (channel == 0) ? &dist->pre_hp_y1_left : &dist->pre_hp_y1_right;

        // 1-pole highpass: y[n] = a * (y[n-1] + x[n] - x[n-1])
        float filtered = dist->pre_hp_coeff * (*y1 + signal - *z1);
        *z1 = signal;
        *y1 = filtered;

        // Mix filtered with dry
        signal = dry + dist->pre_hp_mix * (filtered - dry);
    }
    dry = signal;

    // ========== INNER FILTER 1: Pre-Emphasis with Makeup Gain ==========
    float *preemph_z1 = (channel == 0) ? &dist->preemph_z1_left : &dist->preemph_z1_right;
    float *preemph_y1 = (channel == 0) ? &dist->preemph_y1_left : &dist->preemph_y1_right;

    if (dist->emphasis_mode == EMPHASIS_MODE_HP) {
        // High-pass pre-emphasis: y[n] = a * (y[n-1] + x[n] - x[n-1])
        float filtered = dist->preemph_coeff * (*preemph_y1 + signal - *preemph_z1);
        *preemph_z1 = signal;
        *preemph_y1 = filtered;
        signal = filtered * dist->emphasis_gain_makeup;
    } else {
        // Low-pass pre-emphasis: y[n] = a * x[n] + (1 - a) * y[n-1]
        float filtered = dist->preemph_coeff * signal + (1.0f - dist->preemph_coeff) * (*preemph_y1);
        *preemph_y1 = filtered;
        signal = filtered * dist->emphasis_gain_makeup;
    }

    // ========== PRE-GAIN STAGE ==========
    signal *= dist->pregain;

    // ========== WAVESHAPER (Mode-Dependent) ==========
    switch (dist->waveshaper_mode) {
        case WAVESHAPER_MODE_TANH:
            signal = waveshape_tanh(signal, dist->current_drive);
            break;

        case WAVESHAPER_MODE_ARCTAN:
            signal = waveshape_arctan(signal, dist->current_drive);
            break;

        case WAVESHAPER_MODE_BLEND:
            signal = waveshape_blend(signal, dist->current_drive, dist->curve_blend);
            break;

        case WAVESHAPER_MODE_ASYMMETRIC:
            signal = waveshape_asymmetric(signal, dist->drive_pos, dist->drive_neg,
                                          WAVESHAPER_MODE_TANH);
            break;

        case WAVESHAPER_MODE_POLYNOMIAL:
            signal = waveshape_polynomial(signal, dist->poly_c1, dist->poly_c2, dist->poly_c3);
            break;

        default:
            signal = waveshape_tanh(signal, dist->current_drive);
            break;
    }

    // ========== INNER FILTER 2: De-Emphasis (Automatic Inverse) ==========
    float *deemph_z1 = (channel == 0) ? &dist->deemph_z1_left : &dist->deemph_z1_right;
    float *deemph_y1 = (channel == 0) ? &dist->deemph_y1_left : &dist->deemph_y1_right;

    if (dist->emphasis_mode == EMPHASIS_MODE_HP) {
        // De-emphasis is low-pass (inverse of HP pre-emphasis)
        float filtered = dist->deemph_coeff * signal + (1.0f - dist->deemph_coeff) * (*deemph_y1);
        *deemph_y1 = filtered;
        signal = filtered;
    } else {
        // De-emphasis is high-pass (inverse of LP pre-emphasis)
        float filtered = dist->deemph_coeff * (*deemph_y1 + signal - *deemph_z1);
        *deemph_z1 = signal;
        *deemph_y1 = filtered;
        signal = filtered;
    }

    dry = signal;

    // ========== OUTER FILTER 2: Post-LP Filter (user-controlled) ==========
    if (dist->post_lp_mix > 0.0f) {
        float *z1, *z2, *y1, *y2;
        if (channel == 0) {
            z1 = &dist->post_lp_z1_left;
            z2 = &dist->post_lp_z2_left;
            y1 = &dist->post_lp_y1_left;
            y2 = &dist->post_lp_y2_left;
        } else {
            z1 = &dist->post_lp_z1_right;
            z2 = &dist->post_lp_z2_right;
            y1 = &dist->post_lp_y1_right;
            y2 = &dist->post_lp_y2_right;
        }

        // 2-pole Butterworth lowpass (Direct Form II)
        float filtered = dist->post_lp_b0 * signal +
                        dist->post_lp_b1 * (*z1) +
                        dist->post_lp_b2 * (*z2) -
                        dist->post_lp_a1 * (*y1) -
                        dist->post_lp_a2 * (*y2);

        *z2 = *z1;
        *z1 = signal;
        *y2 = *y1;
        *y1 = filtered;

        // Mix filtered with dry
        signal = dry + dist->post_lp_mix * (filtered - dry);
    }
    dry = signal;

    // ========== OUTER FILTER 3: Notch Filter (user-controlled) ==========
    if (dist->notch_mix > 0.0f) {
        float *z1, *z2, *y1, *y2;
        if (channel == 0) {
            z1 = &dist->notch_z1_left;
            z2 = &dist->notch_z2_left;
            y1 = &dist->notch_y1_left;
            y2 = &dist->notch_y2_left;
        } else {
            z1 = &dist->notch_z1_right;
            z2 = &dist->notch_z2_right;
            y1 = &dist->notch_y1_right;
            y2 = &dist->notch_y2_right;
        }

        // Notch/bandstop filter (Direct Form II)
        float filtered = dist->notch_b0 * signal +
                        dist->notch_b1 * (*z1) +
                        dist->notch_b2 * (*z2) -
                        dist->notch_a1 * (*y1) -
                        dist->notch_a2 * (*y2);

        *z2 = *z1;
        *z1 = signal;
        *y2 = *y1;
        *y1 = filtered;

        // Mix filtered with dry
        signal = dry + dist->notch_mix * (filtered - dry);
    }

    //  Final NaN/Inf check before returning
    if (!isfinite(signal)) {
        return input;  // Return original input if processing produced invalid output
    }

    return signal;
}

// @endregion:ligase_pd.core.grain.distortion.process

// @region:ligase_pd.core.grain.distortion.api Public API

grain_distortion_t* grain_distortion_create(int sample_rate) {
    //  Validate sample_rate to prevent NaN in coefficient calculations
    if (sample_rate <= 0 || sample_rate > 384000) {
        fprintf(stderr, "grain_distortion: ERROR - invalid sample_rate %d, must be 1-384000\n", sample_rate);
        return NULL;
    }

    grain_distortion_t *dist = (grain_distortion_t*)malloc(sizeof(grain_distortion_t));
    if (!dist) return NULL;

    memset(dist, 0, sizeof(grain_distortion_t));

    //  Set magic number to detect use-after-free
    dist->magic = 0xD157BEEF;

    dist->sample_rate = sample_rate;
    dist->enabled = 0;

    // Initialize positioning and oversampling
    dist->position_mode = 1;          // Post-mix by default
    dist->oversample_factor = 4;      // 4x oversampling by default

    // Pre-allocate upsample buffers for max block size (8192) * max oversample (8)
    // Avoids malloc/free on the audio thread
    int prealloc_size = 8192 * 8;
    dist->upsample_buffer_left = (float*)calloc(prealloc_size, sizeof(float));
    dist->upsample_buffer_right = (float*)calloc(prealloc_size, sizeof(float));
    if (!dist->upsample_buffer_left || !dist->upsample_buffer_right) {
        free(dist->upsample_buffer_left);
        free(dist->upsample_buffer_right);
        free(dist);
        return NULL;
    }
    dist->upsample_buffer_size = prealloc_size;

    // Initialize drive parameters
    update_drive_params(dist, 0.0f);

    // Initialize OUTER pre-hp filter (default: 30Hz, mix 0.5)
    dist->pre_hp_freq = 30.0f;
    dist->pre_hp_mix = 0.5f;
    update_highpass_coeffs(dist);

    // Initialize OUTER post-lp filter (default: 16000Hz, mix 0.5)
    dist->post_lp_freq = 16000.0f;
    dist->post_lp_mix = 0.5f;
    update_lowpass_coeffs(dist);

    // Initialize OUTER notch filter (default: 3000Hz, 500Hz bandwidth, mix 0.0 = inactive)
    dist->notch_freq = 3000.0f;
    dist->notch_bandwidth = 500.0f;
    dist->notch_mix = 0.0f;
    update_notch_coeffs(dist);

    // Initialize INNER pre-emphasis/de-emphasis (default: HP mode, 800Hz)
    dist->emphasis_mode = EMPHASIS_MODE_HP;
    dist->emphasis_freq = 800.0f;
    update_emphasis_coeffs(dist);

    // Initialize pre-gain (default: unity gain)
    dist->pregain = 1.0f;

    // Initialize waveshaper mode (default: tanh)
    dist->waveshaper_mode = WAVESHAPER_MODE_TANH;
    dist->curve_blend = 0.0f;  // Full tanh in blend mode
    dist->drive_pos = 1.0f;    // Unity for asymmetric mode
    dist->drive_neg = 1.0f;    // Unity for asymmetric mode

    // Initialize polynomial coefficients (default: linear passthrough)
    dist->poly_c1 = 1.0f;  // Linear term
    dist->poly_c2 = 0.0f;  // No even harmonics
    dist->poly_c3 = 0.0f;  // No cubic

    // Initialize anti-aliasing filter coefficients
    update_antialias_coeffs(dist);

    fprintf(stderr, "grain_distortion_create: SUCCESS (ptr=%p, magic=0x%08X)\n", (void*)dist, dist->magic);

    return dist;
}

void grain_distortion_destroy(grain_distortion_t *dist) {
    if (dist) {
        //  Invalidate magic number before freeing to detect use-after-free
        dist->magic = 0xDEADBEEF;
        free(dist);
    }
}

void grain_distortion_set_intensity(grain_distortion_t *dist, float intensity) {
    if (!dist) return;
    update_drive_params(dist, intensity);
}

void grain_distortion_set_enabled(grain_distortion_t *dist, int enabled) {
    if (!dist) return;
    dist->enabled = enabled;
}

void grain_distortion_set_oversampling(grain_distortion_t *dist, int enabled) {
    // No-op: Oversampling removed, kept for API compatibility
    (void)dist;
    (void)enabled;
}

// OUTER pre-hp filter setters
void grain_distortion_set_pre_hp_freq(grain_distortion_t *dist, float freq) {
    if (!dist) return;
    if (freq < 30.0f) freq = 30.0f;
    if (freq > 500.0f) freq = 500.0f;
    dist->pre_hp_freq = freq;
    update_highpass_coeffs(dist);
}

void grain_distortion_set_pre_hp_mix(grain_distortion_t *dist, float mix) {
    if (!dist) return;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    dist->pre_hp_mix = mix;
}

// OUTER post-lp filter setters
void grain_distortion_set_post_lp_freq(grain_distortion_t *dist, float freq) {
    if (!dist) return;
    if (freq < 2400.0f) freq = 2400.0f;
    if (freq > 10000.0f) freq = 10000.0f;
    dist->post_lp_freq = freq;
    update_lowpass_coeffs(dist);
}

void grain_distortion_set_post_lp_mix(grain_distortion_t *dist, float mix) {
    if (!dist) return;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    dist->post_lp_mix = mix;
}

// OUTER notch filter setters
void grain_distortion_set_notch_freq(grain_distortion_t *dist, float freq) {
    if (!dist) return;
    //  Clamp freq to safe range (avoid 0 Hz and Nyquist)
    float nyquist = (float)dist->sample_rate * 0.5f;
    if (freq < 20.0f) freq = 20.0f;
    if (freq > nyquist - 100.0f) freq = nyquist - 100.0f;
    dist->notch_freq = freq;
    update_notch_coeffs(dist);
}

void grain_distortion_set_notch_bandwidth(grain_distortion_t *dist, float bandwidth) {
    if (!dist) return;
    //  Clamp bandwidth to reasonable range
    if (bandwidth < 10.0f) bandwidth = 10.0f;
    if (bandwidth > 5000.0f) bandwidth = 5000.0f;
    dist->notch_bandwidth = bandwidth;
    update_notch_coeffs(dist);
}

void grain_distortion_set_notch_mix(grain_distortion_t *dist, float mix) {
    if (!dist) return;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    dist->notch_mix = mix;
}

// INNER pre-emphasis/de-emphasis setters
void grain_distortion_set_emphasis_mode(grain_distortion_t *dist, emphasis_mode_t mode) {
    if (!dist) return;
    dist->emphasis_mode = mode;
    update_emphasis_coeffs(dist);
}

void grain_distortion_set_emphasis_freq(grain_distortion_t *dist, float freq) {
    if (!dist) return;
    if (freq < 100.0f) freq = 100.0f;
    if (freq > 5000.0f) freq = 5000.0f;
    dist->emphasis_freq = freq;
    update_emphasis_coeffs(dist);
}

// Pre-gain setter
void grain_distortion_set_pregain(grain_distortion_t *dist, float gain) {
    if (!dist) return;
    if (gain < 0.1f) gain = 0.1f;
    if (gain > 10.0f) gain = 10.0f;
    dist->pregain = gain;
}

// Waveshaper mode setter
void grain_distortion_set_waveshaper_mode(grain_distortion_t *dist, waveshaper_mode_t mode) {
    if (!dist) return;
    dist->waveshaper_mode = mode;
}

// Blend mode setter
void grain_distortion_set_curve_blend(grain_distortion_t *dist, float blend) {
    if (!dist) return;
    if (blend < 0.0f) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;
    dist->curve_blend = blend;
}

// Asymmetric mode setters
void grain_distortion_set_drive_pos(grain_distortion_t *dist, float drive) {
    if (!dist) return;
    if (drive < 1.0f) drive = 1.0f;
    if (drive > 20.0f) drive = 20.0f;
    dist->drive_pos = drive;
}

void grain_distortion_set_drive_neg(grain_distortion_t *dist, float drive) {
    if (!dist) return;
    if (drive < 1.0f) drive = 1.0f;
    if (drive > 20.0f) drive = 20.0f;
    dist->drive_neg = drive;
}

// Polynomial mode setters
void grain_distortion_set_poly_c1(grain_distortion_t *dist, float c1) {
    if (!dist) return;
    if (c1 < -10.0f) c1 = -10.0f;
    if (c1 > 10.0f) c1 = 10.0f;
    dist->poly_c1 = c1;
}

void grain_distortion_set_poly_c2(grain_distortion_t *dist, float c2) {
    if (!dist) return;
    if (c2 < -10.0f) c2 = -10.0f;
    if (c2 > 10.0f) c2 = 10.0f;
    dist->poly_c2 = c2;
}

void grain_distortion_set_poly_c3(grain_distortion_t *dist, float c3) {
    if (!dist) return;
    if (c3 < -10.0f) c3 = -10.0f;
    if (c3 > 10.0f) c3 = 10.0f;
    dist->poly_c3 = c3;
}

// @endregion:ligase_pd.core.grain.distortion.api

// @endregion:ligase_pd.core.grain.distortion
