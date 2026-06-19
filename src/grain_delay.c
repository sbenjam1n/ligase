// @region:ligase_pd.core.grain.delay Grain Output Delay (DD-4 Style)

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Forward declarations for mode-specific functions
extern grain_delay_stut_t* grain_delay_stut_create(int sample_rate);
extern void grain_delay_stut_destroy(grain_delay_stut_t *stut);
extern void grain_delay_stut_process(grain_delay_stut_t *stut, grain_delay_t *delay, float *out_left, float *out_right, int blocksize);

extern grain_delay_bencina_t* grain_delay_bencina_create(envelope_t *envelope, int sample_rate);
extern void grain_delay_bencina_destroy(grain_delay_bencina_t *bencina);
extern void grain_delay_bencina_process(grain_delay_bencina_t *bencina, grain_delay_t *delay, float *in_left, float *in_right, float *out_left, float *out_right, int blocksize, uint32_t splice_start, uint32_t splice_end, float pan_base, param_range_t *pan_range, perlin_state_t *pan_perlin);

// Create a new grain delay processor
grain_delay_t* grain_delay_create(int sample_rate) {
    grain_delay_t *delay = (grain_delay_t*)malloc(sizeof(grain_delay_t));
    if (!delay) return NULL;  // Memory allocation failed

    delay->sample_rate = sample_rate;

    // Maximum delay time: 9.5 seconds at 48kHz
    delay->buffer_size = (int)(9.5f * sample_rate);
    delay->buffer_left = (float*)calloc(delay->buffer_size, sizeof(float));
    if (!delay->buffer_left) {
        free(delay);
        return NULL;  // Left buffer allocation failed
    }

    delay->buffer_right = (float*)calloc(delay->buffer_size, sizeof(float));
    if (!delay->buffer_right) {
        free(delay->buffer_left);
        free(delay);
        return NULL;  // Right buffer allocation failed
    }

    delay->write_pos = 0;

    // Initialize mode
    delay->mode = DELAY_MODE_DD4;  // Default to DD-4 mode

    // Initialize parameters
    delay->delay_time = 0.0f;     // Default off (0ms delay)
    delay->current_delay_samples = 0.0f * sample_rate;  // Initialize smoothed delay
    delay->feedback = 0.0f;       // Default no feedback
    delay->tone = 0.5f;           // Default neutral tone
    delay->mix = 0.0f;            // Default dry (effect off)
    delay->delay_glide_ms = 20.0f; // Default ~20ms glide on delay-time changes (de-zippers the tap)

    // Initialize filter state
    delay->lpf_state_left = 0.0f;
    delay->lpf_state_right = 0.0f;

    return delay;
}

// Reallocate the delay line for a new sample rate so it always holds 9.5 s of delay
// (the buffer was previously sized once at the construction-time rate, so long delays
// were silently clamped at non-48k rates). Resets transient state. Call from the dsp
// method (main thread), never the audio thread.
void grain_delay_set_sample_rate(grain_delay_t *delay, int sample_rate) {
    if (!delay || sample_rate <= 0) return;
    if (sample_rate == delay->sample_rate && delay->buffer_left && delay->buffer_right) return;

    int new_size = (int)(9.5f * sample_rate);
    float *nl = (float*)calloc(new_size, sizeof(float));
    float *nr = (float*)calloc(new_size, sizeof(float));
    if (!nl || !nr) {
        // Allocation failed: keep the existing buffer, only update the rate scalar
        free(nl);
        free(nr);
        delay->sample_rate = sample_rate;
        return;
    }
    free(delay->buffer_left);
    free(delay->buffer_right);
    delay->buffer_left = nl;
    delay->buffer_right = nr;
    delay->buffer_size = new_size;
    delay->sample_rate = sample_rate;

    // Old read offsets / feedback tail are invalid against the resized buffer
    delay->write_pos = 0;
    delay->current_delay_samples = delay->delay_time * sample_rate;
    delay->lpf_state_left = 0.0f;
    delay->lpf_state_right = 0.0f;
}

// Set delay mode
void grain_delay_set_mode(grain_delay_t *delay, grain_delay_mode_t mode) {
    if (delay) {
        delay->mode = mode;
    }
}

// Destroy grain delay processor
void grain_delay_destroy(grain_delay_t *delay) {
    if (delay) {
        if (delay->buffer_left) free(delay->buffer_left);
        if (delay->buffer_right) free(delay->buffer_right);
        free(delay);
    }
}

// Internal DD-4 processing (separated for mode dispatching)
static void grain_delay_process_dd4(grain_delay_t *delay, float *in_left, float *in_right,
                                   float *out_left, float *out_right, int blocksize) {
    // Calculate target delay time in samples
    float target_delay_samples = delay->delay_time * delay->sample_rate;

    // Clamp target to valid range
    if (target_delay_samples < 1.0f) target_delay_samples = 1.0f;
    if (target_delay_samples >= delay->buffer_size) target_delay_samples = delay->buffer_size - 1.0f;

    // Calculate low-pass filter coefficient from tone parameter
    // tone = 0.0 -> heavily filtered (dark, analog-style)
    // tone = 1.0 -> no filtering (bright, digital-style)
    // Using one-pole IIR filter coefficient
    float lpf_coeff = delay->tone;

    // Glide: smooth current_delay_samples toward the target so the read tap doesn't jump when the
    // delay time changes (a moving tap repitches the signal — an abrupt move = zipper/click). The
    // glide time is user-set (delay_glide ms) and converted to a one-pole coefficient here. This
    // smooths BOTH message- and signal-inlet-driven delay-time changes (e.g. CV sweeping inlet 11).
    // glide 0 => coefficient 1.0 => instant (no glide). Larger glide => slower, cleaner sweep.
    float glide_samples = (delay->delay_glide_ms * 0.001f) * (float)delay->sample_rate;
    float smoothing = (glide_samples > 1.0f) ? (1.0f - expf(-1.0f / glide_samples)) : 1.0f;

    for (int i = 0; i < blocksize; i++) {
        // Smooth delay time interpolation to prevent clicks
        // Gradually move current_delay_samples toward target_delay_samples
        delay->current_delay_samples += (target_delay_samples - delay->current_delay_samples) * smoothing;

        // Calculate read position using smoothed delay time (supports fractional delay)
        float read_pos_float = delay->write_pos - delay->current_delay_samples;

        // Fold read_pos_float into [0, buffer_size) in O(1) — never hangs even if the smoothed
        // delay drives it non-finite or far out of range.
        if (!isfinite(read_pos_float)) read_pos_float = 0.0f;
        read_pos_float = fmodf(read_pos_float, (float)delay->buffer_size);
        if (read_pos_float < 0.0f) read_pos_float += (float)delay->buffer_size;

        // Integer and fractional parts for linear interpolation
        int read_pos = (int)read_pos_float;
        float frac = read_pos_float - read_pos;

        // Read delayed samples from buffer with linear interpolation
        // Get next sample position for interpolation
        // Apply modulo to both positions for safety (read_pos should already be in bounds,
        // but this provides defense-in-depth against edge cases)
        read_pos = read_pos % delay->buffer_size;
        int read_pos_next = (read_pos + 1) % delay->buffer_size;

        // Linear interpolation between current and next sample
        float delayed_left = delay->buffer_left[read_pos] * (1.0f - frac) +
                            delay->buffer_left[read_pos_next] * frac;
        float delayed_right = delay->buffer_right[read_pos] * (1.0f - frac) +
                             delay->buffer_right[read_pos_next] * frac;

        // Apply low-pass filter to delayed signal (analog-style decay)
        // One-pole IIR low-pass filter in feedback path
        // Formula - tone=1.0 is bright (no filter), tone=0.0 is dark (max filter)
        float filtered_left = lpf_coeff * delayed_left + (1.0f - lpf_coeff) * delay->lpf_state_left;
        float filtered_right = lpf_coeff * delayed_right + (1.0f - lpf_coeff) * delay->lpf_state_right;

        // Flush denormals AND non-finite (NaN/Inf) to zero — prevents both the CPU-eating
        // subnormal slowdown and a NaN/Inf poisoning the feedback buffer forever.
        if (!isfinite(filtered_left)  || fabsf(filtered_left)  < 1e-20f) filtered_left  = 0.0f;
        if (!isfinite(filtered_right) || fabsf(filtered_right) < 1e-20f) filtered_right = 0.0f;
        delay->lpf_state_left = filtered_left;
        delay->lpf_state_right = filtered_right;

        // Calculate feedback signal (filtered delayed signal * feedback amount)
        float feedback_left = filtered_left * delay->feedback;
        float feedback_right = filtered_right * delay->feedback;

        // Write to delay buffer: input + feedback (sound-on-sound)
        // This is the DD-4 style feedback loop
        // Soft-clip the buffer write to prevent unbounded feedback accumulation
        float write_left = in_left[i] + feedback_left;
        float write_right = in_right[i] + feedback_right;
        if (write_left > 4.0f) write_left = 4.0f;
        else if (write_left < -4.0f) write_left = -4.0f;
        if (write_right > 4.0f) write_right = 4.0f;
        else if (write_right < -4.0f) write_right = -4.0f;
        delay->buffer_left[delay->write_pos] = write_left;
        delay->buffer_right[delay->write_pos] = write_right;

        // Advance write position (circular buffer)
        delay->write_pos = (delay->write_pos + 1) % delay->buffer_size;

        // Output mix: dry input + wet delayed signal
        //  Use filtered signal in output for immediate analog character
        // mix = 0.0 -> 100% dry (grain output only)
        // mix = 1.0 -> 100% wet (delayed signal only)
        out_left[i] = in_left[i] * (1.0f - delay->mix) + filtered_left * delay->mix;
        out_right[i] = in_right[i] * (1.0f - delay->mix) + filtered_right * delay->mix;
    }
}

// Process audio through delay with mode dispatching
void grain_delay_process(grain_delay_t *delay,
                        grain_delay_stut_t *stut,
                        grain_delay_bencina_t *bencina,
                        float *in_left, float *in_right,
                        float *out_left, float *out_right,
                        int blocksize,
                        uint32_t splice_start,
                        uint32_t splice_end,
                        float bencina_pan_base,
                        param_range_t *bencina_pan_range,
                        perlin_state_t *bencina_pan_perlin) {
    if (!delay) return;

    // Guard a degenerate buffer (sample_rate 0 / realloc failure): the wrap loops and the
    // `% buffer_size` indexing below would otherwise hang the audio thread or divide by zero.
    if (delay->buffer_size <= 0) {
        for (int i = 0; i < blocksize; i++) { out_left[i] = in_left[i]; out_right[i] = in_right[i]; }
        return;
    }

    // Dispatch based on mode
    switch (delay->mode) {
        case DELAY_MODE_DD4:
            // DD-4 style analog delay (original behavior)
            grain_delay_process_dd4(delay, in_left, in_right, out_left, out_right, blocksize);
            break;

        case DELAY_MODE_BENCINA:
            // Bencina pitch-preserving grain delay with TRUE FEEDBACK
            if (bencina) {
                // Bencina mode handles buffer writing internally with feedback
                // It writes: buffer[write_pos] = input + (grain_output * feedback)
                grain_delay_bencina_process(bencina, delay, in_left, in_right, out_left, out_right, blocksize, splice_start, splice_end, bencina_pan_base, bencina_pan_range, bencina_pan_perlin);
            } else {
                // Bencina not initialized, pass through dry
                for (int i = 0; i < blocksize; i++) {
                    out_left[i] = in_left[i];
                    out_right[i] = in_right[i];
                }
            }
            break;

        case DELAY_MODE_STUT:
            // Stut quantized rhythmic delay
            if (stut) {
                // First, write input to buffer (stut reads from it)
                for (int i = 0; i < blocksize; i++) {
                    delay->buffer_left[delay->write_pos] = in_left[i];
                    delay->buffer_right[delay->write_pos] = in_right[i];
                    delay->write_pos = (delay->write_pos + 1) % delay->buffer_size;
                }

                // Initialize output to dry signal
                for (int i = 0; i < blocksize; i++) {
                    out_left[i] = in_left[i];
                    out_right[i] = in_right[i];
                }

                // Process stut grains (adds wet signal to output)
                grain_delay_stut_process(stut, delay, out_left, out_right, blocksize);

                // Apply mix
                for (int i = 0; i < blocksize; i++) {
                    float dry_left = in_left[i];
                    float dry_right = in_right[i];
                    float wet_left = out_left[i] - dry_left;  // Extract wet component
                    float wet_right = out_right[i] - dry_right;

                    out_left[i] = dry_left * (1.0f - delay->mix) + wet_left * delay->mix;
                    out_right[i] = dry_right * (1.0f - delay->mix) + wet_right * delay->mix;
                }
            } else {
                // Stut not initialized, pass through dry
                for (int i = 0; i < blocksize; i++) {
                    out_left[i] = in_left[i];
                    out_right[i] = in_right[i];
                }
            }
            break;

        default:
            // Unknown mode, pass through dry
            for (int i = 0; i < blocksize; i++) {
                out_left[i] = in_left[i];
                out_right[i] = in_right[i];
            }
            break;
    }
}

// Set delay time in seconds
void grain_delay_set_time(grain_delay_t *delay, float time_seconds) {
    if (delay) {
        if (time_seconds < 0.0f) time_seconds = 0.0f;
        if (time_seconds > 9.5f) time_seconds = 9.5f;
        delay->delay_time = time_seconds;
    }
}

// Glide time (ms) for smoothing delay-time changes (DD-4 de-zipper). 0 = instant.
void grain_delay_set_glide(grain_delay_t *delay, float glide_ms) {
    if (delay) {
        if (glide_ms < 0.0f) glide_ms = 0.0f;
        if (glide_ms > 5000.0f) glide_ms = 5000.0f;
        delay->delay_glide_ms = glide_ms;
    }
}

// Set feedback amount (0-1)
void grain_delay_set_feedback(grain_delay_t *delay, float feedback) {
    if (delay) {
        if (feedback < 0.0f) feedback = 0.0f;
        if (feedback > 1.0f) feedback = 1.0f;
        delay->feedback = feedback;
    }
}

// Set tone (low-pass filter coefficient, 0-1)
void grain_delay_set_tone(grain_delay_t *delay, float tone) {
    if (delay) {
        if (tone < 0.0f) tone = 0.0f;
        if (tone > 1.0f) tone = 1.0f;
        delay->tone = tone;
    }
}

// Set dry/wet mix (0-1)
void grain_delay_set_mix(grain_delay_t *delay, float mix) {
    if (delay) {
        if (mix < 0.0f) mix = 0.0f;
        if (mix > 1.0f) mix = 1.0f;
        delay->mix = mix;
    }
}

// Clear delay buffer
void grain_delay_clear(grain_delay_t *delay) {
    if (delay) {
        memset(delay->buffer_left, 0, delay->buffer_size * sizeof(float));
        memset(delay->buffer_right, 0, delay->buffer_size * sizeof(float));
        delay->lpf_state_left = 0.0f;
        delay->lpf_state_right = 0.0f;
    }
}

// @endregion:ligase_pd.core.grain.delay
