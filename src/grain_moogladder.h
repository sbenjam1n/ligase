// @region:ligase_pd.core.grain.moogladder Grain Moogladder Filter
// Moog ladder low-pass filter for post-grain-sum filtering
// Provides resonant filtering for Karplus-Strong effects and timbral shaping
//
// AUDIO: Classic Moog ladder topology with 4 cascaded one-pole stages
// Resonance > 3.5 can self-oscillate, creating pitched tones at cutoff frequency
// Ideal for:
// - Karplus-Strong plucked string synthesis (with looping grains)
// - Resonant sweeps and formant filtering
// - Musical timbral shaping before delay
//
// STABILITY: Filter can self-oscillate at high resonance (>3.5)
// This is musically useful but can cause loud sine tones if cutoff is low
// Resonance clamped to [0.0, 4.0] to prevent instability
//
// Signal path placement: POST grain sum, PRE grain delay
// This allows filtered grains to feed into delay for complex textures

#ifndef GRAIN_MOOGLADDER_H
#define GRAIN_MOOGLADDER_H

#include "types.h"

// Create/destroy moogladder filter
grain_moogladder_t* grain_moogladder_create(int sample_rate);
void grain_moogladder_destroy(grain_moogladder_t *filter);

// Set filter parameters
void grain_moogladder_set_cutoff(grain_moogladder_t *filter, float cutoff_hz);
void grain_moogladder_set_resonance(grain_moogladder_t *filter, float resonance);
void grain_moogladder_set_mix(grain_moogladder_t *filter, float mix);
void grain_moogladder_set_enabled(grain_moogladder_t *filter, int enabled);
void grain_moogladder_set_fb_threshold(grain_moogladder_t *filter, float threshold);
void grain_moogladder_set_fb_saturation(grain_moogladder_t *filter, float saturation);

// Process audio through filter (stereo in-place)
void grain_moogladder_process(grain_moogladder_t *filter, float *left, float *right, int blocksize);

#endif // GRAIN_MOOGLADDER_H

// @endregion:ligase_pd.core.grain.moogladder
