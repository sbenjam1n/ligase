// @region:ligase_pd.core.grain.delay_stut Stut Mode (Quantized Rhythmic Delay)

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// @region:ligase_pd.core.grain.delay_stut.types Stut Type Management

// Create stut mode processor
grain_delay_stut_t* grain_delay_stut_create(int sample_rate) {
    grain_delay_stut_t *stut = (grain_delay_stut_t*)malloc(sizeof(grain_delay_stut_t));
    if (!stut) return NULL;

    stut->sample_rate = sample_rate;

    // Initialize parameters
    stut->num_repetitions = 4;        // Default 4 repetitions (TidalCycles default)
    stut->gain_reduction = 0.5f;      // Default 50% gain reduction per repeat
    stut->spacing_ms = 62.5f;         // Default 1/16 note at 120 BPM (62.5ms)

    // Initialize grain pool
    stut->num_active_grains = 0;
    memset(stut->grain_pool, 0, sizeof(stut->grain_pool));

    return stut;
}

// Destroy stut mode processor
void grain_delay_stut_destroy(grain_delay_stut_t *stut) {
    if (stut) {
        free(stut);
    }
}

// @endregion:ligase_pd.core.grain.delay_stut.types

// @region:ligase_pd.core.grain.delay_stut.trigger Stut Trigger Logic

// Find a free (inactive) slot in the voice pool. Returns NULL when all MAX_STUT_GRAINS voices
// are busy. Mirrors Bencina's get_free_grain so overlapping triggers LAYER rather than clobber.
static grain_stut_grain_t* get_free_stut_grain(grain_delay_stut_t *stut) {
    for (int i = 0; i < MAX_STUT_GRAINS; i++) {
        if (!stut->grain_pool[i].active) return &stut->grain_pool[i];
    }
    return NULL;
}

// Trigger a stut sequence
// This captures the current buffer position and schedules N repetitions
void grain_delay_stut_trigger(grain_delay_stut_t *stut,
                               grain_delay_t *delay,
                               uint32_t splice_start,
                               uint32_t splice_end,
                               float quantized_spacing_ms,
                               float play_length_samples) {
    if (!stut || !delay) return;

    // Use quantized spacing if provided, otherwise use base spacing
    float spacing_ms = (quantized_spacing_ms > 0.0f) ? quantized_spacing_ms : stut->spacing_ms;
    float spacing_samples = (spacing_ms / 1000.0f) * stut->sample_rate;

    // Slice length each repeat replays. The caller computes it (independent stut_length /
    // BPM-quantized, or grainsize); fall back to the spacing if it wasn't supplied. Decoupled
    // from spacing, so length<spacing = gated (gaps), =spacing = gapless, >spacing = overlap.
    float play_length = (play_length_samples >= 1.0f) ? play_length_samples : spacing_samples;
    if (play_length > (float)delay->buffer_size) play_length = (float)delay->buffer_size;

    // Capture current write position in delay buffer
    float capture_position = (float)delay->write_pos;

    // Schedule this trigger's echo train into FREE pool slots (never fixed slots [0..reps-1]),
    // so a new trigger LAYERS on top of still-playing grains instead of clobbering them — the
    // bug behind "banging the trigger clicks and doesn't layer." reps within one train are
    // capped at MAX_STUT_REPS; the pool (MAX_STUT_GRAINS) caps total simultaneous voices across
    // all overlapping triggers.
    int reps = stut->num_repetitions;
    if (reps > MAX_STUT_REPS) reps = MAX_STUT_REPS;
    for (int rep = 0; rep < reps; rep++) {
        grain_stut_grain_t *g = get_free_stut_grain(stut);
        if (!g) break;  // Pool saturated by overlapping stutters — drop the extra repeats.

        g->active = 1;
        g->trigger_time_samples = rep * spacing_samples;  // Delay for each repetition in THIS train
        g->gain = powf(stut->gain_reduction, (float)rep); // Exponential decay within THIS train
        g->capture_splice_start = splice_start;
        g->capture_splice_end = splice_end;
        g->capture_position = capture_position;
        // Each repeat REPLAYS the captured slice (the play_length samples just before the
        // trigger), not a single sample — that is what makes it a stutter rather than a click.
        g->play_length = play_length;
        g->play_pos = 0.0f;
    }
    // num_active_grains is recomputed every block in grain_delay_stut_process(); do NOT reset it
    // here, or we'd erase the count of grains still playing from earlier (overlapping) triggers.
}

// @endregion:ligase_pd.core.grain.delay_stut.trigger

// @region:ligase_pd.core.grain.delay_stut.process Stut Processing

// Process stut mode grains
// This decrements trigger timers and outputs active grains
void grain_delay_stut_process(grain_delay_stut_t *stut,
                              grain_delay_t *delay,
                              float *out_left,
                              float *out_right,
                              int blocksize) {
    if (!stut || !delay) return;

    // Process each active stut grain
    for (int i = 0; i < MAX_STUT_GRAINS; i++) {
        grain_stut_grain_t *g = &stut->grain_pool[i];

        if (!g->active) continue;

        // Short edge fade (~4 ms) so each replayed slice doesn't click at its boundaries.
        float fade = 0.004f * (float)stut->sample_rate;
        if (fade > g->play_length * 0.5f) fade = g->play_length * 0.5f;

        // Process each sample in the block
        for (int s = 0; s < blocksize; s++) {
            // Wait for this repetition's start time
            if (g->trigger_time_samples > 0.0f) {
                g->trigger_time_samples -= 1.0f;
                continue;
            }

            // Replay the captured slice: the `play_length` samples ending at capture_position
            // (i.e. the audio just before the trigger), read forward. capture_position is the
            // write head at trigger time, so [capture_position - play_length, capture_position)
            // is the recent audio, frozen for the duration of the stutter.
            if (g->play_pos < g->play_length) {
                int read_pos = (int)(g->capture_position - g->play_length + g->play_pos);
                read_pos = read_pos % delay->buffer_size;
                if (read_pos < 0) read_pos += delay->buffer_size;

                float w = 1.0f;
                if (fade > 0.0f) {
                    if (g->play_pos < fade) w = g->play_pos / fade;
                    else if (g->play_pos > g->play_length - fade) w = (g->play_length - g->play_pos) / fade;
                }

                out_left[s]  += delay->buffer_left[read_pos]  * g->gain * w;
                out_right[s] += delay->buffer_right[read_pos] * g->gain * w;
                g->play_pos += 1.0f;
            } else {
                // Slice finished — retire this repeat
                g->active = 0;
                break;
            }
        }
    }

    // Update active grain count
    int active_count = 0;
    for (int i = 0; i < MAX_STUT_GRAINS; i++) {
        if (stut->grain_pool[i].active) active_count++;
    }
    stut->num_active_grains = active_count;
}

// @endregion:ligase_pd.core.grain.delay_stut.process

// @region:ligase_pd.core.grain.delay_stut.messages Stut Message Interface

// Set number of repetitions
void grain_delay_stut_set_repetitions(grain_delay_stut_t *stut, int num) {
    if (stut) {
        if (num < 1) num = 1;
        if (num > MAX_STUT_REPS) num = MAX_STUT_REPS;  // per-train repeat cap (pool is larger)
        stut->num_repetitions = num;
    }
}

// Set gain reduction factor
void grain_delay_stut_set_reduction(grain_delay_stut_t *stut, float reduction) {
    if (stut) {
        if (reduction < 0.0f) reduction = 0.0f;
        if (reduction > 1.0f) reduction = 1.0f;
        stut->gain_reduction = reduction;
    }
}

// Set spacing in milliseconds
void grain_delay_stut_set_spacing(grain_delay_stut_t *stut, float spacing_ms) {
    if (stut) {
        if (spacing_ms < 1.0f) spacing_ms = 1.0f;
        if (spacing_ms > 5000.0f) spacing_ms = 5000.0f;  // Max 5 seconds
        stut->spacing_ms = spacing_ms;
    }
}

// @endregion:ligase_pd.core.grain.delay_stut.messages

// @endregion:ligase_pd.core.grain.delay_stut
