// @region:ligase_pd.core.grain.distortion Grain Tanh Distortion
//
// Simplified tanh waveshaper for per-grain distortion
// - No oversampling (removed for clean, efficient processing)
// - No filtering (just pure waveshaping)
// - Exponential drive curve (1.0 - 20.0) with gain compensation
// - Disabled by default (opt-in via distortion_enable 1)

#ifndef GRAIN_DISTORTION_H
#define GRAIN_DISTORTION_H

#include "types.h"

// @region:ligase_pd.core.grain.distortion.api Public API

// Create and destroy grain distortion
grain_distortion_t* grain_distortion_create(int sample_rate);
void grain_distortion_destroy(grain_distortion_t *dist);

// Update the intensity index (0.0 = clean, 1.0 = saturated)
void grain_distortion_set_intensity(grain_distortion_t *dist, float intensity);

// Enable/disable distortion
void grain_distortion_set_enabled(grain_distortion_t *dist, int enabled);

// Oversampling function (no-op, kept for API compatibility)
void grain_distortion_set_oversampling(grain_distortion_t *dist, int enabled);

// Process a single sample through waveshaper with filters (per-grain mode)
// channel: 0 = left, 1 = right
float grain_distortion_process_sample(grain_distortion_t *dist, float input, int channel);

// Process block of samples with oversampling (post-mix mode)
// Includes upsample → distort → anti-alias → downsample
void grain_distortion_process_block(
    grain_distortion_t *dist,
    float *in_left,
    float *in_right,
    float *out_left,
    float *out_right,
    int blocksize
);

// Pre-tanh highpass filter controls [OUTER FILTER]
void grain_distortion_set_pre_hp_freq(grain_distortion_t *dist, float freq);  // 30-500Hz
void grain_distortion_set_pre_hp_mix(grain_distortion_t *dist, float mix);    // 0-1

// Post-tanh lowpass filter controls [OUTER FILTER]
void grain_distortion_set_post_lp_freq(grain_distortion_t *dist, float freq); // 2400-10000Hz
void grain_distortion_set_post_lp_mix(grain_distortion_t *dist, float mix);   // 0-1

// Reject notch filter controls [OUTER FILTER]
void grain_distortion_set_notch_freq(grain_distortion_t *dist, float freq);      // Center frequency
void grain_distortion_set_notch_bandwidth(grain_distortion_t *dist, float bw);   // Bandwidth in Hz
void grain_distortion_set_notch_mix(grain_distortion_t *dist, float mix);        // 0-1 (0 = inactive)

// Pre-emphasis/de-emphasis controls [INNER FILTERS]
void grain_distortion_set_emphasis_mode(grain_distortion_t *dist, emphasis_mode_t mode);  // HP or LP
void grain_distortion_set_emphasis_freq(grain_distortion_t *dist, float freq);  // 100-5000Hz

// Pre-gain control
void grain_distortion_set_pregain(grain_distortion_t *dist, float gain);  // 0.1-10.0

// Waveshaper mode selection
void grain_distortion_set_waveshaper_mode(grain_distortion_t *dist, waveshaper_mode_t mode);

// Blend mode control (WAVESHAPER_MODE_BLEND)
void grain_distortion_set_curve_blend(grain_distortion_t *dist, float blend);  // 0.0-1.0

// Asymmetric mode controls (WAVESHAPER_MODE_ASYMMETRIC)
void grain_distortion_set_drive_pos(grain_distortion_t *dist, float drive);  // 1.0-20.0
void grain_distortion_set_drive_neg(grain_distortion_t *dist, float drive);  // 1.0-20.0

// Polynomial mode controls (WAVESHAPER_MODE_POLYNOMIAL)
void grain_distortion_set_poly_c1(grain_distortion_t *dist, float c1);  // -10.0 to 10.0
void grain_distortion_set_poly_c2(grain_distortion_t *dist, float c2);  // -10.0 to 10.0
void grain_distortion_set_poly_c3(grain_distortion_t *dist, float c3);  // -10.0 to 10.0

// @endregion:ligase_pd.core.grain.distortion.api

#endif // GRAIN_DISTORTION_H

// @endregion:ligase_pd.core.grain.distortion
