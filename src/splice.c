// @region:ligase_pd.core.splice Splice Management

#include "types.h"
#include <string.h>

// @region:ligase_pd.core.splice.markers Splice Markers

int splice_add(splice_array_t *splices, uint32_t position, const char *label) {
    // Validate array bounds
    if (!splices) return -1;
    if (splices->count >= MAX_SPLICES) return -1;  // Error: max splices reached

    splice_marker_t *marker = &splices->markers[splices->count];
    marker->position = position;
    if (label) {
        strncpy(marker->label, label, sizeof(marker->label) - 1);
        marker->label[sizeof(marker->label) - 1] = '\0';  // Ensure null termination
    } else {
        marker->label[0] = '\0';
    }
    marker->message[0] = '\0';  // Initialize message to empty

    splices->count++;
    return 0;  // Success
}

void splice_clear(splice_array_t *splices) {
    splices->count = 0;
    splices->current_splice = 0;
}

void splice_remove(splice_array_t *splices, int index) {
    if (!splices) return;
    if (index < 0 || index >= splices->count) return;
    if (splices->count <= 0) return;

    // Use memmove for safe overlapping copy
    if (index < splices->count - 1) {
        memmove(&splices->markers[index],
                &splices->markers[index + 1],
                (splices->count - index - 1) * sizeof(splice_marker_t));
    }
    splices->count--;

    // Update current splice index if needed
    if (splices->count == 0) {
        splices->current_splice = 0;
    } else if (splices->current_splice >= splices->count) {
        splices->current_splice = splices->count - 1;
    }
}

int splice_find_at_position(splice_array_t *splices, uint32_t position, uint32_t tolerance) {
    if (!splices || splices->count == 0) return -1;

    for (int i = 0; i < splices->count; i++) {
        uint32_t diff = (position > splices->markers[i].position) ?
                        (position - splices->markers[i].position) :
                        (splices->markers[i].position - position);
        if (diff <= tolerance) {
            return i;
        }
    }
    return -1;
}

// @endregion:ligase_pd.core.splice.markers

// @region:ligase_pd.core.splice.navigation Splice Navigation

void splice_get_bounds(splice_array_t *splices, int splice_index, uint32_t reel_length,
                      uint32_t *start, uint32_t *end) {
    if (!splices || !start || !end) return;

    if (splices->count == 0 || reel_length == 0) {
        *start = 0;
        *end = reel_length;
        return;
    }

    // Clamp splice index to valid range
    if (splice_index < 0) splice_index = 0;
    if (splice_index >= splices->count) splice_index = splices->count - 1;

    *start = splices->markers[splice_index].position;

    // Ensure start is within reel bounds
    if (*start >= reel_length) *start = 0;

    // Calculate end position
    if (splice_index + 1 < splices->count) {
        *end = splices->markers[splice_index + 1].position;
        if (*end > reel_length) *end = reel_length;
    } else {
        *end = reel_length;
    }

    // Validate bounds - ensure start < end
    if (*start >= *end) {
        *start = 0;
        *end = reel_length;
    }
}

void splice_shift(splice_array_t *splices, int delta) {
    if (!splices || splices->count == 0) return;

    int target = splices->current_splice + delta;
    int count = splices->count;

    // FIX: Wrap around circularly instead of clamping (handles negative values correctly)
    // This makes immediate mode consistent with deferred mode
    target = ((target % count) + count) % count;

    splices->current_splice = target;
}

void splice_clear_except_current(splice_array_t *splices, uint32_t reel_length) {
    if (!splices || splices->count == 0) return;

    // Get current splice bounds
    uint32_t start, end;
    splice_get_bounds(splices, splices->current_splice, reel_length, &start, &end);

    int current = splices->current_splice;
    uint32_t splice_length = end - start;

    // Keep only the markers that define the current splice
    // Adjust positions so splice now starts at 0
    if (current + 1 < splices->count) {
        // Current splice has a marker to its right
        splices->markers[0].position = 0;  // Splice now starts at 0
        splices->markers[0].label[0] = '\0';
        splices->markers[0].message[0] = '\0';
        splices->markers[1].position = splice_length;  // End marker at splice length
        splices->markers[1].label[0] = '\0';
        splices->markers[1].message[0] = '\0';
        splices->count = 2;
    } else {
        // Current splice goes to end of reel
        splices->markers[0].position = 0;  // Splice now starts at 0
        splices->markers[0].label[0] = '\0';
        splices->markers[0].message[0] = '\0';
        splices->count = 1;
    }

    splices->current_splice = 0;
}

void splice_join_right(splice_array_t *splices) {
    if (!splices || splices->count == 0) return;

    // Can't join if only one splice
    if (splices->count == 1) return;

    // Calculate right splice index with wraparound
    int right_splice = (splices->current_splice + 1) % splices->count;

    // Remove the marker at the right splice (boundary between them)
    splice_remove(splices, right_splice);

    // Update current splice index if needed
    if (right_splice <= splices->current_splice && splices->current_splice > 0) {
        splices->current_splice--;
    }
}

void splice_join_all(splice_array_t *splices) {
    if (!splices) return;

    // Join all splices by clearing all markers
    // This makes the entire reel one continuous splice
    splice_clear(splices);
}

// @endregion:ligase_pd.core.splice.navigation

// @region:ligase_pd.core.splice.organize Organize Control

void splice_organize(splice_array_t *splices, float normalized_input) {
    if (!splices || splices->count == 0) return;

    // Map normalized input (0.0 to 1.0) to discrete splice index
    // Scaling is uniform across the number of splices
    float normalized = normalized_input;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;

    int splice_idx = (int)(normalized * (splices->count - 1));

    // Bounds check (should be redundant but safe)
    if (splice_idx < 0) splice_idx = 0;
    if (splice_idx >= splices->count) splice_idx = splices->count - 1;

    splices->current_splice = splice_idx;
}

// @endregion:ligase_pd.core.splice.organize

// @endregion:ligase_pd.core.splice
