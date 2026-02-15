#include "corpus_manager.h"
#include <stdlib.h>
#include <string.h>

// Select corpus entry using weighted random selection
// Higher coverage entries get more mutations
size_t corpus_select_weighted(corpus_t *corpus, uint32_t *rng_state) {
    if (!corpus || corpus->count == 0) return 0;

    double target = (xorshift32(rng_state) / (double)UINT32_MAX) * corpus->total_weight;
    double cumulative = 0.0;

    for (size_t i = 0; i < corpus->count; i++) {
        cumulative += corpus->selection_weights[i];
        if (cumulative >= target) {
            return i;
        }
    }
    return corpus->count - 1;
}

// Update weights based on coverage feedback
void corpus_update_energy(corpus_t *corpus, size_t idx, uint32_t new_coverage,
                          int caused_crash) {
    if (!corpus || idx >= corpus->count) return;

    corpus_entry_t *entry = &corpus->entries[idx];

    // Remove old weight
    corpus->total_weight -= corpus->selection_weights[idx];

    // Calculate new energy score
    double energy = 1.0;

    // Boost for new coverage
    if (new_coverage > 0) {
        energy += new_coverage * 10.0;
        entry->coverage_hits += new_coverage;
    }

    // Boost for crash potential
    if (caused_crash) {
        energy *= 5.0;
        entry->crash_potential += 10;
    }

    // Decay based on mutation depth (favor shallow mutations)
    energy /= (1.0 + entry->mutation_depth * 0.1);

    // Boost under-explored subsystems
    uint32_t subsys = entry->subsystem;
    if (subsys != 0) {
        int subsys_idx = __builtin_ctz(subsys);  // Count trailing zeros
        if (subsys_idx < 16) {
            uint32_t subsys_coverage = corpus->subsystem_coverage[subsys_idx];
            if (subsys_coverage < 100) {
                energy *= 2.0;
            }
        }
    }

    corpus->selection_weights[idx] = energy;
    corpus->total_weight += energy;
}

// Add new corpus entry (from mutation that found new coverage)
void corpus_add_entry(corpus_t *corpus, const uint8_t *data, size_t length,
                      uint32_t parent_idx, uint8_t mutation_type,
                      uint32_t new_coverage) {
    if (!corpus || !data) return;

    if (corpus->count >= corpus->capacity) {
        // TODO: Grow or evict low-value entries
        return;
    }

    corpus_entry_t *entry = &corpus->entries[corpus->count];
    entry->data = malloc(length + 1);
    if (!entry->data) return;

    memcpy(entry->data, data, length);
    entry->data[length] = '\0';
    entry->length = length;

    // Inherit subsystem from parent
    if (parent_idx < corpus->count) {
        entry->subsystem = corpus->entries[parent_idx].subsystem;
        entry->mutation_depth = corpus->entries[parent_idx].mutation_depth + 1;
    } else {
        entry->subsystem = 0;
        entry->mutation_depth = 0;
    }

    entry->parent_idx = parent_idx;
    entry->mutation_type = mutation_type;
    entry->coverage_hits = new_coverage;
    entry->last_mutated = 0;
    entry->crash_potential = 0;

    // Initial weight
    double weight = 10.0 + new_coverage * 5.0;
    corpus->selection_weights[corpus->count] = weight;
    corpus->total_weight += weight;

    corpus->count++;
}
