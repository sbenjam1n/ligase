// @region:ligase_pd.core.grain Granular Synthesis Engine

#include "types.h"
#include "perlin.h"
#include "grain_distortion.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>

// @region:ligase_pd.core.grain.config Configuration File Reader

// Read max_grains from ligase.conf configuration file
// Returns DEFAULT_MAX_GRAINS (200) if file not found or invalid
static int read_max_grains_from_config(void) {
    const char *config_filename = "ligase.conf";
    FILE *f = fopen(config_filename, "r");

    if (!f) {
        // Config file not found - use default
        return DEFAULT_MAX_GRAINS;
    }

    int max_grains = DEFAULT_MAX_GRAINS;
    char line[256];
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Try to parse max_grains = value
        int value;
        if (sscanf(line, " max_grains = %d", &value) == 1) {
            max_grains = value;
            found = 1;
            break;
        }
    }

    fclose(f);

    // Validate and clamp to safe range
    if (max_grains < 1) {
        fprintf(stderr, "ligase~: Warning: max_grains %d too small, clamping to 1\n", max_grains);
        max_grains = 1;
    }
    if (max_grains > MAX_POOL_SIZE) {
        fprintf(stderr, "ligase~: Warning: max_grains %d too large, clamping to %d\n",
                max_grains, MAX_POOL_SIZE);
        max_grains = MAX_POOL_SIZE;
    }

    if (found) {
        fprintf(stderr, "ligase~: Loaded max_grains = %d from ligase.conf\n", max_grains);
    } else {
        fprintf(stderr, "ligase~: No max_grains setting found, using default %d\n", DEFAULT_MAX_GRAINS);
    }

    return max_grains;
}

// @endregion:ligase_pd.core.grain.config

// @region:ligase_pd.utils.random.basic Basic Random Generators

// Random number generator with seed (0.0 to 1.0)
static float rand_float_seeded(unsigned int *seed) {
    // Simple LCG (Linear Congruential Generator)
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return (float)(*seed) / 2147483648.0f;
}

// @endregion:ligase_pd.utils.random.basic

// @region:ligase_pd.core.pitch.conversion Pitch to Speed Conversion

// Convert semitones to speed ratio
// Formula: speed = 2^(semitones/12)
// Examples: +12 semitones = 2x speed (1 octave up)
//           -12 semitones = 0.5x speed (1 octave down)
//           +7 semitones = ~1.498x speed (perfect fifth up)
static float semitones_to_speed(float semitones) {
    return powf(2.0f, semitones / 12.0f);
}

// @endregion:ligase_pd.core.pitch.conversion

// @region:ligase_pd.core.pitch.scale Pitch Scale Management

// Sample a random semitone value from a scale
// Non-static so the SMEAR pitch SCALE source (ligase~.c smear stanza) can reuse it (extern-declared there).
float sample_scale_semitones(pitch_scale_t *scale, perlin_state_t *perlin_state, param_range_t *semitone_range) {
    if (scale->count == 0) {
        return 0.0f;  // No scale defined, return 0 semitones
    }

    if (scale->count == 1) {
        return scale->semitones[0];  // Only one note, return it
    }

    // Use the semitone_range's random generator to pick from scale
    float random_value = 0.0f;
    int instance = semitone_range->rand_instance;

    switch (semitone_range->rand_type) {
        case RAND_TYPE_NONE:
            // No randomization - use middle of range (0.5)
            random_value = 0.5f;
            break;

        case RAND_TYPE_RAND:
            random_value = rand_float_seeded(&perlin_state->rand_seed[instance]);
            break;

        case RAND_TYPE_PERLIN_1D: {
            float coord_1d = perlin_state->noise_1d_coord[instance] + perlin_state->instance_offset_1d[instance];
            random_value = (perlin1d(coord_1d) + 1.0f) * 0.5f;
            break;
        }

        case RAND_TYPE_PERLIN_2D: {
            float coord_2d_x = perlin_state->noise_2d_coord_x[instance] + perlin_state->instance_offset_2d[instance];
            random_value = (perlin2d(coord_2d_x, 0.5f) + 1.0f) * 0.5f;
            break;
        }

        case RAND_TYPE_LORENZ: {
            int axis = instance % 3;
            random_value = lorenz_get_normalized(&perlin_state->lorenz[instance], axis);
            break;
        }

        case RAND_TYPE_NBODY: {
            // Use N-body gravitational simulation (bounded chaotic motion)
            // Output mode is user-controllable via nbody_mode message
            int mode = perlin_state->nbody_output_mode[instance];
            random_value = nbody_get_normalized(&perlin_state->nbody[instance], mode);
            break;
        }

        case RAND_TYPE_SPHERE: {
            // Use 3D sphere physics simulation (STK-based)
            // Output mode is user-controllable via sphere_mode message
            int mode = perlin_state->sphere_output_mode[instance];
            random_value = sphere_get_normalized(&perlin_state->sphere[instance], mode);
            break;
        }

        case RAND_TYPE_SAW: {
            // Sawtooth wave LFO: unipolar ramp from 0 to 1
            random_value = perlin_state->waveform_phase[instance];
            break;
        }

        case RAND_TYPE_SINE: {
            // Sine wave LFO: map to unipolar 0 to 1
            float phase_radians = perlin_state->waveform_phase[instance] * 2.0f * M_PI;
            random_value = (sinf(phase_radians) + 1.0f) * 0.5f;
            break;
        }

        case RAND_TYPE_SQUARE: {
            // Square wave LFO: bipolar switching at 50% duty cycle
            random_value = (perlin_state->waveform_phase[instance] < 0.5f) ? 0.0f : 1.0f;
            break;
        }

        case RAND_TYPE_PATTERN: {
            // Pattern-driven scale-degree selection (e.g. pitch_rand_type pattern_N): read the slot
            // cache as the [0,1] selector. Read rand_instance directly (slots 0..PATTERN_SLOTS-1).
            int slot = semitone_range->rand_instance;
            random_value = (slot >= 0 && slot < PATTERN_SLOTS &&
                            perlin_state->pattern[slot].step_count > 0)
                         ? perlin_state->pattern[slot].cached_value : 0.5f;
            break;
        }
    }

    // Apply inversion if enabled in semitone_range
    if (semitone_range->invert) {
        random_value = 1.0f - random_value;
    }

    // Map random_value (0.0 to 1.0) to scale index
    int index = (int)(random_value * scale->count);
    if (index >= scale->count) index = scale->count - 1;

    return scale->semitones[index];
}

// @endregion:ligase_pd.core.pitch.scale

// @region:ligase_pd.core.params.range.mapping Range Value Mapping

// Sample a value from a parameter range using the selected random generator
// (Exposed for use in ligase~.c for IOT/MaxGrains/GDelay ranging)
float sample_param_range(param_range_t *range, perlin_state_t *perlin_state, float base_value) {
    // If range is not enabled, return base value
    if (!range->enabled) {
        return base_value;
    }

    // If min == max, return that value (no randomization)
    if (range->min == range->max) {
        return range->min;
    }

    float random_value = 0.0f;
    int instance = range->rand_instance;

    // Defensive bounds checking: clamp instance to valid range [0-3]
    // This protects against corrupted state or uninitialized values
    if (instance < 0 || instance > 3) {
        instance = 0;  // Fail safe: default to instance 0
    }

    // Use range's base_value if provided (for PERLIN_2D decorrelation)
    // Otherwise fall back to passed-in base_value
    float perlin_2d_y = range->base_value;

    switch (range->rand_type) {
        case RAND_TYPE_NONE:
            // No randomization - use middle of range (0.5)
            random_value = 0.5f;
            break;

        case RAND_TYPE_RAND:
            // Use seeded basic random generator
            random_value = rand_float_seeded(&perlin_state->rand_seed[instance]);
            break;

        case RAND_TYPE_PERLIN_1D: {
            // Use 1D Perlin noise with instance offset for decorrelation
            // (returns -1.0 to 1.0, map to 0.0 to 1.0)
            float coord_1d = perlin_state->noise_1d_coord[instance] + perlin_state->instance_offset_1d[instance];
            random_value = (perlin1d(coord_1d) + 1.0f) * 0.5f;
            break;
        }

        case RAND_TYPE_PERLIN_2D: {
            // Use 2D Perlin noise with base_value as Y coordinate and instance offset
            // (returns -1.0 to 1.0, map to 0.0 to 1.0)
            float coord_2d_x = perlin_state->noise_2d_coord_x[instance] + perlin_state->instance_offset_2d[instance];
            random_value = (perlin2d(coord_2d_x, perlin_2d_y) + 1.0f) * 0.5f;
            break;
        }

        case RAND_TYPE_LORENZ: {
            // Use Lorenz attractor (chaotic but deterministic)
            // Choose axis based on parameter type for variety:
            // 0=X (intensity), 1=Y (temperature), 2=Z (profile)
            int axis = instance % 3;  // Rotate through X, Y, Z for different instances
            random_value = lorenz_get_normalized(&perlin_state->lorenz[instance], axis);
            break;
        }

        case RAND_TYPE_NBODY: {
            // Use N-body gravitational simulation (bounded chaotic motion)
            // Output mode is user-controllable via nbody_mode message
            int mode = perlin_state->nbody_output_mode[instance];
            random_value = nbody_get_normalized(&perlin_state->nbody[instance], mode);
            break;
        }

        case RAND_TYPE_SPHERE: {
            // Use 3D sphere physics simulation (STK-based)
            // Output mode is user-controllable via sphere_mode message
            int mode = perlin_state->sphere_output_mode[instance];
            random_value = sphere_get_normalized(&perlin_state->sphere[instance], mode);
            break;
        }

        case RAND_TYPE_SAW: {
            // Sawtooth wave LFO: unipolar ramp from 0 to 1
            random_value = perlin_state->waveform_phase[instance];
            break;
        }

        case RAND_TYPE_SINE: {
            // Sine wave LFO: map to unipolar 0 to 1
            float phase_radians = perlin_state->waveform_phase[instance] * 2.0f * M_PI;
            random_value = (sinf(phase_radians) + 1.0f) * 0.5f;
            break;
        }

        case RAND_TYPE_SQUARE: {
            // Square wave LFO: bipolar switching at 50% duty cycle
            random_value = (perlin_state->waveform_phase[instance] < 0.5f) ? 0.0f : 1.0f;
            break;
        }

        case RAND_TYPE_PATTERN: {
            // Pattern source: read the slot cache pattern_eval_slot wrote this block. Pattern slots
            // run 0..PATTERN_SLOTS-1 (NOT 0..3), so read rand_instance directly, bypassing the 0..3
            // clamp on the `instance` local used by the stochastic sources above. Unloaded / out-of-
            // range -> neutral 0.5. cached_value is already in [0,1] (held across rests).
            int slot = range->rand_instance;
            random_value = (slot >= 0 && slot < PATTERN_SLOTS &&
                            perlin_state->pattern[slot].step_count > 0)
                         ? perlin_state->pattern[slot].cached_value : 0.5f;
            break;
        }
    }

    // Apply inversion if enabled
    if (range->invert) {
        random_value = 1.0f - random_value;
    }

    // Map random_value (0.0 to 1.0) to range (min to max)
    float new_value = range->min + random_value * (range->max - range->min);

    // Apply exponential smoothing if slew > 0
    // slew = 0.0: instant (no smoothing)
    // slew = 0.5: moderate smoothing
    // slew = 0.99: very slow tracking
    if (range->slew > 0.0f && range->slew < 1.0f) {
        // Exponential moving average: smoothed = smoothed * slew + new * (1 - slew)
        range->smoothed_value = range->smoothed_value * range->slew + new_value * (1.0f - range->slew);
        return range->smoothed_value;
    } else {
        // No smoothing or slew=1.0 (frozen), update smoothed_value and return new_value
        range->smoothed_value = new_value;
        return new_value;
    }
}

// @region:ligase_pd.core.params.mod_matrix Modulation Matrix Helpers

// Read ANY matrix source as a normalized [0,1] value. The single point that maps a
// mod_source_t id to a number; reused by matrix_sum_for_dest. Generator instances reuse the
// SAME readouts sample_param_range uses (lorenz_get_normalized, nbody_get_normalized, ...),
// so a matrix source and a param_range source pointing at the same instance track together.
// Out-of-range / unloaded ids -> neutral 0.5 (defensive, mirroring the pattern-slot guard
// above) — no out-of-bounds read is possible on the audio thread. NOTE: MOD_SRC_RANDn
// advances the shared rand_seed[n] LCG on each read (same seed the param_range RAND source
// uses) — only when a rand connection exists, i.e. behavior the user opted into.
float mod_source_value(perlin_state_t *ps, int source) {
    if (source >= MOD_SRC_SINE1 && source <= MOD_SRC_SINE4) {
        float phase_radians = ps->waveform_phase[source - MOD_SRC_SINE1] * 2.0f * M_PI;
        return (sinf(phase_radians) + 1.0f) * 0.5f;
    }
    if (source >= MOD_SRC_SAW1 && source <= MOD_SRC_SAW4) {
        return ps->waveform_phase[source - MOD_SRC_SAW1];
    }
    if (source >= MOD_SRC_SQUARE1 && source <= MOD_SRC_SQUARE4) {
        return (ps->waveform_phase[source - MOD_SRC_SQUARE1] < 0.5f) ? 0.0f : 1.0f;
    }
    if (source >= MOD_SRC_PERLIN1 && source <= MOD_SRC_PERLIN4) {
        int i = source - MOD_SRC_PERLIN1;
        return (perlin1d(ps->noise_1d_coord[i] + ps->instance_offset_1d[i]) + 1.0f) * 0.5f;
    }
    if (source >= MOD_SRC_LORENZ1 && source <= MOD_SRC_LORENZ4) {
        int i = source - MOD_SRC_LORENZ1;
        return lorenz_get_normalized(&ps->lorenz[i], i % 3);   // same axis rotation as sample_param_range
    }
    if (source >= MOD_SRC_NBODY1 && source <= MOD_SRC_NBODY4) {
        int i = source - MOD_SRC_NBODY1;
        return nbody_get_normalized(&ps->nbody[i], ps->nbody_output_mode[i]);
    }
    if (source >= MOD_SRC_SPHERE1 && source <= MOD_SRC_SPHERE4) {
        int i = source - MOD_SRC_SPHERE1;
        return sphere_get_normalized(&ps->sphere[i], ps->sphere_output_mode[i]);
    }
    if (source >= MOD_SRC_RAND1 && source <= MOD_SRC_RAND4) {
        return rand_float_seeded(&ps->rand_seed[source - MOD_SRC_RAND1]);
    }
    if (source >= MOD_SRC_PATTERN0 && source <= MOD_SRC_PATTERN7) {
        int slot = source - MOD_SRC_PATTERN0;
        return (ps->pattern[slot].step_count > 0) ? ps->pattern[slot].cached_value : 0.5f;
    }
    switch (source) {
        case MOD_SRC_ENV_L:    return ps->env_follow_value[0];
        case MOD_SRC_ENV_R:    return ps->env_follow_value[1];
        case MOD_SRC_ENV_MONO: return ps->env_follow_value[2];
        default:               return 0.5f;   // MOD_SRC_NONE / unknown -> neutral (no contribution)
    }
}

// Cheap "does any enabled connection target this dest?" scan — the enabled-gate extension:
// per-block setters gated on range.enabled become (range.enabled || matrix_dest_active(dest))
// so a destination driven ONLY by the matrix still applies. O(mod_conn_count); 0 when inert.
int matrix_dest_active(scheduler_t *sched, int dest) {
    for (int i = 0; i < sched->mod_conn_count; i++) {
        if (sched->mod_matrix[i].enabled && sched->mod_matrix[i].dest == dest) return 1;
    }
    return 0;
}

// Sum all enabled connections targeting `dest`. Returns a SIGNED OFFSET to add to the base
// value. The [0,1] source is centered at 0.5 so a bipolar depth pushes both directions
// symmetrically ((s-0.5)*2 -> [-1,1]); depth scales to destination units. A dest with no
// connections -> 0 -> identical output (backward compat).
float matrix_sum_for_dest(scheduler_t *sched, int dest) {
    float sum = 0.0f;
    for (int i = 0; i < sched->mod_conn_count; i++) {
        mod_conn_t *c = &sched->mod_matrix[i];
        if (!c->enabled || c->dest != dest) continue;
        float s01 = mod_source_value(&sched->perlin_state, c->source);   // [0,1]
        sum += c->depth * (s01 - 0.5f) * 2.0f;
    }
    return sum;
}

// v1.5 PER-GRAIN tier: cache the six per-grain destination sums ONCE PER BLOCK (sources are
// per-block values; mirrors the env-follower's block-cache discipline, and keeps MOD_SRC_RANDn's
// shared-LCG advance at one step per connection per block instead of per grain). Called from
// ligase_perform before update_inlets/process_grains, so grains spawned this block read fresh
// sums (pattern-event grains, fired during pattern eval earlier in perform, read the previous
// block's cache — same one-block staleness as the follower, documented). With zero per-grain
// connections mask == 0 and every sum is 0, so scheduler_trigger_grain pays ~one branch per
// apply site (R5 backward compat).
void matrix_cache_grain_sums(scheduler_t *sched) {
    int mask = 0;
    for (int d = 0; d < MOD_GRAIN_DEST_COUNT; d++) {
        int dest = MOD_DEST_SPEED + d;
        if (matrix_dest_active(sched, dest)) {
            sched->mod_grain_sum[d] = matrix_sum_for_dest(sched, dest);
            mask |= (1 << d);
        } else {
            sched->mod_grain_sum[d] = 0.0f;
        }
    }
    sched->mod_grain_mask = mask;
}

// @endregion:ligase_pd.core.params.mod_matrix

// Update Perlin noise coordinates and Lorenz attractors based on IOT (called at grain trigger)
static void update_perlin_coords(perlin_state_t *perlin_state, float iot_seconds) {
    // Advance all 4 1D coordinates (each with its own frequency scale)
    for (int i = 0; i < 4; i++) {
        perlin_state->noise_1d_coord[i] += iot_seconds * perlin_state->noise_frequency_scale[i];
    }

    // Advance all 4 2D X coordinates (each with its own frequency scale)
    for (int i = 0; i < 4; i++) {
        perlin_state->noise_2d_coord_x[i] += iot_seconds * perlin_state->noise_frequency_scale[i];
    }

    // Update all 4 Lorenz attractor instances
    // Number of iterations based on IOT and per-instance frequency scale
    // This keeps the chaotic evolution musically tied to grain density
    for (int i = 0; i < 4; i++) {
        // Scale iterations by this instance's frequency scale
        int iterations = (int)(iot_seconds * 100.0f * perlin_state->noise_frequency_scale[i]);
        if (iterations < 1) iterations = 1;
        if (iterations > 50) iterations = 50;  // Cap for stability

        for (int j = 0; j < iterations; j++) {
            lorenz_update(&perlin_state->lorenz[i]);
        }
    }

    // Update all 4 N-body gravitational simulation instances
    // Number of iterations based on IOT and per-instance frequency scale
    // Keeps orbital evolution musically tied to grain density
    for (int i = 0; i < 4; i++) {
        // Scale iterations by this instance's frequency scale
        int iterations = (int)(iot_seconds * 100.0f * perlin_state->noise_frequency_scale[i]);
        if (iterations < 1) iterations = 1;
        if (iterations > 50) iterations = 50;  // Cap for stability

        for (int j = 0; j < iterations; j++) {
            nbody_update(&perlin_state->nbody[i]);
        }
    }

    // Update all 4 Sphere physics simulation instances (STK-based)
    // Time increment scaled by IOT and frequency scale
    // Keeps sphere motion musically tied to grain density
    for (int i = 0; i < 4; i++) {
        // Scale time increment by this instance's frequency scale
        float time_increment = iot_seconds * perlin_state->noise_frequency_scale[i];
        sphere_tick(&perlin_state->sphere[i], time_increment);
    }

    // Update all 4 Waveform LFO phase accumulators
    // Phase advances based on IOT and frequency scale
    // Keeps waveform modulation musically tied to grain density
    for (int i = 0; i < 4; i++) {
        // Advance phase by (iot_seconds * frequency_scale)
        // This makes waveforms cycle at a rate proportional to grain density
        perlin_state->waveform_phase[i] += iot_seconds * perlin_state->noise_frequency_scale[i];

        // Wrap phase to [0.0, 1.0) range
        while (perlin_state->waveform_phase[i] >= 1.0f) {
            perlin_state->waveform_phase[i] -= 1.0f;
        }
        while (perlin_state->waveform_phase[i] < 0.0f) {
            perlin_state->waveform_phase[i] += 1.0f;
        }
    }
}

// @endregion:ligase_pd.core.params.range.mapping

// @region:ligase_pd.core.pattern_eval Pattern Slot Evaluator

// pattern_eval_slot: the once-per-block evaluator for a mini-notation pattern slot. Called from
// ligase_perform AFTER the slot's free-running cycle phase has been advanced. On a new cycle it
// re-selects which alternation members are present and rebuilds the present-step prefix sums; then
// it maps the current phase to the active present-step and writes the slot cache. This is the SOLE
// writer of cached_value / cached_is_rest / changed / last_step_index. No allocation; perform-safe.
void pattern_eval_slot(perlin_state_t *ps, int slot) {
    if (slot < 0 || slot >= PATTERN_SLOTS) return;
    pattern_table_t *pt = &ps->pattern[slot];
    if (pt->step_count < 1) return;  // inactive slot

    long cycle = ps->pattern_cycle_index[slot];
    float phase = ps->pattern_phase[slot];
    if (phase < 0.0f) phase = 0.0f;
    if (phase >= 1.0f) phase = 0.999999f;

    // (1) Re-select alternation members + rebuild present-step prefix sums on a new cycle.
    if (cycle != pt->last_alt_cycle || pt->total_weight <= 0.0f) {
        float cum = 0.0f;
        for (int i = 0; i < pt->step_count; i++) {
            pattern_step_t *st = &pt->steps[i];
            int present = 1;
            if (st->alt_group >= 0) {
                int members = pt->alt_group_count[st->alt_group];
                int sel = (members > 0) ? (int)(((cycle % members) + members) % members) : 0;
                present = (st->alt_member == sel);
            }
            if (present) cum += st->weight;  // present steps advance the prefix; absent copy prev
            pt->cum_weight[i] = cum;
        }
        pt->total_weight = cum;
        pt->last_alt_cycle = cycle;
    }

    // (2) Map phase -> the present step whose [prev_cum, cum) span contains phase*total_weight.
    int idx = -1;
    if (pt->total_weight > 0.0f) {
        float target = phase * pt->total_weight;
        float prev = 0.0f;
        for (int i = 0; i < pt->step_count; i++) {
            float c = pt->cum_weight[i];
            if (c > prev) {              // a present step (nonzero span)
                if (target < c) { idx = i; break; }
                prev = c;
            }
        }
        if (idx < 0) {                   // fp edge (target == total): take the last present step
            for (int i = pt->step_count - 1; i >= 0; i--) {
                float p = (i > 0) ? pt->cum_weight[i - 1] : 0.0f;
                if (pt->cum_weight[i] > p) { idx = i; break; }
            }
        }
    }
    if (idx < 0) idx = 0;

    // (3) Write the cache. A rest holds the previous cached_value.
    pattern_step_t *cur = &pt->steps[idx];
    if (cur->is_rest) {
        pt->cached_is_rest = 1;          // cached_value unchanged (hold-previous)
    } else {
        pt->cached_is_rest = 0;
        pt->cached_value = cur->value;
    }
    pt->changed = (idx != pt->last_step_index) ? 1 : 0;
    pt->last_step_index = idx;
}

// @endregion:ligase_pd.core.pattern_eval

// @region:ligase_pd.core.grain.scheduler Grain Scheduler

scheduler_t* scheduler_create(envelope_t *env, int sample_rate) {
    scheduler_t *sched = (scheduler_t*)malloc(sizeof(scheduler_t));
    if (!sched) return NULL;  // Memory allocation failed

    // CRITICAL: zero the whole struct before touching any field. malloc returns garbage, and
    // every param_range_t carries an `enabled` flag that the perform routine checks each block
    // to decide whether to APPLY that range's modulation (overwriting the live parameter). A
    // range that is declared but not explicitly initialized below would inherit a garbage
    // enabled flag — and a garbage-enabled range silently clobbers its parameter with garbage
    // every block, non-deterministically per instantiation. This was the root cause of the
    // intermittent "can't record / playback stops" (sos_range), "delay just attenuates"
    // (gdelay_mix_range), "stut reduction stuck" (gdelay_feedback_range) and skew/organize
    // glitches. Zeroing guarantees enabled = 0 (disabled) for EVERY range, including any added
    // in future, so the explicit defaults below only need to set sensible min/max.
    memset(sched, 0, sizeof(scheduler_t));

    sched->envelope = env;
    sched->sample_rate = sample_rate;
    sched->grain_size = 0.1f;

    // Initialize DLGranulator parameters
    sched->max_grains = 4;         // Default max grains
    sched->iot = 0.05f;            // Default interonset time (50ms)

    // Initialize parameter ranges (all disabled by default, using rand_1)
    // Fields: min, max, rand_type, rand_instance, enabled, base_value, slew, smoothed_value, invert
    param_range_t default_range = {0.0f, 1.0f, RAND_TYPE_RAND, 0, 0, 0.5f, 0.0f, 0.0f, 0, RAND_TYPE_NONE, 0};
    sched->speed_range = default_range;
    sched->scanrate_range = default_range;
    sched->iot_range = default_range;
    sched->maxgrains_range = default_range;
    sched->grainsize_range = default_range;
    sched->grainstart_range = default_range;
    sched->gdelay_range = default_range;
    sched->gdelay_feedback_range = default_range;  // also routes to stut_reduction in Stut mode
    sched->gdelay_tone_range = default_range;       // also routes to stut_spacing in Stut mode
    sched->gdelay_mix_range = default_range;
    sched->organize_range = default_range;
    sched->sos_range = default_range;
    sched->env_skew_range = default_range;
    sched->distortion_range = default_range;
    sched->amplitude_range = default_range;
    sched->pan_range = default_range;
    sched->saw_cycles_range = default_range;
    sched->saw_depth_range = default_range;
    sched->moog_cutoff_range = default_range;
    sched->moog_resonance_range = default_range;
    sched->moog_mix_range = default_range;

    // Initialize distortion enhancement parameter ranges (all disabled by default)
    sched->dist_emphasis_freq_range = default_range;
    sched->dist_pregain_range = default_range;
    sched->dist_curve_blend_range = default_range;
    sched->dist_drive_pos_range = default_range;
    sched->dist_drive_neg_range = default_range;
    sched->dist_poly_c1_range = default_range;
    sched->dist_poly_c2_range = default_range;
    sched->dist_poly_c3_range = default_range;

    // Initialize stut parameter range (disabled by default)
    sched->stut_reps_range = default_range;

    // Initialize bencina parameter ranges (disabled by default)
    sched->bencina_iot_range = default_range;
    sched->bencina_grainsize_range = default_range;
    sched->bencina_pan_range = default_range;

    // Initialize smear parameter ranges (disabled by default)
    sched->smear_frequency_range = default_range;
    sched->smear_resonance_range = default_range;
    sched->smear_stages_range = default_range;
    sched->smear_feedback_range = default_range;
    sched->smear_pitch_fine_range = default_range;                 // P3: smear fine-tune (disabled)
    sched->smear_pitch_fine_range.min = -0.5f;
    sched->smear_pitch_fine_range.max =  0.5f;

    // Initialize grain distortion (enabled by default with zero intensity)
    sched->distortion = grain_distortion_create(sample_rate);
    if (!sched->distortion) {
        // If distortion creation fails, clean up and return NULL
        free(sched);
        return NULL;
    }

    // Initialize pitch control system (default: OFF, uses speed directly)
    sched->pitch_control.mode = PITCH_MODE_OFF;
    sched->pitch_control.semitones = 0.0f;
    sched->pitch_control.semitone_range = default_range;
    sched->pitch_control.semitone_range.min = -12.0f;
    sched->pitch_control.semitone_range.max = 12.0f;
    sched->pitch_control.scale.count = 0;
    sched->pitch_control.midi_note = 60;  // Middle C
    sched->pitch_control.midi_enabled = 0;
    sched->pitch_control.last_semitone = 0.0f;  // Initialize for change detection
    sched->pitch_control.pitch_pattern_slot = -1;  // -1 = no pattern slot bound (0 is a valid slot)
    sched->pitch_control.pitch_fine = 0.0f;                         // P3: no fine offset by default
    sched->pitch_control.pitch_fine_range = default_range;          // P3: disabled
    sched->pitch_control.pitch_fine_range.min = -0.5f;             // -50 cents
    sched->pitch_control.pitch_fine_range.max =  0.5f;             // +50 cents

    // SMEAR pitch destination defaults (memset already zeroed enabled/source/note/midi_enabled).
    // Explicit non-zero musical defaults: A440 reference, no slot bound (0 is a valid slot).
    sched->smear_pitch_control.enabled      = 0;                  // off -> backward compat (manual smear_frequency)
    sched->smear_pitch_control.source       = SMEAR_PITCH_OFF;
    sched->smear_pitch_control.semitone     = 0.0f;
    sched->smear_pitch_control.ref_hz       = 440.0f;            // A4
    sched->smear_pitch_control.ref_note     = 69;                // note 69 -> 440 Hz (standard A440 MIDI)
    sched->smear_pitch_control.pattern_slot = -1;                // -1 = no slot bound (0 is a valid slot)
    sched->smear_pitch_control.midi_channel = 2;                 // default smear MIDI channel (used by P2)
    sched->smear_pitch_control.scale.count  = 0;                 // no scale loaded
    sched->smear_pitch_control.semitone_range = default_range;   // disabled by default
    sched->smear_pitch_control.last_hz      = 0.0f;
    sched->smear_pitch_control.semitone_fine = 0.0f;               // P3: no smear fine offset by default

    // P2: channel-aware MIDI routing defaults (independent: grain ch1, smear ch2; equal channels = unison).
    sched->grain_midi_channel = 1;
    sched->smear_midi_channel = 2;

    // CHORDAL POLY voice pool defaults (memset already zeroed voice_note/voice_age). Explicit for clarity:
    sched->poly_enabled   = 0;   // mono by default -> trigger sites take the single-call path
    sched->voice_count    = 0;   // no active voices
    sched->voice_next_age = 0;   // monotonic age counter for oldest-note stealing

    // Initialize pan mode (default: constant-power mono panning)
    sched->pan_mode = 0;

    // SPATIAL granulation defaults (pan_mode 2). memset already zeroed these, but 0 is a valid
    // instance and RAND_TYPE_NONE == 0, so set explicit musical defaults. Lean v1 = azimuth-only:
    // width live, depth/tilt off.
    sched->spatial_source     = RAND_TYPE_SPHERE;  // default driver = bouncing sphere instance 0
    sched->spatial_instance   = 0;
    sched->spatial_nbody_body = 1;                 // body 1 = the orbiting body (most musical motion)
    sched->spatial_width      = 1.0f;              // full L/R spread
    sched->spatial_depth_amt  = 0.0f;              // lean v1: distance->level OFF
    sched->spatial_tilt_amt   = 0.0f;              // lean v1: elevation tilt OFF

    // Initialize Perlin noise state
    // Initialize all 4 per-instance frequency scales to 1.0 (normal speed)
    for (int i = 0; i < 4; i++) {
        sched->perlin_state.noise_frequency_scale[i] = 1.0f;
    }

    // Initialize 4 random seeds with unique values
    unsigned int base_seed = (unsigned int)time(NULL);
    for (int i = 0; i < 4; i++) {
        sched->perlin_state.rand_seed[i] = base_seed + i * 12345;
    }

    // Initialize one Perlin permutation table (shared by all instances)
    perlin_init(base_seed);

    // Initialize 4 1D coordinates to 0 with unique offsets (large primes for decorrelation)
    float offsets_1d[4] = {0.0f, 1000.0f, 2003.0f, 3001.0f};
    float offsets_2d[4] = {0.0f, 5000.0f, 10007.0f, 15013.0f};

    for (int i = 0; i < 4; i++) {
        sched->perlin_state.noise_1d_coord[i] = 0.0f;
        sched->perlin_state.noise_2d_coord_x[i] = 0.0f;
        sched->perlin_state.instance_offset_1d[i] = offsets_1d[i];
        sched->perlin_state.instance_offset_2d[i] = offsets_2d[i];
    }

    // Initialize 4 Lorenz attractor instances with different starting positions
    // Using different starting points ensures decorrelation between instances
    float lorenz_start_x[4] = {0.1f, 5.0f, -3.0f, 8.0f};
    float lorenz_start_y[4] = {0.0f, 10.0f, -5.0f, 15.0f};
    float lorenz_start_z[4] = {0.0f, 20.0f, 10.0f, 30.0f};

    for (int i = 0; i < 4; i++) {
        lorenz_init(&sched->perlin_state.lorenz[i],
                   lorenz_start_x[i],
                   lorenz_start_y[i],
                   lorenz_start_z[i],
                   0.01f);  // dt = 0.01 for smooth evolution
    }

    // Initialize 4 N-body gravitational simulation instances
    // Each instance gets different initial configuration for variety
    for (int i = 0; i < 4; i++) {
        nbody_init(&sched->perlin_state.nbody[i], i, 0.005f);  // dt = 0.005 for stable integration
        // Initialize output mode to default (position-based for first 3, distance for 4th)
        sched->perlin_state.nbody_output_mode[i] = i;  // 0=Body0 X, 1=Body1 Y, 2=Body2 X, 3=Dist 0-1
    }

    // Initialize 4 Sphere physics simulation instances (STK-based)
    // Each instance gets different initial configuration for variety
    for (int i = 0; i < 4; i++) {
        sphere_init(&sched->perlin_state.sphere[i]);
        // Set unique initial positions for each instance
        sphere_set_position(&sched->perlin_state.sphere[i],
                          (i - 1.5f) * 2.0f,  // Spread along X: -3, -1, 1, 3
                          0.0f,
                          0.0f);
        // Initialize output mode to cycle through outputs (X, Y, Z, VelMag)
        sched->perlin_state.sphere_output_mode[i] = i % 7;  // 0=X, 1=Y, 2=Z, 3=VelX, 4=VelY, 5=VelZ, 6=VelMag
    }

    // Initialize 4 Waveform LFO phase accumulators
    // Start at different phases for decorrelation
    for (int i = 0; i < 4; i++) {
        sched->perlin_state.waveform_phase[i] = (float)i * 0.25f;  // 0.0, 0.25, 0.5, 0.75
    }

    // MOD MATRIX: the memset above zeroed mod_matrix[]/mod_conn_count (inert) and the envelope
    // follower state/value caches (silence -> 0). Only the follower release coeff needs a
    // non-zero default: ~30 ms release (coeff = exp(-1/(t*sr))). Tunable via 'env_follow_ms'.
    sched->perlin_state.env_follow_ms = 30.0f;
    sched->perlin_state.env_follow_coeff = (sample_rate > 0)
        ? expf(-1.0f / (0.030f * (float)sample_rate)) : 0.0f;

    // Read pool size from config file
    sched->pool_size = read_max_grains_from_config();

    // Allocate grain pool dynamically based on config
    sched->grain_pool = (grain_t*)calloc(sched->pool_size, sizeof(grain_t));
    if (!sched->grain_pool) {
        // Pool allocation failed - clean up and return NULL
        int pool_size = sched->pool_size;  // Save before freeing
        grain_distortion_destroy(sched->distortion);
        free(sched);
        fprintf(stderr, "ligase~: ERROR: Failed to allocate grain pool (%d grains)\n",
                pool_size);
        return NULL;
    }

    // Initialize grain pool
    sched->free_list = &sched->grain_pool[0];
    sched->active_list = NULL;

    // Link free list using dynamic pool_size
    for (int i = 0; i < sched->pool_size - 1; i++) {
        sched->grain_pool[i].next = &sched->grain_pool[i + 1];
    }
    sched->grain_pool[sched->pool_size - 1].next = NULL;

    return sched;
}

void scheduler_destroy(scheduler_t *sched) {
    if (sched) {
        if (sched->distortion) {
            grain_distortion_destroy(sched->distortion);
        }
        if (sched->grain_pool) {
            free(sched->grain_pool);
        }
        free(sched);
    }
}

grain_t* scheduler_allocate_grain(scheduler_t *sched) {
    if (!sched->free_list) return NULL;

    // Sample max_grains with range (allows dynamic polyphony variation)
    float sampled_max_grains_float = sample_param_range(&sched->maxgrains_range,
                                                        &sched->perlin_state,
                                                        (float)sched->max_grains);
    int sampled_max_grains = (int)sampled_max_grains_float;

    // Clamp to valid range (pool_size is the actual allocated pool size)
    if (sampled_max_grains < 1) sampled_max_grains = 1;
    if (sampled_max_grains > sched->pool_size) sampled_max_grains = sched->pool_size;

    // Check if we've hit max_grains limit (using sampled value)
    int active_count = 0;
    grain_t *g = sched->active_list;
    while (g) {
        active_count++;
        g = g->next;
    }
    if (active_count >= sampled_max_grains) return NULL;

    grain_t *grain = sched->free_list;
    sched->free_list = grain->next;

    grain->next = sched->active_list;
    sched->active_list = grain;
    grain->active = 1;

    return grain;
}

void scheduler_release_grain(scheduler_t *sched, grain_t *grain) {
    // Remove from active list
    if (sched->active_list == grain) {
        sched->active_list = grain->next;
    } else {
        grain_t *prev = sched->active_list;
        while (prev && prev->next != grain) {
            prev = prev->next;
        }
        if (prev) prev->next = grain->next;
    }

    // Add to free list
    grain->active = 0;
    grain->next = sched->free_list;
    sched->free_list = grain;
}

void scheduler_trigger_grain(scheduler_t *sched, float position, float speed, uint32_t splice_start, uint32_t splice_end, float amplitude, float pan, float saw_cycles, float saw_depth, int voice_note, int voice_active) {
    if (!sched) return;

    //  Validate splice bounds
    if (splice_end <= splice_start) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "ligase~: ERROR - invalid splice bounds: start=%d end=%d\n", splice_start, splice_end);
            warn_count++;
        }
        return;
    }

    //  Ensure grain_size is valid (prevent division by zero in envelope)
    if (sched->grain_size <= 0.0f) {
        sched->grain_size = 0.01f;  // Minimum 10ms grain size
    }

    grain_t *grain = scheduler_allocate_grain(sched);
    if (!grain) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "ligase~: ERROR - failed to allocate grain (pool full?)\n");
            warn_count++;
        }
        return;
    }

    // DEBUG: Log first few grain triggers
    static int trigger_count = 0;
    if (trigger_count < 5) {
        fprintf(stderr, "ligase~: grain triggered #%d: pos=%.1f speed=%.2f bounds=%d-%d\n",
                trigger_count, position, speed, splice_start, splice_end);
        trigger_count++;
    }

    // Update Perlin noise coordinates based on IOT
    update_perlin_coords(&sched->perlin_state, sched->iot);

    // Calculate final speed based on pitch control mode
    // Step 1: Get base speed (with range modulation only in PITCH_MODE_OFF)
    float base_speed;
    if (sched->pitch_control.mode == PITCH_MODE_OFF) {
        // Apply speed range modulation when in OFF mode
        base_speed = sample_param_range(&sched->speed_range, &sched->perlin_state, speed);
    } else {
        // Use speed parameter directly - it's the base pitch we transpose from
        base_speed = speed;
    }

    // Step 2: Apply pitch transposition multiplicatively
    float final_speed = base_speed;
    float current_semitone = 0.0f;  // Track semitone for change detection

    switch (sched->pitch_control.mode) {
        case PITCH_MODE_OFF:
            // Already applied range above, use base_speed as-is
            current_semitone = 0.0f;
            break;

        case PITCH_MODE_SEMITONES:
            // Transpose from base speed by fixed semitones
            current_semitone = sched->pitch_control.semitones;
            final_speed = base_speed * semitones_to_speed(current_semitone);
            break;

        case PITCH_MODE_RANGE:
            // Transpose from base speed by sampled semitones from range
            {
                current_semitone = sample_param_range(&sched->pitch_control.semitone_range,
                                                      &sched->perlin_state,
                                                      0.0f);
                final_speed = base_speed * semitones_to_speed(current_semitone);
            }
            break;

        case PITCH_MODE_SCALE:
            // Transpose from base speed by semitones from scale
            {
                current_semitone = sample_scale_semitones(&sched->pitch_control.scale,
                                                         &sched->perlin_state,
                                                         &sched->pitch_control.semitone_range);
                final_speed = base_speed * semitones_to_speed(current_semitone);
            }
            break;

        case PITCH_MODE_PATTERN:
            // Pattern-driven scale-degree stepper: read the (block-advanced) slot cache as a scale
            // degree, wrap into the scale + octave-shift (the wrap sample_scale_semitones lacks),
            // then transpose. A rest holds the previous semitone. Reads the cache only; perform-safe.
            {
                int slot = sched->pitch_control.pitch_pattern_slot;
                int count = sched->pitch_control.scale.count;
                if (slot >= 0 && slot < PATTERN_SLOTS && count > 0 &&
                    sched->perlin_state.pattern[slot].step_count > 0) {
                    pattern_table_t *pt = &sched->perlin_state.pattern[slot];
                    if (pt->cached_is_rest) {
                        current_semitone = sched->pitch_control.last_semitone;   // rest: hold prev note
                    } else {
                        int degree = (int)pt->cached_value;                      // leaf value = scale degree
                        int idx = ((degree % count) + count) % count;            // [0, count-1]
                        int oct = (int)floorf((float)degree / (float)count);     // octave compensation
                        current_semitone = sched->pitch_control.scale.semitones[idx] + 12.0f * (float)oct;
                    }
                }
                // else: slot / scale not ready -> current_semitone stays 0 (unison), never crashes
                final_speed = base_speed * semitones_to_speed(current_semitone);
            }
            break;

        case PITCH_MODE_MIDI:
            // Assumes sample at base speed is middle C (60), transpose by MIDI offset.
            // CHORDAL POLY: when voice_active, use this voice's note instead of the shared scalar,
            // so each voice in a chord freezes its own transposition into grain->increment.
            {
                int note = voice_active ? voice_note : sched->pitch_control.midi_note;
                if (voice_active || sched->pitch_control.midi_enabled) {
                    current_semitone = note - 60;
                    final_speed = base_speed * semitones_to_speed(current_semitone);
                }
                // else: use base_speed as-is (no MIDI input)
            }
            break;
    }

    // FINE TUNE (P3, +/-0.5 semitone = +/-50 cents): overall offset on top of whatever the source
    // produced. Sampled per grain; range disabled -> returns pitch_fine (base, default 0) -> no offset.
    // Recompute final_speed once so the fine applies uniformly across ALL modes (incl. OFF).
    {
        float fine = sample_param_range(&sched->pitch_control.pitch_fine_range,
                                        &sched->perlin_state, sched->pitch_control.pitch_fine);
        // MOD MATRIX (v1.5): pitch_fine offset adds to the sampled fine value, then the
        // destination's own +/-0.5-semitone bound. Clamped only when a connection is active,
        // so the unmodulated path keeps its historical no-clamp behavior. FUNCTIONAL — the
        // pitch_control.pitch_fine base is never written.
        if (sched->mod_grain_mask & MOD_GRAIN_BIT(MOD_DEST_PITCH_FINE)) {
            fine += sched->mod_grain_sum[MOD_GRAIN_IDX(MOD_DEST_PITCH_FINE)];
            if (fine < -0.5f) fine = -0.5f;
            if (fine >  0.5f) fine =  0.5f;
        }
        current_semitone += fine;
        final_speed = base_speed * semitones_to_speed(current_semitone);
    }

    // Store the semitone value for change detection in ligase~.c
    sched->pitch_control.last_semitone = current_semitone;

    // MOD MATRIX (v1.5): per-grain SPEED offset — applies in ALL pitch modes, added to the
    // final pitch-derived speed (R2: a pin on speed is a detune/drift AROUND the note, not a
    // pitch-source bypass), then the existing +/-4.0 clamp below. FUNCTIONAL — the shared
    // speed field is never written back.
    if (sched->mod_grain_mask & MOD_GRAIN_BIT(MOD_DEST_SPEED)) {
        final_speed += sched->mod_grain_sum[MOD_GRAIN_IDX(MOD_DEST_SPEED)];
    }

    // Clamp final_speed to prevent extreme position jumps and aliasing
    // Max speed of ±4.0 prevents reading >4 samples per sample (Nyquist/aliasing issues)
    // This allows ±48 semitones (4 octaves) which is musically sufficient
    static int clamp_warning_count = 0;
    if (final_speed > 4.0f) {
        if (clamp_warning_count < 3) {
            fprintf(stderr, "ligase~: speed clamped from %.2f to 4.0 (check pitch/speed parameter ranges)\n", final_speed);
            clamp_warning_count++;
        }
        final_speed = 4.0f;
    }
    if (final_speed < -4.0f) {
        if (clamp_warning_count < 3) {
            fprintf(stderr, "ligase~: speed clamped from %.2f to -4.0 (check pitch/speed parameter ranges)\n", final_speed);
            clamp_warning_count++;
        }
        final_speed = -4.0f;
    }

    float grain_size = sample_param_range(&sched->grainsize_range, &sched->perlin_state, sched->grain_size);

    // MOD MATRIX (v1.5): per-grain size offset, added AFTER the range sample and BEFORE the
    // existing in-place clamps (R1). FUNCTIONAL — sched->grain_size is never written.
    if (sched->mod_grain_mask & MOD_GRAIN_BIT(MOD_DEST_GRAINSIZE)) {
        grain_size += sched->mod_grain_sum[MOD_GRAIN_IDX(MOD_DEST_GRAINSIZE)];
    }

    //  Clamp grain_size to safe minimum (prevent zero-length grains)
    if (grain_size < 0.01f) grain_size = 0.01f;
    if (grain_size > 2.0f) grain_size = 2.0f;

    // Sample distortion intensity (0.0-1.0) - use base value 0.0 (clean)
    if (sched->distortion) {
        float distortion_intensity = sample_param_range(&sched->distortion_range, &sched->perlin_state, 0.0f);
        grain_distortion_set_intensity(sched->distortion, distortion_intensity);

        // Sample distortion enhancement parameters (use distortion struct's current values as base)
        float emphasis_freq = sample_param_range(&sched->dist_emphasis_freq_range,
                                                 &sched->perlin_state,
                                                 sched->distortion->emphasis_freq);
        grain_distortion_set_emphasis_freq(sched->distortion, emphasis_freq);

        float pregain = sample_param_range(&sched->dist_pregain_range,
                                           &sched->perlin_state,
                                           sched->distortion->pregain);
        grain_distortion_set_pregain(sched->distortion, pregain);

        float curve_blend = sample_param_range(&sched->dist_curve_blend_range,
                                               &sched->perlin_state,
                                               sched->distortion->curve_blend);
        grain_distortion_set_curve_blend(sched->distortion, curve_blend);

        // For asymmetric mode: sample pos and neg drives
        if (sched->distortion->waveshaper_mode == WAVESHAPER_MODE_ASYMMETRIC) {
            float drive_pos = sample_param_range(&sched->dist_drive_pos_range,
                                                 &sched->perlin_state,
                                                 sched->distortion->drive_pos);
            grain_distortion_set_drive_pos(sched->distortion, drive_pos);

            float drive_neg = sample_param_range(&sched->dist_drive_neg_range,
                                                 &sched->perlin_state,
                                                 sched->distortion->drive_neg);
            grain_distortion_set_drive_neg(sched->distortion, drive_neg);
        }

        // For polynomial mode: sample coefficients
        if (sched->distortion->waveshaper_mode == WAVESHAPER_MODE_POLYNOMIAL) {
            float poly_c1 = sample_param_range(&sched->dist_poly_c1_range,
                                               &sched->perlin_state,
                                               sched->distortion->poly_c1);
            grain_distortion_set_poly_c1(sched->distortion, poly_c1);

            float poly_c2 = sample_param_range(&sched->dist_poly_c2_range,
                                               &sched->perlin_state,
                                               sched->distortion->poly_c2);
            grain_distortion_set_poly_c2(sched->distortion, poly_c2);

            float poly_c3 = sample_param_range(&sched->dist_poly_c3_range,
                                               &sched->perlin_state,
                                               sched->distortion->poly_c3);
            grain_distortion_set_poly_c3(sched->distortion, poly_c3);
        }
    }

    // GrainStart range: offset from current position (normalized 0-1)
    // If enabled, this adds a random offset within the splice.
    // MOD MATRIX (v1.5): the per-grain grain_start sum adds to the normalized offset (0 when
    // the range is disabled) BEFORE the scale to splice length, clamped to [0, 1] only when a
    // connection is active. Range-only path is byte-identical to the pre-v1.5 code.
    float grain_start_offset = 0.0f;
    {
        int mx = sched->mod_grain_mask & MOD_GRAIN_BIT(MOD_DEST_GRAIN_START);
        if (sched->grainstart_range.enabled || mx) {
            float splice_length = splice_end - splice_start;
            // Sample normalized offset (0-1), then scale to splice length
            float normalized_offset = sched->grainstart_range.enabled
                ? sample_param_range(&sched->grainstart_range, &sched->perlin_state, 0.5f)
                : 0.0f;
            if (mx) {
                normalized_offset += sched->mod_grain_sum[MOD_GRAIN_IDX(MOD_DEST_GRAIN_START)];
                if (normalized_offset < 0.0f) normalized_offset = 0.0f;
                if (normalized_offset > 1.0f) normalized_offset = 1.0f;
            }
            grain_start_offset = normalized_offset * splice_length;
        }
    }

    // Grain amplitude: use range if enabled, otherwise use inlet/message value
    float grain_amplitude;
    if (sched->amplitude_range.enabled) {
        // Sample amplitude from range using inlet/message value as base
        grain_amplitude = sample_param_range(&sched->amplitude_range, &sched->perlin_state, amplitude);
    } else {
        // Use amplitude from inlet or message
        grain_amplitude = amplitude;
    }

    // MOD MATRIX (v1.5): per-grain amplitude offset, before the existing in-place clamps (R1).
    if (sched->mod_grain_mask & MOD_GRAIN_BIT(MOD_DEST_AMPLITUDE)) {
        grain_amplitude += sched->mod_grain_sum[MOD_GRAIN_IDX(MOD_DEST_AMPLITUDE)];
    }

    //  Clamp amplitude to reasonable range (prevent extreme clipping)
    if (grain_amplitude < 0.0f) grain_amplitude = 0.0f;
    if (grain_amplitude > 2.0f) grain_amplitude = 2.0f;  // Allow +6dB headroom

    // Grain pan: use range if enabled, otherwise use inlet/message value
    float grain_pan;
    if (sched->pan_range.enabled) {
        // Sample pan from range using inlet/message value as base
        grain_pan = sample_param_range(&sched->pan_range, &sched->perlin_state, pan);
    } else {
        // Use pan from inlet or message
        grain_pan = pan;
    }

    // MOD MATRIX (v1.5): per-grain pan offset, before the existing in-place clamps (R1).
    if (sched->mod_grain_mask & MOD_GRAIN_BIT(MOD_DEST_PAN)) {
        grain_pan += sched->mod_grain_sum[MOD_GRAIN_IDX(MOD_DEST_PAN)];
    }

    //  Clamp pan to valid range (prevent invalid gain calculations)
    if (grain_pan < 0.0f) grain_pan = 0.0f;
    if (grain_pan > 1.0f) grain_pan = 1.0f;

    // Grain saw cycles: use range if enabled, otherwise use inlet/message value
    float grain_saw_cycles;
    if (sched->saw_cycles_range.enabled) {
        // Sample saw_cycles from range using inlet/message value as base
        grain_saw_cycles = sample_param_range(&sched->saw_cycles_range, &sched->perlin_state, saw_cycles);
    } else {
        // Use saw_cycles from inlet or message
        grain_saw_cycles = saw_cycles;
    }

    //  Clamp saw_cycles to valid range (0-64)
    if (grain_saw_cycles < 0.0f) grain_saw_cycles = 0.0f;
    if (grain_saw_cycles > 64.0f) grain_saw_cycles = 64.0f;

    // Grain saw depth: use range if enabled, otherwise use inlet/message value
    float grain_saw_depth;
    if (sched->saw_depth_range.enabled) {
        // Sample saw_depth from range using inlet/message value as base
        grain_saw_depth = sample_param_range(&sched->saw_depth_range, &sched->perlin_state, saw_depth);
    } else {
        // Use saw_depth from inlet or message
        grain_saw_depth = saw_depth;
    }

    //  Clamp saw_depth to valid range (0.0-1.0)
    if (grain_saw_depth < 0.0f) grain_saw_depth = 0.0f;
    if (grain_saw_depth > 1.0f) grain_saw_depth = 1.0f;

    // Apply grain start offset to position
    position += grain_start_offset;

    // Clamp position to splice bounds
    while (position < splice_start) position += (splice_end - splice_start);
    while (position >= splice_end) position -= (splice_end - splice_start);

    grain->position = position;
    grain->increment = final_speed;
    grain->envelope_phase = 0;
    grain->grain_length = (int)(grain_size * sched->sample_rate);
    grain->amplitude = grain_amplitude;
    grain->pan = grain_pan;
    grain->saw_cycles = grain_saw_cycles;
    grain->saw_depth = grain_saw_depth;

    grain->splice_start = splice_start;
    grain->splice_end = splice_end;

    // DEBUG: log the FINAL per-grain values for the first few grains (v1.5 verification aid:
    // shows the per-grain matrix offsets landing on inc/len/amp/pan without touching the
    // shared fields). Same first-N stderr discipline as the trigger log above.
    static int final_dbg_count = 0;
    if (final_dbg_count < 8) {
        fprintf(stderr, "ligase~: grain final #%d: pos=%.1f inc=%.4f len=%d amp=%.3f pan=%.3f\n",
                final_dbg_count, grain->position, grain->increment, grain->grain_length,
                grain->amplitude, grain->pan);
        final_dbg_count++;
    }

    // --- SPATIAL snapshot (pan_mode 2): freeze the driving sim's 3D position onto the grain ---
    // Gated on the mode so the scalar pan path (pan_mode 0/1) is untouched. All the trig lives here
    // (trigger thread, once per grain), never in the per-sample render loop.
    if (sched->pan_mode == 2) {
        float v[3] = {0.0f, 0.0f, 0.0f};
        if (sched->spatial_source == RAND_TYPE_SPHERE) {
            sphere_get_normalized_vec3(&sched->perlin_state.sphere[sched->spatial_instance], v);
        } else if (sched->spatial_source == RAND_TYPE_NBODY) {
            nbody_get_normalized_vec3(&sched->perlin_state.nbody[sched->spatial_instance],
                                      sched->spatial_nbody_body, v);
        }
        grain->pos_x = v[0];   // [-1,1]  L .. R
        grain->pos_y = v[1];   // [-1,1]  down .. up (elevation)
        grain->pos_z = v[2];   // [-1,1]  back .. front (depth)

        // Front-biased azimuth from the horizontal plane (x,z): the whole orbit stays in the frontal
        // arc so a 2-speaker field only varies L/R (no front/back collapse). fmaxf guard keeps the
        // second arg off the atan2 (0,0) singularity.
        float azimuth = atan2f(grain->pos_x, fmaxf(grain->pos_z + 1.0f, 1e-6f));
        azimuth *= sched->spatial_width;   // 0 = collapse to center, 1 = full spread
        // Map azimuth (~[-pi/2, pi/2]) to the pan angle [0, pi/2] the cos/sin law expects.
        float pan01 = 0.5f + 0.5f * (azimuth / 1.5707963267948966f);
        if (pan01 < 0.0f) pan01 = 0.0f;
        if (pan01 > 1.0f) pan01 = 1.0f;
        float pan_angle = pan01 * 1.5707963267948966f;
        float lg = cosf(pan_angle);   // SAME constant-power law as the pan_mode 0/1 branches
        float rg = sinf(pan_angle);

        // Optional distance->level (lean v1 leaves spatial_depth_amt == 0 == off):
        // pos_z in [-1,1]: front(+1)=near=louder, back(-1)=far=quieter.
        if (sched->spatial_depth_amt > 0.0f) {
            float dist_gain = 1.0f - sched->spatial_depth_amt * (1.0f - (grain->pos_z * 0.5f + 0.5f));
            lg *= dist_gain;
            rg *= dist_gain;
        }
        // (elevation tilt from pos_y: spatial_tilt_amt reserved for fast-follow; inert in lean v1.)

        grain->spatial_left_gain  = lg;
        grain->spatial_right_gain = rg;
    }
}

void scheduler_set_grain_size(scheduler_t *sched, float grain_size) {
    if (sched) sched->grain_size = grain_size;
}

// @endregion:ligase_pd.core.grain.scheduler

// @region:ligase_pd.core.grain.source Grain Source

float grain_read_sample(reel_t *reel, float position, int channel) {
    if (!reel || reel->length == 0) {
        return 0.0f;
    }

    //  Wrap position within reel bounds for smooth looping
    // Using while loops handles extreme negative/positive values safely
    while (position < 0) position += reel->length;
    while (position >= reel->length) position -= reel->length;

    // Linear interpolation with wrapping
    int idx = (int)position;
    float frac = position - idx;

    //  Additional safety check after float->int conversion
    if (idx < 0) idx = 0;
    if (idx >= reel->length) idx = reel->length - 1;

    float *buffer = (channel == 0) ? reel->buffer_left : reel->buffer_right;

    //  Handle wrapping at buffer end with bounds check
    int next_idx = idx + 1;
    if (next_idx >= reel->length) next_idx = 0;

    return buffer[idx] * (1.0f - frac) + buffer[next_idx] * frac;
}

// @endregion:ligase_pd.core.grain.source

// @region:ligase_pd.core.grain.mixer Grain Mixer

void scheduler_process(scheduler_t *sched, reel_t *reel, float *out_left, float *out_right, int blocksize) {
    //  Comprehensive validation before processing
    if (!sched) {
        memset(out_left, 0, blocksize * sizeof(float));
        memset(out_right, 0, blocksize * sizeof(float));
        return;
    }

    if (!sched->envelope || !sched->envelope->table || sched->envelope->length <= 0) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "ligase~: ERROR - invalid envelope (envelope=%p, table=%p, length=%d)\n",
                    (void*)sched->envelope,
                    sched->envelope ? (void*)sched->envelope->table : NULL,
                    sched->envelope ? sched->envelope->length : 0);
            warn_count++;
        }
        memset(out_left, 0, blocksize * sizeof(float));
        memset(out_right, 0, blocksize * sizeof(float));
        return;
    }

    if (!reel || !reel->buffer_left || !reel->buffer_right || reel->length <= 0) {
        static int warn_count = 0;
        if (warn_count < 1) {
            fprintf(stderr, "ligase~: ERROR - invalid reel (reel=%p, left=%p, right=%p, length=%d)\n",
                    (void*)reel,
                    reel ? (void*)reel->buffer_left : NULL,
                    reel ? (void*)reel->buffer_right : NULL,
                    reel ? reel->length : 0);
            warn_count++;
        }
        memset(out_left, 0, blocksize * sizeof(float));
        memset(out_right, 0, blocksize * sizeof(float));
        return;
    }

    memset(out_left, 0, blocksize * sizeof(float));
    memset(out_right, 0, blocksize * sizeof(float));

    // DEBUG: Count active grains
    static int debug_count = 0;
    if (debug_count < 10) {
        int active_count = 0;
        grain_t *g = sched->active_list;
        while (g) {
            active_count++;
            g = g->next;
        }
        if (debug_count == 0 || active_count > 0) {
            fprintf(stderr, "ligase~: scheduler_process: %d active grains, blocksize=%d\n",
                    active_count, blocksize);
            debug_count++;
        }
    }

    grain_t *grain = sched->active_list;

    while (grain) {
        //  Validate grain pointer before accessing
        if (!grain || grain->active != 1) {
            static int warn_count = 0;
            if (warn_count < 1) {
                fprintf(stderr, "ligase~: ERROR - corrupted grain pointer in active list (grain=%p, active=%d)\n",
                        (void*)grain, grain ? grain->active : -1);
                warn_count++;
            }
            break;  // Exit loop if grain list is corrupted
        }

        int grain_finished = 0;  // Track if we manually advanced to next grain

        // Grain output goes to the main output buffers.
        float *target_left = out_left;
        float *target_right = out_right;

        for (int i = 0; i < blocksize; i++) {
            if (grain->envelope_phase >= grain->grain_length) {
                // Grain finished
                grain_t *next = grain->next;
                scheduler_release_grain(sched, grain);
                grain = next;
                grain_finished = 1;  // Mark that we already advanced
                break;
            }

            // Get envelope value with comprehensive bounds checking
            int env_idx = 0;
            if (grain->grain_length > 0) {
                env_idx = (grain->envelope_phase * sched->envelope->length) / grain->grain_length;
                // Clamp to valid range to prevent buffer overrun (handles both overflow and underflow)
                if (env_idx < 0) {
                    env_idx = 0;
                } else if (env_idx >= sched->envelope->length) {
                    env_idx = sched->envelope->length - 1;
                }
            }
            float env_val = sched->envelope->table[env_idx];

            // @region:ligase_pd.core.grain.envelope.saw_modulation Saw Wave Envelope Modulation
            // Apply saw wave modulation to envelope if depth > 0
            // Formula: env_val = base_env * (1.0 - depth * (1.0 - saw_val))
            // This carves the envelope into jagged peaks controlled by cycles and depth
            if (grain->saw_depth > 0.0f && grain->grain_length > 0) {
                // Calculate normalized time within grain (0.0 to 1.0)
                float t = (float)grain->envelope_phase / (float)grain->grain_length;

                // Calculate saw wave: unipolar sawtooth [0, 1)
                float phase = t * grain->saw_cycles;
                float saw_val = phase - floorf(phase);

                // Apply modulation with depth control
                // depth = 0.0: pure base envelope
                // depth = 1.0: maximum jaggedness with spikes dropping to zero
                float modulation_factor = 1.0f - (grain->saw_depth * (1.0f - saw_val));
                env_val *= modulation_factor;
            }
            // @endregion:ligase_pd.core.grain.envelope.saw_modulation

            // Read source samples from reel
            float sample_left = grain_read_sample(reel, grain->position, 0);
            float sample_right = grain_read_sample(reel, grain->position, 1);

            // Apply distortion if enabled AND in per-grain mode (position_mode == 0)
            // Post-mix mode (position_mode == 1) is handled in ligase_perform() after mixing
            // STABILITY NOTE: Per-grain distortion has NO oversampling (too CPU expensive per grain)
            // Use post-mix mode (default) for clean, oversampled distortion
            if (sched->distortion &&
                sched->distortion->enabled &&
                sched->distortion->position_mode == 0) {

                sample_left = grain_distortion_process_sample(sched->distortion, sample_left, 0);
                sample_right = grain_distortion_process_sample(sched->distortion, sample_right, 1);
            }

            // Pan mode selection: mono point source vs stereo balance
            if (sched->pan_mode == 0) {
                // Mode 0: Constant-power mono panning (default)
                // Sum stereo source to mono, creating a point source in stereo field
                float mono_sample = (sample_left + sample_right) * 0.5f;

                // Apply envelope and amplitude to mono signal
                mono_sample *= env_val * grain->amplitude;

                // Apply constant-power panning using sine/cosine law
                // cos²(θ) + sin²(θ) = 1 maintains constant power at all pan positions
                float pan_angle = grain->pan * 1.5707963267948966f;  // pan * (pi/2)
                float left_gain = cosf(pan_angle);   // 1.0 at left, 0.707 at center, 0.0 at right
                float right_gain = sinf(pan_angle);  // 0.0 at left, 0.707 at center, 1.0 at right

                // Output panned mono signal to both channels
                target_left[i] += mono_sample * left_gain;
                target_right[i] += mono_sample * right_gain;
            } else if (sched->pan_mode == 1) {
                // Mode 1: Stereo balance (preserve stereo width)
                // Apply envelope and amplitude to both channels independently
                sample_left *= env_val * grain->amplitude;
                sample_right *= env_val * grain->amplitude;

                // Apply stereo balance using constant-power law on each channel
                // Preserves stereo image while adjusting L/R balance
                float pan_angle = grain->pan * 1.5707963267948966f;  // pan * (pi/2)
                float left_gain = cosf(pan_angle);   // 1.0 at left, 0.707 at center, 0.0 at right
                float right_gain = sinf(pan_angle);  // 0.0 at left, 0.707 at center, 1.0 at right

                // Output balanced stereo signal
                target_left[i] += sample_left * left_gain;
                target_right[i] += sample_right * right_gain;
            } else {
                // Mode 2: Spatial 3D (physics-driven binaural placement)
                // Mono point source placed by the frozen 3D azimuth. The constant-power L/R gains
                // were precomputed at trigger (spatial_left_gain/right_gain), so the per-sample cost
                // is two multiplies + two adds — cheaper than mode 0/1 (no per-sample cos/sin).
                float mono_sample = (sample_left + sample_right) * 0.5f;
                mono_sample *= env_val * grain->amplitude;
                target_left[i]  += mono_sample * grain->spatial_left_gain;
                target_right[i] += mono_sample * grain->spatial_right_gain;
            }

            // Advance grain position
            grain->position += grain->increment;

            // Wrap within splice boundaries for seamless looping
            float splice_length = grain->splice_end - grain->splice_start;
            if (splice_length > 0) {
                while (grain->position >= grain->splice_end) {
                    grain->position -= splice_length;
                }
                while (grain->position < grain->splice_start) {
                    grain->position += splice_length;
                }
            }

            grain->envelope_phase++;
        }

        // Only advance to next grain if we didn't already do it in the finish logic
        if (!grain_finished && grain) {
            grain = grain->next;
        }
    }
}

// @endregion:ligase_pd.core.grain.mixer

// @endregion:ligase_pd.core.grain
