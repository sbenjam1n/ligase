// @region:ligase_pd.core.buffer Buffer Management

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

// @region:ligase_pd.core.buffer.reel Reel Buffer

reel_t* reel_create() {
    reel_t *reel = (reel_t*)malloc(sizeof(reel_t));
    if (!reel) return NULL;  // Memory allocation failed

    int max_samples = MAX_REEL_SECONDS * SAMPLE_RATE;

    reel->buffer_left = (float*)calloc(max_samples, sizeof(float));
    if (!reel->buffer_left) {
        free(reel);
        return NULL;  // Left buffer allocation failed
    }

    reel->buffer_right = (float*)calloc(max_samples, sizeof(float));
    if (!reel->buffer_right) {
        free(reel->buffer_left);
        free(reel);
        return NULL;  // Right buffer allocation failed
    }

    reel->length = 0;
    reel->splices.count = 0;
    reel->splices.current_splice = 0;
    memset(reel->filename, 0, sizeof(reel->filename));

    return reel;
}

void reel_destroy(reel_t *reel) {
    if (reel) {
        if (reel->buffer_left) free(reel->buffer_left);
        if (reel->buffer_right) free(reel->buffer_right);
        free(reel);
    }
}

void reel_clear(reel_t *reel) {
    if (!reel) return;
    int max_samples = MAX_REEL_SECONDS * SAMPLE_RATE;
    memset(reel->buffer_left, 0, max_samples * sizeof(float));
    memset(reel->buffer_right, 0, max_samples * sizeof(float));
    reel->length = 0;
    reel->splices.count = 0;
}

void reel_clear_except_splice(reel_t *reel, uint32_t splice_start, uint32_t splice_end) {
    if (!reel) return;

    // Calculate splice length
    uint32_t splice_length = splice_end - splice_start;

    // If splice doesn't start at 0, move it to the beginning
    if (splice_start > 0 && splice_length > 0) {
        memmove(reel->buffer_left, reel->buffer_left + splice_start, splice_length * sizeof(float));
        memmove(reel->buffer_right, reel->buffer_right + splice_start, splice_length * sizeof(float));
    }

    // Clear everything after the splice
    int max_samples = MAX_REEL_SECONDS * SAMPLE_RATE;
    if (splice_length < max_samples) {
        memset(reel->buffer_left + splice_length, 0, (max_samples - splice_length) * sizeof(float));
        memset(reel->buffer_right + splice_length, 0, (max_samples - splice_length) * sizeof(float));
    }

    // Update reel length to match the kept splice
    reel->length = splice_length;
}

// @endregion:ligase_pd.core.buffer.reel

// @region:ligase_pd.core.buffer.record Record Buffer

recorder_t* recorder_create(reel_t *reel) {
    recorder_t *rec = (recorder_t*)malloc(sizeof(recorder_t));
    if (!rec) return NULL;  // Memory allocation failed

    rec->reel = reel;
    rec->record_position = 0;
    rec->is_recording = 0;
    rec->crossfade_mix = 0.5f;
    rec->mode = RECORD_MODE_OVERDUB;
    rec->current_splice_start = 0;
    rec->current_splice_end = 0;
    rec->new_splice_start = 0;
    return rec;
}

void recorder_destroy(recorder_t *rec) {
    if (rec) free(rec);
}

void recorder_start(recorder_t *rec) {
    rec->is_recording = 1;

    if (rec->mode == RECORD_MODE_NEW_SPLICE || rec->mode == RECORD_MODE_INPUT_ONLY) {
        // Record onto end, creating new splice
        rec->new_splice_start = rec->reel->length;
        rec->record_position = rec->reel->length;
    } else {
        // Morphagene "Rec": overdub into current splice
        rec->record_position = rec->current_splice_start;
    }
}

void recorder_stop(recorder_t *rec) {
    rec->is_recording = 0;
}

void recorder_set_splice_bounds(recorder_t *rec, int start, int end) {
    rec->current_splice_start = start;
    rec->current_splice_end = end;
}

void recorder_set_mode(recorder_t *rec, record_mode_t mode) {
    rec->mode = mode;
}

void recorder_process(recorder_t *rec, float *in_left, float *in_right, int blocksize) {
    if (!rec->is_recording) return;

    int max_samples = MAX_REEL_SECONDS * SAMPLE_RATE;

    for (int i = 0; i < blocksize; i++) {
        // Circular buffer: wrap around when reaching capacity
        // Per Morphagene theory: "wraps to beginning, overwriting oldest data"
        if (rec->record_position >= max_samples) {
            rec->record_position = rec->record_position % max_samples;
        }

        // Check splice bounds for overdub mode only
        if (rec->mode == RECORD_MODE_OVERDUB && rec->record_position >= rec->current_splice_end) {
            // Don't loop - let it continue into next splice for cross-splice recording
            // This allows recording across splice boundaries when shifting
        }

        // Recording logic based on mode
        if (rec->mode == RECORD_MODE_INPUT_ONLY) {
            // Input-only mode: record input directly without mixing with existing audio
            rec->reel->buffer_left[rec->record_position] = in_left[i];
            rec->reel->buffer_right[rec->record_position] = in_right[i];
        } else {
            // STABILITY FIX: Sound-on-Sound mixing with clamped crossfade (OVERDUB and NEW_SPLICE modes)
            float existing_left = rec->reel->buffer_left[rec->record_position];
            float existing_right = rec->reel->buffer_right[rec->record_position];

            // Clamp crossfade_mix to [0.0, 1.0] to prevent amplification or phase issues
            float mix = rec->crossfade_mix;
            if (mix < 0.0f) mix = 0.0f;
            if (mix > 1.0f) mix = 1.0f;

            rec->reel->buffer_left[rec->record_position] =
                in_left[i] * mix + existing_left * (1.0f - mix);
            rec->reel->buffer_right[rec->record_position] =
                in_right[i] * mix + existing_right * (1.0f - mix);
        }

        rec->record_position++;

        // Extend reel length when recording beyond current length
        // This ensures recording on an empty buffer works in ALL modes
        // Previously only NEW_SPLICE and INPUT_ONLY extended the buffer,
        // causing OVERDUB mode to record silence when starting from empty
        if (rec->record_position > rec->reel->length) {
            int old_length = rec->reel->length;
            rec->reel->length = (rec->record_position < max_samples) ? rec->record_position : max_samples;

            //  Auto-create initial splice when first recording to empty buffer
            // This ensures playback works immediately after recording without manual splice creation
            if (old_length == 0 && rec->reel->length > 0 && rec->reel->splices.count == 0) {
                rec->reel->splices.markers[0].position = 0;
                rec->reel->splices.markers[0].label[0] = '\0';
                rec->reel->splices.count = 1;
                rec->reel->splices.current_splice = 0;
            }
        }
    }
}

void recorder_set_crossfade_mix(recorder_t *rec, float mix) {
    if (rec) rec->crossfade_mix = mix;
}

int recorder_get_new_splice_start(recorder_t *rec) {
    return rec ? rec->new_splice_start : 0;
}

// @endregion:ligase_pd.core.buffer.record

// @region:ligase_pd.core.buffer.wav WAV File I/O

// Simple WAV header structure
typedef struct {
    char riff[4];           // "RIFF"
    uint32_t file_size;
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;
    uint16_t format;        // 3 = IEEE float
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];           // "data"
    uint32_t data_size;
} wav_header_t;

// WAV cue point structure (24 bytes each)
typedef struct {
    uint32_t id;            // Unique identifier
    uint32_t position;      // Sample position
    char data_chunk[4];     // "data"
    uint32_t chunk_start;   // Byte offset to containing chunk
    uint32_t block_start;   // Byte offset within chunk
    uint32_t sample_offset; // Sample offset (same as position for uncompressed)
} cue_point_t;

// WAV cue chunk header
typedef struct {
    char chunk_id[4];       // "cue "
    uint32_t chunk_size;    // Size of chunk data (not including header)
    uint32_t num_cue_points;
} cue_chunk_header_t;

static int is_path_safe(const char *path) {
    if (strstr(path, "..")) {
        return 0; // Path contains ".."
    }
    return 1;
}

int reel_load_wav(reel_t *reel, const char *filename) {
    if (!is_path_safe(filename)) {
        fprintf(stderr, "ligase~: ERROR - Path traversal detected in filename\n");
        return -1;
    }
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "ligase~: ERROR - Cannot open file: %s\n", filename);
        return -1;
    }

    wav_header_t header;
    if (fread(&header, sizeof(wav_header_t), 1, f) != 1) {
        fprintf(stderr, "ligase~: ERROR - Invalid or corrupt WAV header\n");
        fclose(f);
        return -1;
    }

    //  Verify format with detailed error messages
    if (header.format != 3) {
        fprintf(stderr, "ligase~: ERROR - WAV must be 32-bit float format (format code 3), got format %d\n", header.format);
        fclose(f);
        return -1;
    }
    if (header.channels != 2) {
        fprintf(stderr, "ligase~: ERROR - WAV must be stereo (2 channels), got %d channels\n", header.channels);
        fclose(f);
        return -1;
    }
    if (header.sample_rate != SAMPLE_RATE) {
        fprintf(stderr, "ligase~: ERROR - WAV must be %d Hz, got %d Hz\n", SAMPLE_RATE, header.sample_rate);
        fclose(f);
        return -1;
    }

    int num_samples = header.data_size / (header.channels * sizeof(float));
    if (num_samples > MAX_REEL_SECONDS * SAMPLE_RATE) {
        num_samples = MAX_REEL_SECONDS * SAMPLE_RATE;
    }

    // Read interleaved stereo samples
    float *temp = (float*)malloc(num_samples * 2 * sizeof(float));
    if (!temp) {
        fprintf(stderr, "ligase~: ERROR - Out of memory loading WAV file (%d samples)\n", num_samples);
        fclose(f);
        return -1;
    }
    fread(temp, sizeof(float), num_samples * 2, f);

    // Deinterleave
    for (int i = 0; i < num_samples; i++) {
        reel->buffer_left[i] = temp[i * 2];
        reel->buffer_right[i] = temp[i * 2 + 1];
    }

    free(temp);

    reel->length = num_samples;
    strncpy(reel->filename, filename, sizeof(reel->filename) - 1);

    // Clear existing splices before loading cue points
    reel->splices.count = 0;
    reel->splices.current_splice = 0;

    // Look for cue chunk after data chunk
    char chunk_id[4];
    uint32_t chunk_size;

    while (fread(chunk_id, 4, 1, f) == 1) {
        if (fread(&chunk_size, 4, 1, f) != 1) break;

        if (memcmp(chunk_id, "cue ", 4) == 0) {
            // Found cue chunk - read cue points
            uint32_t num_cues;
            if (fread(&num_cues, 4, 1, f) != 1) break;

            // Limit to MAX_SPLICES
            if (num_cues > MAX_SPLICES) num_cues = MAX_SPLICES;

            for (uint32_t i = 0; i < num_cues; i++) {
                cue_point_t cue;
                if (fread(&cue, sizeof(cue_point_t), 1, f) != 1) break;

                // Convert cue point to splice marker (cue.position is sample offset)
                if (reel->splices.count < MAX_SPLICES) {
                    reel->splices.markers[reel->splices.count].position = cue.position;
                    reel->splices.markers[reel->splices.count].label[0] = '\0';
                    reel->splices.count++;
                }
            }
            break;  // Found and processed cue chunk
        } else {
            // Skip unknown chunk
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    // If no cue points were found, create a default splice covering entire file
    // This ensures playback works for standard WAV files without cue markers
    if (reel->splices.count == 0 && reel->length > 0) {
        reel->splices.markers[0].position = 0;
        reel->splices.markers[0].label[0] = '\0';
        reel->splices.count = 1;
        reel->splices.current_splice = 0;
    }

    fclose(f);
    return 0;
}

int reel_save_wav(reel_t *reel, const char *filename) {
    if (!is_path_safe(filename)) {
        fprintf(stderr, "ligase~: ERROR - Path contains illegal '..' sequence: %s\n", filename);
        return -1; // Path traversal attempt
    }

    // Check if there's any audio to save
    if (reel->length == 0) {
        fprintf(stderr, "ligase~: ERROR - Cannot save empty buffer (record or load audio first)\n");
        return -1;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "ligase~: ERROR - Cannot open file for writing: %s (errno: %d)\n", filename, errno);
        fprintf(stderr, "          Hint: Use absolute path like /tmp/out.wav or check directory exists\n");
        return -1;
    }

    // Calculate cue chunk size
    uint32_t cue_chunk_size = 0;
    if (reel->splices.count > 0) {
        cue_chunk_size = 4 + (reel->splices.count * sizeof(cue_point_t));  // num_cues + cue_points
    }

    wav_header_t header;
    memcpy(header.riff, "RIFF", 4);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    memcpy(header.data, "data", 4);

    header.fmt_size = 16;
    header.format = 3;  // IEEE float
    header.channels = 2;
    header.sample_rate = SAMPLE_RATE;
    header.bits_per_sample = 32;
    header.block_align = header.channels * (header.bits_per_sample / 8);
    header.byte_rate = header.sample_rate * header.block_align;
    header.data_size = reel->length * header.channels * sizeof(float);

    // Calculate total file size (including cue chunk if present)
    // RIFF header (8) + fmt chunk (8+16) + data chunk (8+data) + cue chunk (8+cue_size)
    header.file_size = 36 + header.data_size;
    if (cue_chunk_size > 0) {
        header.file_size += 8 + cue_chunk_size;  // chunk header + chunk data
    }

    fwrite(&header, sizeof(wav_header_t), 1, f);

    // Write interleaved stereo samples
    float *temp = (float*)malloc(reel->length * 2 * sizeof(float));
    if (!temp) {
        fclose(f);
        return -1;  // Memory allocation failed
    }

    for (int i = 0; i < reel->length; i++) {
        temp[i * 2] = reel->buffer_left[i];
        temp[i * 2 + 1] = reel->buffer_right[i];
    }

    fwrite(temp, sizeof(float), reel->length * 2, f);
    free(temp);

    // Write cue chunk if splices exist (Morphagene compatibility)
    if (reel->splices.count > 0) {
        cue_chunk_header_t cue_header;
        memcpy(cue_header.chunk_id, "cue ", 4);
        cue_header.chunk_size = cue_chunk_size;
        cue_header.num_cue_points = reel->splices.count;

        fwrite(&cue_header, sizeof(cue_chunk_header_t), 1, f);

        // Write each splice as a cue point
        for (int i = 0; i < reel->splices.count; i++) {
            cue_point_t cue;
            cue.id = i + 1;  // IDs start at 1
            cue.position = reel->splices.markers[i].position;
            memcpy(cue.data_chunk, "data", 4);
            cue.chunk_start = 0;
            cue.block_start = 0;
            cue.sample_offset = reel->splices.markers[i].position;

            fwrite(&cue, sizeof(cue_point_t), 1, f);
        }
    }

    fclose(f);

    strncpy(reel->filename, filename, sizeof(reel->filename) - 1);
    return 0;
}

// @endregion:ligase_pd.core.buffer.wav

// @endregion:ligase_pd.core.buffer
