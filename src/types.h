// @region:ligase_pd.core.types Data Structures

#ifndef LIGASE_TYPES_H
#define LIGASE_TYPES_H

#include <stdint.h>
#include "sphere.h"

// @region:ligase_pd.core.types.envelope Envelope Structure

typedef enum {
    ENVELOPE_PARABOLIC,
    ENVELOPE_TRAPEZOIDAL,
    ENVELOPE_COSINE,
    ENVELOPE_GAUSSIAN,
    ENVELOPE_EXPONENTIAL
} envelope_type_t;

typedef struct {
    envelope_type_t type;
    float *table;
    int length;
    float skew;           // Envelope skew factor (0.0-1.0, default 0.5)
    float sigma;          // Gaussian width (0.1-0.3, default 0.15)
    float alpha;          // Exponential decay sharpness (5.0-20.0, default 10.0)
} envelope_t;

// @endregion:ligase_pd.core.types.envelope

// @region:ligase_pd.core.types.grain_delay Grain Output Delay Structure

typedef enum {
    DELAY_MODE_DD4,      // Mode 0: DD-4 style analog delay (default)
    DELAY_MODE_BENCINA,  // Mode 1: Bencina pitch-preserving grain delay
    DELAY_MODE_STUT      // Mode 2: Stut quantized rhythmic delay
} grain_delay_mode_t;

typedef struct {
    float *buffer_left;        // Left channel delay buffer
    float *buffer_right;       // Right channel delay buffer
    int buffer_size;           // Buffer size in samples
    int write_pos;             // Write position in buffer

    // Mode selection
    grain_delay_mode_t mode;   // Active delay mode

    // Delay parameters (shared across modes)
    float delay_time;          // Delay time in seconds (target)
    float current_delay_samples; // Smoothed delay time in samples (prevents clicks)
    float feedback;            // Feedback amount (0-1)
    float tone;                // Low-pass filter coefficient (0-1)
    float mix;                 // Dry/wet mix (0=dry, 1=wet)
    float delay_glide_ms;      // Glide time (ms) smoothing delay-time changes (DD-4 de-zipper)

    // Filter state for low-pass filter in feedback loop
    float lpf_state_left;
    float lpf_state_right;

    int sample_rate;
} grain_delay_t;

// @endregion:ligase_pd.core.types.grain_delay

// @region:ligase_pd.core.types.grain_delay_stut Stut Mode Structure

#define MAX_STUT_REPS   16  // Max repeats in a SINGLE trigger's echo train (TidalCycles `count`)
#define MAX_STUT_GRAINS 64  // Voice-pool size: total simultaneous grains across OVERLAPPING
                            // triggers. Each trigger schedules up to MAX_STUT_REPS grains into
                            // free pool slots, so banging the trigger layers stutters (up to 64
                            // voices) instead of clobbering the previous one.

typedef struct grain_stut_grain {
    int active;                    // Is this stut grain active
    float trigger_time_samples;    // When to trigger this grain (in samples from now)
    float gain;                    // Gain for this repetition
    uint32_t capture_splice_start; // Captured splice start
    uint32_t capture_splice_end;   // Captured splice end
    float capture_position;        // Captured read position in samples (write head at trigger)
    float play_length;             // Length of the slice to replay per repeat (samples)
    float play_pos;                // Samples replayed so far within the current slice
    struct grain_stut_grain *next; // Next grain in queue
} grain_stut_grain_t;

typedef struct {
    int num_repetitions;           // Number of stut repetitions (1-16)
    float gain_reduction;          // Gain reduction factor per repeat (0.0-1.0)
    float spacing_ms;              // Base spacing between repetitions in ms

    // Grain queue for scheduled stut grains (scheduled repetitions)
    grain_stut_grain_t grain_pool[MAX_STUT_GRAINS];
    int num_active_grains;         // Current number of active stut grains

    int sample_rate;
} grain_delay_stut_t;

// @endregion:ligase_pd.core.types.grain_delay_stut

// @region:ligase_pd.core.types.grain_delay_bencina Bencina Mode Structure

#define MAX_BENCINA_GRAINS 32  // Maximum simultaneous bencina grains

typedef struct grain_bencina_grain {
    int active;                    // Is this grain active
    float read_offset;             // Delay distance from write head (RELATIVE, not absolute)
    float phase;                   // Current envelope phase (0.0-1.0)
    float grain_length;            // Length of this grain in samples
    float amplitude;               // Current amplitude
    float pan;                     // Pan position (0.0-1.0)
    uint32_t splice_start;         // Splice boundary start (for wrapping)
    uint32_t splice_end;           // Splice boundary end (for wrapping)
    int wrap_mode;                 // 0=global wrap, 1=splice wrap
    struct grain_bencina_grain *next;
} grain_bencina_grain_t;

typedef struct {
    // Grain triggering parameters
    float grain_spacing_ms;        // Time between grain triggers in ms (IOT for bencina grains)
    float grain_size;              // Individual grain size in seconds

    // Grain pool
    grain_bencina_grain_t grain_pool[MAX_BENCINA_GRAINS];
    int num_active_grains;         // Current number of active grains

    // Triggering state
    int samples_until_next_grain;  // Countdown to next grain trigger
    int trigger_period_samples;    // Period between grain triggers in samples

    // Envelope for grains (shared with main grain envelope)
    envelope_t *envelope;

    int sample_rate;
    int default_wrap_mode;         // wrap mode stamped on newly triggered grains (0=global, 1=loop)
    float scatter;                 // per-grain position-scatter amount 0..1 (fraction of a grain
                                   // length). DEFAULT 1.0 (full = the grainy cloud character).
                                   // 0 = coherent (grains share the tap; stereo cloud from pan
                                   // only). bencina_spread sets this — lower = tamer/smoother.
    float edge;                    // grain edge-round amount 0..1. DEFAULT 0 (OFF) so the envelope/
                                   // skew edges stay as-is (the skew-edge clickiness is a usable
                                   // character). >0 ramps each grain in/out over edge*0.5 grain
                                   // lengths (raised cosine) to de-click. bencina_edge sets this.
    float level;                   // wet makeup gain driven INTO the tanh soft-limit. DEFAULT 6.0
                                   // (BENCINA_WET_GAIN) compensates for the incoherent scatter sum;
                                   // tanh still bounds the output to ±1. bencina_level sets this.
} grain_delay_bencina_t;

// @endregion:ligase_pd.core.types.grain_delay_bencina

// @region:ligase_pd.core.types.grain_distortion Grain Distortion Structure

// @region:ligase_pd.core.types.grain_distortion.enums Distortion Enums

typedef enum {
    EMPHASIS_MODE_HP,  // High-pass pre-emphasis (boost highs → brighter distortion)
    EMPHASIS_MODE_LP   // Low-pass pre-emphasis (boost lows → darker distortion)
} emphasis_mode_t;

typedef enum {
    WAVESHAPER_MODE_TANH,       // Pure tanh (aggressive odd harmonics)
    WAVESHAPER_MODE_ARCTAN,     // Pure arctan (smooth odd harmonics)
    WAVESHAPER_MODE_ASYMMETRIC, // Asymmetric pos/neg drives (even harmonics)
    WAVESHAPER_MODE_BLEND,      // Morph between tanh and arctan
    WAVESHAPER_MODE_POLYNOMIAL  // User-defined polynomial coefficients
} waveshaper_mode_t;

// @endregion:ligase_pd.core.types.grain_distortion.enums

typedef struct {
    uint32_t magic;              // Magic number to detect use-after-free (0xD157BEEF)
    float intensity_index;
    float current_drive;
    int enabled;
    int sample_rate;

    // Positioning and oversampling
    int position_mode;           // 0 = per-grain, 1 = post-mix (default)
    int oversample_factor;       // 1, 2, 4, or 8 (default: 4)

    // Pre-tanh highpass filter (6dB/octave, 1-pole) [OUTER FILTER]
    float pre_hp_freq;           // 30-500Hz, default 120Hz
    float pre_hp_mix;            // 0-1, default 0.5
    float pre_hp_z1_left;        // Filter state: previous input (left)
    float pre_hp_z1_right;       // Filter state: previous input (right)
    float pre_hp_y1_left;        // Filter state: previous output (left)
    float pre_hp_y1_right;       // Filter state: previous output (right)
    float pre_hp_coeff;          // Computed filter coefficient

    // Post-tanh lowpass filter (12dB/octave, 2-pole Butterworth) [OUTER FILTER]
    float post_lp_freq;          // 2400-10000Hz, default 5000Hz
    float post_lp_mix;           // 0-1, default 0.5
    float post_lp_z1_left;       // Filter state: input t-1 (left)
    float post_lp_z2_left;       // Filter state: input t-2 (left)
    float post_lp_y1_left;       // Filter state: output t-1 (left)
    float post_lp_y2_left;       // Filter state: output t-2 (left)
    float post_lp_z1_right;      // Filter state: input t-1 (right)
    float post_lp_z2_right;      // Filter state: input t-2 (right)
    float post_lp_y1_right;      // Filter state: output t-1 (right)
    float post_lp_y2_right;      // Filter state: output t-2 (right)
    float post_lp_a1, post_lp_a2; // IIR coefficients
    float post_lp_b0, post_lp_b1, post_lp_b2; // FIR coefficients

    // Reject notch filter (bandstop) [OUTER FILTER]
    float notch_freq;            // Center frequency: 3000Hz
    float notch_bandwidth;       // Bandwidth: 500Hz
    float notch_mix;             // 0-1, default 0.0 (inactive)
    float notch_z1_left;         // Filter state: input t-1 (left)
    float notch_z2_left;         // Filter state: input t-2 (left)
    float notch_y1_left;         // Filter state: output t-1 (left)
    float notch_y2_left;         // Filter state: output t-2 (left)
    float notch_z1_right;        // Filter state: input t-1 (right)
    float notch_z2_right;        // Filter state: input t-2 (right)
    float notch_y1_right;        // Filter state: output t-1 (right)
    float notch_y2_right;        // Filter state: output t-2 (right)
    float notch_a1, notch_a2;    // IIR coefficients
    float notch_b0, notch_b1, notch_b2; // FIR coefficients

    // Pre-emphasis/de-emphasis system [INNER FILTERS]
    emphasis_mode_t emphasis_mode;      // HP or LP mode
    float emphasis_freq;                // Corner frequency (100-5000 Hz)
    float emphasis_gain_makeup;         // Automatic makeup gain (computed)
    float preemph_z1_left, preemph_z1_right;   // Pre-emphasis input delay
    float preemph_y1_left, preemph_y1_right;   // Pre-emphasis output delay
    float preemph_coeff;                        // Pre-emphasis coefficient
    float deemph_z1_left, deemph_z1_right;     // De-emphasis input delay
    float deemph_y1_left, deemph_y1_right;     // De-emphasis output delay
    float deemph_coeff;                         // De-emphasis coefficient

    // Pre-gain stage
    float pregain;  // Pre-distortion gain (0.1-10.0, default 1.0)

    // Waveshaper mode and parameters
    waveshaper_mode_t waveshaper_mode;  // Waveshaper algorithm selection
    float curve_blend;                   // Blend amount (0.0=tanh, 1.0=arctan) for BLEND mode
    float drive_pos;                     // Positive cycle drive (1.0-20.0) for ASYMMETRIC mode
    float drive_neg;                     // Negative cycle drive (1.0-20.0) for ASYMMETRIC mode

    // Polynomial coefficients for POLYNOMIAL mode
    float poly_c1;  // Linear term coefficient (odd harmonics)
    float poly_c2;  // Quadratic term coefficient (even harmonics)
    float poly_c3;  // Cubic term coefficient (odd harmonics)

    // Oversampling buffers and filter state
    float *upsample_buffer_left;     // Temporary buffer for upsampled left channel
    float *upsample_buffer_right;    // Temporary buffer for upsampled right channel
    int upsample_buffer_size;        // Size of upsample buffers (blocksize * oversample_factor)

    // Anti-aliasing filter (2-pole Butterworth lowpass after upsampling)
    float antialias_z1_left;         // Filter state: input t-1 (left)
    float antialias_z2_left;         // Filter state: input t-2 (left)
    float antialias_y1_left;         // Filter state: output t-1 (left)
    float antialias_y2_left;         // Filter state: output t-2 (left)
    float antialias_z1_right;        // Filter state: input t-1 (right)
    float antialias_z2_right;        // Filter state: input t-2 (right)
    float antialias_y1_right;        // Filter state: output t-1 (right)
    float antialias_y2_right;        // Filter state: output t-2 (right)
    float antialias_a1, antialias_a2; // IIR coefficients
    float antialias_b0, antialias_b1, antialias_b2; // FIR coefficients
} grain_distortion_t;

// @endregion:ligase_pd.core.types.grain_distortion

// @region:ligase_pd.core.types.grain_moogladder Grain Moogladder Filter Structure

typedef struct {
    // Filter state variables (4 cascaded one-pole stages)
    float stage[4][2];        // [stage_index][channel] - L/R stereo state

    // Filter parameters
    float cutoff;             // Cutoff frequency in Hz (20-20000)
    float resonance;          // Resonance amount (0.0-4.0, >3.5 = self-oscillation)
    float mix;                // Dry/wet mix (0.0=dry, 1.0=wet)
    int enabled;              // Enable flag (0=bypassed, 1=active)

    // Feedback soft limiter parameters
    float fb_threshold;       // Hard clip threshold for feedback (1.0-5.0, default 2.0)
    float fb_saturation;      // Saturation coefficient (0.1-2.0, default 0.5)

    // Internal coefficients (computed from cutoff/resonance)
    float f;                  // Cutoff coefficient (target)
    float fb;                 // Feedback coefficient (target)
    float f_smooth;           // Smoothed cutoff coefficient
    float fb_smooth;          // Smoothed feedback coefficient

    int sample_rate;
} grain_moogladder_t;

// @endregion:ligase_pd.core.types.grain_moogladder

// @region:ligase_pd.core.types.grain Grain Structure

typedef struct grain {
    float position;           // Current read position in samples
    float increment;          // Read increment (speed)
    float amplitude;          // Current amplitude
    float pan;                // Stereo pan (0=left, 0.5=center, 1=right)
    int envelope_phase;       // Current envelope index
    int grain_length;         // Grain duration in samples (stored at trigger time)
    int active;               // Is grain active
    float saw_cycles;         // Saw modulation cycles (number of spikes per grain)
    float saw_depth;          // Saw modulation depth/intensity (0.0-1.0)
    uint32_t splice_start;    // Splice boundary start (for wrapping)
    uint32_t splice_end;      // Splice boundary end (for wrapping)
    struct grain *next;       // Next grain in pool
} grain_t;

// @endregion:ligase_pd.core.types.grain

// @region:ligase_pd.core.types.splice Splice Marker Structure

#define MAX_SPLICES 300

typedef struct {
    uint32_t position;        // Position in samples
    char label[32];           // Optional label
    char message[256];        // Message to send when navigating to this splice (empty = no message)
} splice_marker_t;

typedef struct {
    splice_marker_t markers[MAX_SPLICES];
    int count;
    int current_splice;       // Currently selected splice
} splice_array_t;

// @endregion:ligase_pd.core.types.splice

// @region:ligase_pd.core.types.splice_behavior Splice Behavior Options Structure

typedef struct {
    int create_position;       // 0=at playback pos, 1=right of current splice
    int jump_to_new;          // 0=stay in current splice, 1=jump to new splice
    int finish_before_nav;    // 0=immediate nav, 1=finish playback before nav
    int split_current;        // 0=allow split (default), 1=preserve current splice length
    int pending_splice;       // Index of pending splice to navigate to (-1 if none)
    int send_splice_msg;      // 0=disabled (default), 1=send messages when navigating to splices
} splice_behavior_t;

// @endregion:ligase_pd.core.types.splice_behavior

// @region:ligase_pd.core.types.reel Reel Structure

#define SAMPLE_RATE 48000
#define MAX_REEL_SECONDS 600  // 10 minutes (~234MB, cut to 2.9min for Morphagene export)

typedef struct {
    float *buffer_left;       // Left channel audio
    float *buffer_right;      // Right channel audio
    int length;               // Length in samples
    int sample_rate;          // Rate the reel is sized/recorded at (follows the host rate)
    int capacity;             // Allocated samples per channel (= MAX_REEL_SECONDS * sample_rate)
    splice_array_t splices;   // Splice markers
    char filename[256];       // Current reel filename
} reel_t;

// Reel WAV I/O result (0 = OK, negatives are specific failures so the Pd layer can report them)
typedef enum {
    REEL_IO_OK         = 0,
    REEL_IO_ERR_OPEN   = -1,  // cannot open file (not found / permission)
    REEL_IO_ERR_BADWAV = -2,  // not a parseable RIFF/WAVE (missing fmt or data)
    REEL_IO_ERR_FORMAT = -3,  // unsupported format (need stereo 16-bit PCM or 32-bit float)
    REEL_IO_ERR_READ   = -4,  // truncated / short read or write
    REEL_IO_ERR_MEM    = -5,  // out of memory
    REEL_IO_ERR_EMPTY  = -6   // (save) nothing to write
} reel_io_status_t;

// @endregion:ligase_pd.core.types.reel

// @region:ligase_pd.core.types.param_range Parameter Range Structure

typedef enum {
    RAND_TYPE_NONE,      // Disabled (for modulation outlets)
    RAND_TYPE_RAND,      // Basic random
    RAND_TYPE_PERLIN_1D, // 1D Perlin noise
    RAND_TYPE_PERLIN_2D, // 2D Perlin noise
    RAND_TYPE_LORENZ,    // Lorenz attractor (chaotic)
    RAND_TYPE_NBODY,     // N-body gravitational simulation (bounded chaos)
    RAND_TYPE_SPHERE,    // 3D sphere physics simulation (STK-based)
    RAND_TYPE_SAW,       // Sawtooth wave LFO
    RAND_TYPE_SINE,      // Sine wave LFO
    RAND_TYPE_SQUARE,    // Square wave LFO
    RAND_TYPE_PATTERN    // Step-sequence / mini-notation source (P2): reads a pattern[] slot's cache
} rand_type_t;

typedef struct {
    float min;              // Minimum value
    float max;              // Maximum value
    rand_type_t rand_type;  // Random generator type (rand, perlin_1d, perlin_2d)
    int rand_instance;      // Random generator instance (0-3 for rand_1 through rand_4)
    int enabled;            // 1 if range is enabled, 0 for single value
    float base_value;       // Base value for PERLIN_2D Y-coordinate (default 0.5)
    float slew;             // Exponential smoothing coefficient 0.0-1.0 (0=instant, 1=frozen, default 0.0)
    float smoothed_value;   // Current smoothed value (internal state)
    int invert;             // Invert modulation output (0=normal, 1=inverted)
    rand_type_t saved_rand_type;  // P2: prior source before a pattern attach (restored by pattern_clear)
    int saved_rand_instance;      // P2: prior instance before a pattern attach
} param_range_t;

// @endregion:ligase_pd.core.types.param_range

// @region:ligase_pd.core.pitch.types Pitch Control Data Types

typedef enum {
    PITCH_MODE_OFF,          // Disabled: speed inlet controls speed directly (default)
    PITCH_MODE_SEMITONES,    // Fixed semitone shift (calculates speed from semitones)
    PITCH_MODE_RANGE,        // Semitone range with random source
    PITCH_MODE_SCALE,        // Scale (list of semitones) with random source
    PITCH_MODE_MIDI,         // MIDI note input (assumes sample tuned to middle C = 60)
    PITCH_MODE_PATTERN       // Pattern-driven scale-degree stepper (P3): steps scale degrees on the cycle clock
} pitch_mode_t;

#define MAX_SCALE_NOTES 128  // Maximum notes in a scale

typedef struct {
    float semitones[MAX_SCALE_NOTES];  // Array of semitone values
    int count;                          // Number of notes in scale
} pitch_scale_t;

typedef struct {
    pitch_mode_t mode;           // Current pitch mode (OFF = use speed directly)
    float semitones;             // Fixed semitone shift (PITCH_MODE_SEMITONES)
    param_range_t semitone_range; // Semitone range for PITCH_MODE_RANGE
    pitch_scale_t scale;         // Scale for PITCH_MODE_SCALE
    int midi_note;               // Current MIDI note (PITCH_MODE_MIDI, 0-127)
    int midi_enabled;            // Whether MIDI input is active
    float last_semitone;         // Last semitone value used (for change detection)
    int pitch_pattern_slot;      // perlin_state.pattern[] slot supplying scale degrees for
                                 // PITCH_MODE_PATTERN; -1 = none/inactive (wired in P3)
} pitch_control_t;

// SMEAR (resonator) pitch destination — an independent note->Hz controller for the allpass
// center frequency. Deliberately NOT pitch_control_t (that is the shared GRAIN-speed controller).
// Emits a single Hz via grain_smear_set_frequency. Source menu mirrors grain pitch.
typedef enum {
    SMEAR_PITCH_OFF,        // 0 — default; manual smear_frequency + smear_frequency_range own freq
    SMEAR_PITCH_SEMITONE,   // 1 — fixed transpose: hz = ref_hz * 2^(semitone/12)
    SMEAR_PITCH_SCALE,      // 2 — stochastic scale-degree via sample_scale_semitones
    SMEAR_PITCH_MIDI,       // 3 — note from the channel-aware 'midi' message (fed in P2; field reserved)
    SMEAR_PITCH_PATTERN     // 4 — mini-notation scale-degree stepper on a dedicated pattern slot
} smear_pitch_source_t;

typedef struct {
    int   enabled;        // 0 = smear pitch off (default) -> manual/range path owns freq (backward compat)
    int   source;         // smear_pitch_source_t: OFF / SEMITONE / SCALE / MIDI / PATTERN
    float semitone;       // fixed transpose for SMEAR_PITCH_SEMITONE
    int   note;           // last MIDI note (SMEAR_PITCH_MIDI; written by P2's 'midi' message)
    int   midi_enabled;   // a valid MIDI note has arrived (SMEAR_PITCH_MIDI)
    float ref_hz;         // reference Hz (default 440)
    int   ref_note;       // reference note (default 69 = A4 -> 440 Hz, standard A440 MIDI tuning)
    int   pattern_slot;   // perlin_state.pattern[] slot for SMEAR_PITCH_PATTERN; -1 = none
    int   midi_channel;   // which MIDI channel routes here (used by P2; stored, unused in P1)
    pitch_scale_t   scale;          // for SMEAR_PITCH_SCALE (degree -> semitone)
    param_range_t   semitone_range; // for SMEAR_PITCH_SCALE random source
    float last_hz;        // last applied Hz (precedence/override bookkeeping + state dump)
} smear_pitch_control_t;

// @endregion:ligase_pd.core.pitch.types

// @region:ligase_pd.core.types.pattern Pattern (mini-notation) Types

#define PATTERN_MAX_STEPS  64    // compiled flat-table cap (runtime); >64-leaf patterns are rejected
#define PATTERN_MAX_NODES  256   // parse-time tree node pool cap (scratch, message-thread stack)
#define PATTERN_MAX_DEPTH  8     // recursive-descent open-group stack cap
#define PATTERN_SLOTS      8     // independent pattern slots (>4 generator instances => per-target independence)
#define PATTERN_MAX_SEGS   16    // pattern_cycle segment-list cap

// One compiled flat leaf (runtime representation, produced by flattening the parse tree)
typedef struct {
    float value;        // normalized 0..1 (params) OR raw scale degree as float (pitch)
    float weight;       // @-weight; default 1.0
    int   is_rest;      // ~ : hold previous value, emit no fresh step
    int   alt_group;    // -1 = always present; >=0 = member of alternation group G
    int   alt_member;   // index within its alt group (one present per cycle)
} pattern_step_t;

// One pattern slot's compiled table + cached evaluator output (the ONLY thing perform touches)
typedef struct {
    pattern_step_t steps[PATTERN_MAX_STEPS];
    int   step_count;                          // 0 => slot inactive (publish barrier; set LAST on commit)
    int   alt_group_count[PATTERN_MAX_STEPS];  // members per alternation group G (cycle-mod select)
    int   alt_group_total;                     // number of distinct alternation groups
    float cum_weight[PATTERN_MAX_STEPS];       // prefix sums over PRESENT steps (recomputed on reselection)
    float total_weight;                        // sum of present-step weights this cycle
    // evaluator cache (written ONLY by pattern_eval_slot once per block; read by P2/P3):
    float cached_value;        // current normalized value / degree for this block
    int   cached_is_rest;      // current step is a rest
    int   changed;             // 1 on the block where the active present-step index changed
    int   last_step_index;     // for change detection
    long  last_alt_cycle;      // cycle index of last alt reselection (skip recompute when unchanged)
} pattern_table_t;

// Parse-time ONLY (function-local automatic array inside ligase_pattern; never in perform state)
typedef enum { PN_LEAF, PN_SEQ, PN_ALT } pattern_node_kind_t;
typedef struct {
    pattern_node_kind_t kind;
    float value; int is_rest;        // LEAF fields
    int   weight;                    // @N (default 1)
    int   first_child, next_sibling; // index links into the node pool (-1 if none)
} pattern_node_t;

// @endregion:ligase_pd.core.types.pattern

// @region:ligase_pd.core.types.lorenz_state Lorenz Attractor State

typedef struct {
    float x;              // X coordinate
    float y;              // Y coordinate
    float z;              // Z coordinate
    float sigma;          // Sigma parameter (typically 10.0)
    float rho;            // Rho parameter (typically 28.0)
    float beta;           // Beta parameter (typically 8/3)
    float dt;             // Time step for integration
    float x0;             // Initial X coordinate (for reset)
    float y0;             // Initial Y coordinate (for reset)
    float z0;             // Initial Z coordinate (for reset)
} lorenz_state_t;

// @endregion:ligase_pd.core.types.lorenz_state

// @region:ligase_pd.core.types.nbody_state N-Body Gravitational System State

#define NBODY_COUNT 3  // Number of bodies in simulation

typedef struct {
    // Body state (3 bodies: hierarchical masses for stable chaos)
    float pos[NBODY_COUNT][3];    // [body_index][x/y/z] position vectors
    float vel[NBODY_COUNT][3];    // [body_index][vx/vy/vz] velocity vectors
    float mass[NBODY_COUNT];      // Mass of each body

    // Simulation parameters (user-controllable)
    float epsilon;          // Softening parameter for collision avoidance (0.01-1.0)
    float G;                // Gravitational constant (typically 1.0)
    float damping;          // Damping coefficient for energy dissipation (0.0-0.1)
    float pump_amount;      // Energy pump strength (0.0-0.01)
    int pump_counter;       // Counter for pump timing
    int pump_interval;      // Apply pump every N updates (10-100)
    float dt;               // Integration timestep (0.001-0.01)

    // Auto-adjusting normalization bounds
    float pos_min;          // Current minimum position across all bodies/axes
    float pos_max;          // Current maximum position across all bodies/axes

    // Configuration tracking
    int initial_instance;   // Initial configuration index (0-3) for reset
} nbody_state_t;

// @endregion:ligase_pd.core.types.nbody_state

// @region:ligase_pd.core.types.perlin_state Perlin Noise State

typedef struct {
    // 4 instances of 1D coordinates
    float noise_1d_coord[4];

    // 4 instances of 2D coordinates
    float noise_2d_coord_x[4];

    // 4 instance offsets (large prime numbers to decorrelate Perlin instances)
    float instance_offset_1d[4];
    float instance_offset_2d[4];

    // Per-instance frequency scaling factors (one for each noise trio)
    // noise_frequency_scale[0] → perlin_1d_1, perlin_2d_1, lorenz_1
    // noise_frequency_scale[1] → perlin_1d_2, perlin_2d_2, lorenz_2
    // noise_frequency_scale[2] → perlin_1d_3, perlin_2d_3, lorenz_3
    // noise_frequency_scale[3] → perlin_1d_4, perlin_2d_4, lorenz_4
    float noise_frequency_scale[4];

    // 4 random seeds (one for each rand instance)
    unsigned int rand_seed[4];

    // 4 Lorenz attractor instances
    lorenz_state_t lorenz[4];

    // 4 N-body gravitational simulation instances
    nbody_state_t nbody[4];

    // N-body output mode selection (one per instance, 0-10)
    // 0=Body0 X, 1=Body1 Y, 2=Body2 X, 3=Dist 0-1, 4=Vel0, 5=Vel1, 6=Vel2,
    // 7=Dist 0-2, 8=Dist 1-2, 9=AngMom, 10=Energy
    int nbody_output_mode[4];

    // 4 Sphere physics simulation instances (STK-based)
    sphere_state_t sphere[4];

    // Sphere output mode selection (one per instance, 0-6)
    // 0=Pos X, 1=Pos Y, 2=Pos Z, 3=Vel X, 4=Vel Y, 5=Vel Z, 6=Velocity Magnitude
    int sphere_output_mode[4];

    // Waveform LFO phase accumulators (one per instance, 0.0-1.0)
    float waveform_phase[4];

    // Pattern (mini-notation) slots — compiled tables + free-running cycle phase per slot.
    // Covered by the scheduler_create memset (perlin_state is a value member of scheduler_t),
    // so step_count==0 (inactive) for every slot on construction.
    pattern_table_t pattern[PATTERN_SLOTS];
    float           pattern_phase[PATTERN_SLOTS];        // free-running 0..1 cycle phase per slot
    long            pattern_cycle_index[PATTERN_SLOTS];  // integer cycle counter per slot (<> alternation)
} perlin_state_t;

// @endregion:ligase_pd.core.types.perlin_state

// @region:ligase_pd.core.types.scheduler Scheduler Structure


#define DEFAULT_MAX_GRAINS 200
#define MAX_POOL_SIZE 2000  // Absolute maximum for safety

typedef struct scheduler {
    grain_t *grain_pool;       // Dynamic pool allocated at initialization
    int pool_size;             // Actual pool size (read from config)
    grain_t *free_list;
    grain_t *active_list;
    envelope_t *envelope;
    float grain_size;          // in seconds
    int sample_rate;

    // DLGranulator-style parameters
    int max_grains;            // Maximum concurrent grains (1-pool_size)
    float iot;                 // Interonset time in seconds (0.0005-2.0)

    // Parameter ranges for randomization
    param_range_t speed_range;
    param_range_t scanrate_range;
    param_range_t organize_range;        // Splice organization range (0.0-1.0)
    param_range_t sos_range;             // Sound-on-sound mix range (0.0-1.0)
    param_range_t iot_range;
    param_range_t maxgrains_range;
    param_range_t grainsize_range;
    param_range_t grainstart_range;
    param_range_t env_skew_range;        // Envelope skew range (-1.0 to 1.0)
    param_range_t saw_cycles_range;      // Saw modulation cycles range (0-64)
    param_range_t saw_depth_range;       // Saw modulation depth range (0.0-1.0)
    param_range_t gdelay_range;
    param_range_t gdelay_feedback_range;  // Grain delay feedback range (0.0-0.99)
    param_range_t gdelay_tone_range;      // Grain delay tone range (0.0-1.0)
    param_range_t gdelay_mix_range;       // Grain delay mix range (0.0-1.0)
    param_range_t distortion_range;  // Distortion intensity range (0.0-1.0)
    param_range_t amplitude_range;   // Grain amplitude range (0.0-1.0)
    param_range_t pan_range;         // Grain pan range (0.0-1.0, 0=left, 1=right)
    param_range_t moog_cutoff_range;    // Moogladder cutoff frequency range (20-20000 Hz)
    param_range_t moog_resonance_range; // Moogladder resonance range (0.0-4.0)
    param_range_t moog_mix_range;       // Moogladder dry/wet mix range (0.0-1.0)

    // Distortion enhancement parameter ranges
    param_range_t dist_emphasis_freq_range;  // Pre-emphasis frequency (100-5000 Hz)
    param_range_t dist_pregain_range;        // Pre-gain (0.1-10.0)
    param_range_t dist_curve_blend_range;    // Curve blend (0.0-1.0, tanh to arctan)
    param_range_t dist_drive_pos_range;      // Positive drive for asymmetric mode (1.0-20.0)
    param_range_t dist_drive_neg_range;      // Negative drive for asymmetric mode (1.0-20.0)
    param_range_t dist_poly_c1_range;        // Polynomial c1 coefficient (-10.0 to 10.0)
    param_range_t dist_poly_c2_range;        // Polynomial c2 coefficient (-10.0 to 10.0)
    param_range_t dist_poly_c3_range;        // Polynomial c3 coefficient (-10.0 to 10.0)

    // Fog parameter ranges

    // Stut parameter range
    param_range_t stut_reps_range;           // Stut repetitions (1-16)

    // Bencina parameter ranges
    param_range_t bencina_iot_range;         // Bencina grain spacing (1.0-1000.0 ms)
    param_range_t bencina_grainsize_range;   // Bencina grain size (0.001-2.0 sec)
    param_range_t bencina_pan_range;         // Bencina per-grain pan spread/cloud (0-1); sampled
                                             // PER GRAIN like the main pan. Disabled -> grains at
                                             // the base pan (inlet 22). Enabled -> random per-grain
                                             // pan in [min,max] = stereo cloud (width=span, skew=pos)
    param_range_t smear_frequency_range;     // Smear allpass center freq (20 - ~0.45*sr Hz)
    param_range_t smear_resonance_range;     // Smear pole radius / sharpness (0.0-0.999)
    param_range_t smear_stages_range;        // Smear allpass stages / depth (0-48)
    param_range_t smear_feedback_range;      // Smear global feedback (-0.99-0.99)

    // Perlin noise state
    perlin_state_t perlin_state;

    // Grain distortion (optional, NULL if disabled)
    grain_distortion_t *distortion;

    // Pitch control system
    pitch_control_t pitch_control;
    smear_pitch_control_t smear_pitch_control;  // SMEAR (resonator) pitch destination — independent of pitch_control

    // Pan mode (0 = constant-power mono panning, 1 = stereo balance)
    int pan_mode;

    // Delay mode structures (for bencina and stut modes)
    grain_delay_stut_t *delay_stut;      // Stut mode state (NULL if not allocated)
    grain_delay_bencina_t *delay_bencina; // Bencina mode state (NULL if not allocated)

} scheduler_t;

// @endregion:ligase_pd.core.types.scheduler

// @region:ligase_pd.core.types.recorder Recorder Structure

typedef enum {
    RECORD_MODE_OVERDUB,      // Record into current splice (Morphagene "Rec")
    RECORD_MODE_NEW_SPLICE,   // Create new splice and record (Morphagene "Rec + Splice")
    RECORD_MODE_INPUT_ONLY    // Record input only to new splice (no sound-on-sound mix)
} record_mode_t;

typedef struct recorder {
    reel_t *reel;
    int record_position;
    int is_recording;
    float crossfade_mix;  // Sound-on-Sound mix level
    record_mode_t mode;
    int current_splice_start;
    int current_splice_end;
    int new_splice_start;  // For recsplice mode: where the new splice began
} recorder_t;

// @endregion:ligase_pd.core.types.recorder


#endif // LIGASE_TYPES_H

// @endregion:ligase_pd.core.types
