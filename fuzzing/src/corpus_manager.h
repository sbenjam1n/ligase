#ifndef CORPUS_MANAGER_H
#define CORPUS_MANAGER_H

#include <stdint.h>
#include <stddef.h>

// Subsystem categories based on ligase~ architecture
typedef enum {
    SUBSYS_GRAIN_CORE      = 0x0001,  // grainsize, iot, maxgrains
    SUBSYS_PLAYHEAD        = 0x0002,  // playhead modes, scanrate
    SUBSYS_SPLICE          = 0x0004,  // splice navigation, creation
    SUBSYS_RECORDING       = 0x0008,  // record modes, sos
    SUBSYS_PITCH_SPEED     = 0x0010,  // pitch_mode, speed
    SUBSYS_DELAY           = 0x0020,  // gdelay_*
    SUBSYS_DISTORTION      = 0x0040,  // distortion_*, dist_*
    SUBSYS_MOOG            = 0x0080,  // moog_*
    SUBSYS_MODULATION      = 0x0100,  // param_range, rand_type, nbody
    SUBSYS_QUANTIZATION    = 0x0200,  // timesig, quantize, quant
    SUBSYS_FILE_IO         = 0x0400,  // load, save
    SUBSYS_ENVELOPE        = 0x0800,  // envelope, env_skew
    SUBSYS_STATE_QUERY     = 0x1000,  // get_*, query
    SUBSYS_CROSS_SYSTEM    = 0x8000   // Messages affecting multiple systems
} ligase_subsystem_t;

// Corpus entry with metadata for intelligent mutation
typedef struct {
    uint8_t *data;
    size_t length;

    // Metadata for guided fuzzing
    uint32_t subsystem;        // Which ligase~ subsystem this targets
    uint32_t coverage_hits;    // How many new edges this found
    uint32_t crash_potential;  // Heuristic score for crash likelihood
    uint64_t last_mutated;     // Timestamp for freshness
    uint32_t mutation_depth;   // How many mutations from seed

    // Lineage tracking
    uint32_t parent_idx;       // Index of parent corpus entry
    uint8_t mutation_type;     // What mutation created this
} corpus_entry_t;

typedef struct {
    corpus_entry_t *entries;
    size_t count;
    size_t capacity;

    // Coverage-guided selection weights
    double *selection_weights;
    double total_weight;

    // Subsystem coverage tracking
    uint32_t subsystem_coverage[16];

    // Energy scheduling (AFL-style)
    uint32_t *energy_scores;
} corpus_t;

// Mutation strategies
typedef enum {
    MUT_BIT_FLIP,
    MUT_BYTE_FLIP,
    MUT_ARITHMETIC,
    MUT_INTERESTING_VALUES,
    MUT_SPLICE,
    MUT_INSERT,
    MUT_DELETE,
    MUT_DICTIONARY
} mutation_type_t;

// Fast PRNG for mutation
static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

// Function declarations
corpus_t *corpus_init_from_seeds(void);
void corpus_free(corpus_t *corpus);
size_t corpus_select_weighted(corpus_t *corpus, uint32_t *rng_state);
void corpus_update_energy(corpus_t *corpus, size_t idx, uint32_t new_coverage, int caused_crash);
void corpus_add_entry(corpus_t *corpus, const uint8_t *data, size_t length,
                      uint32_t parent_idx, uint8_t mutation_type, uint32_t new_coverage);
void mutate_message(uint8_t *buf, size_t *len, size_t max_len, uint32_t *rng_state);

#endif // CORPUS_MANAGER_H
