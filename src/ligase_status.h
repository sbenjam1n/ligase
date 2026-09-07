// ligase_status.h — read-only host status snapshot of a ligase~ engine object.
//
// Used by embedding hosts (the VST/CLAP plugin in plugin/) to drive displays — splice LED,
// transport lamps, VU-adjacent readouts, the snapshot-slot lights — without parsing the
// outlet-9 text protocol. Filled by ligase_status() (implemented in ligase~.c), which only
// reads plain fields, so it may be called from a control thread between perform calls.
#ifndef LIGASE_STATUS_H
#define LIGASE_STATUS_H

#include <stdint.h>

typedef struct {
    int sample_rate;
    int reel_length;         // recorded samples per channel
    int reel_capacity;       // allocated samples per channel
    int splice_count;
    int splice_current;      // 0-based index of the selected splice
    int splice_start;        // bounds of the current splice (samples)
    int splice_end;
    int playing;             // transport engaged (`play 1`)
    int triggering;          // grains being scheduled (0 once a one-shot splice ends)
    int recording;
    int record_mode;         // record_mode_t: 0 overdub, 1 new splice, 2 input only
    int record_position;
    float playback_position; // samples
    float bpm;               // 0 = no clock yet
    int clock_running;
    int poly;
    int voice_count;
    int pitch_mode;
    int midi_note;
    int max_grains;
    int pool_size;
    int active_grains;
    int headless;
    int delay_mode;
    int smear_mode;
    int playhead_mode;       // 0 static, 1 scanning, 2 clock-advance (engine enum; panel = +1)
    int snapbuf_has;
    int snapbuf_audition;
    float morph_cursor_x, morph_cursor_y;
    int morph_points;
    int morph_route_len;
    int morph_running;
    uint64_t snapshot_mask;  // bit i set = snapshot slot i holds a capture (slots 0-63)
} ligase_status_t;

// obj = the ligase~ object pointer (what pd_new() returned). Never NULL-derefs; zeroes st on error.
void ligase_status(const void *obj, ligase_status_t *st);

#endif
