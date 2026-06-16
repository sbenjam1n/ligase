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

    reel->sample_rate = SAMPLE_RATE;                       // default; updated to the host rate in dsp
    reel->capacity = MAX_REEL_SECONDS * reel->sample_rate;

    reel->buffer_left = (float*)calloc(reel->capacity, sizeof(float));
    if (!reel->buffer_left) {
        free(reel);
        return NULL;  // Left buffer allocation failed
    }

    reel->buffer_right = (float*)calloc(reel->capacity, sizeof(float));
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

// Resize the reel for a new host sample rate so capacity = MAX_REEL_SECONDS at that rate
// (keeps a full 10-minute reel and consistent timing at any rate). Existing audio was sampled
// at the old rate, so it is dropped on change. Called from the dsp method (main thread) only.
void reel_set_sample_rate(reel_t *reel, int sample_rate) {
    if (!reel || sample_rate <= 0) return;
    if (sample_rate == reel->sample_rate && reel->buffer_left && reel->buffer_right) return;

    int new_cap = MAX_REEL_SECONDS * sample_rate;
    float *nl = (float*)calloc(new_cap, sizeof(float));
    float *nr = (float*)calloc(new_cap, sizeof(float));
    if (!nl || !nr) {
        free(nl);
        free(nr);
        return;  // allocation failed: keep the existing buffer/rate
    }
    free(reel->buffer_left);
    free(reel->buffer_right);
    reel->buffer_left = nl;
    reel->buffer_right = nr;
    reel->capacity = new_cap;
    reel->sample_rate = sample_rate;
    reel->length = 0;
    reel->splices.count = 0;
    reel->splices.current_splice = 0;
}

void reel_clear(reel_t *reel) {
    if (!reel) return;
    int max_samples = reel->capacity;
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
    int max_samples = reel->capacity;
    if (splice_length < (uint32_t)max_samples) {
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

    int max_samples = rec->reel->capacity;

    for (int i = 0; i < blocksize; i++) {
        // Circular buffer: wrap around when reaching capacity
        // Per Morphagene theory: "wraps to beginning, overwriting oldest data"
        if (rec->record_position >= max_samples) {
            rec->record_position = rec->record_position % max_samples;
        }

        // Overdub: loop the record head WITHIN the current splice, so each pass re-records the
        // same region = Time Lag Accumulation. Cross-splice recording is done by SHIFTING the
        // current splice (which moves current_splice_start/end), not by running off the end —
        // so overdub never extends the reel bounds.
        if (rec->mode == RECORD_MODE_OVERDUB &&
            rec->current_splice_end > rec->current_splice_start &&
            rec->record_position >= rec->current_splice_end) {
            rec->record_position = rec->current_splice_start;
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
    // Block parent-directory traversal, but allow legit names like "my..mix.wav".
    // (Paths arriving from ligase~ are already canvas-resolved, so this is a light guard.)
    if (strstr(path, "../") || strstr(path, "..\\")) return 0;
    return 1;
}

// Human-readable reason for a reel_io_status_t (for the Pd layer to surface via pd_error).
const char *reel_io_strerror(int status) {
    switch (status) {
        case REEL_IO_OK:         return "ok";
        case REEL_IO_ERR_OPEN:   return "cannot open file (not found or unwritable)";
        case REEL_IO_ERR_BADWAV: return "not a valid WAV (missing fmt/data chunk)";
        case REEL_IO_ERR_FORMAT: return "unsupported format — need stereo 16-bit PCM or 32-bit float";
        case REEL_IO_ERR_READ:   return "truncated file / short read or write";
        case REEL_IO_ERR_MEM:    return "out of memory";
        case REEL_IO_ERR_EMPTY:  return "buffer is empty — record or load audio first";
        default:                 return "unknown error";
    }
}

int reel_load_wav(reel_t *reel, const char *filename) {
    if (!is_path_safe(filename)) {
        fprintf(stderr, "ligase~: load - unsafe path: %s\n", filename);
        return REEL_IO_ERR_OPEN;
    }
    FILE *f = fopen(filename, "rb");
    if (!f) return REEL_IO_ERR_OPEN;

    // RIFF / WAVE container (little-endian; this build targets LE hosts)
    char id[4];
    uint32_t u32;
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) != 0 ||
        fread(&u32, 4, 1, f) != 1 ||                              // RIFF size (ignored)
        fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4) != 0) {
        fclose(f);
        return REEL_IO_ERR_BADWAV;
    }

    // Walk chunks: capture fmt fields, the data chunk location, and any cue points. This
    // tolerates extra chunks (LIST/fact/bext), a non-16-byte fmt, and data-after-cue ordering.
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0, data_size = 0;
    long data_pos = -1;
    int have_fmt = 0;
    reel->splices.count = 0;
    reel->splices.current_splice = 0;

    uint32_t csize;
    while (fread(id, 1, 4, f) == 4 && fread(&csize, 4, 1, f) == 1) {
        long next = ftell(f) + (long)csize + (csize & 1);   // chunks are word-aligned

        if (memcmp(id, "fmt ", 4) == 0 && csize >= 16) {
            uint32_t byte_rate; uint16_t block_align;
            if (fread(&format, 2, 1, f) == 1 && fread(&channels, 2, 1, f) == 1 &&
                fread(&sample_rate, 4, 1, f) == 1 && fread(&byte_rate, 4, 1, f) == 1 &&
                fread(&block_align, 2, 1, f) == 1 && fread(&bits, 2, 1, f) == 1) {
                have_fmt = 1;
            }
        } else if (memcmp(id, "data", 4) == 0) {
            data_pos = ftell(f);
            data_size = csize;
        } else if (memcmp(id, "cue ", 4) == 0) {
            uint32_t num_cues;
            if (fread(&num_cues, 4, 1, f) == 1) {
                if (num_cues > MAX_SPLICES) num_cues = MAX_SPLICES;
                for (uint32_t i = 0; i < num_cues; i++) {
                    cue_point_t cue;
                    if (fread(&cue, sizeof(cue_point_t), 1, f) != 1) break;
                    if (reel->splices.count < MAX_SPLICES) {
                        reel->splices.markers[reel->splices.count].position = cue.position;
                        reel->splices.markers[reel->splices.count].label[0] = '\0';
                        reel->splices.count++;
                    }
                }
            }
        }
        fseek(f, next, SEEK_SET);   // advance to the next chunk regardless of what we read
    }

    if (!have_fmt || data_pos < 0) {
        fclose(f);
        return REEL_IO_ERR_BADWAV;
    }
    if (channels != 2) {
        fprintf(stderr, "ligase~: load - WAV must be stereo, got %d channels\n", channels);
        fclose(f);
        return REEL_IO_ERR_FORMAT;
    }
    int is_float32 = (format == 3 && bits == 32);
    int is_pcm16   = (format == 1 && bits == 16);
    if (!is_float32 && !is_pcm16) {
        fprintf(stderr, "ligase~: load - unsupported format %d, %d-bit (need 32-bit float or 16-bit PCM)\n", format, bits);
        fclose(f);
        return REEL_IO_ERR_FORMAT;
    }
    if ((int)sample_rate != reel->sample_rate) {
        fprintf(stderr, "ligase~: load - WAV is %u Hz, engine is %d Hz; loaded but playback speed differs (no resample)\n",
                sample_rate, reel->sample_rate);
    }

    int bytes_per = is_float32 ? 4 : 2;
    int num_frames = (int)(data_size / (channels * bytes_per));
    if (num_frames > reel->capacity) num_frames = reel->capacity;

    fseek(f, data_pos, SEEK_SET);
    int short_read = 0;
    if (is_float32) {
        float *temp = (float*)malloc((size_t)num_frames * 2 * sizeof(float));
        if (!temp) { fclose(f); return REEL_IO_ERR_MEM; }
        if (fread(temp, sizeof(float), (size_t)num_frames * 2, f) != (size_t)num_frames * 2) short_read = 1;
        for (int i = 0; i < num_frames; i++) {
            reel->buffer_left[i]  = temp[i * 2];
            reel->buffer_right[i] = temp[i * 2 + 1];
        }
        free(temp);
    } else {  // 16-bit PCM -> float
        int16_t *temp = (int16_t*)malloc((size_t)num_frames * 2 * sizeof(int16_t));
        if (!temp) { fclose(f); return REEL_IO_ERR_MEM; }
        if (fread(temp, sizeof(int16_t), (size_t)num_frames * 2, f) != (size_t)num_frames * 2) short_read = 1;
        const float inv = 1.0f / 32768.0f;
        for (int i = 0; i < num_frames; i++) {
            reel->buffer_left[i]  = temp[i * 2] * inv;
            reel->buffer_right[i] = temp[i * 2 + 1] * inv;
        }
        free(temp);
    }
    fclose(f);

    reel->length = num_frames;
    strncpy(reel->filename, filename, sizeof(reel->filename) - 1);

    // Default splice covering the whole file if no cue points were present
    if (reel->splices.count == 0 && reel->length > 0) {
        reel->splices.markers[0].position = 0;
        reel->splices.markers[0].label[0] = '\0';
        reel->splices.count = 1;
        reel->splices.current_splice = 0;
    }

    return short_read ? REEL_IO_ERR_READ : REEL_IO_OK;
}

int reel_save_wav(reel_t *reel, const char *filename) {
    if (!is_path_safe(filename)) {
        fprintf(stderr, "ligase~: save - unsafe path: %s\n", filename);
        return REEL_IO_ERR_OPEN;
    }
    if (reel->length == 0) {
        return REEL_IO_ERR_EMPTY;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "ligase~: save - cannot open for writing: %s (errno %d)\n", filename, errno);
        return REEL_IO_ERR_OPEN;
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
    header.sample_rate = reel->sample_rate;
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

    int wrote_ok = (fwrite(&header, sizeof(wav_header_t), 1, f) == 1);

    // Write interleaved stereo samples
    float *temp = (float*)malloc((size_t)reel->length * 2 * sizeof(float));
    if (!temp) {
        fclose(f);
        return REEL_IO_ERR_MEM;
    }

    for (int i = 0; i < reel->length; i++) {
        temp[i * 2] = reel->buffer_left[i];
        temp[i * 2 + 1] = reel->buffer_right[i];
    }

    if (fwrite(temp, sizeof(float), (size_t)reel->length * 2, f) != (size_t)reel->length * 2) wrote_ok = 0;
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

    if (!wrote_ok) return REEL_IO_ERR_READ;
    strncpy(reel->filename, filename, sizeof(reel->filename) - 1);
    return REEL_IO_OK;
}

// @endregion:ligase_pd.core.buffer.wav

// @endregion:ligase_pd.core.buffer
