#include "corpus_manager.h"
#include <string.h>
#include <stdlib.h>

// Structured seed with subsystem tagging
typedef struct {
    const char *message;
    uint32_t subsystem;
    uint8_t priority;  // 1-10, higher = more important to fuzz
} seed_entry_t;

static const seed_entry_t SEED_CORPUS[] = {
    // === GRAIN CORE (highest priority - most complex state) ===
    {"grainsize 0.001", SUBSYS_GRAIN_CORE, 10},      // Minimum
    {"grainsize 10.0", SUBSYS_GRAIN_CORE, 10},       // Maximum
    {"grainsize 0.1", SUBSYS_GRAIN_CORE, 5},         // Default
    {"iot 0.0005", SUBSYS_GRAIN_CORE, 10},           // Minimum - rapid triggering
    {"iot 2.0", SUBSYS_GRAIN_CORE, 10},              // Maximum
    {"maxgrains 1", SUBSYS_GRAIN_CORE, 9},           // Minimum polyphony
    {"maxgrains 2000", SUBSYS_GRAIN_CORE, 10},       // Maximum (pool_size)
    {"maxgrains 200", SUBSYS_GRAIN_CORE, 5},         // Default
    {"grainstart 0", SUBSYS_GRAIN_CORE, 8},
    {"grainstart 1", SUBSYS_GRAIN_CORE, 8},
    {"grainstart 0.5", SUBSYS_GRAIN_CORE, 5},
    {"amplitude 0", SUBSYS_GRAIN_CORE, 7},
    {"amplitude 2.0", SUBSYS_GRAIN_CORE, 8},         // Maximum
    {"pan 0", SUBSYS_GRAIN_CORE, 6},
    {"pan 1", SUBSYS_GRAIN_CORE, 6},
    {"grain_bang_rate 0", SUBSYS_GRAIN_CORE, 5},
    {"grain_bang_rate 1", SUBSYS_GRAIN_CORE, 7},
    {"grain_bang_rate 1000", SUBSYS_GRAIN_CORE, 8},

    // === PLAYHEAD MODES ===
    {"playhead 1", SUBSYS_PLAYHEAD, 9},              // Static
    {"playhead 2", SUBSYS_PLAYHEAD, 9},              // Scanning
    {"playhead 3", SUBSYS_PLAYHEAD, 9},              // Clock advance
    {"scanrate 0", SUBSYS_PLAYHEAD, 8},
    {"scanrate 1000", SUBSYS_PLAYHEAD, 9},
    {"scanrate -1000", SUBSYS_PLAYHEAD, 9},          // Reverse
    {"clock_advance_quant 0", SUBSYS_PLAYHEAD, 7},
    {"clock_advance_quant 1", SUBSYS_PLAYHEAD, 7},
    {"clockstop", SUBSYS_PLAYHEAD, 6},

    // === SPLICE MANAGEMENT (complex state machine) ===
    {"splice", SUBSYS_SPLICE, 10},
    {"shift 1", SUBSYS_SPLICE, 8},
    {"shift -1", SUBSYS_SPLICE, 8},
    {"shift 0", SUBSYS_SPLICE, 7},
    {"shift 64", SUBSYS_SPLICE, 9},                  // MAX_SPLICES boundary
    {"shift -64", SUBSYS_SPLICE, 9},
    {"organize 0", SUBSYS_SPLICE, 8},
    {"organize 0.5", SUBSYS_SPLICE, 7},
    {"organize 1", SUBSYS_SPLICE, 8},
    {"clear_splices", SUBSYS_SPLICE, 10},            // Destructive
    {"clear_splices_except_current", SUBSYS_SPLICE, 10},
    {"clear_current_splice", SUBSYS_SPLICE, 9},
    {"splice_join_right", SUBSYS_SPLICE, 9},
    {"splice_join_all", SUBSYS_SPLICE, 9},
    {"splice_create_pos 0", SUBSYS_SPLICE, 7},
    {"splice_create_pos 1", SUBSYS_SPLICE, 7},
    {"splice_create_pos 2", SUBSYS_SPLICE, 7},
    {"splice_jump 0", SUBSYS_SPLICE, 6},
    {"splice_jump 1", SUBSYS_SPLICE, 6},
    {"splice_finish_nav 0", SUBSYS_SPLICE, 7},
    {"splice_finish_nav 1", SUBSYS_SPLICE, 7},
    {"splice_split 0", SUBSYS_SPLICE, 6},
    {"splice_split 1", SUBSYS_SPLICE, 6},

    // === RECORDING (state transitions) ===
    {"play 1", SUBSYS_RECORDING, 10},
    {"play 0", SUBSYS_RECORDING, 10},
    {"record 1", SUBSYS_RECORDING, 10},
    {"record 0", SUBSYS_RECORDING, 10},
    {"recsplice", SUBSYS_RECORDING, 9},
    {"recinput", SUBSYS_RECORDING, 9},
    {"sos 0", SUBSYS_RECORDING, 7},
    {"sos 0.5", SUBSYS_RECORDING, 6},
    {"sos 1", SUBSYS_RECORDING, 7},
    {"sos_mode 0", SUBSYS_RECORDING, 8},
    {"sos_mode 1", SUBSYS_RECORDING, 8},

    // === PITCH & SPEED ===
    {"speed -4", SUBSYS_PITCH_SPEED, 9},             // Minimum
    {"speed 4", SUBSYS_PITCH_SPEED, 9},              // Maximum
    {"speed 0", SUBSYS_PITCH_SPEED, 8},              // Edge case
    {"speed 1", SUBSYS_PITCH_SPEED, 5},
    {"pitch_mode 0", SUBSYS_PITCH_SPEED, 8},
    {"pitch_mode 1", SUBSYS_PITCH_SPEED, 8},
    {"pitch_mode 2", SUBSYS_PITCH_SPEED, 8},
    {"pitch_mode 3", SUBSYS_PITCH_SPEED, 8},
    {"pitch_mode 4", SUBSYS_PITCH_SPEED, 8},
    {"pitch_semitones -24", SUBSYS_PITCH_SPEED, 9},
    {"pitch_semitones 24", SUBSYS_PITCH_SPEED, 9},
    {"pitch_range -24 24", SUBSYS_PITCH_SPEED, 8},
    {"pitch_scale 0 2 4 5 7 9 11", SUBSYS_PITCH_SPEED, 7},

    // === DELAY ===
    {"gdelay_time 0", SUBSYS_DELAY, 8},
    {"gdelay_time 9.5", SUBSYS_DELAY, 9},            // Maximum
    {"gdelay_feed 0", SUBSYS_DELAY, 7},
    {"gdelay_feed 1", SUBSYS_DELAY, 9},              // Infinite feedback
    {"gdelay_tone 0", SUBSYS_DELAY, 7},
    {"gdelay_tone 1", SUBSYS_DELAY, 7},
    {"gdelay_mix 0", SUBSYS_DELAY, 6},
    {"gdelay_mix 1", SUBSYS_DELAY, 6},
    {"gdelay_clear", SUBSYS_DELAY, 8},

    // === DISTORTION (complex signal processing) ===
    {"distortion 0", SUBSYS_DISTORTION, 7},
    {"distortion 1", SUBSYS_DISTORTION, 8},
    {"distortion_enable 0", SUBSYS_DISTORTION, 6},
    {"distortion_enable 1", SUBSYS_DISTORTION, 6},
    {"distortion_oversampling 1", SUBSYS_DISTORTION, 8},
    {"distortion_oversampling 2", SUBSYS_DISTORTION, 7},
    {"distortion_oversampling 4", SUBSYS_DISTORTION, 7},
    {"distortion_oversampling 8", SUBSYS_DISTORTION, 8},
    {"distortion_position 0", SUBSYS_DISTORTION, 9}, // Per-grain
    {"distortion_position 1", SUBSYS_DISTORTION, 8}, // Post-mix
    {"dist_waveshaper_mode 0", SUBSYS_DISTORTION, 8},
    {"dist_waveshaper_mode 1", SUBSYS_DISTORTION, 8},
    {"dist_waveshaper_mode 2", SUBSYS_DISTORTION, 8},
    {"dist_waveshaper_mode 3", SUBSYS_DISTORTION, 8},
    {"dist_waveshaper_mode 4", SUBSYS_DISTORTION, 8},
    {"dist_emphasis_mode 0", SUBSYS_DISTORTION, 7},
    {"dist_emphasis_mode 1", SUBSYS_DISTORTION, 7},
    {"dist_emphasis_freq 100", SUBSYS_DISTORTION, 7},
    {"dist_emphasis_freq 5000", SUBSYS_DISTORTION, 7},
    {"dist_pregain 0.1", SUBSYS_DISTORTION, 8},
    {"dist_pregain 10", SUBSYS_DISTORTION, 9},
    {"dist_curve_blend 0", SUBSYS_DISTORTION, 7},
    {"dist_curve_blend 1", SUBSYS_DISTORTION, 7},
    {"dist_drive_pos 1", SUBSYS_DISTORTION, 8},
    {"dist_drive_pos 20", SUBSYS_DISTORTION, 9},
    {"dist_drive_neg 1", SUBSYS_DISTORTION, 8},
    {"dist_drive_neg 20", SUBSYS_DISTORTION, 9},
    {"dist_poly_c1 -10", SUBSYS_DISTORTION, 9},
    {"dist_poly_c1 10", SUBSYS_DISTORTION, 9},
    {"dist_poly_c2 -10", SUBSYS_DISTORTION, 9},
    {"dist_poly_c2 10", SUBSYS_DISTORTION, 9},
    {"dist_poly_c3 -10", SUBSYS_DISTORTION, 9},
    {"dist_poly_c3 10", SUBSYS_DISTORTION, 9},
    {"distortion_pre_hp_freq 30", SUBSYS_DISTORTION, 7},
    {"distortion_pre_hp_freq 500", SUBSYS_DISTORTION, 7},
    {"distortion_pre_hp_mix 0", SUBSYS_DISTORTION, 6},
    {"distortion_pre_hp_mix 1", SUBSYS_DISTORTION, 6},
    {"distortion_post_lp_freq 2400", SUBSYS_DISTORTION, 7},
    {"distortion_post_lp_freq 10000", SUBSYS_DISTORTION, 7},
    {"distortion_post_lp_mix 0", SUBSYS_DISTORTION, 6},
    {"distortion_post_lp_mix 1", SUBSYS_DISTORTION, 6},
    {"distortion_notch_freq 20", SUBSYS_DISTORTION, 8},
    {"distortion_notch_freq 23900", SUBSYS_DISTORTION, 9}, // Near Nyquist
    {"distortion_notch_bw 10", SUBSYS_DISTORTION, 8},
    {"distortion_notch_bw 5000", SUBSYS_DISTORTION, 8},
    {"distortion_notch_mix 0", SUBSYS_DISTORTION, 6},
    {"distortion_notch_mix 1", SUBSYS_DISTORTION, 7},

    // === MOOG FILTER ===
    {"moog_cutoff 20", SUBSYS_MOOG, 9},              // Minimum
    {"moog_cutoff 20000", SUBSYS_MOOG, 9},           // Maximum
    {"moog_resonance 0", SUBSYS_MOOG, 7},
    {"moog_resonance 4", SUBSYS_MOOG, 10},           // Self-oscillation
    {"moog_mix 0", SUBSYS_MOOG, 6},
    {"moog_mix 1", SUBSYS_MOOG, 6},
    {"moog_enable 0", SUBSYS_MOOG, 6},
    {"moog_enable 1", SUBSYS_MOOG, 6},

    // === MODULATION SYSTEM (most complex) ===
    {"param_range speed 0.5 2.0", SUBSYS_MODULATION, 9},
    {"param_range grainsize 0.001 10.0", SUBSYS_MODULATION, 9},
    {"param_range amplitude 0 2", SUBSYS_MODULATION, 8},
    {"param_range pan 0 1", SUBSYS_MODULATION, 8},
    {"param_range iot 0.0005 2.0", SUBSYS_MODULATION, 9},
    {"param_range moog_cutoff 20 20000", SUBSYS_MODULATION, 8},
    {"rand_type rand_1", SUBSYS_MODULATION, 8},
    {"rand_type rand_4", SUBSYS_MODULATION, 8},
    {"rand_type perlin_1d_1 amplitude", SUBSYS_MODULATION, 8},
    {"rand_type perlin_2d_1 pan", SUBSYS_MODULATION, 8},
    {"rand_type lorenz_1 speed", SUBSYS_MODULATION, 9},
    {"rand_type lorenz_4 grainsize", SUBSYS_MODULATION, 9},
    {"rand_type nbody_1 amplitude", SUBSYS_MODULATION, 10},
    {"rand_type nbody_4 moog_cutoff", SUBSYS_MODULATION, 10},
    {"noise_freq 0.001", SUBSYS_MODULATION, 9},
    {"noise_freq 1000", SUBSYS_MODULATION, 9},
    {"noise_freq_1 100", SUBSYS_MODULATION, 7},
    {"nbody_mode 1 0", SUBSYS_MODULATION, 8},
    {"nbody_mode 1 10", SUBSYS_MODULATION, 9},
    {"nbody_epsilon 1 0.001", SUBSYS_MODULATION, 9},
    {"nbody_damping 1 0.1", SUBSYS_MODULATION, 9},
    {"nbody_reset 1", SUBSYS_MODULATION, 7},
    {"perlin_reset 1", SUBSYS_MODULATION, 7},
    {"lorenz_reset 1", SUBSYS_MODULATION, 7},
    {"param_lock pan", SUBSYS_MODULATION, 8},
    {"param_lock pan amplitude grainsize", SUBSYS_MODULATION, 9},
    {"param_slew modout1 0.9", SUBSYS_MODULATION, 8},
    {"modout1_source perlin_2d 1", SUBSYS_MODULATION, 8},
    {"modout1_range 0 1", SUBSYS_MODULATION, 7},

    // === QUANTIZATION ===
    {"timesig 4/4", SUBSYS_QUANTIZATION, 7},
    {"timesig 7/8", SUBSYS_QUANTIZATION, 8},
    {"timesig 1/1", SUBSYS_QUANTIZATION, 8},
    {"timesig 128/128", SUBSYS_QUANTIZATION, 9},
    {"quantize 1", SUBSYS_QUANTIZATION, 8},
    {"quantize 128", SUBSYS_QUANTIZATION, 9},
    {"quant 0", SUBSYS_QUANTIZATION, 7},
    {"quant 1", SUBSYS_QUANTIZATION, 8},
    {"gs_timesig 4/4", SUBSYS_QUANTIZATION, 7},
    {"gs_quantize 16", SUBSYS_QUANTIZATION, 7},
    {"gs_quant 1", SUBSYS_QUANTIZATION, 8},
    {"delay_timesig 4/4", SUBSYS_QUANTIZATION, 7},
    {"delay_quantize 16", SUBSYS_QUANTIZATION, 7},
    {"delay_quant 1", SUBSYS_QUANTIZATION, 8},

    // === ENVELOPE ===
    {"envelope 0", SUBSYS_ENVELOPE, 7},              // Parabolic
    {"envelope 1", SUBSYS_ENVELOPE, 7},              // Trapezoidal
    {"envelope 2", SUBSYS_ENVELOPE, 7},              // Cosine
    {"env_skew 0", SUBSYS_ENVELOPE, 8},
    {"env_skew 0.5", SUBSYS_ENVELOPE, 6},
    {"env_skew 1", SUBSYS_ENVELOPE, 8},

    // === FILE I/O ===
    {"load test.wav", SUBSYS_FILE_IO, 9},
    {"save output.wav", SUBSYS_FILE_IO, 8},

    // === STATE QUERY ===
    {"get_inlets", SUBSYS_STATE_QUERY, 5},
    {"get_params", SUBSYS_STATE_QUERY, 6},
    {"get_ranges", SUBSYS_STATE_QUERY, 6},
    {"get_generators", SUBSYS_STATE_QUERY, 6},
    {"get_state", SUBSYS_STATE_QUERY, 7},
    {"query speed", SUBSYS_STATE_QUERY, 5},
    {"query bpm", SUBSYS_STATE_QUERY, 6},

    {NULL, 0, 0}  // Terminator
};

// Initialize corpus from seeds
corpus_t *corpus_init_from_seeds(void) {
    corpus_t *corpus = calloc(1, sizeof(corpus_t));
    if (!corpus) return NULL;

    corpus->capacity = 4096;
    corpus->entries = calloc(corpus->capacity, sizeof(corpus_entry_t));
    corpus->selection_weights = calloc(corpus->capacity, sizeof(double));
    corpus->energy_scores = calloc(corpus->capacity, sizeof(uint32_t));

    if (!corpus->entries || !corpus->selection_weights || !corpus->energy_scores) {
        corpus_free(corpus);
        return NULL;
    }

    // Load all seeds
    for (const seed_entry_t *seed = SEED_CORPUS; seed->message != NULL; seed++) {
        corpus_entry_t *entry = &corpus->entries[corpus->count];

        size_t len = strlen(seed->message);
        entry->data = malloc(len + 1);
        if (!entry->data) continue;

        memcpy(entry->data, seed->message, len + 1);
        entry->length = len;
        entry->subsystem = seed->subsystem;
        entry->crash_potential = seed->priority;
        entry->mutation_depth = 0;
        entry->parent_idx = UINT32_MAX;  // No parent (seed)
        entry->mutation_type = 0;
        entry->coverage_hits = 0;
        entry->last_mutated = 0;

        // Initial weight based on priority
        corpus->selection_weights[corpus->count] = (double)seed->priority;
        corpus->total_weight += seed->priority;

        corpus->count++;
    }

    return corpus;
}

void corpus_free(corpus_t *corpus) {
    if (!corpus) return;

    for (size_t i = 0; i < corpus->count; i++) {
        free(corpus->entries[i].data);
    }

    free(corpus->entries);
    free(corpus->selection_weights);
    free(corpus->energy_scores);
    free(corpus);
}
