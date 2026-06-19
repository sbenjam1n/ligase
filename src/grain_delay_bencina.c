// @region:ligase_pd.core.grain.delay_bencina Bencina Mode (Pitch-Preserving Grain Delay)

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Forward declaration for envelope functions
extern float envelope_sample(envelope_t *env, float phase);
// Shared modulation sampler (grain.c) — used to pick each grain's pan from bencina_pan_range,
// exactly as the main granular engine picks its per-grain pan. Returns base_value when the range
// is disabled, else a value in [min,max] from the bound generator.
extern float sample_param_range(param_range_t *range, perlin_state_t *perlin_state, float base_value);

// Makeup gain driven INTO the tanh soft-limit on the Bencina wet output. Because the wet is
// tanh-limited (see process), this can sit well above unity to restore loudness WITHOUT clipping —
// tanh just rounds the transient peaks. The position scatter sums grains incoherently (quieter
// than a coherent stack), so the cloud was too quiet at the old 1.3x. 6.0x brings the level back
// (≈0.46 RMS / 0.89 peak at mix 1 with a steady tone), and it stays bounded to ±1 even with high
// feedback driving the buffer hot. Applied to the OUTPUT only — NOT the feedback path — so the
// recirculation loop gain stays = feedback (stable for feedback < 1). Tunable.
#define BENCINA_WET_GAIN 6.0f

// @region:ligase_pd.core.grain.delay_bencina.types Bencina Type Management

// Create bencina mode processor
grain_delay_bencina_t* grain_delay_bencina_create(envelope_t *envelope, int sample_rate) {
    grain_delay_bencina_t *bencina = (grain_delay_bencina_t*)malloc(sizeof(grain_delay_bencina_t));
    if (!bencina) return NULL;

    bencina->sample_rate = sample_rate;
    bencina->envelope = envelope;  // Share envelope with main scheduler

    // Initialize parameters
    bencina->grain_spacing_ms = 25.0f;  // Default 25ms IOT (denser ~4x-overlap cloud; level
                                        // is overlap-normalized so this sets texture, not gain)
    bencina->grain_size = 0.1f;          // Default 100ms grain size
    bencina->default_wrap_mode = 0;      // Default GLOBAL wrap (straight grain delay that works)
    bencina->scatter = 1.0f;             // Default: FULL position scatter (the grainy cloud
                                         // character). bencina_spread dials it down toward 0
                                         // (coherent/smoother) if wanted; skew remains the main
                                         // texture control.
    bencina->edge = 0.0f;                // Default: edge-round OFF — leave the envelope/skew edges
                                         // intact (the skew-edge clickiness is usable character).
                                         // bencina_edge > 0 rounds the grain in/out to de-click.

    // Initialize grain pool
    bencina->num_active_grains = 0;
    memset(bencina->grain_pool, 0, sizeof(bencina->grain_pool));

    // Initialize triggering state
    bencina->samples_until_next_grain = 0;
    bencina->trigger_period_samples = (int)((bencina->grain_spacing_ms / 1000.0f) * sample_rate);

    return bencina;
}

// Update sample rate: recompute the trigger period (it was frozen at the construction-time
// rate) and clear active grains, whose read offsets reference the shared delay buffer that
// may have just been resized. Call from the dsp method (main thread).
void grain_delay_bencina_set_sample_rate(grain_delay_bencina_t *bencina, int sample_rate) {
    if (!bencina || sample_rate <= 0) return;
    bencina->sample_rate = sample_rate;
    bencina->trigger_period_samples = (int)((bencina->grain_spacing_ms / 1000.0f) * sample_rate);
    bencina->samples_until_next_grain = 0;
    bencina->num_active_grains = 0;
    memset(bencina->grain_pool, 0, sizeof(bencina->grain_pool));
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
// Each grain captures its OWN read position into the delay line, scattered behind the base
// delay — this is what makes it a granular CLOUD (Bencina "tapped delay line" GS) rather than a
// plain delay.
static void trigger_bencina_grain(grain_delay_bencina_t *bencina,
                                  grain_delay_t *delay,
                                  uint32_t splice_start,
                                  uint32_t splice_end,
                                  float grain_pan) {
    grain_bencina_grain_t *g = get_free_grain(bencina);
    if (!g) return;  // No free grains

    // Bencina grains store a RELATIVE OFFSET (delay distance) from the write head; read position
    // is computed each sample as write_head - read_offset. Playback rate is fixed at 1.0 (the
    // offset is constant for the grain's life), so the grain reads the delayed stream WITHOUT
    // transposition — pitch in ligase is deliberately handled elsewhere (the allpass smear and the
    // morphagene tape speed), not in the delay.
    float delay_samples = delay->delay_time * delay->sample_rate;
    g->grain_length = bencina->grain_size * bencina->sample_rate;

    // Per-grain POSITION SCATTER (the cloud): without it every grain used the identical
    // read_offset = delay_samples, so all simultaneous grains read the SAME delayed sample and the
    // overlap-normalized sum collapsed to a plain delay (just envelope ripple). Scatter each
    // grain's tap by a random 0..one-grain-length further back, so overlapping grains read
    // different points of the recent past — a diffuse granular cloud. Scatter is tied to grain
    // size (bencina_grainsize), so a bigger grain = a wider cloud. No pitch change (rate stays 1).
    float jitter = ((float)rand() / (float)RAND_MAX) * g->grain_length * bencina->scatter;  // [0, scatter*grain_length)

    // Initialize grain
    g->active = 1;
    g->read_offset = delay_samples + jitter;  // captured, scattered per-grain tap
    g->phase = 0.0f;                    // Start of envelope
    g->amplitude = 1.0f;                // Full amplitude (mix controlled globally)
    g->pan = grain_pan;                 // per-grain pan: base (inlet 22) or, if bencina_pan_range
                                        // is enabled, a random per-grain position = stereo cloud
                                        // (constant-power panning applied in process)
    g->splice_start = splice_start;
    g->splice_end = splice_end;
    g->wrap_mode = bencina->default_wrap_mode;  // honor the configured wrap mode (was hardcoded 1)

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
                                 uint32_t splice_end,
                                 float pan_base,
                                 param_range_t *pan_range,
                                 perlin_state_t *pan_perlin) {
    if (!bencina || !delay) return;

    // Calculate splice length for wrapping
    uint32_t splice_length = (splice_end > splice_start) ? (splice_end - splice_start) : 1;

    // Overlap normalization: grain DENSITY (overlap = grain length / trigger spacing) should
    // shape TEXTURE, not LEVEL. Scale the grain sum back toward a ~2x-overlap reference so the wet
    // level (and the feedback loop gain) stay roughly consistent across any bencina_iot /
    // bencina_grainsize. Grains are now position-scattered (a diffuse cloud), so the sum is partly
    // incoherent and runs a little quieter than the old coherent stack — the makeup gain + tanh
    // soft-limit set the final level. (Applied before lpf/feedback.)
    float grain_len_s = bencina->grain_size * (float)bencina->sample_rate;
    float overlap = (bencina->trigger_period_samples > 0)
        ? grain_len_s / (float)bencina->trigger_period_samples : 2.0f;
    float gnorm = 2.0f / (overlap > 2.0f ? overlap : 2.0f);

    // Optional raised-cosine edge round (bencina_edge, default 0 = OFF). When > 0, ramps each grain
    // in/out from zero with zero slope over edge*0.5 grain lengths, ON TOP OF the envelope, to
    // de-click steep-skew / non-zero-edge (Gaussian/Exponential) windows. OFF by default so the
    // envelope's skew edges — including their clickiness — are left as a usable character.
    float edge_fade = bencina->edge * grain_len_s * 0.5f;  // 0 (off) .. half a grain

    // Process each sample in the block
    for (int s = 0; s < blocksize; s++) {
        // Check if we should trigger a new grain
        bencina->samples_until_next_grain--;
        if (bencina->samples_until_next_grain <= 0) {
            // Pick this grain's pan exactly like the main engine picks per-grain pan: from
            // bencina_pan_range if enabled (random in [min,max] = the stereo cloud), else the base
            // pan (inlet 22). pan_range may be NULL if not wired — fall back to the base then.
            float gp = pan_range ? sample_param_range(pan_range, pan_perlin, pan_base) : pan_base;
            trigger_bencina_grain(bencina, delay, splice_start, splice_end, gp);
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
            if (!isfinite(read_pos_float)) read_pos_float = 0.0f;

            // Handle wrapping. BOTH modes wrap within the DELAY BUFFER (a separate 9.5 s
            // circular history) relative to the write head — never in reel coordinates. The
            // splice_* args are REEL positions and previously indexed this buffer directly
            // (read = splice_start + offset), which read unwritten/silent regions of the delay
            // line for any splice not starting at buffer position 0 → no wet. (The bug.)
            if (g->wrap_mode == 1 && splice_length > 1 && (uint32_t)splice_length < (uint32_t)delay->buffer_size) {
                // LOOP WRAP: confine the read to the most recent `splice_length` samples behind
                // the write head, so the delay loops a short window (the phasing/layering intent),
                // but in delay-buffer space where the audio actually lives.
                float offset = (float)delay->write_pos - read_pos_float;
                offset = fmodf(offset, (float)delay->buffer_size);
                if (offset < 0.0f) offset += (float)delay->buffer_size;
                offset = fmodf(offset, (float)splice_length);
                read_pos_float = (float)delay->write_pos - offset;
                read_pos_float = fmodf(read_pos_float, (float)delay->buffer_size);
                if (read_pos_float < 0.0f) read_pos_float += (float)delay->buffer_size;
            } else {
                // GLOBAL WRAP: straight grain delay — wrap within the whole delay buffer.
                read_pos_float = fmodf(read_pos_float, (float)delay->buffer_size);
                if (read_pos_float < 0.0f) read_pos_float += (float)delay->buffer_size;
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
            float env_phase = (g->grain_length > 0.0f) ? (g->phase / g->grain_length) : 1.0f;
            if (env_phase > 1.0f) env_phase = 1.0f;
            float env_amp = envelope_sample(bencina->envelope, env_phase);

            // Raised-cosine edge round on top of the envelope: ramp 0->1 over the first edge_fade
            // samples and 1->0 over the last, so the grain always starts/ends at zero with zero
            // slope no matter what the main envelope does at its edges (de-clicks every onset).
            if (edge_fade > 0.0f) {
                if (g->phase < edge_fade) {
                    env_amp *= 0.5f * (1.0f - cosf((float)M_PI * g->phase / edge_fade));
                } else if (g->phase > g->grain_length - edge_fade) {
                    env_amp *= 0.5f * (1.0f - cosf((float)M_PI * (g->grain_length - g->phase) / edge_fade));
                }
            }

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

        // Normalize the summed grains for overlap so density = texture, not level.
        grain_sum_left  *= gnorm;
        grain_sum_right *= gnorm;

        // TRUE FEEDBACK (Ross Bencina architecture):
        // Mix grain output back into the buffer BEFORE write head advances
        // This creates the "shimmer" where delayed grains get re-granulated

        // Apply low-pass filter to grain output (same as DD-4)
        float lpf_coeff = delay->tone;
        float filtered_left = lpf_coeff * grain_sum_left + (1.0f - lpf_coeff) * delay->lpf_state_left;
        float filtered_right = lpf_coeff * grain_sum_right + (1.0f - lpf_coeff) * delay->lpf_state_right;

        // Flush denormals AND non-finite from the feedback state (DD-4 does this; Bencina didn't):
        // its recirculating loop otherwise accrued subnormals (CPU creep) or a stuck NaN/Inf.
        if (!isfinite(filtered_left)  || fabsf(filtered_left)  < 1e-20f) filtered_left  = 0.0f;
        if (!isfinite(filtered_right) || fabsf(filtered_right) < 1e-20f) filtered_right = 0.0f;
        delay->lpf_state_left = filtered_left;
        delay->lpf_state_right = filtered_right;

        // Feedback: Mix filtered grain output with input
        float feedback_left = filtered_left * delay->feedback;
        float feedback_right = filtered_right * delay->feedback;

        // Write to buffer: input + feedback (Sound-on-Sound)
        // Soft-clip to prevent unbounded feedback accumulation
        float write_left = in_left[s] + feedback_left;
        float write_right = in_right[s] + feedback_right;
        if (write_left > 4.0f) write_left = 4.0f;
        else if (write_left < -4.0f) write_left = -4.0f;
        if (write_right > 4.0f) write_right = 4.0f;
        else if (write_right < -4.0f) write_right = -4.0f;
        delay->buffer_left[delay->write_pos] = write_left;
        delay->buffer_right[delay->write_pos] = write_right;

        // Advance write position
        delay->write_pos = (delay->write_pos + 1) % delay->buffer_size;

        // Output: dry/wet mix. The wet gets BENCINA_WET_GAIN (output-only makeup) then a tanh
        // soft-limit so it stays prominent but can never exceed ±1 (the clipping fix). The
        // feedback path above uses the un-boosted, un-limited filtered signal, keeping the
        // recirculation loop stable (loop gain = feedback only).
        float wet_left  = tanhf(filtered_left  * BENCINA_WET_GAIN);
        float wet_right = tanhf(filtered_right * BENCINA_WET_GAIN);
        out_left[s]  = in_left[s]  * (1.0f - delay->mix) + wet_left  * delay->mix;
        out_right[s] = in_right[s] * (1.0f - delay->mix) + wet_right * delay->mix;
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

// Position-scatter amount 0..1 (fraction of a grain length each grain's tap is randomized back).
// 0 = coherent grains (smooth; stereo cloud from pan only); higher = grainier/more diffuse but
// rougher (the decorrelated-grain sum fluctuates more). Controls cloud diffusion vs smoothness.
void grain_delay_bencina_set_scatter(grain_delay_bencina_t *bencina, float amount) {
    if (bencina) {
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        bencina->scatter = amount;
    }
}

// Grain edge-round amount 0..1 (default 0 = off). 0 leaves the envelope/skew edges as-is
// (clickiness preserved); higher ramps each grain in/out over edge*0.5 grain lengths to de-click.
void grain_delay_bencina_set_edge(grain_delay_bencina_t *bencina, float amount) {
    if (bencina) {
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        bencina->edge = amount;
    }
}

// Set wrap mode (0=global, 1=splice)
void grain_delay_bencina_set_wrap_mode(grain_delay_bencina_t *bencina, int mode) {
    if (bencina) {
        // Store the default so newly triggered grains adopt it (trigger used to hardcode 1,
        // which made this setter inert). Also update currently-active grains.
        bencina->default_wrap_mode = mode;
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
