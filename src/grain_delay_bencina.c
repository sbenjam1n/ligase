// @region:ligase_pd.core.grain.delay_bencina Bencina Mode (Pitch-Preserving Grain Delay)

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Forward declaration for envelope functions
extern float envelope_sample(envelope_t *env, float phase);

// @region:ligase_pd.core.grain.delay_bencina.types Bencina Type Management

// Create bencina mode processor
grain_delay_bencina_t* grain_delay_bencina_create(envelope_t *envelope, int sample_rate) {
    grain_delay_bencina_t *bencina = (grain_delay_bencina_t*)malloc(sizeof(grain_delay_bencina_t));
    if (!bencina) return NULL;

    bencina->sample_rate = sample_rate;
    bencina->envelope = envelope;  // Share envelope with main scheduler

    // Initialize parameters
    bencina->grain_spacing_ms = 50.0f;  // Default 50ms IOT (20 grains/second)
    bencina->grain_size = 0.1f;          // Default 100ms grain size

    // Initialize grain pool
    bencina->num_active_grains = 0;
    memset(bencina->grain_pool, 0, sizeof(bencina->grain_pool));

    // Initialize triggering state
    bencina->samples_until_next_grain = 0;
    bencina->trigger_period_samples = (int)((bencina->grain_spacing_ms / 1000.0f) * sample_rate);

    return bencina;
}

// Destroy bencina mode processor
void grain_delay_bencina_destroy(grain_delay_bencina_t *bencina) {
    if (bencina) {
        free(bencina);
    }
}

// @endregion:ligase_pd.core.grain.delay_bencina.types

// @region:ligase_pd.core.grain.delay_bencina.grain_pool Bencina Grain Pool

// Find free grain in pool
static grain_bencina_grain_t* get_free_grain(grain_delay_bencina_t *bencina) {
    for (int i = 0; i < MAX_BENCINA_GRAINS; i++) {
        if (!bencina->grain_pool[i].active) {
            return &bencina->grain_pool[i];
        }
    }
    return NULL;  // Pool exhausted
}

// Trigger a new bencina grain
// This grain will follow the write head at a fixed distance (the "moving tap")
static void trigger_bencina_grain(grain_delay_bencina_t *bencina,
                                  grain_delay_t *delay,
                                  uint32_t splice_start,
                                  uint32_t splice_end) {
    grain_bencina_grain_t *g = get_free_grain(bencina);
    if (!g) return;  // No free grains

    // KEY DIFFERENCE FROM STUT:
    // Bencina grains store the RELATIVE OFFSET (delay distance), not an absolute position
    // The read position will be calculated every sample as: write_head - read_offset
    float delay_samples = delay->delay_time * delay->sample_rate;

    // Initialize grain
    g->active = 1;
    g->read_offset = delay_samples;     // RELATIVE offset from write head
    g->phase = 0.0f;                    // Start of envelope
    g->grain_length = bencina->grain_size * bencina->sample_rate;
    g->amplitude = 1.0f;                // Full amplitude (mix controlled globally)
    g->pan = 0.5f;                      // Center pan
    g->splice_start = splice_start;
    g->splice_end = splice_end;
    g->wrap_mode = 1;                   // Default: splice wrap mode

    bencina->num_active_grains++;
}

// @endregion:ligase_pd.core.grain.delay_bencina.grain_pool

// @region:ligase_pd.core.grain.delay_bencina.process Bencina Processing

// Process bencina mode with TRUE FEEDBACK (Ross Bencina architecture)
// This is the key: grain output is fed back into the buffer BEFORE write head advances
void grain_delay_bencina_process(grain_delay_bencina_t *bencina,
                                 grain_delay_t *delay,
                                 float *in_left,
                                 float *in_right,
                                 float *out_left,
                                 float *out_right,
                                 int blocksize,
                                 uint32_t splice_start,
                                 uint32_t splice_end) {
    if (!bencina || !delay) return;

    // Calculate splice length for wrapping
    uint32_t splice_length = (splice_end > splice_start) ? (splice_end - splice_start) : 1;

    // Process each sample in the block
    for (int s = 0; s < blocksize; s++) {
        // Check if we should trigger a new grain
        bencina->samples_until_next_grain--;
        if (bencina->samples_until_next_grain <= 0) {
            trigger_bencina_grain(bencina, delay, splice_start, splice_end);
            bencina->samples_until_next_grain = bencina->trigger_period_samples;
        }

        // Sum all active grains (the "delayed signal")
        float grain_sum_left = 0.0f;
        float grain_sum_right = 0.0f;

        for (int i = 0; i < MAX_BENCINA_GRAINS; i++) {
            grain_bencina_grain_t *g = &bencina->grain_pool[i];
            if (!g->active) continue;

            // CORE BENCINA LOGIC: Calculate read position RELATIVE to current write head
            // This is the "moving tap" - it follows the write head at a fixed distance
            float read_pos_float = delay->write_pos - g->read_offset;

            // Handle wrapping based on mode
            if (g->wrap_mode == 1) {
                // SPLICE WRAP MODE: Wrap within current splice bounds
                // If delay > splice length, wrap within the splice (creates phasing/layering)

                // Convert global buffer position to splice-relative position
                while (read_pos_float < 0) {
                    read_pos_float += delay->buffer_size;
                }

                // Map to splice coordinates
                int global_read_pos = (int)read_pos_float % delay->buffer_size;

                // If read position is outside current splice, wrap within splice
                if (global_read_pos < (int)splice_start || global_read_pos >= (int)splice_end) {
                    // Calculate how far back we're trying to read
                    int offset_from_start = ((int)delay->write_pos - global_read_pos);
                    if (offset_from_start < 0) offset_from_start += delay->buffer_size;

                    // Wrap within splice length
                    int splice_relative_offset = offset_from_start % splice_length;
                    global_read_pos = (int)splice_start + splice_relative_offset;
                }

                read_pos_float = (float)global_read_pos;
            } else {
                // GLOBAL WRAP MODE: Wrap within entire buffer (ignores splice boundaries)
                // This creates "ghost" effects where grains can read from previous splices
                while (read_pos_float < 0) {
                    read_pos_float += delay->buffer_size;
                }
                while (read_pos_float >= delay->buffer_size) {
                    read_pos_float -= delay->buffer_size;
                }
            }

            // Integer and fractional parts for linear interpolation
            int read_pos = (int)read_pos_float;
            float frac = read_pos_float - read_pos;

            // Safety bounds check
            read_pos = read_pos % delay->buffer_size;
            if (read_pos < 0) read_pos += delay->buffer_size;
            int read_pos_next = (read_pos + 1) % delay->buffer_size;

            // Read from delay buffer with linear interpolation
            float sample_left = delay->buffer_left[read_pos] * (1.0f - frac) +
                               delay->buffer_left[read_pos_next] * frac;
            float sample_right = delay->buffer_right[read_pos] * (1.0f - frac) +
                                delay->buffer_right[read_pos_next] * frac;

            // Calculate envelope amplitude
            float env_phase = g->phase / g->grain_length;  // Normalize to 0.0-1.0
            if (env_phase > 1.0f) env_phase = 1.0f;
            float env_amp = envelope_sample(bencina->envelope, env_phase);

            // Apply envelope and amplitude
            sample_left *= env_amp * g->amplitude;
            sample_right *= env_amp * g->amplitude;

            // Constant-power panning
            float pan_left = cosf(g->pan * M_PI * 0.5f);
            float pan_right = sinf(g->pan * M_PI * 0.5f);

            grain_sum_left += sample_left * pan_left;
            grain_sum_right += sample_right * pan_right;

            // Advance grain state
            // KEY: Grain phase advances, but read_offset stays CONSTANT
            // This means the grain always reads at the same distance from write head
            g->phase += 1.0f;

            // Deactivate grain when envelope completes
            if (g->phase >= g->grain_length) {
                g->active = 0;
                bencina->num_active_grains--;
            }
        }

        // TRUE FEEDBACK (Ross Bencina architecture):
        // Mix grain output back into the buffer BEFORE write head advances
        // This creates the "shimmer" where delayed grains get re-granulated

        // Apply low-pass filter to grain output (same as DD-4)
        float lpf_coeff = delay->tone;
        float filtered_left = lpf_coeff * grain_sum_left + (1.0f - lpf_coeff) * delay->lpf_state_left;
        float filtered_right = lpf_coeff * grain_sum_right + (1.0f - lpf_coeff) * delay->lpf_state_right;

        delay->lpf_state_left = filtered_left;
        delay->lpf_state_right = filtered_right;

        // Feedback: Mix filtered grain output with input
        float feedback_left = filtered_left * delay->feedback;
        float feedback_right = filtered_right * delay->feedback;

        // Write to buffer: input + feedback (Sound-on-Sound)
        delay->buffer_left[delay->write_pos] = in_left[s] + feedback_left;
        delay->buffer_right[delay->write_pos] = in_right[s] + feedback_right;

        // Advance write position
        delay->write_pos = (delay->write_pos + 1) % delay->buffer_size;

        // Output: dry/wet mix (grain sum is the wet signal)
        out_left[s] = in_left[s] * (1.0f - delay->mix) + filtered_left * delay->mix;
        out_right[s] = in_right[s] * (1.0f - delay->mix) + filtered_right * delay->mix;
    }
}

// @endregion:ligase_pd.core.grain.delay_bencina.process

// @region:ligase_pd.core.grain.delay_bencina.messages Bencina Message Interface

// Set grain spacing (IOT) in milliseconds
void grain_delay_bencina_set_spacing(grain_delay_bencina_t *bencina, float spacing_ms) {
    if (bencina) {
        if (spacing_ms < 1.0f) spacing_ms = 1.0f;
        if (spacing_ms > 1000.0f) spacing_ms = 1000.0f;  // Max 1 second
        bencina->grain_spacing_ms = spacing_ms;
        bencina->trigger_period_samples = (int)((spacing_ms / 1000.0f) * bencina->sample_rate);
    }
}

// Set grain size in seconds
void grain_delay_bencina_set_grain_size(grain_delay_bencina_t *bencina, float size_seconds) {
    if (bencina) {
        if (size_seconds < 0.001f) size_seconds = 0.001f;  // Min 1ms
        if (size_seconds > 2.0f) size_seconds = 2.0f;       // Max 2 seconds
        bencina->grain_size = size_seconds;
    }
}

// Set wrap mode (0=global, 1=splice)
void grain_delay_bencina_set_wrap_mode(grain_delay_bencina_t *bencina, int mode) {
    if (bencina) {
        // Update wrap mode for future grains
        // Note: Existing grains keep their mode until they finish
        for (int i = 0; i < MAX_BENCINA_GRAINS; i++) {
            bencina->grain_pool[i].wrap_mode = mode;
        }
    }
}

// Clear all active grains
void grain_delay_bencina_clear(grain_delay_bencina_t *bencina) {
    if (bencina) {
        memset(bencina->grain_pool, 0, sizeof(bencina->grain_pool));
        bencina->num_active_grains = 0;
    }
}

// @endregion:ligase_pd.core.grain.delay_bencina.messages

// @endregion:ligase_pd.core.grain.delay_bencina
