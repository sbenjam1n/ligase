// @region:ligase_pd.core.grain.fog Spectral Fog Effect
//
// Combines horizontal smear (frequency-axis magnitude averaging) and
// vertical specmagfilter (time-axis resonant filtering of magnitude and phase)
// to create a "spectral fog" effect.
//
// Processing chain:
// 1. FFT input → polar conversion (magnitude/phase per bin)
// 2. Horizontal smear: average magnitudes across neighboring bins
// 3. Vertical filter: resonant lowpass on magnitudes + phase smoothing over time
// 4. Polar → Cartesian conversion → IFFT output
//
// Controllable via single inlet (0-1) with logarithmic crossfade,
// plus separate onset curves for smear and specmagfilter.

#ifndef GRAIN_FOG_H
#define GRAIN_FOG_H

#include "types.h"

// @region:ligase_pd.core.grain.fog.types
// Onset curve types defined in types.h
// @endregion:ligase_pd.core.grain.fog.types

// @region:ligase_pd.core.grain.fog.api Public API

// Create and destroy fog effect
grain_fog_t* grain_fog_create(int sample_rate, int fft_size);
void grain_fog_destroy(grain_fog_t *fog);

// Main inlet control (0.0-1.0, logarithmic equal-power crossfade)
void grain_fog_set_mix(grain_fog_t *fog, float mix);

// @region:ligase_pd.core.grain.fog.messages.smear Smear Control Messages

// Smear (horizontal spectral blurring) parameters
void grain_fog_set_smear_bins(grain_fog_t *fog, int bins);  // Number of neighbor bins to average (0-32)
void grain_fog_set_smear_enabled(grain_fog_t *fog, int enabled);
void grain_fog_set_smear_onset_curve(grain_fog_t *fog, fog_onset_curve_t curve);
void grain_fog_set_smear_onset_amount(grain_fog_t *fog, float amount);  // 0.0-1.0

// @endregion:ligase_pd.core.grain.fog.messages.smear

// @region:ligase_pd.core.grain.fog.messages.specmagfilter SpecMagFilter Control Messages

// SpecMagFilter (vertical temporal filtering) parameters
void grain_fog_set_mag_cutoff(grain_fog_t *fog, float cutoff_hz);  // Magnitude lowpass cutoff (0.1-20 Hz)
void grain_fog_set_mag_resonance(grain_fog_t *fog, float q);       // Resonance Q factor (0.1-10.0)
void grain_fog_set_phase_cutoff(grain_fog_t *fog, float cutoff_hz); // Phase lowpass cutoff (0.1-20 Hz)
void grain_fog_set_specmagfilter_enabled(grain_fog_t *fog, int enabled);
void grain_fog_set_specmagfilter_onset_curve(grain_fog_t *fog, fog_onset_curve_t curve);
void grain_fog_set_specmagfilter_onset_amount(grain_fog_t *fog, float amount);  // 0.0-1.0

// @endregion:ligase_pd.core.grain.fog.messages.specmagfilter

// @region:ligase_pd.core.grain.fog.process Fog Processing Pipeline

// Process block of samples through fog effect
// Includes FFT → smear → specmagfilter → IFFT
void grain_fog_process_block(
    grain_fog_t *fog,
    float *in_left,
    float *in_right,
    float *out_left,
    float *out_right,
    int blocksize
);

// @endregion:ligase_pd.core.grain.fog.process

// @endregion:ligase_pd.core.grain.fog.api

#endif // GRAIN_FOG_H

// @endregion:ligase_pd.core.grain.fog
