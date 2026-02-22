// @region:ligase_pd.core.grain.fog Spectral Fog Effect Implementation

#include "grain_fog.h"
#include "kiss_fftr.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FOG_MAGIC 0xF06BEEF0

// @region:ligase_pd.core.grain.fog.smear Smear (Horizontal Spectral Blurring)

// Apply magnitude smearing across frequency bins (horizontal axis)
static void smear_magnitudes(grain_fog_t *fog, float amount) {
    if (!fog->smear_enabled || amount < 0.001f || fog->smear_bins < 1) {
        // No smearing - copy magnitudes directly
        memcpy(fog->smeared_mags, fog->magnitudes, fog->num_bins * sizeof(float));
        return;
    }

    // Apply neighbor averaging with amount scaling
    int bins = fog->smear_bins;

    for (int i = 0; i < fog->num_bins; i++) {
        float sum = 0.0f;
        int count = 0;

        // Average across neighbors
        for (int j = i - bins; j <= i + bins; j++) {
            if (j >= 0 && j < fog->num_bins) {
                sum += fog->magnitudes[j];
                count++;
            }
        }

        float smeared = sum / (float)count;

        // Blend between original and smeared based on amount
        fog->smeared_mags[i] = fog->magnitudes[i] * (1.0f - amount) + smeared * amount;
    }
}

// @endregion:ligase_pd.core.grain.fog.smear

// @region:ligase_pd.core.grain.fog.specmagfilter SpecMagFilter (Vertical Temporal Filtering)

// @region:ligase_pd.core.grain.fog.specmagfilter.resonance Resonance Control

// Update magnitude filter coefficients (2-pole resonant lowpass)
static void update_magnitude_filter_coefficients(grain_fog_t *fog) {
    // Design a 2-pole resonant lowpass filter (biquad)
    // Using cookbook formulas for resonant lowpass
    // Note: filter runs at the FFT hop rate, not the audio sample rate
    int hop_size = fog->fft_size / 4;
    float frame_rate = (float)fog->sample_rate / (float)hop_size;

    float omega = 2.0f * M_PI * fog->mag_cutoff_hz / frame_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * fog->mag_resonance);

    float a0 = 1.0f + alpha;
    fog->mag_a1 = (-2.0f * cs) / a0;
    fog->mag_a2 = (1.0f - alpha) / a0;
    fog->mag_b0 = ((1.0f - cs) / 2.0f) / a0;
    fog->mag_b1 = (1.0f - cs) / a0;
    fog->mag_b2 = ((1.0f - cs) / 2.0f) / a0;
}

// @endregion:ligase_pd.core.grain.fog.specmagfilter.resonance

// @region:ligase_pd.core.grain.fog.specmagfilter.magnitude_filter Magnitude Filter

// Apply resonant lowpass filtering to magnitudes (vertical/temporal axis)
static void filter_magnitudes(grain_fog_t *fog, float amount) {
    if (!fog->specmagfilter_enabled || amount < 0.001f) {
        // No filtering - copy smeared mags directly
        memcpy(fog->filtered_mags, fog->smeared_mags, fog->num_bins * sizeof(float));
        return;
    }

    // Apply 2-pole IIR filter to each bin's magnitude using Direct Form II Transposed
    // This form uses both feedforward (b0,b1,b2) and feedback (a1,a2) coefficients
    // and requires only 2 state variables per bin (z1, z2)
    for (int i = 0; i < fog->num_bins; i++) {
        float x_n = fog->smeared_mags[i];

        // Direct Form II Transposed:
        //   y[n] = b0*x[n] + z1[n-1]
        //   z1[n] = b1*x[n] - a1*y[n] + z2[n-1]
        //   z2[n] = b2*x[n] - a2*y[n]
        float filtered = fog->mag_b0 * x_n + fog->mag_z1[i];

        // Apply soft limiting to prevent magnitude explosion
        // tanhf provides soft knee compression to tame resonance peaks
        filtered = tanhf(filtered * 0.5f) * 2.0f;

        // Update state variables (Direct Form II Transposed)
        fog->mag_z1[i] = fog->mag_b1 * x_n - fog->mag_a1 * filtered + fog->mag_z2[i];
        fog->mag_z2[i] = fog->mag_b2 * x_n - fog->mag_a2 * filtered;

        // Flush denormals to prevent CPU spikes
        if (fabsf(fog->mag_z1[i]) < 1e-15f) fog->mag_z1[i] = 0.0f;
        if (fabsf(fog->mag_z2[i]) < 1e-15f) fog->mag_z2[i] = 0.0f;

        // Blend between unfiltered and filtered based on amount
        fog->filtered_mags[i] = fog->smeared_mags[i] * (1.0f - amount) + filtered * amount;
    }
}

// @endregion:ligase_pd.core.grain.fog.specmagfilter.magnitude_filter

// @region:ligase_pd.core.grain.fog.specmagfilter.phase_filter Phase Lowpass Filter

// Update phase filter coefficient (1-pole lowpass)
static void update_phase_filter_coefficient(grain_fog_t *fog) {
    // Simple 1-pole lowpass: y[n] = (1-a)*x[n] + a*y[n-1]
    // where a = exp(-2*pi*fc/fs)
    // Note: filter runs at the FFT hop rate, not the audio sample rate
    int hop_size = fog->fft_size / 4;
    float frame_rate = (float)fog->sample_rate / (float)hop_size;

    float omega = 2.0f * M_PI * fog->phase_cutoff_hz / frame_rate;
    fog->phase_lp_coeff = expf(-omega);
}

// Apply lowpass filtering to phase deltas (vertical/temporal axis)
static void filter_phases(grain_fog_t *fog, float amount) {
    if (!fog->specmagfilter_enabled || amount < 0.001f) {
        // No filtering - copy phases directly
        memcpy(fog->filtered_phases, fog->phases, fog->num_bins * sizeof(float));
        return;
    }

    // Apply phase delta lowpass filtering
    for (int i = 0; i < fog->num_bins; i++) {
        float current_phase = fog->phases[i];
        float prev_phase = fog->phase_prev[i];

        // Calculate phase delta (unwrapped)
        float delta = current_phase - prev_phase;

        // Wrap delta to [-pi, pi]
        while (delta > M_PI) delta -= 2.0f * M_PI;
        while (delta < -M_PI) delta += 2.0f * M_PI;

        // Lowpass filter the delta
        float filtered_delta = delta * (1.0f - fog->phase_lp_coeff) +
                              fog->phase_delta_z1[i] * fog->phase_lp_coeff;

        // Reconstruct filtered phase
        float filtered_phase = prev_phase + filtered_delta;

        // Update state
        fog->phase_delta_z1[i] = filtered_delta;
        fog->phase_prev[i] = filtered_phase;

        // Flush denormals to prevent CPU spikes
        if (fabsf(fog->phase_delta_z1[i]) < 1e-15f) fog->phase_delta_z1[i] = 0.0f;

        // Blend between unfiltered and filtered based on amount
        fog->filtered_phases[i] = current_phase * (1.0f - amount) + filtered_phase * amount;
    }
}

// @endregion:ligase_pd.core.grain.fog.specmagfilter.phase_filter

// @endregion:ligase_pd.core.grain.fog.specmagfilter

// @region:ligase_pd.core.grain.fog.coordinate_conversion Cartesian ↔ Polar Conversion

// Convert complex FFT bins to magnitude/phase representation
static void complex_to_polar(grain_fog_t *fog, kiss_fft_cpx *bins) {
    for (int i = 0; i < fog->num_bins; i++) {
        float re = bins[i].r;
        float im = bins[i].i;
        fog->magnitudes[i] = sqrtf(re*re + im*im);
        fog->phases[i] = atan2f(im, re);
    }
}

// Convert magnitude/phase back to complex FFT bins
static void polar_to_complex(grain_fog_t *fog, kiss_fft_cpx *bins,
                             float *mags, float *phases) {
    for (int i = 0; i < fog->num_bins; i++) {
        bins[i].r = mags[i] * cosf(phases[i]);
        bins[i].i = mags[i] * sinf(phases[i]);
    }
}

// @endregion:ligase_pd.core.grain.fog.coordinate_conversion

// @region:ligase_pd.core.grain.fog.api Public API

// Create fog effect processor
grain_fog_t* grain_fog_create(int sample_rate, int fft_size) {
    grain_fog_t *fog = (grain_fog_t*)calloc(1, sizeof(grain_fog_t));
    if (!fog) return NULL;

    fog->magic = FOG_MAGIC;
    fog->sample_rate = sample_rate;
    fog->fft_size = fft_size;
    fog->num_bins = fft_size / 2 + 1;

    // Default parameters
    fog->mix = 0.0f;
    fog->smear_enabled = 1;
    fog->smear_bins = 8;  // Wider blur for more diffuse, hazy timbre
    fog->smear_onset_curve = FOG_ONSET_LOGARITHMIC;
    fog->smear_onset_amount = 1.0f;

    fog->specmagfilter_enabled = 1;
    fog->mag_cutoff_hz = 2.0f;
    fog->mag_resonance = 1.0f;  // Gentle resonance for ghostly spectral persistence
    fog->phase_cutoff_hz = 2.0f;
    fog->specmagfilter_onset_curve = FOG_ONSET_LOGARITHMIC;
    fog->specmagfilter_onset_amount = 1.0f;

    // Allocate per-bin state arrays
    fog->mag_z1 = (float*)calloc(fog->num_bins, sizeof(float));
    fog->mag_z2 = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phase_prev = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phase_delta_z1 = (float*)calloc(fog->num_bins, sizeof(float));

    // Allocate temporary processing buffers
    fog->magnitudes = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phases = (float*)calloc(fog->num_bins, sizeof(float));
    fog->smeared_mags = (float*)calloc(fog->num_bins, sizeof(float));
    fog->filtered_mags = (float*)calloc(fog->num_bins, sizeof(float));
    fog->filtered_phases = (float*)calloc(fog->num_bins, sizeof(float));

    // Allocate FFT buffers
    fog->fft_real_left = (float*)calloc(fft_size, sizeof(float));
    fog->fft_imag_left = (float*)calloc(fft_size, sizeof(float));
    fog->fft_real_right = (float*)calloc(fft_size, sizeof(float));
    fog->fft_imag_right = (float*)calloc(fft_size, sizeof(float));

    // Allocate overlap-add buffers
    fog->input_buffer_left = (float*)calloc(fft_size, sizeof(float));
    fog->input_buffer_right = (float*)calloc(fft_size, sizeof(float));
    fog->output_buffer_left = (float*)calloc(fft_size, sizeof(float));
    fog->output_buffer_right = (float*)calloc(fft_size, sizeof(float));
    fog->window = (float*)calloc(fft_size, sizeof(float));

    // Precompute periodic Hann window: w[n] = 0.5 * (1 - cos(2π*n/N))
    // Periodic form (divide by N, not N-1) gives perfect COLA with 4x overlap:
    //   sum of Hann^2 at every position = 1.5 (constant)
    // Symmetric form (N-1) creates periodic COLA variation → comb-filter artifacts
    for (int i = 0; i < fft_size; i++) {
        fog->window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / fft_size));
    }

    // Initialize overlap-add state
    fog->input_pos = 0;
    fog->samples_until_process = fft_size;  // Will process after filling first frame
    fog->output_read_pos = 0;
    fog->frames_processed = 0;  // No valid output yet

    // Create kissfft configuration objects
    fog->fft_forward = kiss_fftr_alloc(fft_size, 0, NULL, NULL);  // 0 = forward FFT
    fog->fft_inverse = kiss_fftr_alloc(fft_size, 1, NULL, NULL);  // 1 = inverse FFT

    // Allocate complex FFT bins (fft_size/2 + 1 bins for real FFT)
    fog->fft_bins_left = (kiss_fft_cpx*)calloc(fog->num_bins, sizeof(kiss_fft_cpx));
    fog->fft_bins_right = (kiss_fft_cpx*)calloc(fog->num_bins, sizeof(kiss_fft_cpx));

    // Initialize filter coefficients
    update_magnitude_filter_coefficients(fog);
    update_phase_filter_coefficient(fog);

    return fog;
}

void grain_fog_destroy(grain_fog_t *fog) {
    if (!fog) return;

    free(fog->mag_z1);
    free(fog->mag_z2);
    free(fog->phase_prev);
    free(fog->phase_delta_z1);
    free(fog->magnitudes);
    free(fog->phases);
    free(fog->smeared_mags);
    free(fog->filtered_mags);
    free(fog->filtered_phases);
    free(fog->fft_real_left);
    free(fog->fft_imag_left);
    free(fog->fft_real_right);
    free(fog->fft_imag_right);

    // Free overlap-add buffers
    free(fog->input_buffer_left);
    free(fog->input_buffer_right);
    free(fog->output_buffer_left);
    free(fog->output_buffer_right);
    free(fog->window);

    // Free complex FFT bins
    free(fog->fft_bins_left);
    free(fog->fft_bins_right);

    // Free kissfft configurations
    kiss_fftr_free(fog->fft_forward);
    kiss_fftr_free(fog->fft_inverse);

    fog->magic = 0;
    free(fog);
}

// Main inlet control
void grain_fog_set_mix(grain_fog_t *fog, float mix) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->mix = fmaxf(0.0f, fminf(1.0f, mix));
}

// @region:ligase_pd.core.grain.fog.messages.smear Smear Control Messages

void grain_fog_set_smear_bins(grain_fog_t *fog, int bins) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->smear_bins = bins < 0 ? 0 : (bins > 32 ? 32 : bins);
}

void grain_fog_set_smear_enabled(grain_fog_t *fog, int enabled) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->smear_enabled = enabled;
}

void grain_fog_set_smear_onset_curve(grain_fog_t *fog, fog_onset_curve_t curve) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->smear_onset_curve = curve;
}

void grain_fog_set_smear_onset_amount(grain_fog_t *fog, float amount) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->smear_onset_amount = fmaxf(0.0f, fminf(1.0f, amount));
}

// @endregion:ligase_pd.core.grain.fog.messages.smear

// @region:ligase_pd.core.grain.fog.messages.specmagfilter SpecMagFilter Control Messages

void grain_fog_set_mag_cutoff(grain_fog_t *fog, float cutoff_hz) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->mag_cutoff_hz = fmaxf(0.1f, fminf(20.0f, cutoff_hz));
    update_magnitude_filter_coefficients(fog);
}

void grain_fog_set_mag_resonance(grain_fog_t *fog, float q) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->mag_resonance = fmaxf(0.1f, fminf(10.0f, q));
    update_magnitude_filter_coefficients(fog);
}

void grain_fog_set_phase_cutoff(grain_fog_t *fog, float cutoff_hz) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->phase_cutoff_hz = fmaxf(0.1f, fminf(20.0f, cutoff_hz));
    update_phase_filter_coefficient(fog);
}

void grain_fog_set_specmagfilter_enabled(grain_fog_t *fog, int enabled) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->specmagfilter_enabled = enabled;
}

void grain_fog_set_specmagfilter_onset_curve(grain_fog_t *fog, fog_onset_curve_t curve) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->specmagfilter_onset_curve = curve;
}

void grain_fog_set_specmagfilter_onset_amount(grain_fog_t *fog, float amount) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->specmagfilter_onset_amount = fmaxf(0.0f, fminf(1.0f, amount));
}

// @endregion:ligase_pd.core.grain.fog.messages.specmagfilter

// @endregion:ligase_pd.core.grain.fog.api

// @region:ligase_pd.core.grain.fog.process Fog Processing Pipeline

// Helper: Calculate onset amount from mix value using specified curve
static float calculate_onset_amount(float mix, fog_onset_curve_t curve, float curve_amount) {
    if (mix < 0.001f) return 0.0f;
    if (mix > 0.999f) return curve_amount;

    float amount;
    switch (curve) {
        case FOG_ONSET_LINEAR:
            amount = mix;
            break;
        case FOG_ONSET_EXPONENTIAL:
            amount = mix * mix;  // x^2
            break;
        case FOG_ONSET_LOGARITHMIC:
        default:
            amount = sqrtf(mix);  // x^0.5
            break;
    }

    return amount * curve_amount;
}

// @region:ligase_pd.core.grain.fog.fft_processing FFT Frame Processing

// Process a single FFT frame with spectral effects
// Uses fft_imag_left as scratch space for windowed input and IFFT output
static void process_fft_frame(grain_fog_t *fog, float *input, float *output,
                              kiss_fft_cpx *bins) {
    // Use fft_imag_left as scratch buffer (not used for anything else during processing)
    float *scratch = fog->fft_imag_left;

    // Apply analysis window (periodic Hann) to input
    for (int i = 0; i < fog->fft_size; i++) {
        scratch[i] = input[i] * fog->window[i];
    }

    // Forward FFT (real → complex)
    kiss_fftr(fog->fft_forward, scratch, bins);

    // Convert to polar (magnitude/phase)
    complex_to_polar(fog, bins);

    // Stage 1: Horizontal smear (frequency-axis magnitude averaging)
    if (fog->smear_enabled) {
        float smear_amt = calculate_onset_amount(fog->mix,
            fog->smear_onset_curve, fog->smear_onset_amount);
        smear_magnitudes(fog, smear_amt);
    } else {
        memcpy(fog->smeared_mags, fog->magnitudes, fog->num_bins * sizeof(float));
    }

    // Stage 2: Vertical filter (time-axis resonant lowpass on magnitudes + phase smoothing)
    if (fog->specmagfilter_enabled) {
        float specmag_amt = calculate_onset_amount(fog->mix,
            fog->specmagfilter_onset_curve, fog->specmagfilter_onset_amount);
        filter_magnitudes(fog, specmag_amt);
        filter_phases(fog, specmag_amt);
    } else {
        memcpy(fog->filtered_mags, fog->smeared_mags, fog->num_bins * sizeof(float));
        memcpy(fog->filtered_phases, fog->phases, fog->num_bins * sizeof(float));
    }

    // Convert back to Cartesian (real/imag)
    polar_to_complex(fog, bins, fog->filtered_mags, fog->filtered_phases);

    // Inverse FFT (complex → real), reuse scratch buffer
    kiss_fftri(fog->fft_inverse, bins, scratch);

    // Apply synthesis window and normalize
    // kissfft round-trip gain = N (verified: forward is standard DFT, inverse is unscaled)
    // Periodic Hann^2 (analysis * synthesis) COLA with 4x overlap = 1.5
    // Unity gain normalization: 1/(N * COLA) = 1/(N * 1.5) = 2/(3*N)
    const float norm_factor = 2.0f / (3.0f * fog->fft_size);
    for (int i = 0; i < fog->fft_size; i++) {
        output[i] = scratch[i] * fog->window[i] * norm_factor;
    }
}

// @endregion:ligase_pd.core.grain.fog.fft_processing

// Process block of samples through fog effect with overlap-add FFT processing
void grain_fog_process_block(
    grain_fog_t *fog,
    float *in_left,
    float *in_right,
    float *out_left,
    float *out_right,
    int blocksize
) {
    if (!fog || fog->magic != FOG_MAGIC) {
        // Passthrough on error
        memcpy(out_left, in_left, blocksize * sizeof(float));
        memcpy(out_right, in_right, blocksize * sizeof(float));
        return;
    }

    // If mix is zero, just pass through
    if (fog->mix < 0.001f) {
        memcpy(out_left, in_left, blocksize * sizeof(float));
        memcpy(out_right, in_right, blocksize * sizeof(float));
        return;
    }

    // Equal-power crossfade coefficients (logarithmic)
    float theta = fog->mix * (M_PI / 2.0f);
    float wet_gain = sinf(theta);
    float dry_gain = cosf(theta);

    // Hop size for 4x overlap (75% overlap)
    const int hop_size = fog->fft_size / 4;  // 256 samples for 1024 FFT

    // Process each sample in the block
    for (int i = 0; i < blocksize; i++) {
        // 1. WRITE INPUT: Add sample to input buffer
        fog->input_buffer_left[fog->input_pos] = in_left[i];
        fog->input_buffer_right[fog->input_pos] = in_right[i];
        fog->input_pos++;

        // 2. COUNTDOWN: Decrement process counter
        fog->samples_until_process--;

        // 3. PROCESS: When countdown hits 0, process FFT frame
        if (fog->samples_until_process <= 0) {
            // Process left channel FFT frame
            process_fft_frame(fog, fog->input_buffer_left,
                            fog->fft_real_left, fog->fft_bins_left);

            // Process right channel FFT frame
            process_fft_frame(fog, fog->input_buffer_right,
                            fog->fft_real_right, fog->fft_bins_right);

            // ADD to accumulation buffer (the key OLA step)
            for (int j = 0; j < fog->fft_size; j++) {
                fog->output_buffer_left[j] += fog->fft_real_left[j];
                fog->output_buffer_right[j] += fog->fft_real_right[j];
            }

            // Shift input buffer left by hop_size
            memmove(fog->input_buffer_left,
                   fog->input_buffer_left + hop_size,
                   (fog->fft_size - hop_size) * sizeof(float));
            memmove(fog->input_buffer_right,
                   fog->input_buffer_right + hop_size,
                   (fog->fft_size - hop_size) * sizeof(float));

            // Zero-pad the end
            memset(fog->input_buffer_left + (fog->fft_size - hop_size), 0,
                  hop_size * sizeof(float));
            memset(fog->input_buffer_right + (fog->fft_size - hop_size), 0,
                  hop_size * sizeof(float));

            // Reset input position
            fog->input_pos = fog->fft_size - hop_size;  // 768

            // Reset process countdown
            fog->samples_until_process = hop_size;  // 256

            // Track that we've processed a frame
            fog->frames_processed++;
        }

        // 4. READ OUTPUT: Get sample from accumulation buffer
        float wet_left, wet_right;

        // With 4x overlap, need 4 frames before output is fully "baked"
        // (each position has contributions from all 4 overlapping windows)
        if (fog->frames_processed >= 4) {
            // Safety check: ensure output_read_pos is in valid range
            if (fog->output_read_pos >= hop_size) {
                fog->output_read_pos = 0;
            }

            // Read from accumulation buffer
            wet_left = fog->output_buffer_left[fog->output_read_pos];
            wet_right = fog->output_buffer_right[fog->output_read_pos];

            // Apply final safety clipping
            wet_left = fmaxf(-1.0f, fminf(1.0f, wet_left));
            wet_right = fmaxf(-1.0f, fminf(1.0f, wet_right));
        } else {
            // Initial latency period - accumulation buffer not yet fully baked
            // Pass through dry input as the wet signal to avoid volume dip
            wet_left = in_left[i];
            wet_right = in_right[i];
        }

        fog->output_read_pos++;

        // When we've consumed hop_size samples, shift accumulation buffer
        // (this runs during latency too, to keep the buffer advancing)
        if (fog->output_read_pos >= hop_size) {
            memmove(fog->output_buffer_left,
                   fog->output_buffer_left + hop_size,
                   (fog->fft_size - hop_size) * sizeof(float));
            memmove(fog->output_buffer_right,
                   fog->output_buffer_right + hop_size,
                   (fog->fft_size - hop_size) * sizeof(float));

            // Zero-pad the end (the crucial "clear" step)
            memset(fog->output_buffer_left + (fog->fft_size - hop_size), 0,
                  hop_size * sizeof(float));
            memset(fog->output_buffer_right + (fog->fft_size - hop_size), 0,
                  hop_size * sizeof(float));

            fog->output_read_pos = 0;
        }

        // 5. CROSSFADE: Mix dry and wet
        out_left[i] = in_left[i] * dry_gain + wet_left * wet_gain;
        out_right[i] = in_right[i] * dry_gain + wet_right * wet_gain;
    }
}

// @endregion:ligase_pd.core.grain.fog.process

// @endregion:ligase_pd.core.grain.fog
