// @region:ligase_pd.core.grain.fog Spectral Fog Effect Implementation

#include "grain_fog.h"
#include "kiss_fftr.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

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

    // Sliding-window average: O(num_bins) regardless of smear_bins width.
    // Maintain a running sum and count; add the incoming right edge, drop the
    // outgoing left edge as the window slides across the bin array.
    int bins = fog->smear_bins;
    int num_bins = fog->num_bins;

    // Initialise window centred at bin 0: covers [0, bins] (left side clipped)
    float run_sum = 0.0f;
    int   run_count = 0;
    for (int j = 0; j <= bins && j < num_bins; j++) {
        run_sum += fog->magnitudes[j];
        run_count++;
    }

    for (int i = 0; i < num_bins; i++) {
        float smeared = run_sum / (float)run_count;
        fog->smeared_mags[i] = fog->magnitudes[i] * (1.0f - amount) + smeared * amount;

        // Slide right: add new right-edge bin, remove old left-edge bin
        int new_right = i + 1 + bins;
        if (new_right < num_bins) {
            run_sum += fog->magnitudes[new_right];
            run_count++;
        }
        int old_left = i - bins;
        if (old_left >= 0) {
            run_sum -= fog->magnitudes[old_left];
            run_count--;
        }
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
    float frame_rate = (float)fog->sample_rate / (float)fog->hop_size;

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

// Apply resonant lowpass filtering to magnitudes (vertical/temporal axis).
// z1/z2 are the per-bin IIR state arrays for the channel being processed;
// callers pass the appropriate left or right arrays.
static void filter_magnitudes(grain_fog_t *fog, float amount, float *z1, float *z2) {
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
        float filtered = fog->mag_b0 * x_n + z1[i];

        // Apply soft limiting to prevent magnitude explosion
        // tanhf provides soft knee compression to tame resonance peaks
        filtered = tanhf(filtered * 0.5f) * 2.0f;

        // Update state variables (Direct Form II Transposed)
        z1[i] = fog->mag_b1 * x_n - fog->mag_a1 * filtered + z2[i];
        z2[i] = fog->mag_b2 * x_n - fog->mag_a2 * filtered;

        // Bound + sanitize the STATE (the tanh above limits only the output): a high-Q resonance
        // or a stray NaN/Inf could otherwise let the per-bin state run away and flood the FFT/CPU.
        if (!isfinite(z1[i])) z1[i] = 0.0f; else if (z1[i] > 1.0e4f) z1[i] = 1.0e4f; else if (z1[i] < -1.0e4f) z1[i] = -1.0e4f;
        if (!isfinite(z2[i])) z2[i] = 0.0f; else if (z2[i] > 1.0e4f) z2[i] = 1.0e4f; else if (z2[i] < -1.0e4f) z2[i] = -1.0e4f;
        // Flush denormals to prevent CPU spikes
        if (fabsf(z1[i]) < 1e-15f) z1[i] = 0.0f;
        if (fabsf(z2[i]) < 1e-15f) z2[i] = 0.0f;

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
    float frame_rate = (float)fog->sample_rate / (float)fog->hop_size;

    float omega = 2.0f * M_PI * fog->phase_cutoff_hz / frame_rate;
    fog->phase_lp_coeff = expf(-omega);
}

// Apply lowpass filtering to phase deltas (vertical/temporal axis).
// phase_prev/phase_delta_z1 are the per-bin state arrays for the channel being
// processed; callers pass the appropriate left or right arrays.
static void filter_phases(grain_fog_t *fog, float amount,
                          float *phase_prev, float *phase_delta_z1) {
    if (!fog->specmagfilter_enabled || amount < 0.001f) {
        // No filtering - copy phases directly
        memcpy(fog->filtered_phases, fog->phases, fog->num_bins * sizeof(float));
        return;
    }

    // Apply phase delta lowpass filtering
    for (int i = 0; i < fog->num_bins; i++) {
        float current_phase = fog->phases[i];
        float prev_phase = phase_prev[i];

        // Calculate phase delta (unwrapped)
        float delta = current_phase - prev_phase;

        // Wrap delta to [-pi, pi] in O(1). The old while-loops hung the audio thread at 100% CPU
        // if delta ever became Inf (Inf - 2*pi == Inf, so the loop never terminated).
        if (!isfinite(delta)) delta = 0.0f;
        delta -= 2.0f * M_PI * floorf((delta + (float)M_PI) * (float)(1.0 / (2.0 * M_PI)));

        // Lowpass filter the delta
        float filtered_delta = delta * (1.0f - fog->phase_lp_coeff) +
                              phase_delta_z1[i] * fog->phase_lp_coeff;

        // Reconstruct filtered phase, kept wrapped to [-pi, pi] so this per-bin accumulator can
        // never drift to a huge magnitude or Inf (which would re-trip the unwrap above).
        float filtered_phase = prev_phase + filtered_delta;
        filtered_phase -= 2.0f * M_PI * floorf((filtered_phase + (float)M_PI) * (float)(1.0 / (2.0 * M_PI)));
        if (!isfinite(filtered_phase)) filtered_phase = 0.0f;

        // Update state
        phase_delta_z1[i] = filtered_delta;
        phase_prev[i] = filtered_phase;

        // Flush denormals / non-finite to prevent CPU spikes and a stuck NaN
        if (!isfinite(phase_delta_z1[i]) || fabsf(phase_delta_z1[i]) < 1e-15f) phase_delta_z1[i] = 0.0f;

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

// @region:ligase_pd.core.grain.fog.config Configuration Reader

// Read fft_size and overlap_factor from ligase.conf.
// Sets defaults (1024, 4) if the file is missing or values are absent.
// Only 512/1024/2048 are accepted for fft_size; only 2/4/8 for overlap_factor.
static void read_fog_config(int *out_fft_size, int *out_overlap_factor) {
    *out_fft_size = 1024;
    *out_overlap_factor = 4;

    FILE *f = fopen("ligase.conf", "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        int value;
        if (sscanf(line, " fft_size = %d", &value) == 1) {
            if (value == 512 || value == 1024 || value == 2048) {
                *out_fft_size = value;
                fprintf(stderr, "ligase~: Loaded fft_size = %d from ligase.conf\n", value);
            } else {
                fprintf(stderr, "ligase~: Warning: unsupported fft_size %d, using 1024\n", value);
            }
        } else if (sscanf(line, " overlap_factor = %d", &value) == 1) {
            if (value == 2 || value == 4 || value == 8) {
                *out_overlap_factor = value;
                fprintf(stderr, "ligase~: Loaded overlap_factor = %d from ligase.conf\n", value);
            } else {
                fprintf(stderr, "ligase~: Warning: unsupported overlap_factor %d, using 4\n", value);
            }
        }
    }
    fclose(f);
}

// @endregion:ligase_pd.core.grain.fog.config

// Create fog effect processor.
// Pass fft_size <= 0 or overlap_factor <= 0 to read those values from ligase.conf.
grain_fog_t* grain_fog_create(int sample_rate, int fft_size, int overlap_factor) {
    // Resolve fft_size and overlap_factor from config when not explicitly provided
    int cfg_fft_size, cfg_overlap_factor;
    read_fog_config(&cfg_fft_size, &cfg_overlap_factor);
    if (fft_size <= 0)      fft_size      = cfg_fft_size;
    if (overlap_factor <= 0) overlap_factor = cfg_overlap_factor;

    grain_fog_t *fog = (grain_fog_t*)calloc(1, sizeof(grain_fog_t));
    if (!fog) return NULL;

    fog->magic = FOG_MAGIC;
    fog->sample_rate = sample_rate;
    fog->fft_size = fft_size;
    fog->overlap_factor = overlap_factor;
    fog->hop_size = fft_size / overlap_factor;
    fog->num_bins = fft_size / 2 + 1;

    // Default parameters — tuned for a smooth, lush "dreamy fog": a gentle spectral haze with
    // ghostly (not ringy) sustain and stereo width. Off (mix 0) until the fog inlet is raised.
    fog->mix = 0.0f;
    fog->smear_enabled = 1;
    fog->smear_bins = 3;             // gentle frequency haze (8 was a washy ~750 Hz blur)
    fog->smear_onset_curve = FOG_ONSET_LOGARITHMIC;
    fog->smear_onset_amount = 0.8f;  // lush at full mix without smearing to mush

    fog->specmagfilter_enabled = 1;
    fog->mag_cutoff_hz = 2.5f;       // slow magnitude tracking = spectral sustain (the "fog")
    fog->mag_resonance = 0.5f;       // low Q: smooth ghostly persistence, no metallic ring
    fog->phase_cutoff_hz = 3.0f;     // slight phase motion so it isn't frozen/static
    fog->specmagfilter_onset_curve = FOG_ONSET_LOGARITHMIC;
    fog->specmagfilter_onset_amount = 1.0f;

    // Allocate per-bin state arrays (left channel + right-channel duplicates for
    // independent stereo mode; both are always allocated so the mode can be
    // switched at runtime without reallocation)
    fog->mag_z1 = (float*)calloc(fog->num_bins, sizeof(float));
    fog->mag_z2 = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phase_prev = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phase_delta_z1 = (float*)calloc(fog->num_bins, sizeof(float));
    fog->mag_z1_right = (float*)calloc(fog->num_bins, sizeof(float));
    fog->mag_z2_right = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phase_prev_right = (float*)calloc(fog->num_bins, sizeof(float));
    fog->phase_delta_z1_right = (float*)calloc(fog->num_bins, sizeof(float));

    fog->stereo_filter_independent = 1;  // default: independent L/R spectral state = stereo width

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
    fog->scratch = (float*)calloc(fft_size, sizeof(float));

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

    // Compute COLA normalization factor from the window rather than hardcoding 1.5.
    // Sum w[n]^2 at one sample position across all overlapping frames.
    // For overlap-add to be unity-gain: norm = 1 / (fft_size * cola_sum).
    // (kissfft's unscaled IFFT contributes a factor of fft_size, COLA handles the rest.)
    {
        float cola_sum = 0.0f;
        for (int k = 0; k < fog->overlap_factor; k++) {
            // Window contribution at sample position 0 from frame k hops back
            int idx = ((-k * fog->hop_size) % fft_size + fft_size) % fft_size;
            float w = fog->window[idx];
            cola_sum += w * w;
        }
        fog->cola_norm_factor = 1.0f / (cola_sum * (float)fft_size);
    }

    // Initialize overlap-add state
    fog->input_pos = 0;
    fog->samples_until_process = fft_size;  // Process after filling first full frame
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
    free(fog->mag_z1_right);
    free(fog->mag_z2_right);
    free(fog->phase_prev_right);
    free(fog->phase_delta_z1_right);
    free(fog->magnitudes);
    free(fog->phases);
    free(fog->smeared_mags);
    free(fog->filtered_mags);
    free(fog->filtered_phases);
    free(fog->fft_real_left);
    free(fog->fft_imag_left);
    free(fog->fft_real_right);
    free(fog->fft_imag_right);
    free(fog->scratch);

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

// Stereo filter mode
void grain_fog_set_stereo_filter_mode(grain_fog_t *fog, int independent) {
    if (!fog || fog->magic != FOG_MAGIC) return;
    fog->stereo_filter_independent = independent ? 1 : 0;
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

// Process a single FFT frame with spectral effects.
// channel: 0 = left, 1 = right — used to select per-channel filter state when
// stereo_filter_independent is enabled.
static void process_fft_frame(grain_fog_t *fog, float *input, float *output,
                              kiss_fft_cpx *bins, int channel) {
    float *scratch = fog->scratch;

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

    // Stage 2: Vertical filter (time-axis resonant lowpass on magnitudes + phase smoothing).
    // In independent mode each channel uses its own state arrays; in shared mode both
    // channels use the left-channel arrays (preserving the original interleaved behaviour).
    if (fog->specmagfilter_enabled) {
        float specmag_amt = calculate_onset_amount(fog->mix,
            fog->specmagfilter_onset_curve, fog->specmagfilter_onset_amount);

        float *mag_z1, *mag_z2, *ph_prev, *ph_delta;
        if (fog->stereo_filter_independent && channel == 1) {
            mag_z1   = fog->mag_z1_right;
            mag_z2   = fog->mag_z2_right;
            ph_prev  = fog->phase_prev_right;
            ph_delta = fog->phase_delta_z1_right;
        } else {
            mag_z1   = fog->mag_z1;
            mag_z2   = fog->mag_z2;
            ph_prev  = fog->phase_prev;
            ph_delta = fog->phase_delta_z1;
        }

        filter_magnitudes(fog, specmag_amt, mag_z1, mag_z2);
        filter_phases(fog, specmag_amt, ph_prev, ph_delta);
    } else {
        memcpy(fog->filtered_mags, fog->smeared_mags, fog->num_bins * sizeof(float));
        memcpy(fog->filtered_phases, fog->phases, fog->num_bins * sizeof(float));
    }

    // Convert back to Cartesian (real/imag)
    polar_to_complex(fog, bins, fog->filtered_mags, fog->filtered_phases);

    // Inverse FFT (complex → real), reuse scratch buffer
    kiss_fftri(fog->fft_inverse, bins, scratch);

    // Apply synthesis window and normalize.
    // kissfft round-trip gain = N (unscaled IFFT). COLA sum cancels the windowing.
    // norm_factor = 1 / (N * COLA_sum), computed at init from the actual window.
    const float norm_factor = fog->cola_norm_factor;
    for (int i = 0; i < fog->fft_size; i++) {
        float v = scratch[i] * fog->window[i] * norm_factor;
        // Firewall: never let a non-finite spectral result enter the persistent overlap-add
        // buffer, where it would self-sustain (x + NaN = NaN forever) and re-enter the FFT.
        output[i] = isfinite(v) ? v : 0.0f;
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

    const int hop_size = fog->hop_size;

    // Process each sample in the block
    for (int i = 0; i < blocksize; i++) {
        // 1. WRITE INPUT: Add sample to input buffer
        fog->input_buffer_left[fog->input_pos] = in_left[i];
        fog->input_buffer_right[fog->input_pos] = in_right[i];
        fog->input_pos++;

        // 2. COUNTDOWN: Decrement process counter
        fog->samples_until_process--;

        // 3. READ OUTPUT: Read from accumulation buffer BEFORE adding new frame
        // This ensures each output sample sees exactly 4 overlapping frame
        // contributions (correct COLA). Reading after the add would give
        // position hop_size-1 an extra frame contribution, causing gain ripple.
        float wet_left, wet_right;

        // Need overlap_factor frames before output is fully "baked"
        // (each position has contributions from all overlapping windows)
        if (fog->frames_processed >= fog->overlap_factor) {
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

        // 4. SHIFT OUTPUT: When we've consumed hop_size samples, shift accumulation buffer
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

        // 5. PROCESS: When countdown hits 0, process FFT frame
        // Done AFTER reading output so the new frame's contributions
        // go to future output positions, not the current one
        if (fog->samples_until_process <= 0) {
            // Process left channel FFT frame (channel 0)
            process_fft_frame(fog, fog->input_buffer_left,
                            fog->fft_real_left, fog->fft_bins_left, 0);

            // Process right channel FFT frame (channel 1)
            process_fft_frame(fog, fog->input_buffer_right,
                            fog->fft_real_right, fog->fft_bins_right, 1);

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
            fog->input_pos = fog->fft_size - hop_size;

            // Reset process countdown
            fog->samples_until_process = hop_size;

            // Track that we've processed a frame
            fog->frames_processed++;
        }

        // 6. CROSSFADE: Mix dry and wet
        out_left[i] = in_left[i] * dry_gain + wet_left * wet_gain;
        out_right[i] = in_right[i] * dry_gain + wet_right * wet_gain;
    }
}

// @endregion:ligase_pd.core.grain.fog.process

// @region:ligase_pd.core.grain.fog.pool Per-Grain Fog Pool Implementation

// Read fog_pool_size from ligase.conf (same pattern as read_fog_config)
static int read_fog_pool_size(void) {
    int pool_size = FOG_POOL_DEFAULT_SLOTS;

    FILE *f = fopen("ligase.conf", "r");
    if (!f) return pool_size;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        int value;
        if (sscanf(line, " fog_pool_size = %d", &value) == 1) {
            if (value >= 1 && value <= FOG_POOL_MAX_SLOTS) {
                pool_size = value;
                fprintf(stderr, "ligase~: Loaded fog_pool_size = %d from ligase.conf\n", value);
            } else {
                fprintf(stderr, "ligase~: Warning: fog_pool_size %d out of range (1-%d), using %d\n",
                        value, FOG_POOL_MAX_SLOTS, FOG_POOL_DEFAULT_SLOTS);
            }
        }
    }
    fclose(f);
    return pool_size;
}

fog_pool_t* fog_pool_create(int num_slots, int sample_rate, int fft_size, int overlap_factor) {
    // Read from config if num_slots not specified
    if (num_slots <= 0) num_slots = read_fog_pool_size();

    // Clamp to valid range
    if (num_slots < 1) num_slots = 1;
    if (num_slots > FOG_POOL_MAX_SLOTS) num_slots = FOG_POOL_MAX_SLOTS;

    fog_pool_t *pool = (fog_pool_t*)calloc(1, sizeof(fog_pool_t));
    if (!pool) return NULL;

    pool->num_slots = num_slots;
    pool->next_slot = 0;
    pool->position_mode = 1;  // Default: post-mix (existing behavior)

    for (int i = 0; i < num_slots; i++) {
        pool->slots[i].fog = grain_fog_create(sample_rate, fft_size, overlap_factor);
        if (!pool->slots[i].fog) {
            fprintf(stderr, "ligase~: ERROR: Failed to create fog instance for slot %d\n", i);
            // Clean up already-created slots
            for (int j = 0; j < i; j++) {
                grain_fog_destroy(pool->slots[j].fog);
            }
            free(pool);
            return NULL;
        }
        // Pre-allocate accumulators for max block size (avoids malloc on audio thread)
        int prealloc_blocksize = 8192;
        pool->slots[i].accum_left = (float*)calloc(prealloc_blocksize, sizeof(float));
        pool->slots[i].accum_right = (float*)calloc(prealloc_blocksize, sizeof(float));
        if (!pool->slots[i].accum_left || !pool->slots[i].accum_right) {
            // Clean up on failure
            for (int j = 0; j <= i; j++) {
                if (pool->slots[j].fog) grain_fog_destroy(pool->slots[j].fog);
                free(pool->slots[j].accum_left);
                free(pool->slots[j].accum_right);
            }
            free(pool);
            return NULL;
        }
        pool->slots[i].accum_size = prealloc_blocksize;
    }

    fprintf(stderr, "ligase~: fog pool created with %d slots (position_mode=post-mix)\n", num_slots);
    return pool;
}

void fog_pool_destroy(fog_pool_t *pool) {
    if (!pool) return;

    for (int i = 0; i < pool->num_slots; i++) {
        if (pool->slots[i].fog) grain_fog_destroy(pool->slots[i].fog);
        free(pool->slots[i].accum_left);
        free(pool->slots[i].accum_right);
    }
    free(pool);
}

void fog_pool_resize_accumulators(fog_pool_t *pool, int blocksize) {
    if (!pool) return;

    // Accumulators are pre-allocated in fog_pool_create for max block size (8192).
    // This function is now a safety fallback only — no allocation should occur
    // during normal operation since blocksize <= 8192 is enforced by ligase_perform.
    for (int i = 0; i < pool->num_slots; i++) {
        if (pool->slots[i].accum_size < blocksize) {
            // Should not happen in normal operation — log and skip to avoid
            // malloc on the audio thread
            fprintf(stderr, "fog_pool_resize_accumulators: WARNING - blocksize %d exceeds pre-allocated %d\n",
                    blocksize, pool->slots[i].accum_size);
        }
    }
}

void fog_pool_clear_accumulators(fog_pool_t *pool, int blocksize) {
    if (!pool) return;

    for (int i = 0; i < pool->num_slots; i++) {
        if (pool->slots[i].accum_left) {
            memset(pool->slots[i].accum_left, 0, blocksize * sizeof(float));
        }
        if (pool->slots[i].accum_right) {
            memset(pool->slots[i].accum_right, 0, blocksize * sizeof(float));
        }
    }
}

void fog_pool_process(fog_pool_t *pool, float *out_left, float *out_right, int blocksize) {
    if (!pool) return;

    // Temporary buffers for each slot's fog output
    float *temp_left = (float*)calloc(blocksize, sizeof(float));
    float *temp_right = (float*)calloc(blocksize, sizeof(float));
    if (!temp_left || !temp_right) {
        free(temp_left);
        free(temp_right);
        return;
    }

    for (int i = 0; i < pool->num_slots; i++) {
        fog_slot_t *slot = &pool->slots[i];
        if (!slot->fog || !slot->accum_left || !slot->accum_right) continue;

        // Process this slot's accumulated grain audio through its fog instance
        grain_fog_process_block(slot->fog,
                                slot->accum_left, slot->accum_right,
                                temp_left, temp_right,
                                blocksize);

        // Sum into output
        for (int j = 0; j < blocksize; j++) {
            out_left[j] += temp_left[j];
            out_right[j] += temp_right[j];
        }
    }

    free(temp_left);
    free(temp_right);
}

int fog_pool_assign_slot(fog_pool_t *pool) {
    if (!pool || pool->num_slots <= 0) return -1;
    int slot = pool->next_slot % pool->num_slots;
    pool->next_slot++;
    return slot;
}

// Parameter forwarding wrappers — apply setting to all fog instances in the pool

void fog_pool_set_mix(fog_pool_t *pool, float mix) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_mix(pool->slots[i].fog, mix);
}

void fog_pool_set_smear_bins(fog_pool_t *pool, int bins) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_smear_bins(pool->slots[i].fog, bins);
}

void fog_pool_set_smear_enabled(fog_pool_t *pool, int enabled) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_smear_enabled(pool->slots[i].fog, enabled);
}

void fog_pool_set_smear_onset_curve(fog_pool_t *pool, fog_onset_curve_t curve) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_smear_onset_curve(pool->slots[i].fog, curve);
}

void fog_pool_set_smear_onset_amount(fog_pool_t *pool, float amount) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_smear_onset_amount(pool->slots[i].fog, amount);
}

void fog_pool_set_mag_cutoff(fog_pool_t *pool, float cutoff_hz) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_mag_cutoff(pool->slots[i].fog, cutoff_hz);
}

void fog_pool_set_mag_resonance(fog_pool_t *pool, float q) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_mag_resonance(pool->slots[i].fog, q);
}

void fog_pool_set_phase_cutoff(fog_pool_t *pool, float cutoff_hz) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_phase_cutoff(pool->slots[i].fog, cutoff_hz);
}

void fog_pool_set_specmagfilter_enabled(fog_pool_t *pool, int enabled) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_specmagfilter_enabled(pool->slots[i].fog, enabled);
}

void fog_pool_set_specmagfilter_onset_curve(fog_pool_t *pool, fog_onset_curve_t curve) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_specmagfilter_onset_curve(pool->slots[i].fog, curve);
}

void fog_pool_set_specmagfilter_onset_amount(fog_pool_t *pool, float amount) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_specmagfilter_onset_amount(pool->slots[i].fog, amount);
}

void fog_pool_set_stereo_filter_mode(fog_pool_t *pool, int independent) {
    if (!pool) return;
    for (int i = 0; i < pool->num_slots; i++)
        grain_fog_set_stereo_filter_mode(pool->slots[i].fog, independent);
}

// @endregion:ligase_pd.core.grain.fog.pool

// @endregion:ligase_pd.core.grain.fog
