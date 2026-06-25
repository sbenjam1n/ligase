// @region:ligase_pd.pd_external Pure Data External Interface

#include "m_pd.h"
#include "types.h"
#include "perlin.h"
#include "grain_distortion.h"
#include "grain_moogladder.h"
#include "grain_smear.h"
#include "morph.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// x86 denormal handling: subnormal floats are ~100x slower on Intel and accumulate in the
// effect feedback states as audio decays toward silence, making CPU creep up over minutes.
#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#define LIGASE_FLUSH_DENORMALS() do { \
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);        \
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON); \
} while (0)
#else
#define LIGASE_FLUSH_DENORMALS() ((void)0)
#endif

// Export ONLY the Pd entry point; everything else stays hidden (paired with -fvisibility=hidden)
// so ligase's internal symbols (grain_*, reel_*, …) don't collide in Pd's flat namespace with
// other externals that vendor the same code.
#if defined(__GNUC__) || defined(__clang__)
#define LIGASE_PUBLIC __attribute__((visibility("default")))
#else
#define LIGASE_PUBLIC
#endif

// Replace any non-finite (NaN/Inf) sample in a stereo pair with 0. Used at the
// boundaries of the grain/delay path and before the reel write: a single NaN in
// the granular signal otherwise poisons the output (constant_power_mix does
// in*a + granular*b, and NaN*0 == NaN, so it kills even the 100%-dry monitor) and,
// if written into the reel/effect buffers, self-sustains (x + NaN == NaN) — which
// is the "silence and no monitor after recording, nothing restores it" failure.
static inline void ligase_sanitize_pair(float *a, float *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!isfinite(a[i])) a[i] = 0.0f;
        if (!isfinite(b[i])) b[i] = 0.0f;
    }
}

// Forward declarations
typedef struct _ligase ligase_t;
static void ligase_send_current_splice_msg(ligase_t *x);

// External function declarations
extern envelope_t* envelope_create(envelope_type_t type, int length);
extern void envelope_destroy(envelope_t *env);
extern void envelope_set_skew(envelope_t *env, float skew);
extern void envelope_set_type(envelope_t *env, envelope_type_t type);
extern float envelope_sample(envelope_t *env, float phase);

extern scheduler_t* scheduler_create(envelope_t *env, int sample_rate);
extern void scheduler_destroy(scheduler_t *sched);
extern void scheduler_trigger_grain(scheduler_t *sched, float position, float speed, uint32_t splice_start, uint32_t splice_end, float amplitude, float pan, float saw_cycles, float saw_depth);
extern void scheduler_process(scheduler_t *sched, reel_t *reel, float *out_left, float *out_right, int blocksize);
extern float sample_param_range(param_range_t *range, perlin_state_t *perlin_state, float base_value);
extern void pattern_eval_slot(perlin_state_t *perlin_state, int slot);
extern float sample_scale_semitones(pitch_scale_t *scale, perlin_state_t *perlin_state, param_range_t *semitone_range);

extern grain_delay_t* grain_delay_create(int sample_rate);
extern void grain_delay_destroy(grain_delay_t *delay);
extern void grain_delay_process(grain_delay_t *delay, grain_delay_stut_t *stut, grain_delay_bencina_t *bencina, float *in_left, float *in_right, float *out_left, float *out_right, int blocksize, uint32_t splice_start, uint32_t splice_end, float bencina_pan_base, param_range_t *bencina_pan_range, perlin_state_t *bencina_pan_perlin);
extern void grain_delay_set_time(grain_delay_t *delay, float time_seconds);
extern void grain_delay_set_glide(grain_delay_t *delay, float glide_ms);
extern void grain_delay_set_feedback(grain_delay_t *delay, float feedback);
extern void grain_delay_set_tone(grain_delay_t *delay, float tone);
extern void grain_delay_set_mix(grain_delay_t *delay, float mix);
extern void grain_delay_clear(grain_delay_t *delay);
extern void grain_delay_set_mode(grain_delay_t *delay, grain_delay_mode_t mode);
extern void grain_delay_set_sample_rate(grain_delay_t *delay, int sample_rate);

extern grain_delay_stut_t* grain_delay_stut_create(int sample_rate);
extern void grain_delay_stut_destroy(grain_delay_stut_t *stut);
extern void grain_delay_stut_trigger(grain_delay_stut_t *stut, grain_delay_t *delay, uint32_t splice_start, uint32_t splice_end, float quantized_spacing_ms, float play_length_samples);
extern void grain_delay_stut_set_repetitions(grain_delay_stut_t *stut, int num);
extern void grain_delay_stut_set_reduction(grain_delay_stut_t *stut, float reduction);
extern void grain_delay_stut_set_spacing(grain_delay_stut_t *stut, float spacing_ms);

extern grain_delay_bencina_t* grain_delay_bencina_create(envelope_t *envelope, int sample_rate);
extern void grain_delay_bencina_destroy(grain_delay_bencina_t *bencina);
extern void grain_delay_bencina_process(grain_delay_bencina_t *bencina, grain_delay_t *delay, float *in_left, float *in_right, float *out_left, float *out_right, int blocksize, uint32_t splice_start, uint32_t splice_end);
extern void grain_delay_bencina_set_spacing(grain_delay_bencina_t *bencina, float spacing_ms);
extern void grain_delay_bencina_set_grain_size(grain_delay_bencina_t *bencina, float size_seconds);
extern void grain_delay_bencina_set_scatter(grain_delay_bencina_t *bencina, float amount);
extern void grain_delay_bencina_set_edge(grain_delay_bencina_t *bencina, float amount);
extern void grain_delay_bencina_set_level(grain_delay_bencina_t *bencina, float gain);
extern void grain_delay_bencina_set_wrap_mode(grain_delay_bencina_t *bencina, int mode);
extern void grain_delay_bencina_clear(grain_delay_bencina_t *bencina);
extern void grain_delay_bencina_set_sample_rate(grain_delay_bencina_t *bencina, int sample_rate);

extern grain_moogladder_t* grain_moogladder_create(int sample_rate);
extern void grain_moogladder_destroy(grain_moogladder_t *filter);
extern void grain_moogladder_process(grain_moogladder_t *filter, float *left, float *right, int blocksize);
extern void grain_moogladder_set_cutoff(grain_moogladder_t *filter, float cutoff_hz);
extern void grain_moogladder_set_resonance(grain_moogladder_t *filter, float resonance);
extern void grain_moogladder_set_mix(grain_moogladder_t *filter, float mix);
extern void grain_moogladder_set_enabled(grain_moogladder_t *filter, int enabled);

extern reel_t* reel_create();
extern void reel_destroy(reel_t *reel);
extern void reel_clear(reel_t *reel);
extern void reel_set_sample_rate(reel_t *reel, int sample_rate);
extern void reel_clear_except_splice(reel_t *reel, uint32_t splice_start, uint32_t splice_end);
extern int reel_load_wav(reel_t *reel, const char *filename);
extern int reel_save_wav(reel_t *reel, const char *filename);
extern const char *reel_io_strerror(int status);

extern recorder_t* recorder_create(reel_t *reel);
extern void recorder_destroy(recorder_t *rec);
extern void recorder_start(recorder_t *rec);
extern void recorder_stop(recorder_t *rec);
extern void recorder_process(recorder_t *rec, float *in_left, float *in_right, int blocksize);
extern void recorder_set_splice_bounds(recorder_t *rec, int start, int end);
extern void recorder_set_mode(recorder_t *rec, record_mode_t mode);
extern int recorder_get_new_splice_start(recorder_t *rec);

extern int splice_add(splice_array_t *splices, uint32_t position, const char *label);
extern void splice_clear(splice_array_t *splices);
extern void splice_clear_except_current(splice_array_t *splices, uint32_t reel_length);
extern void splice_join_right(splice_array_t *splices);
extern void splice_join_all(splice_array_t *splices);
extern void splice_remove(splice_array_t *splices, int index);
extern void splice_shift(splice_array_t *splices, int delta);
extern void splice_organize(splice_array_t *splices, float normalized_input);
extern void splice_get_bounds(splice_array_t *splices, int splice_index, uint32_t reel_length,
                             uint32_t *start, uint32_t *end);

// @region:ligase_pd.pd_external.class Class Definition

static t_class *ligase_class;

typedef enum {
    PLAYHEAD_MODE_STATIC,        // Mode 1: Static position, GrainStart slides playhead
    PLAYHEAD_MODE_SCANNING,      // Mode 2: Advancing playhead with scan rate
    PLAYHEAD_MODE_CLOCK_ADVANCE  // Mode 3: Clock advance, playhead advances on clock bang
} playhead_mode_t;

struct _ligase {
    t_object x_obj;

    // Dummy field for CLASS_MAINSIGNALIN (main inlet is audio input)
    t_float x_f;

    // Patch canvas, captured at creation — used to resolve load/save paths relative to the
    // patch directory (and search the Pd path). Without it, relative paths resolve against
    // Pd's CWD, which on a Finder-launched Pd.app is "/" → load/save silently fail on macOS.
    t_glist *x_canvas;

    // DSP inlets/outlets
    t_inlet *x_in_right;
    t_inlet *x_grain_size;
    t_inlet *x_grain_start;
    t_inlet *x_speed;
    t_inlet *x_organize;
    t_inlet *x_scanrate;
    t_inlet *x_sos;
    t_inlet *x_iot;
    t_inlet *x_maxgrains;
    t_inlet *x_gdelay_time;
    t_inlet *x_gdelay_feedback;
    t_inlet *x_gdelay_tone;
    t_inlet *x_gdelay_mix;
    t_inlet *x_smear;            // inlet 15: smear mix
    t_inlet *x_moog_cutoff;    // Moogladder cutoff frequency
    t_inlet *x_moog_resonance; // Moogladder resonance
    t_inlet *x_moog_mix;       // Moogladder dry/wet mix
    t_inlet *x_midi;  // MIDI note inlet for pitch control
    t_inlet *x_env_skew;  // Envelope skew inlet
    t_inlet *x_amplitude;  // Grain amplitude inlet
    t_inlet *x_pan;  // Grain pan inlet

    t_outlet *x_out_left;
    t_outlet *x_out_right;
    t_outlet *x_splice_end_out;  // Bang when splice ends
    t_outlet *x_grain_bang_out;  // Bang every x grains at grain onset

    // Modulation outlets (control rate)
    t_outlet *x_modout1;
    t_outlet *x_modout2;
    t_outlet *x_modout3;
    t_outlet *x_modout4;

    // @region:ligase_pd.pd_external.outlets.state State Query Outlet
    // State query outlet (outlet 9)
    t_outlet *x_state_out;
    // @endregion:ligase_pd.pd_external.outlets.state

    // Core components
    reel_t *reel;
    scheduler_t *scheduler;
    recorder_t *recorder;
    envelope_t *envelope;
    grain_delay_t *grain_delay;
    grain_delay_stut_t *delay_stut;        // Stut mode processor
    grain_delay_bencina_t *delay_bencina;  // Bencina mode processor
    grain_moogladder_t *moogladder;
    grain_smear_t *smear;   // allpass smear effect

    morph_state_t *morph;   // morph / Metasurface layer (snapshot interpolation)

    // Parameters
    float grain_size;
    float grain_start;
    float speed;
    float organize_cv;
    float amplitude;  // Default amplitude (0.0-2.0, default 1.0)
    float pan;        // Default pan (0.0-1.0, default 0.5=center)
    float saw_cycles; // Saw modulation cycles (0-64, default 0=off)
    float saw_depth;  // Saw modulation depth (0.0-1.0, default 0.0=off)

    // State
    int is_playing;
    int is_triggering;  // Controls whether new grains are triggered
    float playback_position;
    float prev_grain_start;  // For Mode 1: track grain_start to detect wrap
    int grain_trigger_counter;
    int grain_trigger_period;
    playhead_mode_t playhead_mode;
    float scan_rate;  // For Mode 2: playhead advance rate (independent of Speed)

    // Grain bang output
    int grain_bang_counter;  // Counter for grains triggered
    int grain_bang_rate;     // Bang every x grains (0=off, 1=every grain, 2=every 2nd, etc.)

    // Clock advance mode (Mode 3)
    int clock_advance_use_quantized;  // 0=use current grain length, 1=use quantized grain length
    int clock_bang_received;          // Flag: 1 when clock bang received, triggers playhead advance

    // Splice rate limiting (prevent crashes from rapid triggering)
    double last_splice_time;  // Clock time when last splice was added (in seconds)
    double splice_cooldown;   // Minimum time between splice operations (0.01 sec = 10ms)

    // Splice behavior options
    splice_behavior_t splice_behavior;

    // Timing and quantization system
    int clock_running;        // Whether the clock is currently receiving bangs
    double last_bang_time;    // Time of last bang received (for BPM calculation)
    double bpm;               // Calculated BPM from bang intervals (0 = not calculated)

    // Interonset time (IOT) quantization
    int time_sig_numerator;   // Time signature numerator (e.g., 7 in 7/5)
    int time_sig_denominator; // Time signature denominator (e.g., 5 in 7/5)
    int quant_note;           // Quantization note value (1, 2, 4, 8, 16, 32, 64, 128)
    float quant_amount;       // Quantization amount (0.0 = 0%, 1.0 = 100%)
    float quant_grid_ms;      // Calculated quantization grid size in milliseconds
    int samples_since_quant;  // Sample counter for quantization timing

    // Grain size quantization
    int gs_time_sig_numerator;   // Grain size time signature numerator
    int gs_time_sig_denominator; // Grain size time signature denominator
    int gs_quant_note;           // Grain size quantization note value
    float gs_quant_amount;       // Grain size quantization amount (0.0-1.0)
    float gs_quant_grid_ms;      // Grain size quantization grid in milliseconds

    // Delay time quantization
    int delay_time_sig_numerator;   // Delay time signature numerator
    int delay_time_sig_denominator; // Delay time signature denominator
    int delay_quant_note;           // Delay quantization note value
    float delay_quant_amount;       // Delay quantization amount (0.0-1.0)
    float delay_quant_grid_ms;      // Delay quantization grid in milliseconds

    // Stut slice length: how much audio each repeat replays (decoupled from spacing).
    int stut_length_mode;           // 0 = independent (stut_length_ms / quantized), 1 = grainsize
    float stut_length_ms;           // Stut slice length in ms (independent mode)
    int stut_len_quant_note;        // Stut-length quantization note value (1..128)
    float stut_len_quant_amount;    // Stut-length quantization amount (0.0-1.0)
    float stut_len_quant_grid_ms;   // Stut-length quantization grid in milliseconds (from BPM)

    // Pattern cycle clock — a FIFTH, isolated free-running clock for the mini-notation patterns
    // (NOT one of the four quant grids). Total cycle length derives from the cycle_segments list
    // via the same (60000/bpm)*4 grid math; phase advances once per DSP block in ligase_perform.
    double cycle_total_sec;                 // total cycle length in seconds (0 => clock idle)
    int    cycle_seg_count;                 // # pattern_cycle segments (0 => default 1-bar cycle)
    struct { int num; int den; } cycle_segments[PATTERN_MAX_SEGS];
    int    pattern_debug;                   // 1 => log step changes to stderr (verification aid)
    float  pattern_pitch_last_printed;      // last pitch semitone logged (de-dupe the pitch trace)
    int    smear_pitch_debug;               // 1 => log resolved smear pitch Hz to stderr (verification aid)
    float  smear_pitch_dbg_last;            // last smear Hz logged (de-dupe the trace)

    // SOS mode: 0=Record Only (legacy), 1=Morphagene (crossfade input/granular at output)
    int sos_mode;
    float sos_value;  // Stored SOS mix value (0.0-1.0, default 0.5)

    // Headless mode: 0=disabled (full 0.0-1.0 range), 1=enabled (epsilon thresholds for SOS/grain_start)
    int headless_mode;

    // Sample rate
    int sample_rate;

    // Modulation outlet configuration (unified with param_range system)
    param_range_t modout1_range;
    param_range_t modout2_range;
    param_range_t modout3_range;
    param_range_t modout4_range;

    // Outlet 3 (splice_end_out) mode: 0=splice end/wrap (default), 1=bang on note change
    int outlet3_mode;
    int prev_midi_note;      // Previous MIDI note for change detection
    int midi_msg_active;     // P2: 1 once a 'midi' message owns the grain dest -> suppress the inlet-19 write
    float prev_scale_semitone; // Previous scale semitone for change detection

    // @region:ligase_pd.pd_external.outlets.state.tracking Value Tracking Variables
    // Current parameter values (sampled from DSP perform for query system)
    float speed_current;
    float grainsize_current;
    float grainstart_current;
    float organize_current;
    float scanrate_current;
    float sos_current;
    float iot_current;
    float maxgrains_current;
    float gdelay_time_current;
    float gdelay_feedback_current;
    float gdelay_tone_current;
    float gdelay_mix_current;
    float smear_current;   // tracks inlet 15 (smear mix)
    float moog_cutoff_current;
    float moog_resonance_current;
    float moog_mix_current;
    float midi_current;
    float env_skew_current;
    float amplitude_current;
    float pan_current;
    float stut_reps_current;
    float bencina_iot_current;
    float bencina_grainsize_current;
    // @endregion:ligase_pd.pd_external.outlets.state.tracking

    // Fixed-size temporary buffers for DSP processing (avoid VLAs on audio thread stack)
    // Maximum block size is typically 64-4096, using 8192 for safety
    float temp_left[8192];
    float temp_right[8192];
    float delayed_left[8192];
    float delayed_right[8192];
    float rec_left[8192];
    float rec_right[8192];
};

// @endregion:ligase_pd.pd_external.class

// @region:ligase_pd.dsp.crossfade Constant-Power Crossfade

// Constant-power crossfade using sine/cosine curves
// Maintains constant perceived loudness across the crossfade range
// Based on trigonometric identity: sin²(θ) + cos²(θ) = 1
static inline void constant_power_mix(
    float *out_left, float *out_right,
    const float *a_left, const float *a_right,
    const float *b_left, const float *b_right,
    float mix, int n)
{
    // Convert mix value [0.0, 1.0] to angle [0, π/2]
    float theta = mix * (M_PI / 2.0f);

    // Calculate constant-power gains
    // a_gain: 0.0 at mix=0, 1.0 at mix=1 (sine curve)
    // b_gain: 1.0 at mix=0, 0.0 at mix=1 (cosine curve)
    float a_gain = sinf(theta);
    float b_gain = cosf(theta);

    for (int i = 0; i < n; i++) {
        out_left[i] = (a_left[i] * a_gain) + (b_left[i] * b_gain);
        out_right[i] = (a_right[i] * a_gain) + (b_right[i] * b_gain);
    }
}

// @endregion:ligase_pd.dsp.crossfade

// Fold a position into [start, start+len) in O(1). Never loops — safe for any finite/Inf/NaN
// input and any len; a degenerate splice (len <= 0) or non-finite position pins to start.
// Replaces the subtractive while-wraps that hung the audio thread at 100% CPU on a zero-length
// splice or a runaway/non-finite playhead position.
static inline float wrap_to_splice(float pos, float start, float len) {
    if (!(len > 0.0f) || !isfinite(pos)) return start;
    float rel = fmodf(pos - start, len);
    if (rel < 0.0f) rel += len;
    return start + rel;
}

// @region:ligase_pd.pd_external.dsp DSP Callback

// Debug flag - set to 0 to disable verbose logging
#define LIGASE_DEBUG 0

// Overdub Time Lag Accumulation: maximum feedback coefficient for the re-recorded granular
// playback. Kept just below unity so the loop sustains (long decay) at the "freeze" end of SOS
// without growing unbounded; a tanh soft-limiter is the hard ceiling on top of this. Tunable.
#define TLA_FEEDBACK_MAX 0.95f

static void ligase_update_inlets(ligase_t *x,
    t_sample *grain_size_in, t_sample *grain_start_in,
    t_sample *speed_in, t_sample *organize_in, t_sample *scanrate_in,
    t_sample *sos_in, t_sample *iot_in, t_sample *maxgrains_in,
    t_sample *gdelay_time_in, t_sample *gdelay_feedback_in,
    t_sample *gdelay_tone_in, t_sample *gdelay_mix_in,
    t_sample *smear_in, t_sample *moog_cutoff_in, t_sample *moog_resonance_in,
    t_sample *moog_mix_in, t_sample *midi_in, t_sample *env_skew_in,
    t_sample *amplitude_in, t_sample *pan_in, int n) {
    // Update parameters from inlets, with validation
    //  Only update if inlet has non-zero value (connected)
    // Unconnected inlets read 0 and should NOT overwrite stored values

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading grain_size_in[0]...\n");

    // Grain size: only update if inlet is connected (non-zero within valid range)
    float grain_size_val = grain_size_in[0];

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: grain_size_val=%f, continuing...\n", grain_size_val);
    if (grain_size_val > 0.001f && grain_size_val <= 2.0f) {
        x->grain_size = grain_size_val;
    }
    // Otherwise keep current value (initialized to 0.1 or set via message)

    // Grain start: update based on headless mode
    // headless=1: Use epsilon threshold to detect unconnected inlet (0.0)
    // headless=0: Allow full 0.0-1.0 range for signal control
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading grain_start_in[0]...\n");
    float grain_start_val = grain_start_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: grain_start_val=%f\n", grain_start_val);

    int should_update_grainstart = 0;
    if (x->headless_mode) {
        // Headless mode: epsilon threshold (0.001-1.0 range)
        should_update_grainstart = (grain_start_val >= 0.001f && grain_start_val <= 1.0f);
    } else {
        // Perfect signal mode: full 0.0-1.0 range
        should_update_grainstart = (grain_start_val >= 0.0f && grain_start_val <= 1.0f);
    }

    if (should_update_grainstart) {
        x->grain_start = grain_start_val;
    }

    // Speed: only update if inlet is connected (non-zero within valid range)
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading speed_in[0]...\n");
    float speed_val = speed_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: speed_val=%f\n", speed_val);
    if (speed_val != 0.0f && fabsf(speed_val) <= 4.0f) {
        x->speed = speed_val;
    }
    // Otherwise keep current value (initialized to 1.0 or set via message)

    // Handle organize signal inlet (0.0-1.0 to select splice)
    // headless=1: epsilon threshold to detect unconnected inlet
    // headless=0: full 0.0-1.0 range (0.0 = select first splice)
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading organize_in[0]...\n");
    float organize_val = organize_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: organize_val=%f\n", organize_val);

    int should_update_organize = 0;
    if (x->headless_mode) {
        // Headless mode: epsilon threshold (0.001-1.0 range)
        should_update_organize = (organize_val > 0.001f && organize_val <= 1.0f);
    } else {
        // Perfect signal mode: full 0.0-1.0 range
        should_update_organize = (organize_val >= 0.0f && organize_val <= 1.0f);
    }

    if (should_update_organize) {
        // Only update if value changed significantly (avoid jitter)
        static float last_organize = -1.0f;
        if (fabsf(organize_val - last_organize) > 0.001f) {
            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Calling splice_organize...\n");
            splice_organize(&x->reel->splices, organize_val);
            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: splice_organize returned\n");
            last_organize = organize_val;
        }
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Organize handling complete, reading scanrate...\n");

    // Update scanrate based on headless mode
    // headless=1: epsilon threshold (0.0 = unconnected)
    // headless=0: full range including 0.0 (0.0 = static, no movement)
    float scanrate_val = scanrate_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: scanrate_val=%f\n", scanrate_val);

    int should_update_scanrate = 0;
    if (x->headless_mode) {
        // Headless mode: epsilon threshold
        should_update_scanrate = (fabsf(scanrate_val) > 0.001f && fabsf(scanrate_val) <= 1000.0f);
    } else {
        // Perfect signal mode: allow 0.0
        should_update_scanrate = (fabsf(scanrate_val) <= 1000.0f);
    }

    if (should_update_scanrate) {
        x->scan_rate = scanrate_val;
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking MIDI mode...\n");
    // Update MIDI note if in MIDI mode (only if inlet connected)
    if (x->scheduler->pitch_control.mode == PITCH_MODE_MIDI && !x->midi_msg_active) {
        // P2: suppress the channel-less signal-inlet write once a 'midi' message owns the grain dest.
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: In MIDI mode, reading midi_in[0]...\n");
        int midi_note = (int)midi_in[0];
        // Only update if inlet has valid MIDI note value (1-127, 0 = unconnected)
        if (midi_note >= 1 && midi_note <= 127) {
            x->scheduler->pitch_control.midi_note = midi_note;
            x->scheduler->pitch_control.midi_enabled = 1;
        }
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading env_skew_in[0]...\n");
    // Update envelope skew based on headless mode
    // headless=1: epsilon threshold (0.0 = unconnected)
    // headless=0: full 0.0-1.0 range
    float skew_value = env_skew_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: skew_value=%f\n", skew_value);

    int should_update_skew = 0;
    if (x->headless_mode) {
        // Headless mode: epsilon threshold
        should_update_skew = (skew_value >= 0.001f && skew_value <= 1.0f);
    } else {
        // Perfect signal mode: full 0.0-1.0 range
        should_update_skew = (skew_value >= 0.0f && skew_value <= 1.0f);
    }

    if (should_update_skew && skew_value != x->envelope->skew) {
        envelope_set_skew(x->envelope, skew_value);
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading SOS (sos_in[0])...\n");
    // Update SOS (crossfade mix for recorder in Record Only mode)
    // NOTE: Only affects Record Only mode (sos_mode 0), Morphagene mode overrides this
    // headless=1: epsilon threshold (< 0.01 = unconnected)
    // headless=0: full 0.0-1.0 range (0.0 = 100% granular)
    if (x->recorder && x->sos_mode == 0) {
        float sos_value = sos_in[0];
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: sos_value=%f\n", sos_value);

        int should_update_sos = 0;
        if (x->headless_mode) {
            // Headless mode: epsilon threshold
            should_update_sos = (isfinite(sos_value) && fabsf(sos_value) >= 0.01f);
        } else {
            // Perfect signal mode: allow 0.0
            should_update_sos = isfinite(sos_value);
        }

        if (should_update_sos) {
            if (sos_value < 0.0f) sos_value = 0.0f;
            if (sos_value > 1.0f) sos_value = 1.0f;
            x->recorder->crossfade_mix = sos_value;
        }
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading IOT (iot_in[0])...\n");
    // Update IOT with bounds checking
    //  Only update if inlet has non-zero value (connected)
    // Unconnected inlet reads 0, which would set extreme density (0.0005s IOT)
    float iot_value = iot_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: iot_value=%f\n", iot_value);
    if (iot_value >= 0.001f && iot_value <= 2.0f) {
        x->scheduler->iot = iot_value;
    }
    // Otherwise keep current scheduler->iot value (initialized to 0.1 or set via message)

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading maxgrains_in[0]...\n");
    // Update maxgrains with bounds checking
    //  Only update if inlet has valid non-zero value
    int maxgrains_value = (int)maxgrains_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: maxgrains_value=%d\n", maxgrains_value);
    if (maxgrains_value >= 1 && maxgrains_value <= x->scheduler->pool_size) {
        x->scheduler->max_grains = maxgrains_value;
    }
    // Otherwise keep current max_grains value (initialized to 4 or set via message)

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reading all effect parameters...\n");
    // Update grain delay parameters with validation
    // headless=1: epsilon threshold for 0-1 params, > 0.0 for time
    // headless=0: allow full 0.0 values
    float gdelay_time = gdelay_time_in[0];
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: gdelay_time=%f\n", gdelay_time);
    float gdelay_feedback = gdelay_feedback_in[0];
    float gdelay_tone = gdelay_tone_in[0];
    float gdelay_mix = gdelay_mix_in[0];

    // Inlet 11: delay time (DD-4/Bencina) or stut REPS (Stut). SIGNAL-DRIVEN in every mode (this
    // is a hardware prototype — the physical control/CV on this inlet must drive the parameter).
    // The inlet's native range is the delay-time range 0-10 s (shared by DD-4 and Bencina); in
    // Stut that same 0-10 input is MAPPED linearly to the repeat count 1-16, so one physical
    // control serves all three modes. Headless gating mirrors the delay-time branch exactly.
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        int apply = x->headless_mode ? (gdelay_time > 0.0f && gdelay_time <= 10.0f)
                                     : (gdelay_time >= 0.0f && gdelay_time <= 10.0f);
        if (apply) {
            float v = gdelay_time; if (v < 0.0f) v = 0.0f; if (v > 10.0f) v = 10.0f;
            int reps = 1 + (int)((v / 10.0f) * 15.0f + 0.5f);  // 0->1 .. 10->16
            grain_delay_stut_set_repetitions(x->delay_stut, reps);
        }
    } else {
        // Delay time: 0.0 = off (musically valid)
        if (x->headless_mode) {
            if (gdelay_time > 0.0f && gdelay_time <= 10.0f) {
                grain_delay_set_time(x->grain_delay, gdelay_time);
            }
        } else {
            if (gdelay_time >= 0.0f && gdelay_time <= 10.0f) {
                grain_delay_set_time(x->grain_delay, gdelay_time);
            }
        }
    }

    // Inlet 12: feedback (DD-4/Bencina) or stut REDUCTION (Stut). Signal-driven in every mode.
    // feedback and reduction share the SAME 0-1 range and meaning (decay per echo), so the inlet
    // value passes straight through — no remapping needed. Headless gating mirrors the feedback branch.
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        int apply = x->headless_mode ? (gdelay_feedback >= 0.001f && gdelay_feedback <= 1.0f)
                                     : (gdelay_feedback >= 0.0f && gdelay_feedback <= 1.0f);
        if (apply) {
            grain_delay_stut_set_reduction(x->delay_stut, gdelay_feedback);
        }
    } else {
        // Feedback: 0.0 = single echo (musically valid)
        if (x->headless_mode) {
            if (gdelay_feedback >= 0.001f && gdelay_feedback <= 1.0f) {
                grain_delay_set_feedback(x->grain_delay, gdelay_feedback);
            }
        } else {
            if (gdelay_feedback >= 0.0f && gdelay_feedback <= 1.0f) {
                grain_delay_set_feedback(x->grain_delay, gdelay_feedback);
            }
        }
    }

    // Inlet 13: tone (DD-4/Bencina) or stut SPACING (Stut). Signal-driven in every mode. The inlet's
    // native range is the tone range 0-1; in Stut that 0-1 input is MAPPED EXPONENTIALLY to spacing
    // 1-5000 ms (0->1 ms, 0.5->~70 ms, 1->5000 ms) — exponential because spacing is a time control
    // and a linear 0-1 would cram all the useful musical range into a sliver. This replaces the old
    // raw passthrough where a 0-1 tone value landed on spacing as ~1 ms and piled every repeat onto
    // the first (the phasing bug). Headless gating mirrors the tone branch.
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        int apply = x->headless_mode ? (gdelay_tone >= 0.001f && gdelay_tone <= 1.0f)
                                     : (gdelay_tone >= 0.0f && gdelay_tone <= 1.0f);
        if (apply) {
            float v = gdelay_tone; if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
            float spacing_ms = powf(5000.0f, v);  // 1 ms .. 5000 ms, exponential
            grain_delay_stut_set_spacing(x->delay_stut, spacing_ms);
        }
    } else {
        // Tone: 0.0 = dark (musically valid)
        if (x->headless_mode) {
            if (gdelay_tone >= 0.001f && gdelay_tone <= 1.0f) {
                grain_delay_set_tone(x->grain_delay, gdelay_tone);
            }
        } else {
            if (gdelay_tone >= 0.0f && gdelay_tone <= 1.0f) {
                grain_delay_set_tone(x->grain_delay, gdelay_tone);
            }
        }
    }

    // Mix: 0.0 = 100% dry (musically valid)
    if (x->headless_mode) {
        if (gdelay_mix >= 0.001f && gdelay_mix <= 1.0f) {
            grain_delay_set_mix(x->grain_delay, gdelay_mix);
        }
    } else {
        if (gdelay_mix >= 0.0f && gdelay_mix <= 1.0f) {
            grain_delay_set_mix(x->grain_delay, gdelay_mix);
        }
    }

    // Update smear mix from inlet 15.
    // headless=1: epsilon threshold (0.0 = unconnected); headless=0: allow full 0.0.
    float smear_mix = smear_in[0];
    int should_update_smear = x->headless_mode
        ? (smear_mix >= 0.001f && smear_mix <= 1.0f)
        : (smear_mix >= 0.0f && smear_mix <= 1.0f);
    if (should_update_smear && x->smear) {
        grain_smear_set_mix(x->smear, smear_mix);
    }

    // Distortion is now fully message-controlled (no inlet)
    // All distortion parameters are set via messages like:
    // distortion_enable, distortion_intensity, distortion_pre_hp_freq, etc.

    // Update moogladder parameters with validation
    // headless=1: epsilon threshold for resonance/mix
    // headless=0: allow full 0.0 values
    float moog_cutoff = moog_cutoff_in[0];
    float moog_resonance = moog_resonance_in[0];
    float moog_mix = moog_mix_in[0];

    // Cutoff: 20Hz - 20kHz (minimum is 20, no epsilon needed)
    if (moog_cutoff >= 20.0f && moog_cutoff <= 20000.0f) {
        grain_moogladder_set_cutoff(x->moogladder, moog_cutoff);
    }

    // Resonance: 0.0 = no resonance (musically valid)
    if (x->headless_mode) {
        if (moog_resonance >= 0.001f && moog_resonance <= 4.0f) {
            grain_moogladder_set_resonance(x->moogladder, moog_resonance);
        }
    } else {
        if (moog_resonance >= 0.0f && moog_resonance <= 4.0f) {
            grain_moogladder_set_resonance(x->moogladder, moog_resonance);
        }
    }

    // Mix: 0.0 = 100% dry (musically valid)
    if (x->headless_mode) {
        if (moog_mix >= 0.001f && moog_mix <= 1.0f) {
            grain_moogladder_set_mix(x->moogladder, moog_mix);
        }
    } else {
        if (moog_mix >= 0.0f && moog_mix <= 1.0f) {
            grain_moogladder_set_mix(x->moogladder, moog_mix);
        }
    }

    // Update amplitude (grain output LEVEL) with validation.
    // Ignore a bare 0 in BOTH modes — same as grainsize/iot/speed. An unconnected
    // amplitude inlet (inlet 21) reads 0; honoring that in headless 0 zeroed the grain
    // level and silenced the whole engine the instant you switched to headless 0 (with
    // grainsize/iot/speed already 0-safe, amplitude was the lone hold-out). A true 0
    // (silence) is set via the `amplitude` message or a near-0 signal (e.g. 0.0001),
    // not a bare unconnected inlet — so this only ignores an exact 0.0 reading.
    float amplitude_val = amplitude_in[0];
    if (amplitude_val > 0.0f && amplitude_val <= 2.0f) {
        x->amplitude = amplitude_val;
    }

    // Update pan with validation
    // headless=1: epsilon threshold (0.0 = unconnected)
    // headless=0: allow full 0.0 (0.0 = full left)
    float pan_val = pan_in[0];

    if (x->headless_mode) {
        if (pan_val >= 0.001f && pan_val <= 1.0f) {
            x->pan = pan_val;
        }
    } else {
        if (pan_val >= 0.0f && pan_val <= 1.0f) {
            x->pan = pan_val;
        }
    }

    // @region:ligase_pd.pd_external.outlets.state.sampling Sample Current Values for Query System
    // Sample first value of each DSP block for state query system
    // These represent the ACTUAL values being used (inlet/message OR modulated when active)
    // Note: Per-grain parameters (speed, grainsize, grainstart, amplitude, pan, maxgrains)
    // will be updated with modulated values below if param_range is enabled
    x->speed_current = speed_val;
    x->grainsize_current = grain_size_val;
    x->grainstart_current = grain_start_val;
    x->organize_current = organize_val;
    x->scanrate_current = scanrate_val;
    x->sos_current = x->recorder ? x->recorder->crossfade_mix : x->sos_value;
    x->iot_current = iot_value;
    x->maxgrains_current = (float)maxgrains_value;
    x->gdelay_time_current = gdelay_time;
    x->gdelay_feedback_current = gdelay_feedback;
    x->gdelay_tone_current = gdelay_tone;
    x->gdelay_mix_current = gdelay_mix;
    x->smear_current = smear_mix;   // inlet 15 -> smear mix
    x->moog_cutoff_current = moog_cutoff;
    x->moog_resonance_current = moog_resonance;
    x->moog_mix_current = moog_mix;
    x->midi_current = midi_in[0];
    x->env_skew_current = skew_value;
    x->amplitude_current = amplitude_val;
    x->pan_current = pan_val;
    // @endregion:ligase_pd.pd_external.outlets.state.sampling

    //  Apply grain size quantization only if BPM is valid (prevent division by zero)
    float quantized_grain_size = x->grain_size;
    if (x->gs_quant_amount > 0.0f && x->bpm > 1.0 && x->gs_quant_grid_ms > 0.0f) {
        // Convert quantized grid from ms to seconds
        float quant_grain_size_sec = x->gs_quant_grid_ms / 1000.0f;

        // Blend between current grain size and quantized grain size
        quantized_grain_size = x->grain_size * (1.0f - x->gs_quant_amount) +
                               quant_grain_size_sec * x->gs_quant_amount;
    }

    // Update scheduler parameters
    x->scheduler->grain_size = quantized_grain_size;

    //  Apply delay quantization only if BPM is valid (prevent division by zero)
    if (x->delay_quant_amount > 0.0f && x->bpm > 1.0 && x->delay_quant_grid_ms > 0.0f) {
        // Convert quantized grid from ms to seconds
        float quant_delay_sec = x->delay_quant_grid_ms / 1000.0f;

        // Blend between current grain delay time and quantized delay time
        float current_delay = x->grain_delay->delay_time;
        float quantized_delay = current_delay * (1.0f - x->delay_quant_amount) +
                                quant_delay_sec * x->delay_quant_amount;
        x->grain_delay->delay_time = quantized_delay;
    }

    // Sample GDelay time with range (if enabled)
    // Sampled once per DSP block for smooth variation
    float sampled_gdelay_time = sample_param_range(&x->scheduler->gdelay_range,
                                                   &x->scheduler->perlin_state,
                                                   x->grain_delay->delay_time);
    // Only apply if range is enabled (preserves quantization when not ranging)
    if (x->scheduler->gdelay_range.enabled) {
        grain_delay_set_time(x->grain_delay, sampled_gdelay_time);
        x->gdelay_time_current = sampled_gdelay_time;  // Store modulated value for query
    }

    // Sample GDelay feedback with range (if enabled)
    // In Stut mode, routes to stut_reduction instead of feedback
    if (x->scheduler->gdelay_feedback_range.enabled) {
        if (x->grain_delay->mode == DELAY_MODE_STUT) {
            float sampled = sample_param_range(&x->scheduler->gdelay_feedback_range,
                                               &x->scheduler->perlin_state,
                                               x->delay_stut->gain_reduction);
            grain_delay_stut_set_reduction(x->delay_stut, sampled);
            x->gdelay_feedback_current = sampled;
        } else {
            float sampled_gdelay_feedback = sample_param_range(&x->scheduler->gdelay_feedback_range,
                                                                &x->scheduler->perlin_state,
                                                                x->grain_delay->feedback);
            grain_delay_set_feedback(x->grain_delay, sampled_gdelay_feedback);
            x->gdelay_feedback_current = sampled_gdelay_feedback;
        }
    }

    // Sample GDelay tone with range (if enabled)
    // In Stut mode, routes to stut_spacing instead of tone
    if (x->scheduler->gdelay_tone_range.enabled) {
        if (x->grain_delay->mode == DELAY_MODE_STUT) {
            float sampled = sample_param_range(&x->scheduler->gdelay_tone_range,
                                               &x->scheduler->perlin_state,
                                               x->delay_stut->spacing_ms);
            grain_delay_stut_set_spacing(x->delay_stut, sampled);
            x->gdelay_tone_current = sampled;
        } else {
            float sampled_gdelay_tone = sample_param_range(&x->scheduler->gdelay_tone_range,
                                                            &x->scheduler->perlin_state,
                                                            x->grain_delay->tone);
            grain_delay_set_tone(x->grain_delay, sampled_gdelay_tone);
            x->gdelay_tone_current = sampled_gdelay_tone;
        }
    }

    // Sample GDelay mix with range (if enabled)
    float sampled_gdelay_mix = sample_param_range(&x->scheduler->gdelay_mix_range,
                                                   &x->scheduler->perlin_state,
                                                   x->grain_delay->mix);
    if (x->scheduler->gdelay_mix_range.enabled) {
        grain_delay_set_mix(x->grain_delay, sampled_gdelay_mix);
        x->gdelay_mix_current = sampled_gdelay_mix;  // Store modulated value for query
    }

    // Sample Moogladder parameters with ranges (if enabled)
    // Sampled once per DSP block for smooth variation
    float sampled_moog_cutoff = sample_param_range(&x->scheduler->moog_cutoff_range,
                                                    &x->scheduler->perlin_state,
                                                    x->moogladder->cutoff);
    if (x->scheduler->moog_cutoff_range.enabled) {
        grain_moogladder_set_cutoff(x->moogladder, sampled_moog_cutoff);
        x->moog_cutoff_current = sampled_moog_cutoff;  // Store modulated value for query
    }

    float sampled_moog_resonance = sample_param_range(&x->scheduler->moog_resonance_range,
                                                       &x->scheduler->perlin_state,
                                                       x->moogladder->resonance);
    if (x->scheduler->moog_resonance_range.enabled) {
        grain_moogladder_set_resonance(x->moogladder, sampled_moog_resonance);
        x->moog_resonance_current = sampled_moog_resonance;  // Store modulated value for query
    }

    float sampled_moog_mix = sample_param_range(&x->scheduler->moog_mix_range,
                                                 &x->scheduler->perlin_state,
                                                 x->moogladder->mix);
    if (x->scheduler->moog_mix_range.enabled) {
        grain_moogladder_set_mix(x->moogladder, sampled_moog_mix);
        x->moog_mix_current = sampled_moog_mix;  // Store modulated value for query
    }

    // Sample stut_reps with range (if enabled)
    if (x->scheduler->stut_reps_range.enabled && x->delay_stut) {
        float sampled = sample_param_range(&x->scheduler->stut_reps_range,
                                           &x->scheduler->perlin_state,
                                           (float)x->delay_stut->num_repetitions);
        grain_delay_stut_set_repetitions(x->delay_stut, (int)sampled);
        x->stut_reps_current = sampled;
    }

    // Sample bencina parameters with range (if enabled)
    if (x->scheduler->bencina_iot_range.enabled && x->delay_bencina) {
        float sampled = sample_param_range(&x->scheduler->bencina_iot_range,
                                           &x->scheduler->perlin_state,
                                           x->delay_bencina->grain_spacing_ms);
        grain_delay_bencina_set_spacing(x->delay_bencina, sampled);
        x->bencina_iot_current = sampled;
    }

    if (x->scheduler->bencina_grainsize_range.enabled && x->delay_bencina) {
        float sampled = sample_param_range(&x->scheduler->bencina_grainsize_range,
                                           &x->scheduler->perlin_state,
                                           x->delay_bencina->grain_size);
        grain_delay_bencina_set_grain_size(x->delay_bencina, sampled);
        x->bencina_grainsize_current = sampled;
    }

    // Sample smear (allpass) parameters with range (if enabled). Sampled once per DSP block,
    // same as the other effect params, so smear_frequency/resonance/stages/feedback can be
    // driven by any modulation generator via param_range. grain_smear_t is opaque here and
    // sample_param_range ignores base_value when enabled (it only returns it when disabled,
    // and we sample only inside the enabled guard), so a 0 placeholder base is fine.
    if (x->smear) {
        // OVERRIDE: when the smear pitch destination is enabled it OWNS freq_hz this block, so the
        // continuous smear_frequency_range modulation is BYPASSED here (consistent with grain pitch,
        // where engaging a pitch source bypasses speed_range). Disabled -> exactly today's behavior.
        if (x->scheduler->smear_frequency_range.enabled && !x->scheduler->smear_pitch_control.enabled) {
            grain_smear_set_frequency(x->smear,
                sample_param_range(&x->scheduler->smear_frequency_range, &x->scheduler->perlin_state, 0.0f));
        }
        if (x->scheduler->smear_resonance_range.enabled) {
            grain_smear_set_resonance(x->smear,
                sample_param_range(&x->scheduler->smear_resonance_range, &x->scheduler->perlin_state, 0.0f));
        }
        if (x->scheduler->smear_stages_range.enabled) {
            grain_smear_set_stages(x->smear,
                (int)sample_param_range(&x->scheduler->smear_stages_range, &x->scheduler->perlin_state, 0.0f));
        }
        if (x->scheduler->smear_feedback_range.enabled) {
            grain_smear_set_feedback(x->smear,
                sample_param_range(&x->scheduler->smear_feedback_range, &x->scheduler->perlin_state, 0.0f));
        }

        // SMEAR pitch destination: resolve the source to a semitone, then hz = ref_hz * 2^(semitone/12).
        // When enabled this is the sole writer of freq_hz this block (the range branch above is bypassed).
        // Reads the pattern cache only; one powf at block rate. Raw Hz -> grain_smear_set_frequency, whose
        // smear_update_coeffs [20, 0.45*sr] clamp is the SOLE bounds owner (not duplicated here).
        {
            smear_pitch_control_t *sp = &x->scheduler->smear_pitch_control;
            if (sp->enabled) {
                float semitone = 0.0f;
                int have_note = 1;
                switch (sp->source) {
                    case SMEAR_PITCH_SEMITONE:
                        semitone = sp->semitone;
                        break;
                    case SMEAR_PITCH_MIDI:                       // note fed by P2's 'midi' message
                        if (sp->midi_enabled) semitone = (float)(sp->note - sp->ref_note);
                        else                  have_note = 0;     // no note yet -> hold previous Hz
                        break;
                    case SMEAR_PITCH_SCALE:
                        semitone = sample_scale_semitones(&sp->scale,
                                       &x->scheduler->perlin_state, &sp->semitone_range);
                        break;
                    case SMEAR_PITCH_PATTERN: {
                        int slot = sp->pattern_slot, count = sp->scale.count;
                        pattern_table_t *pt = (slot >= 0 && slot < PATTERN_SLOTS)
                                              ? &x->scheduler->perlin_state.pattern[slot] : NULL;
                        if (pt && pt->step_count > 0 && count > 0 && !pt->cached_is_rest) {
                            int degree = (int)pt->cached_value;          // leaf value = scale degree
                            int idx = ((degree % count) + count) % count; // wrap (mirror grain PATTERN)
                            int oct = (int)floorf((float)degree / (float)count);
                            semitone = sp->scale.semitones[idx] + 12.0f * (float)oct;
                        } else {
                            have_note = 0;                              // rest / not ready -> hold
                        }
                        break;
                    }
                    default: have_note = 0; break;                      // SMEAR_PITCH_OFF with enabled set
                }
                if (have_note) {
                    semitone += sample_param_range(&x->scheduler->smear_pitch_fine_range,
                                                   &x->scheduler->perlin_state,
                                                   sp->semitone_fine);   // P3 fine (semitones; base when disabled)
                    float hz = sp->ref_hz * powf(2.0f, semitone / 12.0f);
                    sp->last_hz = hz;                                   // raw Hz; clamp lives in smear_update_coeffs
                    grain_smear_set_frequency(x->smear, hz);
                    if (x->smear_pitch_debug && hz != x->smear_pitch_dbg_last) {
                        x->smear_pitch_dbg_last = hz;
                        fprintf(stderr, "ligase~ smear_pitch: source %d semitone %.3f -> %.2f Hz\n",
                                sp->source, semitone, hz);
                    }
                }
            }
        }
    }

    // Sample scanrate with range (if enabled)
    if (x->scheduler->scanrate_range.enabled) {
        float sampled = sample_param_range(&x->scheduler->scanrate_range,
                                           &x->scheduler->perlin_state,
                                           x->scan_rate);
        x->scan_rate = sampled;
        x->scanrate_current = sampled;
    }

    // Sample organize with range (if enabled)
    float sampled_organize = sample_param_range(&x->scheduler->organize_range,
                                                 &x->scheduler->perlin_state,
                                                 x->organize_cv);
    if (x->scheduler->organize_range.enabled && sampled_organize > 0.001f && sampled_organize <= 1.0f) {
        splice_organize(&x->reel->splices, sampled_organize);
        x->organize_current = sampled_organize;  // Store modulated value for query
    }

    // Sample SOS mix with range (if enabled)
    if (x->scheduler->sos_range.enabled && x->recorder && x->sos_mode == 0) {
        float sampled_sos = sample_param_range(&x->scheduler->sos_range,
                                                &x->scheduler->perlin_state,
                                                x->sos_value);
        x->recorder->crossfade_mix = sampled_sos;
        x->sos_current = sampled_sos;  // Store modulated value for query
    }

    // Sample env_skew with range (if enabled)
    float sampled_env_skew = sample_param_range(&x->scheduler->env_skew_range,
                                                 &x->scheduler->perlin_state,
                                                 x->envelope->skew);
    if (x->scheduler->env_skew_range.enabled) {
        envelope_set_skew(x->envelope, sampled_env_skew);
        x->env_skew_current = sampled_env_skew;  // Store modulated value for query
    }

    // Calculate grain trigger period
    // Default: use interonset time (iot)
    // Sample IOT range if enabled (varies inter-grain timing)
    float sampled_iot = sample_param_range(&x->scheduler->iot_range,
                                          &x->scheduler->perlin_state,
                                          x->scheduler->iot);
    if (x->scheduler->iot_range.enabled) {
        x->iot_current = sampled_iot;  // Store modulated value for query
    }
    int base_trigger_period = (int)(sampled_iot * x->sample_rate);

    //  Apply IOT quantization only if BPM is valid (prevent division by zero)
    if (x->quant_amount > 0.0f && x->bpm > 1.0 && x->quant_grid_ms > 0.0f) {
        // Calculate quantized trigger period in samples
        int quant_trigger_period = (int)((x->quant_grid_ms / 1000.0f) * x->sample_rate);

        // Blend between base iot and quantized period based on quant_amount
        x->grain_trigger_period = (int)(base_trigger_period * (1.0f - x->quant_amount) +
                                         quant_trigger_period * x->quant_amount);
    } else {
        // No quantization: use base iot
        x->grain_trigger_period = base_trigger_period;
    }

    //  Ensure grain_trigger_period is never zero (prevent CPU overload)
    if (x->grain_trigger_period < 1) x->grain_trigger_period = 1;

    // Sample per-grain parameters for query system (if modulation enabled)
    // These are modulated per-grain in scheduler_trigger_grain(), but we sample
    // once per block here to provide query values representing current modulation state
    if (x->scheduler->speed_range.enabled) {
        float sampled_speed = sample_param_range(&x->scheduler->speed_range,
                                                 &x->scheduler->perlin_state,
                                                 x->speed);
        x->speed_current = sampled_speed;
    }

    if (x->scheduler->grainsize_range.enabled) {
        float sampled_grainsize = sample_param_range(&x->scheduler->grainsize_range,
                                                     &x->scheduler->perlin_state,
                                                     x->grain_size);
        x->grainsize_current = sampled_grainsize;
    }

    if (x->scheduler->grainstart_range.enabled) {
        float sampled_grainstart = sample_param_range(&x->scheduler->grainstart_range,
                                                      &x->scheduler->perlin_state,
                                                      x->grain_start);
        x->grainstart_current = sampled_grainstart;
    }

    if (x->scheduler->amplitude_range.enabled) {
        float sampled_amplitude = sample_param_range(&x->scheduler->amplitude_range,
                                                     &x->scheduler->perlin_state,
                                                     x->amplitude);
        x->amplitude_current = sampled_amplitude;
    }

    if (x->scheduler->pan_range.enabled) {
        float sampled_pan = sample_param_range(&x->scheduler->pan_range,
                                               &x->scheduler->perlin_state,
                                               x->pan);
        x->pan_current = sampled_pan;
    }

    if (x->scheduler->maxgrains_range.enabled) {
        float sampled_maxgrains = sample_param_range(&x->scheduler->maxgrains_range,
                                                     &x->scheduler->perlin_state,
                                                     (float)x->scheduler->max_grains);
        x->maxgrains_current = sampled_maxgrains;
    }

    if (x->scheduler->distortion_range.enabled && x->scheduler->distortion) {
        // Use the inlet/message value as base (distortion_intensity from earlier)
        // Distortion range sampling removed - distortion is now message-controlled only
    }

}

static void ligase_process_grains(ligase_t *x,
    t_sample *in_left, t_sample *in_right,
    t_sample *out_left, t_sample *out_right,
    t_sample *sos_in, int n) {
    // Initialize output buffers to silence
    for (int i = 0; i < n; i++) {
        out_left[i] = 0.0f;
        out_right[i] = 0.0f;
    }

    // Process recording in Record Only mode (legacy behavior)
    // In Morphagene mode, recording happens after output mixing
    if (x->sos_mode == 0 && x->recorder) {
        for (int i = 0; i < n; i++) {
            x->rec_left[i] = in_left[i];
            x->rec_right[i] = in_right[i];
        }
        // Record Only mode: Always replace buffer (ignores SOS for recording)
        // SOS only affects playback level, not recording behavior
        float original_mix = x->recorder->crossfade_mix;
        x->recorder->crossfade_mix = 1.0f;  // Force replacement
        recorder_process(x->recorder, x->rec_left, x->rec_right, n);
        x->recorder->crossfade_mix = original_mix;  // Restore
    }

    // Playback processing
    if (x->is_triggering && x->reel && x->reel->length > 0) {
        // Get current splice bounds
        uint32_t splice_start, splice_end;
        splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                         x->reel->length, &splice_start, &splice_end);

        // Update recorder splice bounds for overdub mode
        recorder_set_splice_bounds(x->recorder, splice_start, splice_end);

        float splice_length = splice_end - splice_start;

        if (x->playhead_mode == PLAYHEAD_MODE_STATIC) {
            // MODE 1: Static position, GrainStart slides playhead (Morphagene default)

            // Detect if grain_start has wrapped around (0→1 or 1→0)
            // Use a threshold of 0.5 to distinguish wraps from normal parameter changes
            float grain_start_diff = fabsf(x->grain_start - x->prev_grain_start);
            if (grain_start_diff > 0.5f) {
                // Bang outlet when grain_start wraps (only in default mode)
                if (x->outlet3_mode == 0) {
                    outlet_bang(x->x_splice_end_out);
                }
            }

            for (int i = 0; i < n; i++) {
                x->grain_trigger_counter++;
                if (x->grain_trigger_counter >= x->grain_trigger_period) {
                    x->grain_trigger_counter = 0;

                    // Grains spawn at static position
                    // GrainStart parameter slides the playhead position through splice
                    float grain_pos = splice_start + (x->grain_start * splice_length);

                    scheduler_trigger_grain(x->scheduler, grain_pos, x->speed, splice_start, splice_end, x->amplitude, x->pan, x->saw_cycles, x->saw_depth);

                    // Grain bang output: bang every x grains
                    if (x->grain_bang_rate > 0) {
                        x->grain_bang_counter++;
                        if (x->grain_bang_counter >= x->grain_bang_rate) {
                            x->grain_bang_counter = 0;
                            outlet_bang(x->x_grain_bang_out);
                        }
                    }
                }
            }

            // Update previous grain_start for next block
            x->prev_grain_start = x->grain_start;
        } else if (x->playhead_mode == PLAYHEAD_MODE_SCANNING) {
            // MODE 2: Scanning playhead (traditional granular synthesis)
            for (int i = 0; i < n; i++) {
                x->grain_trigger_counter++;
                if (x->grain_trigger_counter >= x->grain_trigger_period) {
                    x->grain_trigger_counter = 0;

                    // Grain spawns at advancing playback position
                    // GrainStart offsets where grain reads relative to playhead
                    float grain_pos = x->playback_position + (x->grain_start * splice_length);

                    // Wrap grain position within splice bounds (O(1), can't hang)
                    grain_pos = wrap_to_splice(grain_pos, (float)splice_start, splice_length);

                    scheduler_trigger_grain(x->scheduler, grain_pos, x->speed, splice_start, splice_end, x->amplitude, x->pan, x->saw_cycles, x->saw_depth);

                    // Grain bang output: bang every x grains
                    if (x->grain_bang_rate > 0) {
                        x->grain_bang_counter++;
                        if (x->grain_bang_counter >= x->grain_bang_rate) {
                            x->grain_bang_counter = 0;
                            outlet_bang(x->x_grain_bang_out);
                        }
                    }
                }

                // Advance playback position through splice at scan_rate (can be negative)
                x->playback_position += x->scan_rate;

                // Loop playback position within splice bounds (O(1) fold — never hangs even on a
                // zero-length splice or a runaway/non-finite position)
                int wrapped = (x->playback_position >= (float)splice_end ||
                               x->playback_position < (float)splice_start ||
                               !isfinite(x->playback_position));
                x->playback_position = wrap_to_splice(x->playback_position, (float)splice_start, splice_length);

                // Bang outlet when splice ends (wraps) - only in default mode
                if (wrapped && x->outlet3_mode == 0) {
                    outlet_bang(x->x_splice_end_out);
                }

                // Check for pending splice navigation after wrapping (nav wins over the one-shot stop)
                int had_pending_nav = (wrapped && x->splice_behavior.pending_splice >= 0);
                if (had_pending_nav) {
                    x->reel->splices.current_splice = x->splice_behavior.pending_splice;
                    x->splice_behavior.pending_splice = -1;  // Clear pending

                    // Update bounds for new splice
                    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                                     x->reel->length, &splice_start, &splice_end);
                    x->playback_position = (float)splice_start;
                    splice_length = splice_end - splice_start;

                    // Send splice message if enabled
                    ligase_send_current_splice_msg(x);
                }

                // One-shot: stop at the boundary when loop is off and no nav was queued (nav wins).
                // Active grains are untouched (they finish on their own); the head is left valid.
                if (wrapped && !had_pending_nav && x->splice_behavior.loop_mode == 0) {
                    x->is_triggering = 0;
                    x->is_playing    = 0;
                    break;   // stop advancing the head for the rest of this DSP vector
                }
            }
        } else {
            // MODE 3: Clock advance playhead
            // Playhead advances on clock bang, retriggering grains at fixed position otherwise

            // Determine grain length to use for playhead advance
            float advance_grain_length = x->scheduler->grain_size;  // Default to current/quantized grain size

            //  Only use quantized if BPM is valid (prevent division by zero)
            if (x->clock_advance_use_quantized && x->gs_quant_grid_ms > 0.0f && x->bpm > 1.0) {
                // Use quantized grain length
                advance_grain_length = x->gs_quant_grid_ms / 1000.0f;
            }

            // Check if clock bang was received - advance playhead if so
            if (x->clock_bang_received) {
                // Convert grain length from seconds to samples
                float advance_samples = advance_grain_length * x->sample_rate;
                x->playback_position += advance_samples;

                // Wrap playback position within splice bounds (O(1) fold — never hangs)
                int wrapped = (x->playback_position >= (float)splice_end ||
                               x->playback_position < (float)splice_start ||
                               !isfinite(x->playback_position));
                x->playback_position = wrap_to_splice(x->playback_position, (float)splice_start, splice_length);

                // Bang outlet when splice ends (wraps) - only in default mode
                if (wrapped && x->outlet3_mode == 0) {
                    outlet_bang(x->x_splice_end_out);
                }

                // Check for pending splice navigation after wrapping (nav wins over the one-shot stop)
                int had_pending_nav = (wrapped && x->splice_behavior.pending_splice >= 0);
                if (had_pending_nav) {
                    x->reel->splices.current_splice = x->splice_behavior.pending_splice;
                    x->splice_behavior.pending_splice = -1;  // Clear pending

                    // Update bounds for new splice
                    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                                     x->reel->length, &splice_start, &splice_end);
                    x->playback_position = (float)splice_start;
                    splice_length = splice_end - splice_start;

                    // Send splice message if enabled
                    ligase_send_current_splice_msg(x);
                }

                // One-shot: stop at the boundary when loop is off and no nav was queued (nav wins).
                // No break needed (this wrap is inside the clock-bang guard, not the per-sample loop).
                if (wrapped && !had_pending_nav && x->splice_behavior.loop_mode == 0) {
                    x->is_triggering = 0;
                    x->is_playing    = 0;
                }

                // Clear the clock bang flag
                x->clock_bang_received = 0;
            }

            // Trigger grains at current playhead position
            for (int i = 0; i < n; i++) {
                x->grain_trigger_counter++;
                if (x->grain_trigger_counter >= x->grain_trigger_period) {
                    x->grain_trigger_counter = 0;

                    // Grains spawn at playback position (which only advances on clock bang)
                    // GrainStart offsets where grain reads relative to playhead
                    float grain_pos = x->playback_position + (x->grain_start * splice_length);

                    // Wrap grain position within splice bounds (O(1), can't hang)
                    grain_pos = wrap_to_splice(grain_pos, (float)splice_start, splice_length);

                    scheduler_trigger_grain(x->scheduler, grain_pos, x->speed, splice_start, splice_end, x->amplitude, x->pan, x->saw_cycles, x->saw_depth);

                    // Grain bang output: bang every x grains
                    if (x->grain_bang_rate > 0) {
                        x->grain_bang_counter++;
                        if (x->grain_bang_counter >= x->grain_bang_rate) {
                            x->grain_bang_counter = 0;
                            outlet_bang(x->x_grain_bang_out);
                        }
                    }
                }
            }
        }
    }

    // Process active grains (regardless of is_triggering state, let them finish)
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: About to call scheduler_process...\n");
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking x->reel... (x->reel=%p)\n", (void*)x->reel);
    if (x->reel) {
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: reel exists, checking length... (length=%d)\n", x->reel->length);
    }
    if (x->reel && x->reel->length > 0) {
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reel valid, calling scheduler_process (length=%d, n=%d)...\n",
                x->reel->length, n);
        scheduler_process(x->scheduler, x->reel, x->temp_left, x->temp_right, n);
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: scheduler_process returned\n");
        ligase_sanitize_pair(x->temp_left, x->temp_right, n);  // never let a bad grain read spread

        // Get current splice bounds for delay modes that need it
        uint32_t delay_splice_start, delay_splice_end;
        splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                         x->reel->length, &delay_splice_start, &delay_splice_end);

        // Process grain output through delay (DD-4/Bencina/Stut modes)
        grain_delay_process(x->grain_delay, x->delay_stut, x->delay_bencina,
                           x->temp_left, x->temp_right, x->delayed_left, x->delayed_right,
                           n, delay_splice_start, delay_splice_end,
                           x->pan, &x->scheduler->bencina_pan_range, &x->scheduler->perlin_state);
        // Firewall the granular signal before it reaches the dry/wet mix and the reel
        // write. constant_power_mix does in*a + delayed*b; NaN*0 == NaN, so without this
        // one bad sample silences even the 100%-dry monitor and can be recorded into the
        // reel, where it self-sustains — the "no monitor after recording" bug.
        ligase_sanitize_pair(x->delayed_left, x->delayed_right, n);

        // Output mixing based on SOS mode
        if (x->sos_mode == 1) {
            // Morphagene mode: Crossfade between input and granular output
            // sos = 1.0: 100% input, sos = 0.5: 50/50 mix, sos = 0.0: 100% granular

            // Get SOS value based on headless mode
            // headless=1: epsilon threshold (< 0.01 = use default)
            // headless=0: allow full 0.0 (perfect signal processing)
            float sos_mix = sos_in[0];

            if (x->headless_mode) {
                // Headless mode: epsilon threshold
                if (!isfinite(sos_mix) || fabsf(sos_mix) < 0.01f) {
                    sos_mix = x->sos_value;  // Unconnected or near-zero → use default
                }
            } else {
                // Perfect signal mode: only check for NaN/Inf
                if (!isfinite(sos_mix)) {
                    sos_mix = x->sos_value;
                }
            }
            if (sos_mix < 0.0f) sos_mix = 0.0f;
            if (sos_mix > 1.0f) sos_mix = 1.0f;

            // Constant-power crossfade for smooth monitoring mix
            // Maintains constant perceived loudness across SOS range
            constant_power_mix(out_left, out_right,
                             in_left, in_right,
                             x->delayed_left, x->delayed_right,
                             sos_mix, n);

            // Morphagene mode — what gets written into the reel:
            //   recinput  (INPUT_ONLY): raw input, SOS bypassed — the one non-VCA mode.
            //   recsplice (NEW_SPLICE): "what is heard" (out_*) into a NEW splice.
            //   overdub   (OVERDUB):    "what is heard" (out_*) into the CURRENT splice. Because the
            //                           splice being recorded is also the one being granulated, this
            //                           re-records the playback every pass = Time Lag Accumulation:
            //                           the SOS balance baked into out_* sets the feedback, and
            //                           pitch/grain settings accumulate across successive loops.
            // SOS bypassed only for INPUT_ONLY; the SOS VCA/feedback is already applied in out_*.
            if (x->recorder) {
                // INPUT_ONLY: raw input (SOS bypassed).
                // NEW_SPLICE: what is heard (out_*) into a fresh splice — no feedback loop.
                // OVERDUB:    Time Lag Accumulation. Re-record the granular playback into the
                //   current splice, fed back with a SUB-UNITY coefficient so the loop sustains/
                //   decays instead of growing, plus a tanh soft-limiter so it can never rail.
                //   Same input/feedback balance as the monitor mix (sin/cos), feedback capped.
                float theta = sos_mix * (float)(M_PI / 2.0);
                float in_gain = sinf(theta);
                float fb_gain = cosf(theta) * TLA_FEEDBACK_MAX;
                for (int i = 0; i < n; i++) {
                    switch (x->recorder->mode) {
                    case RECORD_MODE_INPUT_ONLY:
                        x->rec_left[i]  = in_left[i];
                        x->rec_right[i] = in_right[i];
                        break;
                    case RECORD_MODE_OVERDUB: {
                        // Bound the granular feedback to +-1 BEFORE the sub-unity coefficient, so the
                        // loop gain stays < 1 no matter the grain density (the grain engine has no gain
                        // compensation, so delayed_* can be several x unity). tanh on the sum is the
                        // final ceiling. Without this clamp the loop drives delayed_* huge and saturates.
                        float fl = x->delayed_left[i];
                        float fr = x->delayed_right[i];
                        fl = (fl >  1.0f) ?  1.0f : (fl < -1.0f ? -1.0f : fl);
                        fr = (fr >  1.0f) ?  1.0f : (fr < -1.0f ? -1.0f : fr);
                        x->rec_left[i]  = tanhf(in_gain * in_left[i]  + fb_gain * fl);
                        x->rec_right[i] = tanhf(in_gain * in_right[i] + fb_gain * fr);
                        break;
                    }
                    default:  // RECORD_MODE_NEW_SPLICE
                        x->rec_left[i]  = out_left[i];
                        x->rec_right[i] = out_right[i];
                        break;
                    }
                }

                float original_mix = x->recorder->crossfade_mix;
                x->recorder->crossfade_mix = 1.0f;  // full write; input/feedback balance is in the signal above
                recorder_process(x->recorder, x->rec_left, x->rec_right, n);
                x->recorder->crossfade_mix = original_mix;  // Restore
            }
        } else {
            // Record Only mode: Crossfade between input and granular based on SOS
            // SOS affects playback level, but recording always replaces

            // Get SOS value based on headless mode
            // headless=1: epsilon threshold (< 0.01 = use default)
            // headless=0: allow full 0.0 (perfect signal processing)
            float sos_mix = sos_in[0];

            if (x->headless_mode) {
                // Headless mode: epsilon threshold
                if (!isfinite(sos_mix) || fabsf(sos_mix) < 0.01f) {
                    sos_mix = x->sos_value;  // Unconnected or near-zero → use default
                }
            } else {
                // Perfect signal mode: only check for NaN/Inf
                if (!isfinite(sos_mix)) {
                    sos_mix = x->sos_value;
                }
            }
            if (sos_mix < 0.0f) sos_mix = 0.0f;
            if (sos_mix > 1.0f) sos_mix = 1.0f;

            // Constant-power crossfade for smooth monitoring mix
            constant_power_mix(out_left, out_right,
                             in_left, in_right,
                             x->delayed_left, x->delayed_right,
                             sos_mix, n);
        }
    } else {
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Reel length=0, entering no-playback mode\n");
        // No playback: In Morphagene mode, provide input passthrough based on sos
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: sos_mode=%d\n", x->sos_mode);
        if (x->sos_mode == 1) {
            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Morphagene mode - passthrough loop starting (n=%d)\n", n);
            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: in_left=%p, in_right=%p, out_left=%p, out_right=%p, sos_in=%p\n",
                    (void*)in_left, (void*)in_right, (void*)out_left, (void*)out_right, (void*)sos_in);

            // Get SOS value: use signal if >= 0.01, else use stored value
            // Epsilon threshold detects unconnected inlet (allows headless operation)
            float sos_mix = sos_in[0];

            if (!isfinite(sos_mix) || fabsf(sos_mix) < 0.01f) {
                sos_mix = x->sos_value;  // Unconnected or near-zero → use default
            }
            if (sos_mix < 0.0f) sos_mix = 0.0f;
            if (sos_mix > 1.0f) sos_mix = 1.0f;

            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: x->sos_value=%f, sos_in[0]=%f, using sos_mix=%f for passthrough\n",
                    x->sos_value, sos_in[0], sos_mix);

            for (int i = 0; i < n; i++) {
                // When no playback, SOS acts as input VCA:
                // sos = 1.0: full input passthrough
                // sos = 0.0: silence (no granular content available to mix)
                out_left[i] = in_left[i] * sos_mix;
                out_right[i] = in_right[i] * sos_mix;
            }
            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Loop completed\n");

            if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking recorder... (x->recorder=%p)\n", (void*)x->recorder);
            // Record in Morphagene mode (even without playback)
            // When no playback exists, record raw input with full replacement (no feedback needed)
            if (x->recorder) {
                if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Recorder exists, copying to rec buffers...\n");
                if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: x->rec_left=%p, x->rec_right=%p\n",
                        (void*)x->rec_left, (void*)x->rec_right);
                // No existing content yet (empty reel), so there's nothing to feed back here.
                // recinput writes raw input; recsplice/overdub write what is heard (out_*, = in*sos
                // with no playback). recorder_process fully replaces regardless.
                for (int i = 0; i < n; i++) {
                    if (x->recorder->mode == RECORD_MODE_INPUT_ONLY) {
                        x->rec_left[i]  = in_left[i];
                        x->rec_right[i] = in_right[i];
                    } else {
                        x->rec_left[i]  = out_left[i];
                        x->rec_right[i] = out_right[i];
                    }
                }
                if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Buffers copied, calling recorder_process...\n");
                float original_mix = x->recorder->crossfade_mix;
                x->recorder->crossfade_mix = 1.0f;  // Full replacement when no existing content
                recorder_process(x->recorder, x->rec_left, x->rec_right, n);
                if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: recorder_process returned\n");
                x->recorder->crossfade_mix = original_mix;
            }
        }
        // Record Only mode: output remains silence (initialized above)
    }

}

static void ligase_process_effects(ligase_t *x,
    t_sample *out_left, t_sample *out_right, int n) {
    // MONITORING EFFECTS: Smear, Distortion, and Moogladder applied after recording
    // Signal chain: Grains → Mix → Delay → [RECORDING] → SMEAR → DISTORTION → MOOGLADDER → Output
    // These are monitor-only — never written to the reel, so they can't accumulate in overdub
    // Distortion feeds into filter for classic Moog sound with rich harmonics and self-oscillation

    // Allpass smear (monitoring only, not recorded): a cascade of 2nd-order
    // allpass sections — cheap, stable, time-domain spectral smearing.
    if (x->smear) {
        grain_smear_process(x->smear, out_left, out_right, n);
    }

    // Distortion (monitoring only, not recorded)
    if (x->scheduler->distortion &&
        x->scheduler->distortion->enabled &&
        x->scheduler->distortion->position_mode == 1) {

        //Validate before processing
        if (x->scheduler->distortion->magic != 0xD157BEEF) {
            pd_error(x, "ligase~: distortion use-after-free detected!");
        } else {
            // Process final output with oversampling for warm, musical saturation
            grain_distortion_process_block(
                x->scheduler->distortion,
                out_left, out_right,  // Input: final mixed output
                out_left, out_right,  // Output: in-place processing
                n
            );
        }
    }

    // Moogladder filter (monitoring only, not recorded)
    // Fed by distorted signal for natural, musical saturation and self-oscillation
    if (x->moogladder) {
        grain_moogladder_process(x->moogladder, out_left, out_right, n);
    }

    // Final output safety: clamp to [-1, 1] and flush denormals/NaN/Inf
    // Prevents DAC clipping from accumulated effects chain gain
    for (int i = 0; i < n; i++) {
        if (!isfinite(out_left[i])) out_left[i] = 0.0f;
        else if (out_left[i] > 1.0f) out_left[i] = 1.0f;
        else if (out_left[i] < -1.0f) out_left[i] = -1.0f;

        if (!isfinite(out_right[i])) out_right[i] = 0.0f;
        else if (out_right[i] > 1.0f) out_right[i] = 1.0f;
        else if (out_right[i] < -1.0f) out_right[i] = -1.0f;
    }
}


static t_int *ligase_perform(t_int *w) {
    // Flush denormals to zero for this audio callback (FPU mode is per-thread). Prevents the
    // gradual CPU climb from subnormal floats piling up in delay/moog/distortion feedback
    // states when the granular output decays toward silence.
    LIGASE_FLUSH_DENORMALS();

    static int first_call = 1;
    if (first_call && LIGASE_DEBUG) {
        fprintf(stderr, "ligase_perform: FIRST CALL (w=%p)\n", (void*)w);
        first_call = 0;
    }

    ligase_t *x = (ligase_t *)(w[1]);

    if (!x) {
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: ERROR - NULL x pointer!\n");
        return (w + 27);
    }

    if (first_call && LIGASE_DEBUG) {
        fprintf(stderr, "ligase_perform: x=%p, scheduler=%p, reel=%p, envelope=%p\n",
                (void*)x, (void*)x->scheduler, (void*)x->reel, (void*)x->envelope);
        fprintf(stderr, "ligase_perform: Extracting signal pointers...\n");
    }

    t_sample *in_left = (t_sample *)(w[2]);
    t_sample *in_right = (t_sample *)(w[3]);
    t_sample *grain_size_in = (t_sample *)(w[4]);
    t_sample *grain_start_in = (t_sample *)(w[5]);
    t_sample *speed_in = (t_sample *)(w[6]);
    t_sample *organize_in = (t_sample *)(w[7]);
    t_sample *scanrate_in = (t_sample *)(w[8]);
    t_sample *sos_in = (t_sample *)(w[9]);
    t_sample *iot_in = (t_sample *)(w[10]);
    t_sample *maxgrains_in = (t_sample *)(w[11]);
    t_sample *gdelay_time_in = (t_sample *)(w[12]);
    t_sample *gdelay_feedback_in = (t_sample *)(w[13]);
    t_sample *gdelay_tone_in = (t_sample *)(w[14]);
    t_sample *gdelay_mix_in = (t_sample *)(w[15]);
    t_sample *smear_in = (t_sample *)(w[16]);
    t_sample *moog_cutoff_in = (t_sample *)(w[17]);
    t_sample *moog_resonance_in = (t_sample *)(w[18]);
    t_sample *moog_mix_in = (t_sample *)(w[19]);
    t_sample *midi_in = (t_sample *)(w[20]);
    t_sample *env_skew_in = (t_sample *)(w[21]);
    t_sample *amplitude_in = (t_sample *)(w[22]);
    t_sample *pan_in = (t_sample *)(w[23]);
    t_sample *out_left = (t_sample *)(w[24]);
    t_sample *out_right = (t_sample *)(w[25]);
    int n = (int)(w[26]);

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Signal pointers extracted, blocksize=%d\n", n);

    // Bounds check: ensure block size doesn't exceed fixed buffer size
    if (n > 8192) {
        pd_error(x, "ligase~: block size %d exceeds maximum 8192", n);
        // Emit silence rather than leaving the output buffers untouched (which would feed
        // stale/garbage downstream as a dropout burst).
        if (out_left)  memset(out_left, 0, sizeof(t_sample) * n);
        if (out_right) memset(out_right, 0, sizeof(t_sample) * n);
        return (w + 27);
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Blocksize check passed, validating pointers...\n");

    //  Validate all signal pointers before dereferencing
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking in_left...\n");
    if (!in_left) goto null_ptr_error;
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking in_right...\n");
    if (!in_right) goto null_ptr_error;
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking grain_size_in...\n");
    if (!grain_size_in) goto null_ptr_error;
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: Checking grain_start_in...\n");
    if (!grain_start_in) goto null_ptr_error;
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: All input signal pointers OK\n");

    if (!speed_in || !organize_in || !scanrate_in || !sos_in || !iot_in || !maxgrains_in ||
        !gdelay_time_in || !gdelay_feedback_in || !gdelay_tone_in || !gdelay_mix_in ||
        !smear_in || !moog_cutoff_in || !moog_resonance_in || !moog_mix_in ||
        !midi_in || !env_skew_in || !amplitude_in || !pan_in || !out_left || !out_right) {
null_ptr_error:
        if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: ERROR - NULL signal pointer detected!\n");
        if (LIGASE_DEBUG) fprintf(stderr, "  in_left=%p in_right=%p grain_size_in=%p\n",
                (void*)in_left, (void*)in_right, (void*)grain_size_in);
        // Zero output and return
        for (int i = 0; i < n; i++) {
            if (out_left) out_left[i] = 0.0f;
            if (out_right) out_right[i] = 0.0f;
        }
        return (w + 27);
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: All signal pointers validated\n");

    // Advance the free-running pattern cycle clock once per block, then evaluate each active slot.
    // (Fifth clock — independent of the four quant grids.) Guards mirror the grid guards: BPM must
    // be set (>1.0), the cycle length valid (>0), and the scheduler present. Until then the clock
    // holds at phase 0 (no NaN, no div-by-zero). pattern_eval_slot is the sole writer of the cache.
    if (x->scheduler && x->bpm > 1.0 && x->cycle_total_sec > 0.0) {
        perlin_state_t *ps = &x->scheduler->perlin_state;
        double inc = ((double)n / (double)x->sample_rate) / x->cycle_total_sec;  // real samples / cycle
        for (int s = 0; s < PATTERN_SLOTS; s++) {
            if (ps->pattern[s].step_count < 1) continue;
            ps->pattern_phase[s] += (float)inc;
            while (ps->pattern_phase[s] >= 1.0f) {           // handles >1 cycle/block (tiny cycle)
                ps->pattern_phase[s] -= 1.0f;
                ps->pattern_cycle_index[s] += 1;             // integer counter drives <> alternation
            }
            pattern_eval_slot(ps, s);
            if (x->pattern_debug && ps->pattern[s].changed) {
                fprintf(stderr, "ligase~ pat t=%.1fms slot %d: step %d value %.4f rest %d cycle %ld\n",
                        (double)clock_getlogicaltime() / 14112.0, s,
                        ps->pattern[s].last_step_index, ps->pattern[s].cached_value,
                        ps->pattern[s].cached_is_rest, ps->pattern_cycle_index[s]);
            }
        }
    }

    ligase_update_inlets(x, grain_size_in, grain_start_in, speed_in, organize_in,
        scanrate_in, sos_in, iot_in, maxgrains_in,
        gdelay_time_in, gdelay_feedback_in, gdelay_tone_in, gdelay_mix_in,
        smear_in, moog_cutoff_in, moog_resonance_in, moog_mix_in,
        midi_in, env_skew_in, amplitude_in, pan_in, n);
    ligase_process_grains(x, in_left, in_right, out_left, out_right, sos_in, n);

    // Pattern pitch trace (verification aid): log the APPLIED semitone whenever it changes, so the
    // degree->semitone wrap/octave is observable. Reads last_semitone after grains have triggered.
    if (x->pattern_debug && x->scheduler &&
        x->scheduler->pitch_control.mode == PITCH_MODE_PATTERN) {
        float sem = x->scheduler->pitch_control.last_semitone;
        if (sem != x->pattern_pitch_last_printed) {
            x->pattern_pitch_last_printed = sem;
            int psl = PATTERN_SLOTS - 1;
            fprintf(stderr, "ligase~ pitch: degree %.0f -> semitone %.2f (cycle %ld)\n",
                    x->scheduler->perlin_state.pattern[psl].cached_value, sem,
                    x->scheduler->perlin_state.pattern_cycle_index[psl]);
        }
    }
    ligase_process_effects(x, out_left, out_right, n);
    // @region:ligase_pd.pd_external.outlets.modulation Modulation Outlet Computation
    // Compute and output modulation values (control rate, once per DSP block)
    // Modulation outlets now use unified param_range system (same as internal parameters)

    if (x->modout1_range.enabled && x->modout1_range.rand_type != RAND_TYPE_NONE) {
        float value = sample_param_range(&x->modout1_range, &x->scheduler->perlin_state, 0.5f);
        outlet_float(x->x_modout1, value);
    }

    if (x->modout2_range.enabled && x->modout2_range.rand_type != RAND_TYPE_NONE) {
        float value = sample_param_range(&x->modout2_range, &x->scheduler->perlin_state, 0.5f);
        outlet_float(x->x_modout2, value);
    }

    if (x->modout3_range.enabled && x->modout3_range.rand_type != RAND_TYPE_NONE) {
        float value = sample_param_range(&x->modout3_range, &x->scheduler->perlin_state, 0.5f);
        outlet_float(x->x_modout3, value);
    }

    if (x->modout4_range.enabled && x->modout4_range.rand_type != RAND_TYPE_NONE) {
        float value = sample_param_range(&x->modout4_range, &x->scheduler->perlin_state, 0.5f);
        outlet_float(x->x_modout4, value);
    }

    // @endregion:ligase_pd.pd_external.outlets.modulation

    // @region:ligase_pd.pd_external.outlets.outlet3_note_change Outlet 3 Note Change Detection
    // Outlet 3 mode 1: Bang on MIDI note or scale semitone change
    if (x->outlet3_mode == 1) {
        int note_changed = 0;

        if (x->scheduler->pitch_control.mode == PITCH_MODE_MIDI) {
            // Check for MIDI note change
            if (x->scheduler->pitch_control.midi_note != x->prev_midi_note) {
                note_changed = 1;
                x->prev_midi_note = x->scheduler->pitch_control.midi_note;
            }
        } else if (x->scheduler->pitch_control.mode == PITCH_MODE_SCALE ||
                   x->scheduler->pitch_control.mode == PITCH_MODE_RANGE ||
                   x->scheduler->pitch_control.mode == PITCH_MODE_PATTERN) {
            // Check for semitone change (scale / range / pattern mode)
            if (x->scheduler->pitch_control.last_semitone != x->prev_scale_semitone) {
                note_changed = 1;
                x->prev_scale_semitone = x->scheduler->pitch_control.last_semitone;
            }
        }

        if (note_changed) {
            outlet_bang(x->x_splice_end_out);  // Repurpose outlet 3 for note changes
        }
    }
    // @endregion:ligase_pd.pd_external.outlets.outlet3_note_change

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_perform: About to return (w + 27)\n");
    return (w + 27);
}

// Propagate a host sample-rate change to every subsystem, reallocating buffers and
// recomputing any value derived from the previous rate. Called from ligase_dsp (main
// thread, dsp graph locked) ONLY when the rate actually changes — never from the audio
// thread, and not on every dsp re-add (which would needlessly wipe the delay line).
static void ligase_set_sample_rate(ligase_t *x, int sr) {
    if (sr <= 0) return;
    x->sample_rate = sr;

    // SR-dependent values computed live each block/trigger — scalar update suffices
    if (x->scheduler)  x->scheduler->sample_rate = sr;   // grain length computed per-trigger
    if (x->moogladder) x->moogladder->sample_rate = sr;  // cutoff normalized per-block
    if (x->delay_stut) x->delay_stut->sample_rate = sr;  // stut spacing computed per-trigger
    if (x->smear)      grain_smear_set_sample_rate(x->smear, sr);

    // Subsystems that cache derived state — must reallocate / recompute
    if (x->reel)          reel_set_sample_rate(x->reel, sr);                          // resize 10-min reel to rate
    if (x->grain_delay)   grain_delay_set_sample_rate(x->grain_delay, sr);            // realloc 9.5 s line
    if (x->delay_bencina) grain_delay_bencina_set_sample_rate(x->delay_bencina, sr);  // recompute trigger period
    if (x->scheduler && x->scheduler->distortion)
        grain_distortion_set_sample_rate(x->scheduler->distortion, sr);              // recompute IIR coeffs + reset
}

static void ligase_dsp(ligase_t *x, t_signal **sp) {
    if (LIGASE_DEBUG) fprintf(stderr, "ligase_dsp: CALLED (x=%p, sp=%p)\n", (void*)x, (void*)sp);

    // Safety check: verify all required pointers are valid
    if (!x) {
        pd_error(NULL, "ligase~: NULL object pointer in dsp callback");
        return;
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_dsp: x validated, checking components\n");

    // Check critical component pointers
    if (!x->scheduler || !x->envelope || !x->reel || !x->recorder || !x->grain_delay || !x->moogladder) {
        pd_error(x, "ligase~: critical component not initialized");
        return;
    }

    // Verify signal pointers are valid (check first and last)
    if (!sp || !sp[0] || !sp[23]) {
        pd_error(x, "ligase~: invalid signal pointer array");
        return;
    }

    // Verify signal vectors are valid
    for (int i = 0; i < 24; i++) {
        if (!sp[i] || !sp[i]->s_vec) {
            pd_error(x, "ligase~: invalid signal vector at index %d", i);
            return;
        }
    }

    // Re-initialize subsystems only when the host sample rate actually changes. Pd re-calls
    // this method on any DSP graph / SR / blocksize change, so this is the correct re-init
    // hook. Gating on change avoids reallocating (and wiping) the delay line every restart.
    int new_sr = (int)sp[0]->s_sr;
    if (new_sr > 0 && new_sr != x->sample_rate) {
        ligase_set_sample_rate(x, new_sr);
    }

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_dsp: Adding perform callback (sr=%d, blocksize=%d)\n",
            x->sample_rate, sp[0]->s_n);

    dsp_add(ligase_perform, 26, x,
            sp[0]->s_vec,   // in_left
            sp[1]->s_vec,   // in_right
            sp[2]->s_vec,   // grain_size
            sp[3]->s_vec,   // grain_start
            sp[4]->s_vec,   // speed
            sp[5]->s_vec,   // organize
            sp[6]->s_vec,   // scanrate
            sp[7]->s_vec,   // sos
            sp[8]->s_vec,   // iot
            sp[9]->s_vec,   // maxgrains
            sp[10]->s_vec,  // gdelay_time
            sp[11]->s_vec,  // gdelay_feedback
            sp[12]->s_vec,  // gdelay_tone
            sp[13]->s_vec,  // gdelay_mix
            sp[14]->s_vec,  // distortion
            sp[15]->s_vec,  // moog_cutoff
            sp[16]->s_vec,  // moog_resonance
            sp[17]->s_vec,  // moog_mix
            sp[18]->s_vec,  // midi
            sp[19]->s_vec,  // env_skew
            sp[20]->s_vec,  // amplitude
            sp[21]->s_vec,  // pan
            sp[22]->s_vec,  // out_left
            sp[23]->s_vec,  // out_right
            sp[0]->s_n);

    if (LIGASE_DEBUG) fprintf(stderr, "ligase_dsp: COMPLETE\n");
}

// @endregion:ligase_pd.pd_external.dsp

// @region:ligase_pd.pd_external.methods Pd Methods

static void ligase_load(ligase_t *x, t_symbol *s) {
    if (!s || !s->s_name || !*s->s_name) {
        pd_error(x, "ligase~: load needs a filename");
        return;
    }
    // Resolve via Pd: searches the patch directory first, then the Pd search path; absolute
    // paths open directly. (Raw fopen here resolved against Pd's CWD = "/" under a Finder-
    // launched Pd.app, so relative loads failed on macOS.)
    char dirbuf[MAXPDSTRING], *nameptr = NULL;
    int fd = canvas_open(x->x_canvas, s->s_name, "", dirbuf, &nameptr, MAXPDSTRING, 1);
    if (fd < 0) {
        pd_error(x, "ligase~: load — file not found on patch/search path: %s", s->s_name);
        return;
    }
    sys_close(fd);  // reel_load_wav reopens by path
    char path[MAXPDSTRING];
    int wrote = snprintf(path, MAXPDSTRING, "%s/%s", dirbuf, nameptr ? nameptr : s->s_name);
    if (wrote < 0 || wrote >= MAXPDSTRING) {
        pd_error(x, "ligase~: load — resolved path too long");
        return;
    }
    int rc = reel_load_wav(x->reel, path);
    if (rc == REEL_IO_OK) {
        post("ligase~: loaded %s (%d samples)", path, x->reel->length);
    } else {
        pd_error(x, "ligase~: load failed — %s: %s", reel_io_strerror(rc), path);
    }
}

static void ligase_save(ligase_t *x, t_symbol *s) {
    if (!s || !s->s_name || !*s->s_name) {
        pd_error(x, "ligase~: save needs a filename");
        return;
    }
    // Anchor a relative name to the patch directory (absolute paths pass through). Without
    // this, a relative save resolved against Pd's CWD = "/" under a Finder-launched Pd.app.
    char path[MAXPDSTRING];
    canvas_makefilename(x->x_canvas, s->s_name, path, MAXPDSTRING);
    // Ensure a .wav extension (case-insensitive)
    size_t len = strlen(path);
    int has_wav = (len >= 4 && path[len-4] == '.'
                   && (path[len-3]=='w'||path[len-3]=='W')
                   && (path[len-2]=='a'||path[len-2]=='A')
                   && (path[len-1]=='v'||path[len-1]=='V'));
    if (!has_wav && len + 4 < MAXPDSTRING) {
        path[len] = '.'; path[len+1] = 'w'; path[len+2] = 'a'; path[len+3] = 'v'; path[len+4] = '\0';
    }
    int rc = reel_save_wav(x->reel, path);
    if (rc == REEL_IO_OK) {
        post("ligase~: saved %s", path);
    } else {
        pd_error(x, "ligase~: save failed — %s: %s", reel_io_strerror(rc), path);
    }
}

// loop <0|1> : 1 = loop forever (default), 0 = one-shot (stop at splice end).
static void ligase_loop(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->splice_behavior.loop_mode = m;
        post("ligase~: loop set to %d (%s)", m, m ? "loop" : "oneshot");
    } else {
        pd_error(x, "ligase~: invalid loop %d (use 0 or 1)", m);
    }
}

// trigger : (re)start playback from the start of the current splice WITHOUT silencing active grains
// (the re-arm of play 1 minus the debug dump; for re-triggering one-shot playback).
static void ligase_trigger(ligase_t *x) {
    if (x->reel->length == 0) {
        pd_error(x, "ligase~: cannot trigger - no audio loaded");
        return;
    }
    uint32_t s, e;
    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice, x->reel->length, &s, &e);
    x->playback_position = (float)s;
    x->is_playing    = 1;
    x->is_triggering = 1;
}

static void ligase_play(ligase_t *x, t_floatarg f) {
    x->is_playing = (f != 0.0f);
    x->is_triggering = x->is_playing;  // Controls new grain triggering
    // NOTE: Active grains always play out regardless of is_playing state
    // This allows rhythmic stop/play messages without interrupting grains
    if (x->is_playing) {
        // CRITICAL CHECK: Verify audio is loaded
        if (x->reel->length == 0) {
            pd_error(x, "ligase~: cannot play - no audio loaded (record or load a file first)");
            x->is_playing = 0;
            x->is_triggering = 0;
            return;
        }
        // NOTE: No splice check needed - splice_get_bounds handles count==0 gracefully
        // by returning start=0, end=reel_length, allowing playback of recorded audio

        // Initialize playback position at start of current splice
        uint32_t splice_start, splice_end;
        splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                       x->reel->length, &splice_start, &splice_end);
        x->playback_position = splice_start;

        // DEBUG: Show detailed playback state
        post("ligase~: playback started");
        post("  reel length: %d samples (%.2f seconds)", x->reel->length, x->reel->length / (float)x->sample_rate);
        post("  splice bounds: %d - %d (length: %d samples)", splice_start, splice_end, splice_end - splice_start);
        post("  grain_size: %.3f sec, iot: %.3f sec, max_grains: %d",
             x->scheduler->grain_size, x->scheduler->iot, x->scheduler->max_grains);
        post("  grain_trigger_period: %d samples", x->grain_trigger_period);

        // Check buffer content (sample a few points)
        if (x->reel->length > 0) {
            float sum_left = 0.0f, sum_right = 0.0f;
            int check_samples = (x->reel->length > 1000) ? 1000 : x->reel->length;
            for (int i = 0; i < check_samples; i++) {
                sum_left += fabsf(x->reel->buffer_left[i]);
                sum_right += fabsf(x->reel->buffer_right[i]);
            }
            float avg_left = sum_left / check_samples;
            float avg_right = sum_right / check_samples;
            post("  buffer check: avg amplitude L=%.6f R=%.6f", avg_left, avg_right);
            if (avg_left < 0.000001f && avg_right < 0.000001f) {
                pd_error(x, "ligase~: WARNING - buffer appears to be silent!");
            }
        }
    } else {
        post("ligase~: playback stopped (grains finishing)");
    }
}

static void ligase_record(ligase_t *x, t_floatarg f) {
    if (f != 0.0f) {
        // Morphagene "Rec": overdub into current splice
        recorder_set_mode(x->recorder, RECORD_MODE_OVERDUB);
        recorder_start(x->recorder);
        post("ligase~: recording started (overdub mode)");
    } else {
        // recinput and recsplice both record onto fresh tape and plant their boundary marker
        // at START (pinning the current splice's end so the take can't grow it — recinput keeps
        // the playing splice granulating, recsplice also avoids feeding back on its own output).
        // So on stop the new splice is simply the last marker; no marker is created here.
        // Navigation afterward is the SHARED splice_jump (jump_to_new) option — identical for
        // both modes: 0 = stay in the current splice (default), 1 = jump to the new splice and
        // reposition the playhead (same path as ligase_add_splice). They differ only in WHAT was
        // recorded (recinput = raw input, recsplice = the SOS monitor mix).
        int is_new_splice = (x->recorder->mode == RECORD_MODE_NEW_SPLICE);
        int is_input_only = (x->recorder->mode == RECORD_MODE_INPUT_ONLY);
        if ((is_new_splice || is_input_only) && x->recorder->is_recording) {
            int new_splice_index = x->reel->splices.count - 1;  // marker planted at start
            const char *mode_name = is_new_splice ? "recsplice" : "recinput";
            if (x->splice_behavior.jump_to_new == 1) {
                x->reel->splices.current_splice = new_splice_index;
                uint32_t new_start, new_end;
                splice_get_bounds(&x->reel->splices, new_splice_index,
                                 x->reel->length, &new_start, &new_end);
                x->playback_position = (float)new_start;
                post("ligase~: %s stopped, jumped to new splice %d", mode_name, new_splice_index);
                ligase_send_current_splice_msg(x);
            } else {
                post("ligase~: %s stopped, recorded new splice %d (staying on %d)",
                     mode_name, new_splice_index, x->reel->splices.current_splice);
            }
        } else {
            post("ligase~: recording stopped");
        }
        recorder_stop(x->recorder);
    }
}

static void ligase_shift(ligase_t *x, t_floatarg delta) {
    if (x->splice_behavior.finish_before_nav == 1) {
        // Calculate target splice and store as pending
        // Use pending_splice as base if it exists, otherwise use current_splice
        // This allows shifts to accumulate before navigation executes
        int base = (x->splice_behavior.pending_splice >= 0) ?
                   x->splice_behavior.pending_splice :
                   x->reel->splices.current_splice;
        int target = base + (int)delta;
        int count = x->reel->splices.count;
        if (count > 0) {
            target = ((target % count) + count) % count;  // Wrap around
            x->splice_behavior.pending_splice = target;
            post("ligase~: splice %d queued (will navigate after current splice finishes)", target);
        }
    } else {
        // Immediate navigation (original behavior)
        splice_shift(&x->reel->splices, (int)delta);
        post("ligase~: splice %d selected", x->reel->splices.current_splice);
        ligase_send_current_splice_msg(x);
    }
}

static void ligase_organize(ligase_t *x, t_floatarg normalized_input) {
    if (x->splice_behavior.finish_before_nav == 1) {
        // Calculate target splice and store as pending
        if (x->reel->splices.count > 0) {
            // Map normalized input (0.0 to 1.0) to discrete splice index
            float normalized = normalized_input;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;
            int target = (int)(normalized * (x->reel->splices.count - 1));

            x->splice_behavior.pending_splice = target;
            post("ligase~: splice %d queued (will navigate after current splice finishes)", target);
        }
    } else {
        // Immediate navigation (original behavior)
        splice_organize(&x->reel->splices, normalized_input);
        post("ligase~: splice %d selected", x->reel->splices.current_splice);
        ligase_send_current_splice_msg(x);
    }
}

static void ligase_add_splice(ligase_t *x) {
    // Rate limiting: prevent crashes from rapid splice creation
    double current_time = clock_getlogicaltime();
    // Convert cooldown from seconds to Pd time units (TIMEUNITPERMSEC = 14112.0)
    double cooldown_units = x->splice_cooldown * 1000.0 * 14112.0;
    if (current_time - x->last_splice_time < cooldown_units) {
        pd_error(x, "ligase~: splice creation too rapid (min %.0fms between splices)",
                 x->splice_cooldown * 1000.0);
        return;
    }

    uint32_t pos;
    uint32_t splice_start, splice_end;
    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                     x->reel->length, &splice_start, &splice_end);

    // Determine splice creation position based on create_position option
    if (x->splice_behavior.create_position == 1) {
        // Mode 1: Create to the right of current splice (at its end boundary)
        pos = splice_end;
    } else if (x->splice_behavior.create_position == 2) {
        // Mode 2: Create at end of reel (Morphagene append mode)
        pos = x->reel->length;
    } else {
        // Mode 0: Create at playback position (original Morphagene behavior)
        pos = (uint32_t)x->playback_position;
        if (pos == 0 && x->reel->length > 0) {
            pos = x->reel->length;  // If not playing, add at end
        }

        // Check split_current option: if enabled, prevent splitting current splice
        if (x->splice_behavior.split_current == 1) {
            // If position would split current splice, move to end of current splice
            if (pos > splice_start && pos < splice_end) {
                pos = splice_end;
                post("ligase~: preserving current splice length, creating at position %d", pos);
            }
        }
    }

    int new_splice_index = x->reel->splices.count;  // Index of the new splice

    if (splice_add(&x->reel->splices, pos, NULL) != 0) {
        pd_error(x, "ligase~: failed to add splice (max %d splices reached)", MAX_SPLICES);
        return;
    }

    x->last_splice_time = current_time;
    post("ligase~: splice added at %d (total: %d)", pos, x->reel->splices.count);

    // Jump to new splice if jump_to_new option is enabled
    if (x->splice_behavior.jump_to_new == 1) {
        x->reel->splices.current_splice = new_splice_index;
        // Reset playback position to start of new splice
        uint32_t new_start, new_end;
        splice_get_bounds(&x->reel->splices, new_splice_index,
                         x->reel->length, &new_start, &new_end);
        x->playback_position = (float)new_start;
        post("ligase~: jumped to new splice %d", new_splice_index);
        ligase_send_current_splice_msg(x);
    }
}

static void ligase_rec_splice(ligase_t *x) {
    // Morphagene "Rec + Splice": lay what-you-hear onto FRESH tape as a NEW splice.
    // Plant the boundary marker at the take's start point NOW (not on stop): the last
    // splice's end always tracks reel->length (splice_get_bounds), so without a marker
    // here the current splice would grow into the tape being recorded and the granulator
    // would read its own freshly-recorded output = feedback/overdub, not a clean capture.
    // We keep granulating the EXISTING splice during the take (current_splice unchanged)
    // and only move to the new splice on stop, honoring jump_to_new.
    recorder_set_mode(x->recorder, RECORD_MODE_NEW_SPLICE);
    recorder_start(x->recorder);  // sets new_splice_start = record_position = reel->length
    int splice_pos = recorder_get_new_splice_start(x->recorder);
    if (splice_add(&x->reel->splices, splice_pos, NULL) != 0) {
        pd_error(x, "ligase~: failed to create splice (max %d splices reached)", MAX_SPLICES);
        recorder_stop(x->recorder);
        return;
    }
    post("ligase~: recsplice recording started (new splice %d pinned at sample %d)",
         x->reel->splices.count, splice_pos);
}

static void ligase_rec_input(ligase_t *x) {
    // Input-only recording: capture RAW input (no sound-on-sound) onto FRESH tape as a new
    // splice. Like recsplice, plant the boundary marker at START so the current splice's end
    // is pinned at the take's start point and can't grow into the tape being recorded — that
    // keeps the splice you're playing granulating throughout the take instead of drifting into
    // the freshly-recorded input. The only difference from recsplice is WHAT is recorded
    // (raw input vs the SOS monitor mix); switching on stop is the shared jump_to_new option.
    recorder_set_mode(x->recorder, RECORD_MODE_INPUT_ONLY);
    recorder_start(x->recorder);  // sets new_splice_start = record_position = reel->length
    int splice_pos = recorder_get_new_splice_start(x->recorder);
    if (splice_add(&x->reel->splices, splice_pos, NULL) != 0) {
        pd_error(x, "ligase~: failed to create splice (max %d splices reached)", MAX_SPLICES);
        recorder_stop(x->recorder);
        return;
    }
    post("ligase~: input-only recording started (new splice %d pinned at sample %d)",
         x->reel->splices.count, splice_pos);
}

static void ligase_clear_splices(ligase_t *x) {
    // Remove all splices: clear markers and audio buffer
    splice_clear(&x->reel->splices);
    reel_clear(x->reel);
    post("ligase~: all splices removed (buffer cleared)");
}

static void ligase_clear_splices_except_current(ligase_t *x) {
    // Get current splice bounds before reorganizing markers
    uint32_t splice_start, splice_end;
    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                     x->reel->length, &splice_start, &splice_end);

    // Clear audio buffer except for current splice (destructive)
    reel_clear_except_splice(x->reel, splice_start, splice_end);

    // Reorganize splice markers (current splice moved to position 0)
    splice_clear_except_current(&x->reel->splices, x->reel->length);

    post("ligase~: all splices cleared except current splice (kept %d samples at position 0)",
         splice_end - splice_start);
}

static void ligase_splice_join_right(ligase_t *x) {
    int old_count = x->reel->splices.count;
    int old_current = x->reel->splices.current_splice;

    splice_join_right(&x->reel->splices);

    if (x->reel->splices.count < old_count) {
        post("ligase~: joined splice %d with splice to the right (total: %d splices)",
             old_current, x->reel->splices.count);
    } else {
        post("ligase~: cannot join (only one splice exists)");
    }
}

static void ligase_splice_join_all(ligase_t *x) {
    int old_count = x->reel->splices.count;

    splice_join_all(&x->reel->splices);

    post("ligase~: joined all %d splices into one (entire reel is now one splice)", old_count);
}

static void ligase_clear_current_splice(ligase_t *x) {
    if (x->reel->splices.count == 0) {
        post("ligase~: no splices to remove");
        return;
    }

    int old_current = x->reel->splices.current_splice;

    // Remove the current splice immediately (existing grains will finish naturally)
    splice_remove(&x->reel->splices, x->reel->splices.current_splice);

    post("ligase~: removed splice %d, %d splices remain",
         old_current, x->reel->splices.count);
}

static void ligase_splice_create_pos(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1 || m == 2) {
        x->splice_behavior.create_position = m;
        const char *mode_str;
        if (m == 0) mode_str = "at playback position";
        else if (m == 1) mode_str = "right of current splice";
        else mode_str = "at end of reel (Morphagene)";
        post("ligase~: splice create_position set to %d (%s)", m, mode_str);
    } else {
        pd_error(x, "ligase~: invalid create_position %d (use 0, 1, or 2)", m);
    }
}

static void ligase_splice_jump(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->splice_behavior.jump_to_new = m;
        post("ligase~: splice jump_to_new set to %d (%s)",
             m, m == 0 ? "stay in current" : "jump to new");
    } else {
        pd_error(x, "ligase~: invalid jump_to_new %d (use 0 or 1)", m);
    }
}

static void ligase_splice_finish_nav(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->splice_behavior.finish_before_nav = m;
        post("ligase~: splice finish_before_nav set to %d (%s)",
             m, m == 0 ? "immediate navigation" : "finish playback before nav");
    } else {
        pd_error(x, "ligase~: invalid finish_before_nav %d (use 0 or 1)", m);
    }
}

static void ligase_splice_split(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->splice_behavior.split_current = m;
        post("ligase~: splice split_current set to %d (%s)",
             m, m == 0 ? "allow splitting current splice" : "preserve current splice length");
    } else {
        pd_error(x, "ligase~: invalid split_current %d (use 0 or 1)", m);
    }
}

// @region:ligase_pd.core.splice.messages Splice Message System

static void ligase_send_splice_msg_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->splice_behavior.send_splice_msg = m;
        post("ligase~: send_splice_msg set to %d (%s)",
             m, m == 0 ? "disabled" : "enabled");
    } else {
        pd_error(x, "ligase~: invalid send_splice_msg mode %d (use 0 or 1)", m);
    }
}

static void ligase_splice_msg(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;  // Unused

    if (argc < 2) {
        pd_error(x, "ligase~: splice_msg requires at least 2 arguments: <splice_number> <message>");
        return;
    }

    int splice_num = (int)atom_getfloat(&argv[0]);

    // Validate splice number (allow setting messages for splices that don't exist yet)
    if (splice_num < 0 || splice_num >= MAX_SPLICES) {
        pd_error(x, "ligase~: splice number %d out of range (0-%d)", splice_num, MAX_SPLICES - 1);
        return;
    }

    // Build message string from remaining arguments
    char message[256];
    message[0] = '\0';

    for (int i = 1; i < argc; i++) {
        char buf[64];
        if (argv[i].a_type == A_FLOAT) {
            snprintf(buf, sizeof(buf), "%g", atom_getfloat(&argv[i]));
        } else if (argv[i].a_type == A_SYMBOL) {
            snprintf(buf, sizeof(buf), "%s", atom_getsymbol(&argv[i])->s_name);
        } else {
            continue;
        }

        // Add space before each argument except the first
        if (i > 1) {
            strncat(message, " ", sizeof(message) - strlen(message) - 1);
        }
        strncat(message, buf, sizeof(message) - strlen(message) - 1);
    }

    // Create the splice if it doesn't exist yet (up to splice_num)
    while (x->reel->splices.count <= splice_num) {
        uint32_t pos = (x->reel->splices.count == 0) ? 0 : x->reel->length;
        if (splice_add(&x->reel->splices, pos, NULL) != 0) {
            pd_error(x, "ligase~: failed to create splice %d", x->reel->splices.count);
            return;
        }
    }

    // Store the message (safe copy with guaranteed null termination)
    snprintf(x->reel->splices.markers[splice_num].message,
             sizeof(x->reel->splices.markers[splice_num].message),
             "%s", message);

    post("ligase~: splice %d message set to: %s", splice_num, message);
}

static void ligase_clear_splice_msg(ligase_t *x, t_floatarg splice_num_f) {
    int splice_num = (int)splice_num_f;

    if (splice_num < 0 || splice_num >= x->reel->splices.count) {
        pd_error(x, "ligase~: splice number %d out of range (0-%d)",
                 splice_num, x->reel->splices.count - 1);
        return;
    }

    x->reel->splices.markers[splice_num].message[0] = '\0';
    post("ligase~: splice %d message cleared", splice_num);
}

// Helper function to send splice message when navigating
static void ligase_send_current_splice_msg(ligase_t *x) {
    if (!x->splice_behavior.send_splice_msg) return;
    if (x->reel->splices.count == 0) return;

    int current = x->reel->splices.current_splice;
    if (current < 0 || current >= x->reel->splices.count) return;

    const char *msg = x->reel->splices.markers[current].message;
    if (msg[0] == '\0') return;  // No message set

    // Parse message string and send it out the state outlet
    // The message format is expected to be: <selector> <args...>
    // We'll send it as a list/anything message

    // Create a copy for tokenization
    char msg_copy[256];
    strncpy(msg_copy, msg, sizeof(msg_copy) - 1);
    msg_copy[sizeof(msg_copy) - 1] = '\0';

    // Parse the first token as the selector
    char *token = strtok(msg_copy, " ");
    if (!token) return;

    t_symbol *selector = gensym(token);

    // Count and parse remaining tokens as arguments
    t_atom args[32];  // Max 32 arguments
    int arg_count = 0;

    while ((token = strtok(NULL, " ")) != NULL && arg_count < 32) {
        // Try to parse as float, otherwise treat as symbol
        char *endptr;
        float val = strtof(token, &endptr);
        if (*endptr == '\0') {
            SETFLOAT(&args[arg_count], val);
        } else {
            SETSYMBOL(&args[arg_count], gensym(token));
        }
        arg_count++;
    }

    // Send the message out the state outlet
    if (arg_count > 0) {
        outlet_anything(x->x_state_out, selector, arg_count, args);
    } else {
        outlet_anything(x->x_state_out, selector, 0, NULL);
    }
}

// @endregion:ligase_pd.core.splice.messages

static void ligase_crossfade(ligase_t *x, t_floatarg mix) {
    if (!isfinite(mix)) {
        pd_error(x, "ligase~: invalid sos value (NaN or Inf), ignoring");
        return;
    }

    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    x->sos_value = mix;
    if (x->recorder) {
        x->recorder->crossfade_mix = mix;
    }
}

static void ligase_sos_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->sos_mode = m;
        if (m == 0) {
            post("ligase~: SOS mode = Record Only (legacy)");
        } else {
            post("ligase~: SOS mode = Morphagene (crossfade input/granular)");
        }
    } else {
        pd_error(x, "ligase~: invalid sos_mode %d (use 0 or 1)", m);
    }
}

static void ligase_headless(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->headless_mode = m;
        if (m == 0) {
            post("ligase~: Headless mode DISABLED - Full 0.0 range for signal inlets (perfect signal processing)");
        } else {
            post("ligase~: Headless mode ENABLED - Epsilon thresholds active (0.0 from unconnected inlets ignored)");
        }
    } else {
        pd_error(x, "ligase~: invalid headless mode %d (use 0 or 1)", m);
    }
}

static void ligase_playhead_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 1) {
        x->playhead_mode = PLAYHEAD_MODE_STATIC;
        post("ligase~: playhead mode 1 (static, GrainStart slides)");
    } else if (m == 2) {
        x->playhead_mode = PLAYHEAD_MODE_SCANNING;
        post("ligase~: playhead mode 2 (scanning with scan_rate)");
    } else if (m == 3) {
        x->playhead_mode = PLAYHEAD_MODE_CLOCK_ADVANCE;
        post("ligase~: playhead mode 3 (clock advance, playhead advances on clock bang)");
    } else {
        post("ligase~: invalid mode %d (use 1, 2, or 3)", m);
    }
}

static void ligase_scan_rate(ligase_t *x, t_floatarg rate) {
    x->scan_rate = rate;
    post("ligase~: scan_rate set to %.3f", rate);
}

static void ligase_speed(ligase_t *x, t_floatarg speed) {
    x->speed = speed;
    post("ligase~: speed set to %.3f", speed);
}

// @region:ligase_pd.core.params.playhead.clock_advance Clock Advance Mode

static void ligase_clock_advance_quant(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->clock_advance_use_quantized = m;
        post("ligase~: clock_advance_quant set to %d (%s)",
             m, m == 0 ? "use current grain length" : "use quantized grain length");
    } else {
        pd_error(x, "ligase~: invalid clock_advance_quant %d (use 0 or 1)", m);
    }
}

// @endregion:ligase_pd.core.params.playhead.clock_advance

static void ligase_envelope(ligase_t *x, t_floatarg type) {
    int env_type = (int)type;
    envelope_type_t new_type;
    const char *type_name;

    switch(env_type) {
        case 0:
            new_type = ENVELOPE_PARABOLIC;
            type_name = "parabolic";
            break;
        case 1:
            new_type = ENVELOPE_TRAPEZOIDAL;
            type_name = "trapezoidal";
            break;
        case 2:
            new_type = ENVELOPE_COSINE;
            type_name = "cosine";
            break;
        default:
            post("ligase~: invalid envelope type %d (use 0=parabolic, 1=trapezoidal, 2=cosine)", env_type);
            return;
    }

    // Rebuild the envelope IN PLACE for the new type. Do NOT free + recreate the
    // envelope struct: the scheduler AND the Bencina delay cache this pointer, and a
    // recreate left them pointing at freed memory — a use-after-free that crashed in
    // envelope_sample (garbage env->length → out-of-bounds table read) while using
    // Bencina delay mode after an envelope-type change. Regenerating the table in place
    // (same struct/allocation, like envelope_set_skew) keeps every holder valid and
    // preserves skew automatically.
    if (x->envelope) {
        envelope_set_type(x->envelope, new_type);
        post("ligase~: envelope set to %s (skew: %.2f)", type_name, x->envelope->skew);
    }
}

static void ligase_env_skew(ligase_t *x, t_floatarg skew) {
    if (!x->envelope) return;

    float s = skew;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;

    envelope_set_skew(x->envelope, s);
    post("ligase~: envelope skew set to %.2f", s);
}

static void ligase_maxgrains(ligase_t *x, t_floatarg grains) {
    int g = (int)grains;
    if (g < 1) g = 1;
    if (g > x->scheduler->pool_size) g = x->scheduler->pool_size;
    x->scheduler->max_grains = g;
    post("ligase~: max_grains set to %d (pool size: %d)", g, x->scheduler->pool_size);
}

static void ligase_iot(ligase_t *x, t_floatarg iot) {
    float i = iot;
    if (i < 0.0005f) i = 0.0005f;
    if (i > 2.0f) i = 2.0f;
    x->scheduler->iot = i;
    post("ligase~: interonset time set to %.4f seconds", i);
}

static void ligase_amplitude(ligase_t *x, t_floatarg amplitude) {
    float a = amplitude;
    if (a < 0.0f) a = 0.0f;
    if (a > 2.0f) a = 2.0f;
    x->amplitude = a;
    post("ligase~: amplitude set to %.3f", a);
}

static void ligase_pan(ligase_t *x, t_floatarg pan) {
    float p = pan;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    x->pan = p;
    post("ligase~: pan set to %.3f (0=left, 0.5=center, 1=right)", p);
}

static void ligase_saw_cycles(ligase_t *x, t_floatarg cycles) {
    float c = cycles;
    if (c < 0.0f) c = 0.0f;
    if (c > 64.0f) c = 64.0f;
    x->saw_cycles = c;
    post("ligase~: saw_cycles set to %.2f (0=off)", c);
}

static void ligase_saw_depth(ligase_t *x, t_floatarg depth) {
    float d = depth;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    x->saw_depth = d;
    post("ligase~: saw_depth set to %.3f (0=off, 1=max jaggedness)", d);
}

static void ligase_pan_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m < 0) m = 0;
    if (m > 1) m = 1;
    x->scheduler->pan_mode = m;
    post("ligase~: pan mode set to %d (%s)",
         m,
         m == 0 ? "constant-power mono panning" : "stereo balance");
}

static void ligase_grainsize(ligase_t *x, t_floatarg grainsize) {
    float gs = grainsize;
    if (gs < 0.001f) gs = 0.001f;  // Minimum 1ms
    if (gs > 2.0f) gs = 2.0f;      // Maximum 2 seconds
    x->grain_size = gs;
    post("ligase~: grain size set to %.4f seconds", gs);
}

static void ligase_grainstart(ligase_t *x, t_floatarg grainstart) {
    float gst = grainstart;
    if (gst < 0.0f) gst = 0.0f;
    if (gst > 1.0f) gst = 1.0f;
    x->grain_start = gst;
    post("ligase~: grain start set to %.3f", gst);
}

static void ligase_grain_bang_rate(ligase_t *x, t_floatarg rate) {
    int r = (int)rate;
    if (r < 0) r = 0;  // 0 = off
    if (r > 1000) r = 1000;  // Max reasonable value
    x->grain_bang_rate = r;
    x->grain_bang_counter = 0;  // Reset counter when rate changes
    if (r == 0) {
        post("ligase~: grain bang output disabled");
    } else {
        post("ligase~: grain bang output every %d grain(s)", r);
    }
}

// Set outlet 3 mode: 0=splice end/wrap (default), 1=bang on note change
static void ligase_outlet3_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m < 0 || m > 1) {
        pd_error(x, "ligase~: outlet3_mode must be 0 or 1 (0=splice end/wrap, 1=note change)");
        return;
    }
    x->outlet3_mode = m;
    if (m == 0) {
        post("ligase~: outlet 3 set to splice end/wrap mode (default)");
    } else {
        post("ligase~: outlet 3 set to note change mode (MIDI/scale/range)");
    }
}

// @region:ligase_pd.core.timing.clock Clock Management

// Recompute the pattern cycle-clock length from the current BPM and the cycle_segments list
// (or the default 1-bar 4/4 cycle when no pattern_cycle segments are set). Mirrors the four
// quant-grid recomputes; guarded so bpm<=0 leaves the clock idle (cycle_total_sec = 0).
static void ligase_recompute_cycle(ligase_t *x) {
    if (x->bpm <= 0.0) { x->cycle_total_sec = 0.0; return; }
    double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;  // identical to the four grid recomputes
    double total_ms;
    if (x->cycle_seg_count > 0) {
        total_ms = 0.0;
        for (int i = 0; i < x->cycle_seg_count; i++) {
            int den = x->cycle_segments[i].den;
            int num = x->cycle_segments[i].num;
            if (den > 0) total_ms += (ms_per_whole_note / (double)den) * (double)num;
        }
    } else {
        total_ms = ms_per_whole_note;  // default: one bar of 4/4 = a whole note
    }
    x->cycle_total_sec = total_ms / 1000.0;
}

static void ligase_bang(ligase_t *x) {
    double current_time = clock_getlogicaltime();

    if (x->last_bang_time > 0.0) {
        // Calculate BPM from interval between bangs
        // clock_getlogicaltime() returns time in Pure Data's internal time units
        // TIMEUNITPERMSEC = 32 * 441 = 14112.0 (constant, independent of sample rate)
        // This is the LCM of common sample rates (32k, 44.1k, 48k, 88.2k, 96k)
        // See: pure-data/src/m_sched.c
        double interval_units = current_time - x->last_bang_time;
        double interval_ms = interval_units / 14112.0;

        if (interval_ms > 0.0) {
            // BPM = 60000 / interval_in_ms
            x->bpm = 60000.0 / interval_ms;
            x->clock_running = 1;

            // Recalculate IOT quantization grid based on new BPM
            if (x->quant_note > 0) {
                // Calculate milliseconds per whole note at current BPM
                double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
                // Calculate grid size for the quantization note value
                x->quant_grid_ms = ms_per_whole_note / (float)x->quant_note;
            }

            // Recalculate grain size quantization grid based on new BPM
            if (x->gs_quant_note > 0) {
                double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
                x->gs_quant_grid_ms = ms_per_whole_note / (float)x->gs_quant_note;
            }

            // Recalculate grain delay quantization grid based on new BPM
            if (x->delay_quant_note > 0) {
                double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
                x->delay_quant_grid_ms = ms_per_whole_note / (float)x->delay_quant_note;
            }

            // Recalculate stut slice-length quantization grid based on new BPM
            if (x->stut_len_quant_note > 0) {
                double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
                x->stut_len_quant_grid_ms = ms_per_whole_note / (float)x->stut_len_quant_note;
            }

            // Recalculate the pattern cycle-clock length (segment list or default 1-bar cycle)
            ligase_recompute_cycle(x);
        }
    }

    x->last_bang_time = current_time;

    // Clock advance mode: signal playhead to advance on next DSP cycle.
    // (One-shot: gate on is_triggering so a stopped transport is not re-advanced / re-stopped by
    //  subsequent BPM bangs — BPM detection + the quant-grid recompute above still run.)
    if (x->playhead_mode == PLAYHEAD_MODE_CLOCK_ADVANCE && x->is_triggering) {
        x->clock_bang_received = 1;
    }
}

static void ligase_clock_stop(ligase_t *x) {
    x->clock_running = 0;
    // Keep last calculated BPM and quant_grid_ms for continued use
    post("ligase~: clock stopped (using last BPM: %.2f)", x->bpm);
}

// @endregion:ligase_pd.core.timing.clock

// @region:ligase_pd.core.timing.time_signature Time Signature

static void ligase_timesig(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc != 1 || argv[0].a_type != A_SYMBOL) {
        pd_error(x, "ligase~: timesig requires a symbol argument (e.g., '7/5')");
        return;
    }

    const char *sig_str = argv[0].a_w.w_symbol->s_name;
    int num, denom;

    if (sscanf(sig_str, "%d/%d", &num, &denom) == 2) {
        if (num > 0 && denom > 0) {
            x->time_sig_numerator = num;
            x->time_sig_denominator = denom;
            post("ligase~: time signature set to %d/%d", num, denom);
        } else {
            pd_error(x, "ligase~: invalid time signature values");
        }
    } else {
        pd_error(x, "ligase~: time signature must be in format 'numerator/denominator' (e.g., '7/5')");
    }
}

// @endregion:ligase_pd.core.timing.time_signature

// @region:ligase_pd.core.pattern Pattern (mini-notation) cycle + slot control

// Forward decls — definitions live later in the file, after this region.
static param_range_t* get_param_range_by_name(ligase_t *x, const char *name);
static const char* get_rand_type_name(rand_type_t type);

// Choose a pattern slot for a named param target: reuse the param's current pattern slot if it
// already has one, else the first slot with no loaded pattern (step_count==0). Slot PATTERN_SLOTS-1
// is reserved for pitch. Returns -1 if every param slot is occupied.
static int pattern_alloc_param_slot(ligase_t *x, param_range_t *range) {
    perlin_state_t *ps = &x->scheduler->perlin_state;
    // Slots PATTERN_SLOTS-1 (grain pitch) and PATTERN_SLOTS-2 (smear pitch) are reserved; the param
    // auto-allocator hands out only 0 .. PATTERN_SLOTS-3.
    if (range->rand_type == RAND_TYPE_PATTERN &&
        range->rand_instance >= 0 && range->rand_instance < PATTERN_SLOTS - 2) {
        return range->rand_instance;
    }
    for (int i = 0; i < PATTERN_SLOTS - 2; i++) {
        if (ps->pattern[i].step_count == 0) return i;
    }
    return -1;
}

// pattern_cycle <N/D> <N/D> ... : set the quantization-cycle segment list. Each segment is a
// musical duration ("num" notes of value 1/den) at the detected BPM; the cycle length is their
// sum. A bare "pattern_cycle" (no args) resets to the default 1-bar 4/4 cycle. Validate-then-commit
// over the whole list (prior cycle preserved on any error). Mirrors the ligase_timesig %d/%d parse.
static void ligase_pattern_cycle(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (argc < 1) {
        x->cycle_seg_count = 0;
        ligase_recompute_cycle(x);
        post("ligase~: pattern_cycle reset to default 1-bar cycle");
        return;
    }
    if (argc > PATTERN_MAX_SEGS) {
        pd_error(x, "ligase~: pattern_cycle accepts at most %d segments", PATTERN_MAX_SEGS);
        return;
    }
    int nums[PATTERN_MAX_SEGS], dens[PATTERN_MAX_SEGS];
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type != A_SYMBOL) {
            pd_error(x, "ligase~: pattern_cycle segment %d must be a fraction like 4/4", i + 1);
            return;
        }
        const char *seg = argv[i].a_w.w_symbol->s_name;
        int num, den;
        if (sscanf(seg, "%d/%d", &num, &den) != 2) {
            pd_error(x, "ligase~: pattern_cycle segment '%s' must be 'num/den' (e.g. 4/4)", seg);
            return;
        }
        if (num <= 0) {
            pd_error(x, "ligase~: pattern_cycle segment '%s' numerator must be > 0", seg);
            return;
        }
        if (den != 1 && den != 2 && den != 4 && den != 8 && den != 16 && den != 32 && den != 64 && den != 128) {
            pd_error(x, "ligase~: pattern_cycle denominator in '%s' must be 1,2,4,8,16,32,64,128", seg);
            return;
        }
        nums[i] = num; dens[i] = den;
    }
    x->cycle_seg_count = argc;
    for (int i = 0; i < argc; i++) {
        x->cycle_segments[i].num = nums[i];
        x->cycle_segments[i].den = dens[i];
    }
    ligase_recompute_cycle(x);
    if (x->bpm > 0.0)
        post("ligase~: pattern_cycle set (%d segments, %.3f s at %.1f BPM)", argc, x->cycle_total_sec, x->bpm);
    else
        post("ligase~: pattern_cycle set (%d segments; cycle length pending first BPM)", argc);
}

// pattern_clear <slot> : clear a pattern slot (step_count := 0, phase reset). In P1 the target is a
// numeric slot 0..PATTERN_SLOTS-1; P2 generalizes the argument to a param name.
static void ligase_pattern_clear(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (!x->scheduler) return;
    if (argc != 1) {
        pd_error(x, "ligase~: pattern_clear requires a param name, 'pitch', or a slot 0..%d", PATTERN_SLOTS - 1);
        return;
    }
    perlin_state_t *ps = &x->scheduler->perlin_state;

    // Numeric slot: raw clear of that slot (P1 / two-step path).
    if (argv[0].a_type == A_FLOAT) {
        int slot = (int)argv[0].a_w.w_float;
        if (slot < 0 || slot >= PATTERN_SLOTS) {
            pd_error(x, "ligase~: pattern_clear slot must be 0..%d", PATTERN_SLOTS - 1);
            return;
        }
        ps->pattern[slot].step_count = 0;
        ps->pattern_phase[slot] = 0.0f;
        ps->pattern_cycle_index[slot] = 0;
        post("ligase~: pattern slot %d cleared", slot);
        return;
    }
    if (argv[0].a_type != A_SYMBOL) {
        pd_error(x, "ligase~: pattern_clear: bad argument");
        return;
    }

    const char *name = argv[0].a_w.w_symbol->s_name;
    if (strcmp(name, "pitch") == 0) {
        int slot = PATTERN_SLOTS - 1;
        ps->pattern[slot].step_count = 0;
        ps->pattern_phase[slot] = 0.0f;
        ps->pattern_cycle_index[slot] = 0;
        x->scheduler->pitch_control.pitch_pattern_slot = -1;
        if (x->scheduler->pitch_control.mode == PITCH_MODE_PATTERN)
            x->scheduler->pitch_control.mode = PITCH_MODE_OFF;   // GATE A(b): clear -> OFF
        post("ligase~: pitch pattern cleared (slot %d, pitch_mode -> off)", slot);
        return;
    }
    if (strcmp(name, "smear_pitch") == 0) {
        int slot = PATTERN_SLOTS - 2;
        ps->pattern[slot].step_count = 0;
        ps->pattern_phase[slot] = 0.0f;
        ps->pattern_cycle_index[slot] = 0;
        x->scheduler->smear_pitch_control.pattern_slot = -1;
        if (x->scheduler->smear_pitch_control.source == SMEAR_PITCH_PATTERN) {
            x->scheduler->smear_pitch_control.source  = SMEAR_PITCH_OFF;
            x->scheduler->smear_pitch_control.enabled = 0;
        }
        post("ligase~: smear pitch pattern cleared (slot %d, smear_pitch off)", slot);
        return;
    }

    // Named param target: restore its prior source, then free the slot.
    param_range_t *range = get_param_range_by_name(x, name);
    if (!range) {
        pd_error(x, "ligase~: pattern_clear: unknown parameter '%s'", name);
        return;
    }
    if (range->rand_type != RAND_TYPE_PATTERN) {
        post("ligase~: pattern_clear: %s has no pattern attached", name);
        return;
    }
    int slot = range->rand_instance;
    range->rand_type = range->saved_rand_type;            // restore FIRST (audio stops reading the slot)
    range->rand_instance = range->saved_rand_instance;
    if (slot >= 0 && slot < PATTERN_SLOTS) {
        ps->pattern[slot].step_count = 0;                 // free the slot
        ps->pattern_phase[slot] = 0.0f;
        ps->pattern_cycle_index[slot] = 0;
    }
    post("ligase~: pattern cleared from %s (restored type %s)",
         name, get_rand_type_name(range->saved_rand_type));
}

static void ligase_pattern_debug(ligase_t *x, t_floatarg f) {
    x->pattern_debug = (f != 0.0f) ? 1 : 0;
    post("ligase~: pattern_debug %s", x->pattern_debug ? "on" : "off");
}

// Stage-2 flatten: recursively descend the parse tree, assigning each leaf an absolute span
// (fractions multiply down per the Tidal nesting rule) and an alternation tag. Emits pattern_step_t
// leaves into the scratch table. Single-level alternation only in P1 (ALT-inside-ALT is rejected).
typedef struct {
    pattern_node_t  *pool;
    pattern_table_t *out;
    int   next_group;
    int   ok;
    ligase_t *x;
} pattern_flatten_ctx_t;

static void pattern_flatten(pattern_flatten_ctx_t *ctx, int node_idx, float span,
                            int alt_group, int alt_member) {
    if (!ctx->ok) return;
    pattern_node_t *n = &ctx->pool[node_idx];

    if (n->kind == PN_LEAF) {
        if (ctx->out->step_count >= PATTERN_MAX_STEPS) {
            pd_error(ctx->x, "ligase~: pattern exceeds %d steps", PATTERN_MAX_STEPS);
            ctx->ok = 0; return;
        }
        pattern_step_t *st = &ctx->out->steps[ctx->out->step_count++];
        st->value = n->value;
        st->weight = span;
        st->is_rest = n->is_rest;
        st->alt_group = alt_group;
        st->alt_member = alt_member;
        return;
    }

    if (n->kind == PN_SEQ) {
        float total_w = 0.0f;
        for (int c = n->first_child; c >= 0; c = ctx->pool[c].next_sibling)
            total_w += (float)ctx->pool[c].weight;
        if (total_w <= 0.0f) total_w = 1.0f;
        for (int c = n->first_child; c >= 0; c = ctx->pool[c].next_sibling) {
            float child_span = span * ((float)ctx->pool[c].weight / total_w);
            pattern_flatten(ctx, c, child_span, alt_group, alt_member);
            if (!ctx->ok) return;
        }
        return;
    }

    // PN_ALT: a new alternation group; each child (member) gets the FULL span, one present per cycle.
    if (alt_group >= 0) {
        pd_error(ctx->x, "ligase~: nested alternation <...<...>...> not supported in P1");
        ctx->ok = 0; return;
    }
    if (ctx->next_group >= PATTERN_MAX_STEPS) {
        pd_error(ctx->x, "ligase~: pattern has too many alternation groups");
        ctx->ok = 0; return;
    }
    int G = ctx->next_group++;
    int members = 0;
    for (int c = n->first_child; c >= 0; c = ctx->pool[c].next_sibling) members++;
    ctx->out->alt_group_count[G] = members;
    if (G + 1 > ctx->out->alt_group_total) ctx->out->alt_group_total = G + 1;
    int m = 0;
    for (int c = n->first_child; c >= 0; c = ctx->pool[c].next_sibling) {
        pattern_flatten(ctx, c, span, G, m);
        if (!ctx->ok) return;
        m++;
    }
}

// pattern <slot|pitch> <token>... : load a mini-notation pattern into a slot. P1 target is a
// numeric slot 0..PATTERN_SLOTS-1 or the literal 'pitch' (the dedicated last slot; P3 wires the
// pitch mode). Two-stage parse (tree -> flat table) with validate-then-commit (prior slot preserved
// on any error; step_count published LAST). All parse work is on the message thread.
static void ligase_pattern(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (!x->scheduler) return;
    if (argc < 2) { pd_error(x, "ligase~: pattern requires <slot|pitch> then tokens"); return; }

    int slot;
    param_range_t *attach_range = NULL;      // non-NULL => attach this range to the slot after commit
    int attach_pitch = 0;                    // 1 => set PITCH_MODE_PATTERN on this slot after commit
    int attach_smear_pitch = 0;              // 1 => set smear SMEAR_PITCH_PATTERN on this slot after commit
    if (argv[0].a_type == A_SYMBOL && strcmp(argv[0].a_w.w_symbol->s_name, "pitch") == 0) {
        slot = PATTERN_SLOTS - 1;            // grain pitch: dedicated last slot
        attach_pitch = 1;
    } else if (argv[0].a_type == A_SYMBOL && strcmp(argv[0].a_w.w_symbol->s_name, "smear_pitch") == 0) {
        slot = PATTERN_SLOTS - 2;            // smear pitch: dedicated slot 6
        attach_smear_pitch = 1;
    } else if (argv[0].a_type == A_SYMBOL) {
        const char *name = argv[0].a_w.w_symbol->s_name;
        attach_range = get_param_range_by_name(x, name);
        if (!attach_range) {
            pd_error(x, "ligase~: pattern: unknown parameter '%s'", name);
            return;
        }
        slot = pattern_alloc_param_slot(x, attach_range);
        if (slot < 0) {
            pd_error(x, "ligase~: pattern: no free pattern slots (max %d param patterns)", PATTERN_SLOTS - 1);
            return;
        }
    } else if (argv[0].a_type == A_FLOAT) {
        slot = (int)argv[0].a_w.w_float;     // numeric slot = raw load, no attach (testing / two-step)
    } else {
        pd_error(x, "ligase~: pattern target must be a param name, 'pitch', or a slot 0..%d", PATTERN_SLOTS - 1);
        return;
    }
    if (slot < 0 || slot >= PATTERN_SLOTS) {
        pd_error(x, "ligase~: pattern slot must be 0..%d", PATTERN_SLOTS - 1);
        return;
    }

#define PAT_FAIL(...) do { pd_error(x, __VA_ARGS__); return; } while (0)

    // ---- Stage 1: parse tokens argv[1..] into a node tree (function-local scratch pool) ----
    pattern_node_t pool[PATTERN_MAX_NODES];
    int node_count = 0;
    int stack[PATTERN_MAX_DEPTH];
    int last_child[PATTERN_MAX_DEPTH];

    pool[0].kind = PN_SEQ; pool[0].value = 0.0f; pool[0].is_rest = 0;
    pool[0].weight = 1; pool[0].first_child = -1; pool[0].next_sibling = -1;
    stack[0] = 0; last_child[0] = -1;
    int depth = 1; node_count = 1;

    for (int i = 1; i < argc; i++) {
        int open_seq = 0, open_alt = 0, close_seq = 0, close_alt = 0, is_leaf = 0;
        int leaf_rest = 0, leaf_weight = 1, leaf_mult = 1, leaf_repl = 1;
        float leaf_val = 0.0f;

        if (argv[i].a_type == A_FLOAT) {
            is_leaf = 1; leaf_val = argv[i].a_w.w_float;
        } else if (argv[i].a_type == A_SYMBOL) {
            const char *t = argv[i].a_w.w_symbol->s_name;
            if      (strcmp(t, "[") == 0) open_seq = 1;
            else if (strcmp(t, "]") == 0) close_seq = 1;
            else if (strcmp(t, "<") == 0) open_alt = 1;
            else if (strcmp(t, ">") == 0) close_alt = 1;
            else {
                is_leaf = 1;
                const char *p = t;
                if (t[0] == '~') { leaf_rest = 1; p = t + 1; }
                else {
                    char *endp;
                    leaf_val = strtof(t, &endp);
                    if (endp == t) PAT_FAIL("ligase~: pattern: bad token '%s'", t);
                    p = endp;
                }
                if      (*p == '@') { leaf_weight = atoi(p + 1); if (leaf_weight < 1) leaf_weight = 1; }
                else if (*p == '*') { leaf_mult   = atoi(p + 1); if (leaf_mult   < 1) leaf_mult   = 1; }
                else if (*p == '!') { leaf_repl   = atoi(p + 1); if (leaf_repl   < 1) leaf_repl   = 1; }
                else if (*p != '\0') PAT_FAIL("ligase~: pattern: bad suffix in '%s'", t);
            }
        } else {
            PAT_FAIL("ligase~: pattern: unsupported atom at position %d", i);
        }

        if (open_seq || open_alt) {
            if (depth >= PATTERN_MAX_DEPTH) PAT_FAIL("ligase~: pattern nesting too deep (max %d)", PATTERN_MAX_DEPTH);
            if (node_count >= PATTERN_MAX_NODES) PAT_FAIL("ligase~: pattern too large (max %d nodes)", PATTERN_MAX_NODES);
            int idx = node_count++;
            pool[idx].kind = open_seq ? PN_SEQ : PN_ALT;
            pool[idx].value = 0.0f; pool[idx].is_rest = 0; pool[idx].weight = 1;
            pool[idx].first_child = -1; pool[idx].next_sibling = -1;
            int parent = stack[depth - 1];
            if (last_child[depth - 1] < 0) pool[parent].first_child = idx;
            else pool[last_child[depth - 1]].next_sibling = idx;
            last_child[depth - 1] = idx;
            stack[depth] = idx; last_child[depth] = -1; depth++;
        } else if (close_seq || close_alt) {
            if (depth <= 1) PAT_FAIL("ligase~: pattern: unmatched '%s'", close_seq ? "]" : ">");
            int open = stack[depth - 1];
            if (close_seq && pool[open].kind != PN_SEQ) PAT_FAIL("ligase~: pattern: ']' closing a '<' group");
            if (close_alt && pool[open].kind != PN_ALT) PAT_FAIL("ligase~: pattern: '>' closing a '[' group");
            depth--;
        } else if (is_leaf) {
            for (int r = 0; r < leaf_repl; r++) {
                int target_idx;
                if (leaf_mult > 1) {
                    if (node_count >= PATTERN_MAX_NODES) PAT_FAIL("ligase~: pattern too large (max %d nodes)", PATTERN_MAX_NODES);
                    int seqidx = node_count++;
                    pool[seqidx].kind = PN_SEQ; pool[seqidx].value = 0.0f; pool[seqidx].is_rest = 0;
                    pool[seqidx].weight = leaf_weight; pool[seqidx].first_child = -1; pool[seqidx].next_sibling = -1;
                    int lastc = -1;
                    for (int k = 0; k < leaf_mult; k++) {
                        if (node_count >= PATTERN_MAX_NODES) PAT_FAIL("ligase~: pattern too large (max %d nodes)", PATTERN_MAX_NODES);
                        int li = node_count++;
                        pool[li].kind = PN_LEAF; pool[li].value = leaf_val; pool[li].is_rest = leaf_rest;
                        pool[li].weight = 1; pool[li].first_child = -1; pool[li].next_sibling = -1;
                        if (lastc < 0) pool[seqidx].first_child = li; else pool[lastc].next_sibling = li;
                        lastc = li;
                    }
                    target_idx = seqidx;
                } else {
                    if (node_count >= PATTERN_MAX_NODES) PAT_FAIL("ligase~: pattern too large (max %d nodes)", PATTERN_MAX_NODES);
                    int li = node_count++;
                    pool[li].kind = PN_LEAF; pool[li].value = leaf_val; pool[li].is_rest = leaf_rest;
                    pool[li].weight = leaf_weight; pool[li].first_child = -1; pool[li].next_sibling = -1;
                    target_idx = li;
                }
                int parent = stack[depth - 1];
                if (last_child[depth - 1] < 0) pool[parent].first_child = target_idx;
                else pool[last_child[depth - 1]].next_sibling = target_idx;
                last_child[depth - 1] = target_idx;
            }
        }
    }
    if (depth != 1) PAT_FAIL("ligase~: pattern: unbalanced brackets");

    // ---- Stage 2: flatten the tree into a scratch table ----
    pattern_table_t scratch;
    memset(&scratch, 0, sizeof(scratch));
    pattern_flatten_ctx_t ctx;
    ctx.pool = pool; ctx.out = &scratch; ctx.next_group = 0; ctx.ok = 1; ctx.x = x;
    pattern_flatten(&ctx, stack[0], 1.0f, -1, 0);
    if (!ctx.ok) return;                                  // error posted; prior slot preserved
    if (scratch.step_count < 1) PAT_FAIL("ligase~: pattern produced no steps");

    // Seed cache fields; force a fresh present-step/prefix recompute on the first eval.
    scratch.total_weight   = 0.0f;
    scratch.last_alt_cycle = -1;
    scratch.last_step_index = -1;
    scratch.changed = 0;
    scratch.cached_is_rest = 0;
    scratch.cached_value = 0.0f;
    for (int i = 0; i < scratch.step_count; i++) {
        if (!scratch.steps[i].is_rest) { scratch.cached_value = scratch.steps[i].value; break; }
    }

    // ---- Commit: copy scratch -> live slot, publish step_count LAST ----
    perlin_state_t *ps = &x->scheduler->perlin_state;
    pattern_table_t *live = &ps->pattern[slot];
    int committed_steps = scratch.step_count;
    scratch.step_count = 0;                               // memcpy carries 0; real count set after
    memcpy(live, &scratch, sizeof(pattern_table_t));
    ps->pattern_phase[slot] = 0.0f;
    ps->pattern_cycle_index[slot] = 0;
    live->step_count = committed_steps;                  // publish barrier
    post("ligase~: pattern slot %d set (%d steps, %d alt groups)",
         slot, committed_steps, scratch.alt_group_total);

    // ---- Attach (named param target only): point the range at this slot via RAND_TYPE_PATTERN ----
    if (attach_range) {
        if (attach_range->rand_type != RAND_TYPE_PATTERN) {
            // remember the prior source so pattern_clear can restore it (don't clobber on re-load)
            attach_range->saved_rand_type = attach_range->rand_type;
            attach_range->saved_rand_instance = attach_range->rand_instance;
        }
        attach_range->rand_type = RAND_TYPE_PATTERN;
        attach_range->rand_instance = slot;
        attach_range->enabled = 1;                       // a pattern only modulates when enabled
        if (attach_range->min == attach_range->max) {
            // a collapsed range short-circuits in sample_param_range BEFORE the pattern read
            attach_range->min = 0.0f;
            attach_range->max = 1.0f;
            post("ligase~: pattern: %s had min==max; reset map span to [0,1]",
                 argv[0].a_w.w_symbol->s_name);
        }
        post("ligase~: pattern attached to %s (slot %d)", argv[0].a_w.w_symbol->s_name, slot);
    }

    // ---- Pitch attach: set the slot + switch into PITCH_MODE_PATTERN ----
    if (attach_pitch) {
        x->scheduler->pitch_control.pitch_pattern_slot = slot;
        x->scheduler->pitch_control.mode = PITCH_MODE_PATTERN;
        if (x->scheduler->pitch_control.scale.count == 0)
            post("ligase~: pattern pitch set, but no pitch_scale loaded yet (unison until you send one)");
        post("ligase~: pitch pattern set (slot %d), pitch_mode -> pattern", slot);
    }

    // ---- Smear pitch attach: set the slot + switch the smear destination into SMEAR_PITCH_PATTERN ----
    if (attach_smear_pitch) {
        x->scheduler->smear_pitch_control.pattern_slot = slot;
        x->scheduler->smear_pitch_control.source       = SMEAR_PITCH_PATTERN;
        x->scheduler->smear_pitch_control.enabled      = 1;
        if (x->scheduler->smear_pitch_control.scale.count == 0)
            post("ligase~: pattern smear_pitch set, but no smear_pitch_scale loaded yet (unison until you send one)");
        post("ligase~: smear pitch pattern set (slot %d), smear_pitch_source -> pattern", slot);
    }

#undef PAT_FAIL
}

// @endregion:ligase_pd.core.pattern

// @region:ligase_pd.core.timing.quantization Grain Quantization

static void ligase_quantize(ligase_t *x, t_floatarg note) {
    int n = (int)note;

    // Valid note values: 1, 2, 4, 8, 16, 32, 64, 128
    if (n != 1 && n != 2 && n != 4 && n != 8 && n != 16 && n != 32 && n != 64 && n != 128) {
        pd_error(x, "ligase~: quantize note must be 1, 2, 4, 8, 16, 32, 64, or 128");
        return;
    }

    x->quant_note = n;

    // Recalculate grid if BPM is available
    if (x->bpm > 0.0) {
        double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
        x->quant_grid_ms = ms_per_whole_note / (float)x->quant_note;
        post("ligase~: quantize set to 1/%d note (%.2f ms grid)", n, x->quant_grid_ms);
    } else {
        post("ligase~: quantize set to 1/%d note (grid will be calculated when clock starts)", n);
    }
}

static void ligase_quant_amount(ligase_t *x, t_floatarg amount) {
    float a = amount;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    x->quant_amount = a;
    post("ligase~: quantization amount set to %.0f%%", a * 100.0f);
}

// Grain size quantization methods

static void ligase_gs_timesig(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc != 1 || argv[0].a_type != A_SYMBOL) {
        pd_error(x, "ligase~: gs_timesig requires a symbol argument (e.g., '7/5')");
        return;
    }

    const char *sig_str = argv[0].a_w.w_symbol->s_name;
    int num, denom;

    if (sscanf(sig_str, "%d/%d", &num, &denom) == 2) {
        if (num > 0 && denom > 0) {
            x->gs_time_sig_numerator = num;
            x->gs_time_sig_denominator = denom;
            post("ligase~: grain size time signature set to %d/%d", num, denom);
        } else {
            pd_error(x, "ligase~: invalid grain size time signature values");
        }
    } else {
        pd_error(x, "ligase~: grain size time signature must be in format 'numerator/denominator' (e.g., '7/5')");
    }
}

static void ligase_gs_quantize(ligase_t *x, t_floatarg note) {
    int n = (int)note;

    // Valid note values: 1, 2, 4, 8, 16, 32, 64, 128
    if (n != 1 && n != 2 && n != 4 && n != 8 && n != 16 && n != 32 && n != 64 && n != 128) {
        pd_error(x, "ligase~: gs_quantize note must be 1, 2, 4, 8, 16, 32, 64, or 128");
        return;
    }

    x->gs_quant_note = n;

    // Recalculate grid if BPM is available
    if (x->bpm > 0.0) {
        double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
        x->gs_quant_grid_ms = ms_per_whole_note / (float)x->gs_quant_note;
        post("ligase~: grain size quantize set to 1/%d note (%.2f ms grid)", n, x->gs_quant_grid_ms);
    } else {
        post("ligase~: grain size quantize set to 1/%d note (grid will be calculated when clock starts)", n);
    }
}

static void ligase_gs_quant_amount(ligase_t *x, t_floatarg amount) {
    float a = amount;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    x->gs_quant_amount = a;
    post("ligase~: grain size quantization amount set to %.0f%%", a * 100.0f);
}

// Delay time quantization methods

static void ligase_delay_timesig(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc != 1 || argv[0].a_type != A_SYMBOL) {
        pd_error(x, "ligase~: delay_timesig requires a symbol argument (e.g., '7/5')");
        return;
    }

    const char *sig_str = argv[0].a_w.w_symbol->s_name;
    int num, denom;

    if (sscanf(sig_str, "%d/%d", &num, &denom) == 2) {
        if (num > 0 && denom > 0) {
            x->delay_time_sig_numerator = num;
            x->delay_time_sig_denominator = denom;
            post("ligase~: delay time signature set to %d/%d", num, denom);
        } else {
            pd_error(x, "ligase~: invalid delay time signature values");
        }
    } else {
        pd_error(x, "ligase~: delay time signature must be in format 'numerator/denominator' (e.g., '7/5')");
    }
}

static void ligase_delay_quantize(ligase_t *x, t_floatarg note) {
    int n = (int)note;

    // Valid note values: 1, 2, 4, 8, 16, 32, 64, 128
    if (n != 1 && n != 2 && n != 4 && n != 8 && n != 16 && n != 32 && n != 64 && n != 128) {
        pd_error(x, "ligase~: delay_quantize note must be 1, 2, 4, 8, 16, 32, 64, or 128");
        return;
    }

    x->delay_quant_note = n;

    // Recalculate grid if BPM is available
    if (x->bpm > 0.0) {
        double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
        x->delay_quant_grid_ms = ms_per_whole_note / (float)x->delay_quant_note;
        post("ligase~: delay quantize set to 1/%d note (%.2f ms grid)", n, x->delay_quant_grid_ms);
    } else {
        post("ligase~: delay quantize set to 1/%d note (grid will be calculated when clock starts)", n);
    }
}

static void ligase_delay_quant_amount(ligase_t *x, t_floatarg amount) {
    float a = amount;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    x->delay_quant_amount = a;
    post("ligase~: delay quantization amount set to %.0f%%", a * 100.0f);
}

// Stut slice length (how much audio each repeat replays) — independent of stut_spacing.
static void ligase_stut_length(ligase_t *x, t_floatarg ms) {
    float v = ms;
    if (v < 1.0f) v = 1.0f;
    if (v > 5000.0f) v = 5000.0f;
    x->stut_length_ms = v;
    post("ligase~: stut length set to %.2f ms", v);
}

// Stut length mode: 0 = independent (stut_length_ms / quantized), 1 = tie to grainsize.
static void ligase_stut_length_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m != 0 && m != 1) {
        pd_error(x, "ligase~: stut_length_mode must be 0 (independent) or 1 (grainsize)");
        return;
    }
    x->stut_length_mode = m;
    post("ligase~: stut length mode = %d (%s)", m, m ? "grainsize" : "independent");
}

// Stut length note subdivision (1..128) — same pattern as delay_quantize.
static void ligase_stut_length_quantize(ligase_t *x, t_floatarg note) {
    int n = (int)note;
    if (n != 1 && n != 2 && n != 4 && n != 8 && n != 16 && n != 32 && n != 64 && n != 128) {
        pd_error(x, "ligase~: stut_length_quantize note must be 1, 2, 4, 8, 16, 32, 64, or 128");
        return;
    }
    x->stut_len_quant_note = n;
    if (x->bpm > 0.0) {
        double ms_per_whole_note = (60000.0 / x->bpm) * 4.0;
        x->stut_len_quant_grid_ms = ms_per_whole_note / (float)n;
        post("ligase~: stut length quantize set to 1/%d note (%.2f ms grid)", n, x->stut_len_quant_grid_ms);
    } else {
        post("ligase~: stut length quantize set to 1/%d note (grid will be calculated when clock starts)", n);
    }
}

// Stut length quantization amount (0..1) — blends stut_length_ms toward the note grid.
static void ligase_stut_length_quant(ligase_t *x, t_floatarg amount) {
    float a = amount;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    x->stut_len_quant_amount = a;
    post("ligase~: stut length quantization amount set to %.0f%%", a * 100.0f);
}

// @endregion:ligase_pd.core.timing.quantization

// @region:ligase_pd.core.grain.delay Grain Output Delay Methods

static void ligase_gdelay_time(ligase_t *x, t_floatarg time) {
    grain_delay_set_time(x->grain_delay, time);
    post("ligase~: grain delay time set to %.3f seconds", time);
}

// DD-4 delay-time glide: smoothing time (ms) for delay-time changes, to avoid the pitch-zip when
// the read tap moves. Smooths both message- and signal-driven (inlet 11) delay-time changes.
static void ligase_delay_glide(ligase_t *x, t_floatarg ms) {
    grain_delay_set_glide(x->grain_delay, ms);
    post("ligase~: delay glide set to %.1f ms", ms);
}

static void ligase_gdelay_feedback(ligase_t *x, t_floatarg feedback) {
    grain_delay_set_feedback(x->grain_delay, feedback);
    post("ligase~: grain delay feedback set to %.2f", feedback);
}

static void ligase_gdelay_tone(ligase_t *x, t_floatarg tone) {
    grain_delay_set_tone(x->grain_delay, tone);
    post("ligase~: grain delay tone set to %.2f", tone);
}

static void ligase_gdelay_mix(ligase_t *x, t_floatarg mix) {
    grain_delay_set_mix(x->grain_delay, mix);
    post("ligase~: grain delay mix set to %.2f", mix);
}

static void ligase_gdelay_clear(ligase_t *x) {
    grain_delay_clear(x->grain_delay);
    post("ligase~: grain delay buffer cleared");
}

// Delay mode selection
static void ligase_delay_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m >= 0 && m <= 2) {
        grain_delay_set_mode(x->grain_delay, (grain_delay_mode_t)m);
        const char *mode_names[] = {"DD-4", "Bencina", "Stut"};
        post("ligase~: delay mode set to %d (%s)", m, mode_names[m]);
    } else {
        pd_error(x, "ligase~: invalid delay mode %d (use 0=DD-4, 1=Bencina, 2=Stut)", m);
    }
}

// @endregion:ligase_pd.core.grain.delay

// @region:ligase_pd.core.grain.delay_stut Stut Mode Messages

// Trigger a stut sequence
static void ligase_stut(ligase_t *x) {
    // Get current splice bounds
    uint32_t splice_start, splice_end;
    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                     x->reel->length, &splice_start, &splice_end);

    // Use quantized spacing if delay quantization is active and clock is running
    float quantized_spacing = (x->delay_quant_amount > 0.0f && x->bpm > 1.0 && x->clock_running)
                             ? x->delay_quant_grid_ms : 0.0f;

    // Slice length each repeat replays (samples), decoupled from spacing:
    //   mode 1 = grainsize (one granular grain length);
    //   mode 0 = independent stut_length_ms, optionally blended toward the BPM note grid
    //            (stut_length_quant amount), same pattern as delay-time quantization.
    float length_samples;
    if (x->stut_length_mode == 1) {
        length_samples = x->grain_size * (float)x->sample_rate;
    } else {
        float len_ms = x->stut_length_ms;
        if (x->stut_len_quant_amount > 0.0f && x->bpm > 1.0 && x->clock_running &&
            x->stut_len_quant_grid_ms > 0.0f) {
            len_ms = len_ms * (1.0f - x->stut_len_quant_amount) +
                     x->stut_len_quant_grid_ms * x->stut_len_quant_amount;
        }
        length_samples = (len_ms / 1000.0f) * (float)x->sample_rate;
    }
    if (length_samples < 1.0f) length_samples = 1.0f;

    grain_delay_stut_trigger(x->delay_stut, x->grain_delay, splice_start, splice_end,
                             quantized_spacing, length_samples);

    if (quantized_spacing > 0.0f) {
        post("ligase~: stut triggered (reps %d, reduction %.2f, quantized spacing %.2f ms, slice %.1f ms%s)",
             x->delay_stut->num_repetitions, x->delay_stut->gain_reduction, quantized_spacing,
             length_samples * 1000.0f / (float)x->sample_rate,
             x->stut_length_mode ? " [grainsize]" : "");
    } else {
        post("ligase~: stut triggered (reps %d, reduction %.2f, spacing %.2f ms, slice %.1f ms%s)",
             x->delay_stut->num_repetitions, x->delay_stut->gain_reduction, x->delay_stut->spacing_ms,
             length_samples * 1000.0f / (float)x->sample_rate,
             x->stut_length_mode ? " [grainsize]" : "");
    }
}

// Set stut number of repetitions
static void ligase_stut_reps(ligase_t *x, t_floatarg reps) {
    grain_delay_stut_set_repetitions(x->delay_stut, (int)reps);
    post("ligase~: stut repetitions set to %d", (int)reps);
}

// Set stut gain reduction factor
static void ligase_stut_reduction(ligase_t *x, t_floatarg reduction) {
    grain_delay_stut_set_reduction(x->delay_stut, reduction);
    post("ligase~: stut gain reduction set to %.2f", reduction);
}

// Set stut spacing in milliseconds
static void ligase_stut_spacing(ligase_t *x, t_floatarg spacing_ms) {
    grain_delay_stut_set_spacing(x->delay_stut, spacing_ms);
    post("ligase~: stut spacing set to %.2f ms", spacing_ms);
}

// @endregion:ligase_pd.core.grain.delay_stut

// @region:ligase_pd.core.grain.delay_bencina Bencina Mode Messages

// Set bencina grain spacing (IOT)
static void ligase_bencina_iot(ligase_t *x, t_floatarg iot_ms) {
    grain_delay_bencina_set_spacing(x->delay_bencina, iot_ms);
    post("ligase~: bencina grain spacing (IOT) set to %.2f ms", iot_ms);
}

// Set bencina grain size
static void ligase_bencina_grainsize(ligase_t *x, t_floatarg size_sec) {
    grain_delay_bencina_set_grain_size(x->delay_bencina, size_sec);
    post("ligase~: bencina grain size set to %.3f seconds", size_sec);
}

// Bencina position-scatter (cloud diffusion): 0 = coherent grains (smooth; stereo cloud comes
// from bencina_pan only), up to 1 = grains scatter up to a full grain length back (grainy/diffuse
// but rougher). Default 0.25.
static void ligase_bencina_spread(ligase_t *x, t_floatarg amount) {
    grain_delay_bencina_set_scatter(x->delay_bencina, amount);
    post("ligase~: bencina scatter set to %.2f", amount);
}

// Bencina grain edge-round (de-click), 0 = OFF (default; envelope/skew edges left intact, including
// their clickiness), up to 1 = grains ramped in/out over half a grain length.
static void ligase_bencina_edge(ligase_t *x, t_floatarg amount) {
    grain_delay_bencina_set_edge(x->delay_bencina, amount);
    post("ligase~: bencina edge-round set to %.2f", amount);
}

// Bencina wet makeup gain (into the tanh soft-limit), default 6.0. Higher = louder/more saturated;
// tanh keeps the output bounded to +-1 so it can't clip.
static void ligase_bencina_level(ligase_t *x, t_floatarg gain) {
    grain_delay_bencina_set_level(x->delay_bencina, gain);
    post("ligase~: bencina level set to %.2f", gain);
}

// Set bencina wrap mode
static void ligase_bencina_wrap(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        grain_delay_bencina_set_wrap_mode(x->delay_bencina, m);
        const char *mode_names[] = {"Global", "Splice"};
        post("ligase~: bencina wrap mode set to %d (%s)", m, mode_names[m]);
    } else {
        pd_error(x, "ligase~: invalid wrap mode %d (use 0=Global, 1=Splice)", m);
    }
}

// Clear all active bencina grains
static void ligase_bencina_clear(ligase_t *x) {
    grain_delay_bencina_clear(x->delay_bencina);
    post("ligase~: bencina grains cleared");
}

// @endregion:ligase_pd.core.grain.delay_bencina


// @region:ligase_pd.core.grain.smear.messages Allpass Smear Message Handlers
// These stay silent (no post()) — they fire on every value of a slider drag.
static void ligase_smear_frequency(ligase_t *x, t_floatarg hz) {
    if (x->smear) grain_smear_set_frequency(x->smear, hz);
}
static void ligase_smear_resonance(ligase_t *x, t_floatarg r) {
    if (x->smear) grain_smear_set_resonance(x->smear, r);
}
static void ligase_smear_stages(ligase_t *x, t_floatarg stages) {
    if (x->smear) grain_smear_set_stages(x->smear, (int)stages);
}
static void ligase_smear_feedback(ligase_t *x, t_floatarg fb) {
    if (x->smear) grain_smear_set_feedback(x->smear, fb);
}

// --- SMEAR pitch destination (note->Hz resonator pitch; independent of grain pitch) ---

static void ligase_smear_pitch_source(ligase_t *x, t_floatarg src) {
    int s = (int)src;
    if (s < 0 || s > 4) {
        pd_error(x, "ligase~: smear_pitch_source must be 0-4 (0=off,1=semitone,2=scale,3=midi,4=pattern)");
        return;
    }
    x->scheduler->smear_pitch_control.source  = s;
    x->scheduler->smear_pitch_control.enabled = (s != SMEAR_PITCH_OFF) ? 1 : 0;
    const char *names[] = {"off", "semitone", "scale", "midi", "pattern"};
    post("ligase~: smear_pitch_source -> %s", names[s]);
}

static void ligase_smear_pitch_semitones(ligase_t *x, t_floatarg n) {
    x->scheduler->smear_pitch_control.semitone = n;
    x->scheduler->smear_pitch_control.source   = SMEAR_PITCH_SEMITONE;   // auto-select
    x->scheduler->smear_pitch_control.enabled  = 1;
    post("ligase~: smear pitch %.2f semitones (source -> semitone)", n);
}

// smear_note <note> [ref_note] [ref_hz] : a single explicit note for the resonator (channel-free; P2's
// channel-aware 'midi' message writes the same note/midi_enabled fields by channel).
static void ligase_smear_note(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (argc < 1 || argv[0].a_type != A_FLOAT) {
        pd_error(x, "ligase~: smear_note requires <note> [ref_note] [ref_hz]");
        return;
    }
    int note = (int)argv[0].a_w.w_float;
    if (note < 1 || note > 127) {
        pd_error(x, "ligase~: smear_note %d out of range (1-127)", note);
        return;
    }
    smear_pitch_control_t *sp = &x->scheduler->smear_pitch_control;
    sp->note = note;
    sp->midi_enabled = 1;
    sp->source = SMEAR_PITCH_MIDI;
    sp->enabled = 1;
    if (argc >= 2 && argv[1].a_type == A_FLOAT) {
        int rn = (int)argv[1].a_w.w_float;
        if (rn >= 0 && rn <= 127) sp->ref_note = rn;
    }
    if (argc >= 3 && argv[2].a_type == A_FLOAT) {
        float rh = argv[2].a_w.w_float;
        if (rh > 0.0f) sp->ref_hz = rh;
    }
    post("ligase~: smear_note %d (ref note %d = %.1f Hz)", note, sp->ref_note, sp->ref_hz);
}

// smear_pitch_scale <semitones...> : degree->semitone scale for the SMEAR SCALE + PATTERN sources.
static void ligase_smear_pitch_scale(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (!argv) return;
    if (argc == 0 || argc > MAX_SCALE_NOTES) {
        pd_error(x, "ligase~: smear_pitch_scale requires 1-%d semitone values", MAX_SCALE_NOTES);
        return;
    }
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type != A_FLOAT) {
            pd_error(x, "ligase~: smear_pitch_scale requires float values (previous scale preserved)");
            return;
        }
    }
    x->scheduler->smear_pitch_control.scale.count = argc;
    for (int i = 0; i < argc; i++)
        x->scheduler->smear_pitch_control.scale.semitones[i] = argv[i].a_w.w_float;
    post("ligase~: smear pitch scale set with %d notes", argc);
}

// smear_pitch_rand_type <type> : stochastic generator for the SMEAR SCALE source (mirrors pitch_rand_type).
static void ligase_smear_pitch_debug(ligase_t *x, t_floatarg f) {
    x->smear_pitch_debug = (f != 0.0f) ? 1 : 0;
    post("ligase~: smear_pitch_debug %s", x->smear_pitch_debug ? "on" : "off");
}

// Smear pitch fine-tune (P3): message in CENTS, stored in SEMITONES. +/-50 cents = +/-0.5 semitone.
static void ligase_smear_pitch_fine(ligase_t *x, t_floatarg cents) {
    if (cents < -50.0f) cents = -50.0f;
    if (cents >  50.0f) cents =  50.0f;
    x->scheduler->smear_pitch_control.semitone_fine = cents / 100.0f;
    post("ligase~: smear pitch fine %.1f cents (%.4f semitone)", cents, cents / 100.0f);
}

static void ligase_smear_pitch_rand_type(ligase_t *x, t_symbol *s) {
    const char *t = s->s_name;
    rand_type_t rt; int inst = 0;
    if      (strncmp(t, "rand_", 5) == 0)       { rt = RAND_TYPE_RAND;      inst = atoi(t+5)-1; }
    else if (strncmp(t, "perlin_1d_", 10) == 0) { rt = RAND_TYPE_PERLIN_1D; inst = atoi(t+10)-1; }
    else if (strncmp(t, "perlin_2d_", 10) == 0) { rt = RAND_TYPE_PERLIN_2D; inst = atoi(t+10)-1; }
    else if (strncmp(t, "lorenz_", 7) == 0)     { rt = RAND_TYPE_LORENZ;    inst = atoi(t+7)-1; }
    else if (strncmp(t, "nbody_", 6) == 0)      { rt = RAND_TYPE_NBODY;     inst = atoi(t+6)-1; }
    else if (strncmp(t, "sphere_", 7) == 0)     { rt = RAND_TYPE_SPHERE;    inst = atoi(t+7)-1; }
    else if (strncmp(t, "saw_", 4) == 0)        { rt = RAND_TYPE_SAW;       inst = atoi(t+4)-1; }
    else if (strncmp(t, "sine_", 5) == 0)       { rt = RAND_TYPE_SINE;      inst = atoi(t+5)-1; }
    else if (strncmp(t, "square_", 7) == 0)     { rt = RAND_TYPE_SQUARE;    inst = atoi(t+7)-1; }
    else {
        pd_error(x, "ligase~: invalid smear_pitch_rand_type '%s' (rand_N/perlin_1d_N/perlin_2d_N/lorenz_N/nbody_N/sphere_N/saw_N/sine_N/square_N, N=1-4)", t);
        return;
    }
    if (inst < 0 || inst > 3) {
        pd_error(x, "ligase~: smear pitch rand instance must be 1-4");
        return;
    }
    x->scheduler->smear_pitch_control.semitone_range.rand_type = rt;
    x->scheduler->smear_pitch_control.semitone_range.rand_instance = inst;
    post("ligase~: smear pitch random type set to %s", t);
}
// @endregion:ligase_pd.core.grain.smear.messages

// @region:ligase_pd.core.grain.distortion Grain Distortion Methods

// Enable/disable grain distortion
static void ligase_distortion_enable(ligase_t *x, t_floatarg enable) {
    if (x->scheduler->distortion) {
        grain_distortion_set_enabled(x->scheduler->distortion, (int)enable);
        post("ligase~: distortion %s", (int)enable ? "enabled" : "disabled");
    }
}

// Set distortion intensity directly (bypassing signal inlet)
static void ligase_distortion_intensity(ligase_t *x, t_floatarg intensity) {
    if (x->scheduler->distortion) {
        float clamped = intensity;
        if (clamped < 0.0f) clamped = 0.0f;
        if (clamped > 1.0f) clamped = 1.0f;
        grain_distortion_set_intensity(x->scheduler->distortion, clamped);
        post("ligase~: distortion intensity set to %.2f", clamped);
    }
}

// Enable/disable distortion oversampling
static void ligase_distortion_oversampling(ligase_t *x, t_floatarg enabled) {
    if (x->scheduler->distortion) {
        int is_enabled = (int)enabled;
        grain_distortion_set_oversampling(x->scheduler->distortion, is_enabled);
        post("ligase~: distortion oversampling %s", is_enabled ? "enabled" : "disabled");
    }
}

// Set pre-tanh highpass filter frequency (30-500Hz)
static void ligase_distortion_pre_hp_freq(ligase_t *x, t_floatarg freq) {
    if (x->scheduler->distortion) {
        grain_distortion_set_pre_hp_freq(x->scheduler->distortion, freq);
        post("ligase~: distortion pre-tanh highpass frequency set to %.1f Hz", freq);
    }
}

// Set pre-tanh highpass filter mix (0-1)
static void ligase_distortion_pre_hp_mix(ligase_t *x, t_floatarg mix) {
    if (x->scheduler->distortion) {
        grain_distortion_set_pre_hp_mix(x->scheduler->distortion, mix);
        post("ligase~: distortion pre-tanh highpass mix set to %.2f", mix);
    }
}

// Set post-tanh lowpass filter frequency (2400-10000Hz)
static void ligase_distortion_post_lp_freq(ligase_t *x, t_floatarg freq) {
    if (x->scheduler->distortion) {
        grain_distortion_set_post_lp_freq(x->scheduler->distortion, freq);
        post("ligase~: distortion post-tanh lowpass frequency set to %.1f Hz", freq);
    }
}

// Set post-tanh lowpass filter mix (0-1)
static void ligase_distortion_post_lp_mix(ligase_t *x, t_floatarg mix) {
    if (x->scheduler->distortion) {
        grain_distortion_set_post_lp_mix(x->scheduler->distortion, mix);
        post("ligase~: distortion post-tanh lowpass mix set to %.2f", mix);
    }
}

// Set reject notch filter center frequency
static void ligase_distortion_notch_freq(ligase_t *x, t_floatarg freq) {
    if (x->scheduler->distortion) {
        grain_distortion_set_notch_freq(x->scheduler->distortion, freq);
        post("ligase~: distortion notch filter frequency set to %.1f Hz", freq);
    }
}

// Set reject notch filter bandwidth
static void ligase_distortion_notch_bw(ligase_t *x, t_floatarg bw) {
    if (x->scheduler->distortion) {
        grain_distortion_set_notch_bandwidth(x->scheduler->distortion, bw);
        post("ligase~: distortion notch filter bandwidth set to %.1f Hz", bw);
    }
}

// Set reject notch filter mix (0-1, 0=inactive)
static void ligase_distortion_notch_mix(ligase_t *x, t_floatarg mix) {
    if (x->scheduler->distortion) {
        grain_distortion_set_notch_mix(x->scheduler->distortion, mix);
        post("ligase~: distortion notch filter mix set to %.2f", mix);
    }
}

// Distortion Enhancement: Pre-emphasis mode (0=HP, 1=LP)
static void ligase_dist_emphasis_mode(ligase_t *x, t_floatarg mode) {
    if (x->scheduler->distortion) {
        emphasis_mode_t emph_mode = (mode >= 0.5f) ? EMPHASIS_MODE_LP : EMPHASIS_MODE_HP;
        grain_distortion_set_emphasis_mode(x->scheduler->distortion, emph_mode);
        post("ligase~: distortion emphasis mode set to %s", emph_mode == EMPHASIS_MODE_HP ? "HP (bright)" : "LP (dark)");
    }
}

// Distortion Enhancement: Pre-emphasis frequency (100-5000 Hz)
static void ligase_dist_emphasis_freq(ligase_t *x, t_floatarg freq) {
    if (x->scheduler->distortion) {
        grain_distortion_set_emphasis_freq(x->scheduler->distortion, freq);
        post("ligase~: distortion emphasis freq set to %.1f Hz", freq);
    }
}

// Distortion Enhancement: Pre-gain (0.1-10.0)
static void ligase_dist_pregain(ligase_t *x, t_floatarg gain) {
    if (x->scheduler->distortion) {
        grain_distortion_set_pregain(x->scheduler->distortion, gain);
        post("ligase~: distortion pregain set to %.2f", gain);
    }
}

// Distortion Enhancement: Waveshaper mode (0=tanh, 1=arctan, 2=asymmetric, 3=blend, 4=poly)
static void ligase_dist_waveshaper_mode(ligase_t *x, t_floatarg mode) {
    if (x->scheduler->distortion) {
        int mode_int = (int)mode;
        if (mode_int < 0) mode_int = 0;
        if (mode_int > 4) mode_int = 4;

        waveshaper_mode_t ws_mode = (waveshaper_mode_t)mode_int;
        grain_distortion_set_waveshaper_mode(x->scheduler->distortion, ws_mode);

        const char *mode_names[] = {"tanh", "arctan", "asymmetric", "blend", "polynomial"};
        post("ligase~: distortion waveshaper mode set to %s", mode_names[mode_int]);
    }
}

// Distortion Enhancement: Curve blend (0.0-1.0, tanh to arctan)
static void ligase_dist_curve_blend(ligase_t *x, t_floatarg blend) {
    if (x->scheduler->distortion) {
        grain_distortion_set_curve_blend(x->scheduler->distortion, blend);
        post("ligase~: distortion curve blend set to %.2f", blend);
    }
}

// Distortion Enhancement: Positive drive for asymmetric mode (1.0-20.0)
static void ligase_dist_drive_pos(ligase_t *x, t_floatarg drive) {
    if (x->scheduler->distortion) {
        grain_distortion_set_drive_pos(x->scheduler->distortion, drive);
        post("ligase~: distortion positive drive set to %.2f", drive);
    }
}

// Distortion Enhancement: Negative drive for asymmetric mode (1.0-20.0)
static void ligase_dist_drive_neg(ligase_t *x, t_floatarg drive) {
    if (x->scheduler->distortion) {
        grain_distortion_set_drive_neg(x->scheduler->distortion, drive);
        post("ligase~: distortion negative drive set to %.2f", drive);
    }
}

// Distortion Enhancement: Polynomial c1 coefficient (-10.0 to 10.0)
static void ligase_dist_poly_c1(ligase_t *x, t_floatarg c1) {
    if (x->scheduler->distortion) {
        grain_distortion_set_poly_c1(x->scheduler->distortion, c1);
        post("ligase~: distortion polynomial c1 set to %.2f", c1);
    }
}

// Distortion Enhancement: Polynomial c2 coefficient (-10.0 to 10.0)
static void ligase_dist_poly_c2(ligase_t *x, t_floatarg c2) {
    if (x->scheduler->distortion) {
        grain_distortion_set_poly_c2(x->scheduler->distortion, c2);
        post("ligase~: distortion polynomial c2 set to %.2f", c2);
    }
}

// Distortion Enhancement: Polynomial c3 coefficient (-10.0 to 10.0)
static void ligase_dist_poly_c3(ligase_t *x, t_floatarg c3) {
    if (x->scheduler->distortion) {
        grain_distortion_set_poly_c3(x->scheduler->distortion, c3);
        post("ligase~: distortion polynomial c3 set to %.2f", c3);
    }
}

// Distortion Position Mode: 0 = per-grain (no oversample), 1 = post-mix (with oversample, default)
static void ligase_distortion_position(ligase_t *x, t_floatarg mode) {
    if (x->scheduler->distortion) {
        int mode_int = (int)mode;
        // Clamp to valid values
        if (mode_int < 0) mode_int = 0;
        if (mode_int > 1) mode_int = 1;

        x->scheduler->distortion->position_mode = mode_int;
        post("ligase~: distortion position set to %s",
             mode_int == 0 ? "per-grain (experimental)" : "post-mix (default)");

        // Warn about per-grain distortion recording behavior in Morphagene mode
        if (mode_int == 0) {
            post("ligase~: per-grain mode applies distortion BEFORE recording. In Morphagene mode (sos_mode 1), distortion will be recorded to buffer");
        }
    }
}

// Distortion Oversampling Factor: 1, 2, 4, or 8 (default: 4)
static void ligase_distortion_oversample_factor(ligase_t *x, t_floatarg factor) {
    if (x->scheduler->distortion) {
        int f = (int)factor;
        // Validate and clamp to allowed values
        if (f != 1 && f != 2 && f != 4 && f != 8) {
            pd_error(x, "ligase~: oversample factor must be 1, 2, 4, or 8 (got %d)", f);
            return;
        }

        x->scheduler->distortion->oversample_factor = f;

        // Update anti-aliasing filter coefficients for new oversample factor
        // This is done in grain_distortion_process_block() when buffer is resized,
        // but we can also update them now for consistency
        post("ligase~: distortion oversampling set to %dx", f);
    }
}

// @endregion:ligase_pd.core.grain.distortion

// @region:ligase_pd.core.grain.moogladder Moogladder Filter Methods

// Set moogladder cutoff frequency (Hz)
static void ligase_moog_cutoff(ligase_t *x, t_floatarg cutoff) {
    if (x->moogladder) {
        grain_moogladder_set_cutoff(x->moogladder, cutoff);
        post("ligase~: moogladder cutoff set to %.1f Hz", cutoff);
    }
}

// Set moogladder resonance (0.0-4.0)
static void ligase_moog_resonance(ligase_t *x, t_floatarg resonance) {
    if (x->moogladder) {
        grain_moogladder_set_resonance(x->moogladder, resonance);
        post("ligase~: moogladder resonance set to %.2f", resonance);
    }
}

// Set moogladder dry/wet mix (0.0-1.0)
static void ligase_moog_mix(ligase_t *x, t_floatarg mix) {
    if (x->moogladder) {
        grain_moogladder_set_mix(x->moogladder, mix);
        post("ligase~: moogladder mix set to %.2f", mix);
    }
}

// Set moogladder feedback threshold (1.0-5.0)
static void ligase_moog_fb_threshold(ligase_t *x, t_floatarg threshold) {
    if (x->moogladder) {
        grain_moogladder_set_fb_threshold(x->moogladder, threshold);
        post("ligase~: moogladder feedback threshold set to %.2f", threshold);
    }
}

// Set moogladder feedback saturation (0.1-2.0)
static void ligase_moog_fb_saturation(ligase_t *x, t_floatarg saturation) {
    if (x->moogladder) {
        grain_moogladder_set_fb_saturation(x->moogladder, saturation);
        post("ligase~: moogladder feedback saturation set to %.2f", saturation);
    }
}

// Enable/disable moogladder filter
static void ligase_moog_enable(ligase_t *x, t_floatarg enable) {
    if (x->moogladder) {
        grain_moogladder_set_enabled(x->moogladder, (int)enable);
        post("ligase~: moogladder %s", (int)enable ? "enabled" : "disabled");
    }
}

// @endregion:ligase_pd.core.grain.moogladder

// @region:ligase_pd.core.params.range Parameter Range Methods

// Helper: Get parameter range by name
static param_range_t* get_param_range_by_name(ligase_t *x, const char *name) {
    if (strcmp(name, "speed") == 0) return &x->scheduler->speed_range;
    if (strcmp(name, "scanrate") == 0) return &x->scheduler->scanrate_range;
    if (strcmp(name, "organize") == 0) return &x->scheduler->organize_range;
    if (strcmp(name, "sos") == 0) return &x->scheduler->sos_range;
    if (strcmp(name, "iot") == 0) return &x->scheduler->iot_range;
    if (strcmp(name, "maxgrains") == 0) return &x->scheduler->maxgrains_range;
    if (strcmp(name, "grainsize") == 0) return &x->scheduler->grainsize_range;
    if (strcmp(name, "grainstart") == 0) return &x->scheduler->grainstart_range;
    if (strcmp(name, "env_skew") == 0) return &x->scheduler->env_skew_range;
    if (strcmp(name, "gdelay") == 0) return &x->scheduler->gdelay_range;
    if (strcmp(name, "gdelay_feed") == 0) return &x->scheduler->gdelay_feedback_range;
    if (strcmp(name, "gdelay_tone") == 0) return &x->scheduler->gdelay_tone_range;
    if (strcmp(name, "gdelay_mix") == 0) return &x->scheduler->gdelay_mix_range;
    if (strcmp(name, "distortion") == 0) return &x->scheduler->distortion_range;
    if (strcmp(name, "amplitude") == 0) return &x->scheduler->amplitude_range;
    if (strcmp(name, "pan") == 0) return &x->scheduler->pan_range;
    if (strcmp(name, "moog_cutoff") == 0) return &x->scheduler->moog_cutoff_range;
    if (strcmp(name, "moog_resonance") == 0) return &x->scheduler->moog_resonance_range;
    if (strcmp(name, "moog_mix") == 0) return &x->scheduler->moog_mix_range;
    // Distortion enhancement parameters
    if (strcmp(name, "dist_emphasis_freq") == 0) return &x->scheduler->dist_emphasis_freq_range;
    if (strcmp(name, "dist_pregain") == 0) return &x->scheduler->dist_pregain_range;
    if (strcmp(name, "dist_curve_blend") == 0) return &x->scheduler->dist_curve_blend_range;
    if (strcmp(name, "dist_drive_pos") == 0) return &x->scheduler->dist_drive_pos_range;
    if (strcmp(name, "dist_drive_neg") == 0) return &x->scheduler->dist_drive_neg_range;
    if (strcmp(name, "dist_poly_c1") == 0) return &x->scheduler->dist_poly_c1_range;
    if (strcmp(name, "dist_poly_c2") == 0) return &x->scheduler->dist_poly_c2_range;
    if (strcmp(name, "dist_poly_c3") == 0) return &x->scheduler->dist_poly_c3_range;

    // Stut
    if (strcmp(name, "stut_reps") == 0) return &x->scheduler->stut_reps_range;

    // Bencina
    if (strcmp(name, "bencina_iot") == 0) return &x->scheduler->bencina_iot_range;
    if (strcmp(name, "bencina_grainsize") == 0) return &x->scheduler->bencina_grainsize_range;
    if (strcmp(name, "bencina_pan") == 0) return &x->scheduler->bencina_pan_range;
    if (strcmp(name, "smear_frequency") == 0) return &x->scheduler->smear_frequency_range;
    if (strcmp(name, "smear_resonance") == 0) return &x->scheduler->smear_resonance_range;
    if (strcmp(name, "smear_stages") == 0) return &x->scheduler->smear_stages_range;
    if (strcmp(name, "smear_feedback") == 0) return &x->scheduler->smear_feedback_range;
    if (strcmp(name, "pitch_fine") == 0) return &x->scheduler->pitch_control.pitch_fine_range;
    if (strcmp(name, "smear_pitch_fine") == 0) return &x->scheduler->smear_pitch_fine_range;

    // Modulation outlets as first-class parameters
    if (strcmp(name, "modout1") == 0) return &x->modout1_range;
    if (strcmp(name, "modout2") == 0) return &x->modout2_range;
    if (strcmp(name, "modout3") == 0) return &x->modout3_range;
    if (strcmp(name, "modout4") == 0) return &x->modout4_range;

    return NULL;
}

// Set parameter range: "speed_range 0.5 2.0" or "speed_range 1.0" (single value)
static void ligase_param_range(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 2 || argv[0].a_type != A_SYMBOL || argv[1].a_type != A_FLOAT) {
        pd_error(x, "ligase~: param_range requires format: <param_name> <min> [<max>]");
        return;
    }

    const char *param_name = argv[0].a_w.w_symbol->s_name;
    param_range_t *range = get_param_range_by_name(x, param_name);

    if (!range) {
        pd_error(x, "ligase~: unknown parameter '%s'", param_name);
        return;
    }

    float min_val = argv[1].a_w.w_float;

    if (argc >= 3 && argv[2].a_type == A_FLOAT) {
        // Range mode: min and max provided
        float max_val = argv[2].a_w.w_float;
        range->min = min_val;
        range->max = max_val;
        range->enabled = 1;
        const char *type_name = "unknown";
        switch (range->rand_type) {
            case RAND_TYPE_NONE: type_name = "none"; break;
            case RAND_TYPE_RAND: type_name = "rand"; break;
            case RAND_TYPE_PERLIN_1D: type_name = "perlin_1d"; break;
            case RAND_TYPE_PERLIN_2D: type_name = "perlin_2d"; break;
            case RAND_TYPE_LORENZ: type_name = "lorenz"; break;
            case RAND_TYPE_NBODY: type_name = "nbody"; break;
            case RAND_TYPE_SPHERE: type_name = "sphere"; break;
            case RAND_TYPE_SAW: type_name = "saw"; break;
            case RAND_TYPE_SINE: type_name = "sine"; break;
            case RAND_TYPE_SQUARE: type_name = "square"; break;
            case RAND_TYPE_PATTERN: type_name = "pattern"; break;
        }
        post("ligase~: %s range set to %.3f - %.3f (type: %s_%d)",
             param_name, min_val, max_val, type_name, range->rand_instance + 1);
    } else {
        // Single value mode: disable range
        range->min = min_val;
        range->max = min_val;
        range->enabled = 0;
        post("ligase~: %s range disabled (single value: %.3f)", param_name, min_val);
    }
}

// Set parameter base_value for PERLIN_2D: "param_base_value modout1 0.3"
static void ligase_param_base_value(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 2 || argv[0].a_type != A_SYMBOL || argv[1].a_type != A_FLOAT) {
        pd_error(x, "ligase~: param_base_value requires format: <param_name> <value>");
        return;
    }

    const char *param_name = argv[0].a_w.w_symbol->s_name;
    param_range_t *range = get_param_range_by_name(x, param_name);

    if (!range) {
        pd_error(x, "ligase~: unknown parameter '%s'", param_name);
        return;
    }

    float base_val = argv[1].a_w.w_float;
    range->base_value = base_val;
    post("ligase~: %s base_value set to %.3f (affects PERLIN_2D Y-coordinate)", param_name, base_val);
}

// Set parameter slew/smoothing: "param_slew modout1 0.8"
static void ligase_param_slew(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 2 || argv[0].a_type != A_SYMBOL || argv[1].a_type != A_FLOAT) {
        pd_error(x, "ligase~: param_slew requires format: <param_name> <coefficient>");
        return;
    }

    const char *param_name = argv[0].a_w.w_symbol->s_name;
    param_range_t *range = get_param_range_by_name(x, param_name);

    if (!range) {
        pd_error(x, "ligase~: unknown parameter '%s'", param_name);
        return;
    }

    float slew_val = argv[1].a_w.w_float;

    // Clamp to valid range [0.0, 1.0]
    if (slew_val < 0.0f) slew_val = 0.0f;
    if (slew_val > 1.0f) slew_val = 1.0f;

    range->slew = slew_val;

    if (slew_val == 0.0f) {
        post("ligase~: %s slew disabled (instant response)", param_name);
    } else if (slew_val >= 0.99f) {
        post("ligase~: %s slew set to %.3f (nearly frozen)", param_name, slew_val);
    } else {
        post("ligase~: %s slew set to %.3f (exponential smoothing)", param_name, slew_val);
    }
}

// Set parameter invert flag: "param_invert modout1 1" or "param_invert iot 0"
static void ligase_param_invert(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 2 || argv[0].a_type != A_SYMBOL || argv[1].a_type != A_FLOAT) {
        pd_error(x, "ligase~: param_invert requires format: <param_name> <0|1>");
        return;
    }

    const char *param_name = argv[0].a_w.w_symbol->s_name;
    param_range_t *range = get_param_range_by_name(x, param_name);

    if (!range) {
        pd_error(x, "ligase~: unknown parameter '%s'", param_name);
        return;
    }

    int invert_val = (int)argv[1].a_w.w_float;
    range->invert = (invert_val != 0) ? 1 : 0;

    if (range->invert) {
        post("ligase~: %s invert enabled (modulation output will be inverted)", param_name);
    } else {
        post("ligase~: %s invert disabled (normal modulation output)", param_name);
    }
}

// Forward declaration for query helper
static float get_current_value(ligase_t *x, const char *param_name);

// Stop modulation and output current modulated values via outlet 9
// Optionally route outlet 9 → inlet 1 to "lock" parameters at their modulated values
// If not routed, acts as "stop modulation" command (returns to inlet/message control)
// Usage: "param_lock pan" or "param_lock pan amplitude grainsize"
static void ligase_param_lock(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 1) {
        pd_error(x, "ligase~: param_lock requires at least one parameter name");
        return;
    }

    // Loop through all provided parameter names
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type != A_SYMBOL) {
            pd_error(x, "ligase~: param_lock argument %d must be a parameter name", i + 1);
            continue;
        }

        const char *param_name = argv[i].a_w.w_symbol->s_name;
        param_range_t *range = get_param_range_by_name(x, param_name);

        if (!range) {
            pd_error(x, "ligase~: unknown parameter '%s'", param_name);
            continue;
        }

        // Get current modulated value from query system (*_current variables)
        float current_value = get_current_value(x, param_name);

        // Output current modulated value via outlet 9
        // User can route this back to inlet 1 to "lock" at the modulated value
        t_atom out_argv[1];
        SETFLOAT(&out_argv[0], current_value);
        outlet_anything(x->x_state_out, gensym(param_name), 1, out_argv);

        // Disable modulation (stops modulation, returns to inlet/message control)
        range->enabled = 0;

        post("ligase~: %s modulation stopped at %.3f (routed to outlet 9)", param_name, current_value);
    }

    if (argc > 1) {
        post("ligase~: stopped modulation for %d parameters (outlet 9 → inlet 1 to apply values)", argc);
    } else {
        post("ligase~: route outlet 9 → inlet 1 to apply current value, or use inlets/messages");
    }
}

// Set random generator type: "rand_type perlin_2d_3 speed" or "rand_type rand_1" (all params)
static void ligase_rand_type(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 1 || argv[0].a_type != A_SYMBOL) {
        pd_error(x, "ligase~: rand_type requires format: <type_instance> [<param_name>]");
        return;
    }

    const char *type_str = argv[0].a_w.w_symbol->s_name;
    rand_type_t rand_type;
    int instance = 0;
    int is_pattern = 0;

    // Parse type and instance from string (e.g., "perlin_2d_3", "rand_1", or "lorenz_2")
    if (strncmp(type_str, "rand_", 5) == 0) {
        rand_type = RAND_TYPE_RAND;
        instance = atoi(type_str + 5) - 1;  // Convert 1-based to 0-based
    } else if (strncmp(type_str, "perlin_1d_", 10) == 0) {
        rand_type = RAND_TYPE_PERLIN_1D;
        instance = atoi(type_str + 10) - 1;
    } else if (strncmp(type_str, "perlin_2d_", 10) == 0) {
        rand_type = RAND_TYPE_PERLIN_2D;
        instance = atoi(type_str + 10) - 1;
    } else if (strncmp(type_str, "lorenz_", 7) == 0) {
        rand_type = RAND_TYPE_LORENZ;
        instance = atoi(type_str + 7) - 1;
    } else if (strncmp(type_str, "nbody_", 6) == 0) {
        rand_type = RAND_TYPE_NBODY;
        instance = atoi(type_str + 6) - 1;
    } else if (strncmp(type_str, "sphere_", 7) == 0) {
        rand_type = RAND_TYPE_SPHERE;
        instance = atoi(type_str + 7) - 1;
    } else if (strncmp(type_str, "saw_", 4) == 0) {
        rand_type = RAND_TYPE_SAW;
        instance = atoi(type_str + 4) - 1;
    } else if (strncmp(type_str, "sine_", 5) == 0) {
        rand_type = RAND_TYPE_SINE;
        instance = atoi(type_str + 5) - 1;
    } else if (strncmp(type_str, "square_", 7) == 0) {
        rand_type = RAND_TYPE_SQUARE;
        instance = atoi(type_str + 7) - 1;
    } else if (strncmp(type_str, "pattern_", 8) == 0) {
        rand_type = RAND_TYPE_PATTERN;
        instance = atoi(type_str + 8) - 1;   // 1-based wire -> 0-based slot
        is_pattern = 1;
    } else {
        pd_error(x, "ligase~: invalid rand_type '%s' (use rand_N, perlin_1d_N, perlin_2d_N, lorenz_N, nbody_N, sphere_N, saw_N, sine_N, square_N, or pattern_N where N=1-4; pattern_N uses slots 1-8)", type_str);
        return;
    }

    // Validate instance: stochastic sources use 0..3; pattern slots use 0..PATTERN_SLOTS-1.
    if (is_pattern) {
        if (instance < 0 || instance >= PATTERN_SLOTS) {
            pd_error(x, "ligase~: pattern slot must be 1-%d", PATTERN_SLOTS);
            return;
        }
    } else if (instance < 0 || instance > 3) {
        pd_error(x, "ligase~: rand instance must be 1-4");
        return;
    }

    // Check if specific parameter specified
    if (argc >= 2 && argv[1].a_type == A_SYMBOL) {
        const char *param_name = argv[1].a_w.w_symbol->s_name;
        param_range_t *range = get_param_range_by_name(x, param_name);

        if (!range) {
            pd_error(x, "ligase~: unknown parameter '%s'", param_name);
            return;
        }

        range->rand_type = rand_type;
        range->rand_instance = instance;
        post("ligase~: %s random type set to %s", param_name, type_str);
    } else {
        // Apply to all parameters (including modulation outlets)
        param_range_t *ranges[] = {
            &x->scheduler->speed_range,
            &x->scheduler->scanrate_range,
            &x->scheduler->iot_range,
            &x->scheduler->maxgrains_range,
            &x->scheduler->grainsize_range,
            &x->scheduler->grainstart_range,
            &x->scheduler->gdelay_range,
            &x->scheduler->distortion_range,
            &x->scheduler->amplitude_range,
            &x->scheduler->pan_range,
            &x->scheduler->moog_cutoff_range,
            &x->scheduler->moog_resonance_range,
            &x->scheduler->moog_mix_range,
            &x->scheduler->dist_emphasis_freq_range,
            &x->scheduler->dist_pregain_range,
            &x->scheduler->dist_curve_blend_range,
            &x->scheduler->dist_drive_pos_range,
            &x->scheduler->dist_drive_neg_range,
            &x->scheduler->dist_poly_c1_range,
            &x->scheduler->dist_poly_c2_range,
            &x->scheduler->dist_poly_c3_range,
            &x->scheduler->stut_reps_range,
            &x->scheduler->bencina_iot_range,
            &x->scheduler->bencina_grainsize_range,
            &x->scheduler->bencina_pan_range,
            &x->scheduler->smear_frequency_range,
            &x->scheduler->smear_resonance_range,
            &x->scheduler->smear_stages_range,
            &x->scheduler->smear_feedback_range,
            &x->scheduler->pitch_control.pitch_fine_range,
            &x->scheduler->smear_pitch_fine_range,
            &x->modout1_range,
            &x->modout2_range,
            &x->modout3_range,
            &x->modout4_range
        };

        int num_ranges = sizeof(ranges) / sizeof(ranges[0]);
        for (int i = 0; i < num_ranges; i++) {
            ranges[i]->rand_type = rand_type;
            ranges[i]->rand_instance = instance;
        }

        post("ligase~: all parameters set to random type %s", type_str);
    }
}

// Set Perlin noise frequency scale
static void ligase_noise_freq(ligase_t *x, t_floatarg freq) {
    if (freq <= 0.0f) {
        pd_error(x, "ligase~: noise frequency must be > 0");
        return;
    }
    // Set all 4 instances to the same frequency (backward compatible)
    for (int i = 0; i < 4; i++) {
        x->scheduler->perlin_state.noise_frequency_scale[i] = freq;
    }
    post("ligase~: All noise frequency scales set to %.3f", freq);
}

static void ligase_noise_freq_1(ligase_t *x, t_floatarg freq) {
    if (freq <= 0.0f) {
        pd_error(x, "ligase~: noise frequency must be > 0");
        return;
    }
    x->scheduler->perlin_state.noise_frequency_scale[0] = freq;
    post("ligase~: Noise frequency scale 1 set to %.3f", freq);
}

static void ligase_noise_freq_2(ligase_t *x, t_floatarg freq) {
    if (freq <= 0.0f) {
        pd_error(x, "ligase~: noise frequency must be > 0");
        return;
    }
    x->scheduler->perlin_state.noise_frequency_scale[1] = freq;
    post("ligase~: Noise frequency scale 2 set to %.3f", freq);
}

static void ligase_noise_freq_3(ligase_t *x, t_floatarg freq) {
    if (freq <= 0.0f) {
        pd_error(x, "ligase~: noise frequency must be > 0");
        return;
    }
    x->scheduler->perlin_state.noise_frequency_scale[2] = freq;
    post("ligase~: Noise frequency scale 3 set to %.3f", freq);
}

static void ligase_noise_freq_4(ligase_t *x, t_floatarg freq) {
    if (freq <= 0.0f) {
        pd_error(x, "ligase~: noise frequency must be > 0");
        return;
    }
    x->scheduler->perlin_state.noise_frequency_scale[3] = freq;
    post("ligase~: Noise frequency scale 4 set to %.3f", freq);
}

// @endregion:ligase_pd.core.params.range

// @region:ligase_pd.core.nbody N-Body Parameter Control Methods

// Set epsilon (softening parameter) for a specific N-body instance
static void ligase_nbody_epsilon(ligase_t *x, t_floatarg instance, t_floatarg value) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: nbody_epsilon instance must be 1-4");
        return;
    }
    if (value < 0.0f) {
        pd_error(x, "ligase~: nbody_epsilon value must be >= 0");
        return;
    }
    x->scheduler->perlin_state.nbody[inst - 1].epsilon = value;
    post("ligase~: N-body %d epsilon set to %.3f", inst, value);
}

// Set damping coefficient for a specific N-body instance
static void ligase_nbody_damping(ligase_t *x, t_floatarg instance, t_floatarg value) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: nbody_damping instance must be 1-4");
        return;
    }
    if (value < 0.0f || value > 1.0f) {
        pd_error(x, "ligase~: nbody_damping value must be 0.0-1.0");
        return;
    }
    x->scheduler->perlin_state.nbody[inst - 1].damping = value;
    post("ligase~: N-body %d damping set to %.3f", inst, value);
}

// Set energy pump parameters for a specific N-body instance
static void ligase_nbody_pump(ligase_t *x, t_floatarg instance, t_floatarg amount, t_floatarg interval) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: nbody_pump instance must be 1-4");
        return;
    }
    if (amount < 0.0f) {
        pd_error(x, "ligase~: nbody_pump amount must be >= 0");
        return;
    }
    if (interval < 1) {
        pd_error(x, "ligase~: nbody_pump interval must be >= 1");
        return;
    }
    x->scheduler->perlin_state.nbody[inst - 1].pump_amount = amount;
    x->scheduler->perlin_state.nbody[inst - 1].pump_interval = (int)interval;
    post("ligase~: N-body %d pump set to %.3f every %d steps", inst, amount, (int)interval);
}

// Set gravitational constant for a specific N-body instance
static void ligase_nbody_G(ligase_t *x, t_floatarg instance, t_floatarg value) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: nbody_G instance must be 1-4");
        return;
    }
    x->scheduler->perlin_state.nbody[inst - 1].G = value;
    post("ligase~: N-body %d gravitational constant set to %.3f", inst, value);
}

// Reset a specific N-body instance to its initial configuration
static void ligase_nbody_reset(ligase_t *x, t_floatarg instance) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: nbody_reset instance must be 1-4");
        return;
    }
    nbody_reset(&x->scheduler->perlin_state.nbody[inst - 1]);
    post("ligase~: N-body %d reset to initial configuration", inst);
}

// Reset Perlin noise coordinates for a specific instance
static void ligase_perlin_reset(ligase_t *x, t_floatarg instance) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: perlin_reset instance must be 1-4");
        return;
    }
    perlin_reset_coords(&x->scheduler->perlin_state, inst - 1);
    post("ligase~: Perlin noise %d coordinates reset to 0", inst);
}

// Reset Lorenz attractor for a specific instance to initial state
static void ligase_lorenz_reset(ligase_t *x, t_floatarg instance) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: lorenz_reset instance must be 1-4");
        return;
    }
    lorenz_reset(&x->scheduler->perlin_state.lorenz[inst - 1]);
    post("ligase~: Lorenz attractor %d reset to initial state", inst);
}

// Set output mode for a specific N-body instance
// mode: 0=Body0 X, 1=Body1 Y, 2=Body2 X, 3=Dist 0-1, 4=Vel0, 5=Vel1, 6=Vel2,
//       7=Dist 0-2, 8=Dist 1-2, 9=AngMom, 10=Energy
static void ligase_nbody_mode(ligase_t *x, t_floatarg instance, t_floatarg mode) {
    int inst = (int)instance;
    int m = (int)mode;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: nbody_mode instance must be 1-4");
        return;
    }
    if (m < 0 || m > 10) {
        pd_error(x, "ligase~: nbody_mode must be 0-10");
        return;
    }
    x->scheduler->perlin_state.nbody_output_mode[inst - 1] = m;

    const char *mode_names[] = {
        "Body 0 X-pos", "Body 1 Y-pos", "Body 2 X-pos", "Distance 0-1",
        "Velocity 0", "Velocity 1", "Velocity 2",
        "Distance 0-2", "Distance 1-2", "Angular Momentum", "Total Energy"
    };
    post("ligase~: N-body %d output mode set to %d (%s)", inst, m, mode_names[m]);
}

// @endregion:ligase_pd.core.nbody

// @region:ligase_pd.core.sphere Sphere Physics Simulation Control Methods

// Apply kick impulse to sphere (STK-based)
// instance: 1-4
// vx, vy, vz: velocity components to add
static void ligase_sphere_kick(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;  // Unused
    if (argc < 4) {
        pd_error(x, "ligase~: sphere_kick requires 4 arguments (instance vx vy vz)");
        return;
    }
    int inst = (int)atom_getfloat(&argv[0]);
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: sphere_kick instance must be 1-4");
        return;
    }
    float vx = atom_getfloat(&argv[1]);
    float vy = atom_getfloat(&argv[2]);
    float vz = atom_getfloat(&argv[3]);

    sphere_add_velocity(&x->scheduler->perlin_state.sphere[inst - 1], vx, vy, vz);
    post("ligase~: Sphere %d kicked with velocity (%.2f, %.2f, %.2f)", inst, vx, vy, vz);
}

// Set damping factor for sphere
// instance: 1-4
// damping: 0.0-1.0 (0.0 = max damping, 1.0 = no damping)
static void ligase_sphere_damping(ligase_t *x, t_floatarg instance, t_floatarg damping) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: sphere_damping instance must be 1-4");
        return;
    }
    if (damping < 0.0f || damping > 1.0f) {
        pd_error(x, "ligase~: sphere_damping value must be 0.0-1.0");
        return;
    }
    sphere_set_damping(&x->scheduler->perlin_state.sphere[inst - 1], damping);
    post("ligase~: Sphere %d damping set to %.3f", inst, damping);
}

// Set elasticity (bounce coefficient) for sphere
// instance: 1-4
// elasticity: 0.0-1.0 (0.0 = no bounce, 1.0 = perfect bounce)
static void ligase_sphere_elasticity(ligase_t *x, t_floatarg instance, t_floatarg elasticity) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: sphere_elasticity instance must be 1-4");
        return;
    }
    if (elasticity < 0.0f || elasticity > 1.0f) {
        pd_error(x, "ligase~: sphere_elasticity value must be 0.0-1.0");
        return;
    }
    sphere_set_elasticity(&x->scheduler->perlin_state.sphere[inst - 1], elasticity);
    post("ligase~: Sphere %d elasticity set to %.3f", inst, elasticity);
}

// Reset sphere to initial position with zero velocity
static void ligase_sphere_reset(ligase_t *x, t_floatarg instance) {
    int inst = (int)instance;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: sphere_reset instance must be 1-4");
        return;
    }
    sphere_set_position(&x->scheduler->perlin_state.sphere[inst - 1],
                       (inst - 1 - 1.5f) * 2.0f, 0.0f, 0.0f);
    sphere_set_velocity(&x->scheduler->perlin_state.sphere[inst - 1], 0.0f, 0.0f, 0.0f);
    post("ligase~: Sphere %d reset to initial state", inst);
}

// Set output mode for a specific sphere instance
// mode: 0=Pos X, 1=Pos Y, 2=Pos Z, 3=Vel X, 4=Vel Y, 5=Vel Z, 6=Velocity Magnitude
static void ligase_sphere_mode(ligase_t *x, t_floatarg instance, t_floatarg mode) {
    int inst = (int)instance;
    int m = (int)mode;
    if (inst < 1 || inst > 4) {
        pd_error(x, "ligase~: sphere_mode instance must be 1-4");
        return;
    }
    if (m < 0 || m > 6) {
        pd_error(x, "ligase~: sphere_mode must be 0-6");
        return;
    }
    x->scheduler->perlin_state.sphere_output_mode[inst - 1] = m;

    const char *mode_names[] = {
        "Position X", "Position Y", "Position Z",
        "Velocity X", "Velocity Y", "Velocity Z", "Velocity Magnitude"
    };
    post("ligase~: Sphere %d output mode set to %d (%s)", inst, m, mode_names[m]);
}

// @endregion:ligase_pd.core.sphere

// @region:ligase_pd.core.pitch Pitch Control Methods

// Set pitch mode: 0=off (speed controls speed), 1=semitones, 2=range, 3=scale, 4=midi
static void ligase_pitch_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m < 0 || m > 5) {
        pd_error(x, "ligase~: pitch_mode must be 0-5 (0=off, 1=semitones, 2=range, 3=scale, 4=midi, 5=pattern)");
        return;
    }

    pitch_mode_t new_mode = (pitch_mode_t)m;
    x->scheduler->pitch_control.mode = new_mode;

    const char *mode_names[] = {"off (speed controls speed)", "semitones", "range", "scale", "midi", "pattern"};
    post("ligase~: pitch mode set to %s", mode_names[m]);

    // Warn about MIDI inlet behavior when switching to MIDI mode
    if (new_mode == PITCH_MODE_MIDI) {
        post("ligase~: MIDI mode expects inlet 17 connected. If disconnected, grains play at last received note (default: middle C/60)");
    }
}

// Set fixed semitone shift (for PITCH_MODE_SEMITONES)
static void ligase_pitch_semitones(ligase_t *x, t_floatarg semitones) {
    x->scheduler->pitch_control.semitones = semitones;
    post("ligase~: pitch semitones set to %.2f", semitones);
}

// Grain pitch fine-tune (P3): message in CENTS, stored in SEMITONES (cents/100). +/-50 cents = +/-0.5 semitone.
static void ligase_pitch_fine(ligase_t *x, t_floatarg cents) {
    if (cents < -50.0f) cents = -50.0f;
    if (cents >  50.0f) cents =  50.0f;
    x->scheduler->pitch_control.pitch_fine = cents / 100.0f;
    post("ligase~: pitch fine %.1f cents (%.4f semitone)", cents, cents / 100.0f);
}

// Set semitone range (for PITCH_MODE_RANGE and PITCH_MODE_SCALE)
static void ligase_pitch_range(ligase_t *x, t_floatarg min, t_floatarg max) {
    x->scheduler->pitch_control.semitone_range.min = min;
    x->scheduler->pitch_control.semitone_range.max = max;
    x->scheduler->pitch_control.semitone_range.enabled = 1;
    post("ligase~: pitch semitone range set to %.2f - %.2f", min, max);
}

// Set random generator for pitch range/scale
static void ligase_pitch_rand_type(ligase_t *x, t_symbol *s) {
    const char *type_str = s->s_name;
    rand_type_t rand_type;
    int instance = 0;
    int is_pattern = 0;

    // Parse type and instance from string
    if (strncmp(type_str, "rand_", 5) == 0) {
        rand_type = RAND_TYPE_RAND;
        instance = atoi(type_str + 5) - 1;
    } else if (strncmp(type_str, "perlin_1d_", 10) == 0) {
        rand_type = RAND_TYPE_PERLIN_1D;
        instance = atoi(type_str + 10) - 1;
    } else if (strncmp(type_str, "perlin_2d_", 10) == 0) {
        rand_type = RAND_TYPE_PERLIN_2D;
        instance = atoi(type_str + 10) - 1;
    } else if (strncmp(type_str, "lorenz_", 7) == 0) {
        rand_type = RAND_TYPE_LORENZ;
        instance = atoi(type_str + 7) - 1;
    } else if (strncmp(type_str, "nbody_", 6) == 0) {
        rand_type = RAND_TYPE_NBODY;
        instance = atoi(type_str + 6) - 1;
    } else if (strncmp(type_str, "sphere_", 7) == 0) {
        rand_type = RAND_TYPE_SPHERE;
        instance = atoi(type_str + 7) - 1;
    } else if (strncmp(type_str, "saw_", 4) == 0) {
        rand_type = RAND_TYPE_SAW;
        instance = atoi(type_str + 4) - 1;
    } else if (strncmp(type_str, "sine_", 5) == 0) {
        rand_type = RAND_TYPE_SINE;
        instance = atoi(type_str + 5) - 1;
    } else if (strncmp(type_str, "square_", 7) == 0) {
        rand_type = RAND_TYPE_SQUARE;
        instance = atoi(type_str + 7) - 1;
    } else if (strncmp(type_str, "pattern_", 8) == 0) {
        rand_type = RAND_TYPE_PATTERN;
        instance = atoi(type_str + 8) - 1;
        is_pattern = 1;
    } else {
        pd_error(x, "ligase~: invalid pitch_rand_type '%s' (use rand_N, perlin_1d_N, perlin_2d_N, lorenz_N, nbody_N, sphere_N, saw_N, sine_N, square_N, or pattern_N where N=1-4; pattern_N uses slots 1-8)", type_str);
        return;
    }

    if (is_pattern) {
        if (instance < 0 || instance >= PATTERN_SLOTS) {
            pd_error(x, "ligase~: pattern slot must be 1-%d", PATTERN_SLOTS);
            return;
        }
    } else if (instance < 0 || instance > 3) {
        pd_error(x, "ligase~: pitch rand instance must be 1-4");
        return;
    }

    x->scheduler->pitch_control.semitone_range.rand_type = rand_type;
    x->scheduler->pitch_control.semitone_range.rand_instance = instance;
    post("ligase~: pitch random type set to %s", type_str);
}

// Set pitch scale (list of semitones for PITCH_MODE_SCALE)
static void ligase_pitch_scale(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (!argv) return;
    if (argc == 0 || argc > MAX_SCALE_NOTES) {
        pd_error(x, "ligase~: pitch_scale requires 1-%d semitone values", MAX_SCALE_NOTES);
        return;
    }

    // Validate all arguments are floats BEFORE modifying the scale
    for (int i = 0; i < argc; i++) {
        if (argv[i].a_type != A_FLOAT) {
            pd_error(x, "ligase~: pitch_scale requires float values (previous scale preserved)");
            return;
        }
    }

    // All inputs valid, update the scale
    x->scheduler->pitch_control.scale.count = argc;
    for (int i = 0; i < argc; i++) {
        x->scheduler->pitch_control.scale.semitones[i] = argv[i].a_w.w_float;
    }

    post("ligase~: pitch scale set with %d notes", argc);
}

// --- P2: channel-aware MIDI ingress + dual-destination routing ---

// midi <note> [vel] [channel] : fed from Pd [notein] (note/vel/channel). Routes by channel to the two
// pitch destinations. Same channel for both => unison; different => separate. Velocity accepted, unused.
static void ligase_midi(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (argc < 1 || argv[0].a_type != A_FLOAT) {
        pd_error(x, "ligase~: midi requires <note> [vel] [channel]");
        return;
    }
    int note    = (int)argv[0].a_w.w_float;
    int channel = (argc >= 3 && argv[2].a_type == A_FLOAT) ? (int)argv[2].a_w.w_float : 1;
    if (note < 1 || note > 127) {
        pd_error(x, "ligase~: midi note %d out of range (1-127)", note);
        return;
    }
    if (channel < 1 || channel > 16) {
        pd_error(x, "ligase~: midi channel %d out of range (1-16)", channel);
        return;
    }
    scheduler_t *sch = x->scheduler;
    // Route to GRAIN destination (independent test => same channel = unison, different = separate).
    if (channel == sch->grain_midi_channel) {
        sch->pitch_control.midi_note    = note;
        sch->pitch_control.midi_enabled = 1;
        x->midi_msg_active = 1;              // message owns the grain dest; inlet-19 write suppressed
        // prev_midi_note is owned by the outlet-3 detector in perform; do not touch it here.
    }
    // Route to SMEAR destination.
    if (channel == sch->smear_midi_channel) {
        sch->smear_pitch_control.note         = note;
        sch->smear_pitch_control.midi_enabled = 1;
        sch->smear_pitch_control.source       = SMEAR_PITCH_MIDI;
        sch->smear_pitch_control.enabled      = 1;
    }
}

// midi_channel <grain_ch> <smear_ch> : set both routing channels at once (equal values = unison config).
static void ligase_midi_channel(ligase_t *x, t_floatarg g, t_floatarg sm) {
    int gi = (int)g, si = (int)sm;
    if (gi < 1 || gi > 16 || si < 1 || si > 16) {
        pd_error(x, "ligase~: midi_channel needs two channels 1-16");
        return;
    }
    x->scheduler->grain_midi_channel = gi;
    x->scheduler->smear_midi_channel = si;
    post("ligase~: MIDI routing grain<-ch%d, smear<-ch%d (%s)", gi, si, (gi == si) ? "UNISON" : "separate");
}

static void ligase_pitch_channel(ligase_t *x, t_floatarg ch) {
    int c = (int)ch;
    if (c < 1 || c > 16) { pd_error(x, "ligase~: pitch_channel must be 1-16"); return; }
    x->scheduler->grain_midi_channel = c;
    post("ligase~: grain MIDI channel %d", c);
}

static void ligase_smear_pitch_channel(ligase_t *x, t_floatarg ch) {
    int c = (int)ch;
    if (c < 1 || c > 16) { pd_error(x, "ligase~: smear_pitch_channel must be 1-16"); return; }
    x->scheduler->smear_midi_channel = c;
    post("ligase~: smear MIDI channel %d", c);
}

// @endregion:ligase_pd.core.pitch

// @region:ligase_pd.pd_external.methods.query Query and State Export Methods

// @region:ligase_pd.pd_external.methods.query.helpers Helper Functions

// Helper: Get current value of a parameter by name
static float get_current_value(ligase_t *x, const char *param_name) {
    if (strcmp(param_name, "speed") == 0) return x->speed_current;
    if (strcmp(param_name, "grainsize") == 0) return x->grainsize_current;
    if (strcmp(param_name, "grainstart") == 0) return x->grainstart_current;
    if (strcmp(param_name, "organize") == 0) return x->organize_current;
    if (strcmp(param_name, "scanrate") == 0) return x->scanrate_current;
    if (strcmp(param_name, "sos") == 0) return x->sos_current;
    if (strcmp(param_name, "iot") == 0) return x->iot_current;
    if (strcmp(param_name, "maxgrains") == 0) return x->maxgrains_current;
    if (strcmp(param_name, "gdelay") == 0) return x->gdelay_time_current;
    if (strcmp(param_name, "gdelay_feed") == 0) return x->gdelay_feedback_current;
    if (strcmp(param_name, "gdelay_tone") == 0) return x->gdelay_tone_current;
    if (strcmp(param_name, "gdelay_mix") == 0) return x->gdelay_mix_current;
    if (strcmp(param_name, "smear") == 0) return x->smear_current;
    if (strcmp(param_name, "moog_cutoff") == 0) return x->moog_cutoff_current;
    if (strcmp(param_name, "moog_resonance") == 0) return x->moog_resonance_current;
    if (strcmp(param_name, "moog_mix") == 0) return x->moog_mix_current;
    if (strcmp(param_name, "midi") == 0) return x->midi_current;
    if (strcmp(param_name, "env_skew") == 0) return x->env_skew_current;
    if (strcmp(param_name, "amplitude") == 0) return x->amplitude_current;
    if (strcmp(param_name, "pan") == 0) return x->pan_current;
    if (strcmp(param_name, "stut_reps") == 0) return x->stut_reps_current;
    if (strcmp(param_name, "bencina_iot") == 0) return x->bencina_iot_current;
    if (strcmp(param_name, "bencina_grainsize") == 0) return x->bencina_grainsize_current;
    if (strcmp(param_name, "bpm") == 0) return x->bpm;

    // Fallback: ANY modulatable param reports its last sampled value (param_range.smoothed_value,
    // set by sample_param_range to exactly the value it last returned). This covers every range
    // target without per-param tracking — so param_lock / query report the real current modulated
    // value for smear_frequency/_resonance/_stages/_feedback, bencina_pan, pitch_fine,
    // smear_pitch_fine, the dist_* set, and modout1-4 (which had no explicit case above).
    param_range_t *r = get_param_range_by_name(x, param_name);
    if (r) return r->smoothed_value;
    return 0.0f;
}

// Helper: Get random type name as string
static const char* get_rand_type_name(rand_type_t type) {
    switch (type) {
        case RAND_TYPE_RAND: return "rand";
        case RAND_TYPE_PERLIN_1D: return "perlin_1d";
        case RAND_TYPE_PERLIN_2D: return "perlin_2d";
        case RAND_TYPE_LORENZ: return "lorenz";
        case RAND_TYPE_NBODY: return "nbody";
        case RAND_TYPE_SPHERE: return "sphere";
        case RAND_TYPE_SAW: return "saw";       // (were missing -> mislabeled "none" in state dumps)
        case RAND_TYPE_SINE: return "sine";
        case RAND_TYPE_SQUARE: return "square";
        case RAND_TYPE_PATTERN: return "pattern";
        case RAND_TYPE_NONE:
        default: return "none";
    }
}

// Helper: Output a param_range to state outlet
static void output_param_range(ligase_t *x, const char *param_name, param_range_t *range) {
    if (range->enabled) {
        t_atom argv[3];
        SETSYMBOL(&argv[0], gensym(param_name));
        SETFLOAT(&argv[1], range->min);
        SETFLOAT(&argv[2], range->max);
        outlet_anything(x->x_state_out, gensym("param_range"), 3, argv);
    }
}

// Helper: Output a rand_type assignment to state outlet
static void output_rand_type(ligase_t *x, const char *param_name, param_range_t *range) {
    if (range->rand_type != RAND_TYPE_NONE) {
        const char *type_name = get_rand_type_name(range->rand_type);
        char gen_name[32];
        snprintf(gen_name, sizeof(gen_name), "%s_%d",
                 type_name, range->rand_instance + 1);

        t_atom argv[2];
        SETSYMBOL(&argv[0], gensym(gen_name));
        SETSYMBOL(&argv[1], gensym(param_name));
        outlet_anything(x->x_state_out, gensym("rand_type"), 2, argv);
    }
}

// @endregion:ligase_pd.pd_external.methods.query.helpers

// @region:ligase_pd.pd_external.methods.query.get_inlets Get Inlets (Console Reference)

// Print complete inlet documentation to console
static void ligase_get_inlets(ligase_t *x) {
    post("━━━ LIGASE~ INLETS (22 total) ━━━");
    post("Inlet  1: main (messages/audio left)");
    post("Inlet  2: audio_right");
    post("Inlet  3: grainsize");
    post("       Current: %.3f", x->grainsize_current);
    post("       Message: grainsize %.3f", x->grainsize_current);
    post("Inlet  4: grainstart");
    post("       Current: %.3f", x->grainstart_current);
    post("       Message: grainstart %.3f", x->grainstart_current);
    post("Inlet  5: speed");
    post("       Current: %.3f", x->speed_current);
    post("       Message: speed %.3f", x->speed_current);
    post("Inlet  6: organize");
    post("       Current: %.3f", x->organize_current);
    post("       Message: organize %.3f", x->organize_current);
    post("Inlet  7: scanrate");
    post("       Current: %.3f", x->scanrate_current);
    post("       Message: scanrate %.3f", x->scanrate_current);
    post("Inlet  8: sos");
    post("       Current: %.3f", x->sos_current);
    post("       Message: sos %.3f", x->sos_current);
    post("Inlet  9: iot");
    post("       Current: %.3f", x->iot_current);
    post("       Message: iot %.3f", x->iot_current);
    post("Inlet 10: maxgrains");
    post("       Current: %.0f", x->maxgrains_current);
    post("       Message: maxgrains %.0f", x->maxgrains_current);
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        post("Inlet 11: stut_reps (count, stut mode)");
        post("       Current: %d", x->delay_stut->num_repetitions);
        post("       Message: stut_reps %d", x->delay_stut->num_repetitions);
    } else {
        post("Inlet 11: gdelay_time");
        post("       Current: %.3f", x->gdelay_time_current);
        post("       Message: gdelay_time %.3f", x->gdelay_time_current);
    }
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        post("Inlet 12: stut_reduction (stut mode)");
        post("       Current: %.3f", x->gdelay_feedback_current);
        post("       Message: stut_reduction %.3f", x->gdelay_feedback_current);
        post("Inlet 13: stut_spacing (stut mode)");
        post("       Current: %.3f", x->gdelay_tone_current);
        post("       Message: stut_spacing %.3f", x->gdelay_tone_current);
    } else {
        post("Inlet 12: gdelay_feedback");
        post("       Current: %.3f", x->gdelay_feedback_current);
        post("       Message: gdelay_feed %.3f", x->gdelay_feedback_current);
        post("Inlet 13: gdelay_tone");
        post("       Current: %.3f", x->gdelay_tone_current);
        post("       Message: gdelay_tone %.3f", x->gdelay_tone_current);
    }
    post("Inlet 14: gdelay_mix");
    post("       Current: %.3f", x->gdelay_mix_current);
    post("       Message: gdelay_mix %.3f", x->gdelay_mix_current);
    post("Inlet 15: smear mix");
    post("       Current: %.3f", x->smear_current);
    post("Inlet 16: moog_cutoff");
    post("       Current: %.1f", x->moog_cutoff_current);
    post("       Message: moog_cutoff %.1f", x->moog_cutoff_current);
    post("Inlet 17: moog_resonance");
    post("       Current: %.3f", x->moog_resonance_current);
    post("       Message: moog_resonance %.3f", x->moog_resonance_current);
    post("Inlet 18: moog_mix");
    post("       Current: %.3f", x->moog_mix_current);
    post("       Message: moog_mix %.3f", x->moog_mix_current);
    post("Inlet 19: midi");
    post("       Current: %.0f", x->midi_current);
    post("       Message: midi %.0f", x->midi_current);
    post("Inlet 20: env_skew");
    post("       Current: %.3f", x->env_skew_current);
    post("       Message: env_skew %.3f", x->env_skew_current);
    post("Inlet 21: amplitude");
    post("       Current: %.3f", x->amplitude_current);
    post("       Message: amplitude %.3f", x->amplitude_current);
    post("Inlet 22: pan");
    post("       Current: %.3f", x->pan_current);
    post("       Message: pan %.3f", x->pan_current);
}

// @endregion:ligase_pd.pd_external.methods.query.get_inlets

// @region:ligase_pd.pd_external.methods.query.query Query Single Parameter

// Query single parameter (value + range + generator)
static void ligase_query(ligase_t *x, t_symbol *param_name_sym) {
    const char *param_name = param_name_sym->s_name;

    // Get current value
    float value = get_current_value(x, param_name);

    // Output current value: <param> <value>
    t_atom argv[1];
    SETFLOAT(&argv[0], value);
    outlet_anything(x->x_state_out, param_name_sym, 1, argv);

    // TODO: Output range and generator if applicable
    // This would require mapping param names to their param_range_t structures
    // For now, just output the current value
}

// @endregion:ligase_pd.pd_external.methods.query.query

// @region:ligase_pd.pd_external.methods.query.get_params Get All Parameters

// Output all current parameter values
static void ligase_get_params(ligase_t *x) {
    t_atom argv[1];

    // Output each parameter: <param> <value>
    SETFLOAT(&argv[0], x->speed_current);
    outlet_anything(x->x_state_out, gensym("speed"), 1, argv);

    SETFLOAT(&argv[0], x->grainsize_current);
    outlet_anything(x->x_state_out, gensym("grainsize"), 1, argv);

    SETFLOAT(&argv[0], x->grainstart_current);
    outlet_anything(x->x_state_out, gensym("grainstart"), 1, argv);

    SETFLOAT(&argv[0], x->organize_current);
    outlet_anything(x->x_state_out, gensym("organize"), 1, argv);

    SETFLOAT(&argv[0], x->scanrate_current);
    outlet_anything(x->x_state_out, gensym("scanrate"), 1, argv);

    SETFLOAT(&argv[0], x->sos_current);
    outlet_anything(x->x_state_out, gensym("sos"), 1, argv);

    SETFLOAT(&argv[0], x->iot_current);
    outlet_anything(x->x_state_out, gensym("iot"), 1, argv);

    SETFLOAT(&argv[0], x->maxgrains_current);
    outlet_anything(x->x_state_out, gensym("maxgrains"), 1, argv);

    SETFLOAT(&argv[0], x->gdelay_time_current);
    outlet_anything(x->x_state_out, gensym("gdelay"), 1, argv);

    SETFLOAT(&argv[0], x->gdelay_feedback_current);
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        outlet_anything(x->x_state_out, gensym("stut_reduction"), 1, argv);
    } else {
        outlet_anything(x->x_state_out, gensym("gdelay_feed"), 1, argv);
    }

    SETFLOAT(&argv[0], x->gdelay_tone_current);
    if (x->grain_delay->mode == DELAY_MODE_STUT) {
        outlet_anything(x->x_state_out, gensym("stut_spacing"), 1, argv);
    } else {
        outlet_anything(x->x_state_out, gensym("gdelay_tone"), 1, argv);
    }

    SETFLOAT(&argv[0], x->gdelay_mix_current);
    outlet_anything(x->x_state_out, gensym("gdelay_mix"), 1, argv);

    SETFLOAT(&argv[0], x->smear_current);
    outlet_anything(x->x_state_out, gensym("smear"), 1, argv);

    SETFLOAT(&argv[0], x->moog_cutoff_current);
    outlet_anything(x->x_state_out, gensym("moog_cutoff"), 1, argv);

    SETFLOAT(&argv[0], x->moog_resonance_current);
    outlet_anything(x->x_state_out, gensym("moog_resonance"), 1, argv);

    SETFLOAT(&argv[0], x->moog_mix_current);
    outlet_anything(x->x_state_out, gensym("moog_mix"), 1, argv);

    SETFLOAT(&argv[0], x->midi_current);
    outlet_anything(x->x_state_out, gensym("midi"), 1, argv);

    SETFLOAT(&argv[0], x->env_skew_current);
    outlet_anything(x->x_state_out, gensym("env_skew"), 1, argv);

    SETFLOAT(&argv[0], x->amplitude_current);
    outlet_anything(x->x_state_out, gensym("amplitude"), 1, argv);

    SETFLOAT(&argv[0], x->pan_current);
    outlet_anything(x->x_state_out, gensym("pan"), 1, argv);

    // Output BPM
    SETFLOAT(&argv[0], x->bpm);
    outlet_anything(x->x_state_out, gensym("bpm"), 1, argv);
}

// @endregion:ligase_pd.pd_external.methods.query.get_params

// @region:ligase_pd.pd_external.methods.query.get_ranges Get All Ranges

// Output all param_range settings
static void ligase_get_ranges(ligase_t *x) {
    if (!x->scheduler) return;

    // Output modulation outlet ranges
    output_param_range(x, "modout1", &x->modout1_range);
    output_param_range(x, "modout2", &x->modout2_range);
    output_param_range(x, "modout3", &x->modout3_range);
    output_param_range(x, "modout4", &x->modout4_range);

    // Output scheduler internal parameter ranges
    output_param_range(x, "speed", &x->scheduler->speed_range);
    output_param_range(x, "scanrate", &x->scheduler->scanrate_range);
    output_param_range(x, "organize", &x->scheduler->organize_range);
    output_param_range(x, "sos", &x->scheduler->sos_range);
    output_param_range(x, "iot", &x->scheduler->iot_range);
    output_param_range(x, "maxgrains", &x->scheduler->maxgrains_range);
    output_param_range(x, "grainsize", &x->scheduler->grainsize_range);
    output_param_range(x, "grainstart", &x->scheduler->grainstart_range);
    output_param_range(x, "env_skew", &x->scheduler->env_skew_range);
    output_param_range(x, "gdelay", &x->scheduler->gdelay_range);
    output_param_range(x, "gdelay_feed", &x->scheduler->gdelay_feedback_range);
    output_param_range(x, "gdelay_tone", &x->scheduler->gdelay_tone_range);
    output_param_range(x, "gdelay_mix", &x->scheduler->gdelay_mix_range);
    output_param_range(x, "distortion", &x->scheduler->distortion_range);
    output_param_range(x, "amplitude", &x->scheduler->amplitude_range);
    output_param_range(x, "pan", &x->scheduler->pan_range);
    output_param_range(x, "moog_cutoff", &x->scheduler->moog_cutoff_range);
    output_param_range(x, "moog_resonance", &x->scheduler->moog_resonance_range);
    output_param_range(x, "moog_mix", &x->scheduler->moog_mix_range);

    // Output distortion enhancement parameter ranges
    output_param_range(x, "dist_emphasis_freq", &x->scheduler->dist_emphasis_freq_range);
    output_param_range(x, "dist_pregain", &x->scheduler->dist_pregain_range);
    output_param_range(x, "dist_curve_blend", &x->scheduler->dist_curve_blend_range);
    output_param_range(x, "dist_drive_pos", &x->scheduler->dist_drive_pos_range);
    output_param_range(x, "dist_drive_neg", &x->scheduler->dist_drive_neg_range);
    output_param_range(x, "dist_poly_c1", &x->scheduler->dist_poly_c1_range);
    output_param_range(x, "dist_poly_c2", &x->scheduler->dist_poly_c2_range);
    output_param_range(x, "dist_poly_c3", &x->scheduler->dist_poly_c3_range);


    // Output stut and bencina parameter ranges
    output_param_range(x, "stut_reps", &x->scheduler->stut_reps_range);
    output_param_range(x, "bencina_iot", &x->scheduler->bencina_iot_range);
    output_param_range(x, "bencina_grainsize", &x->scheduler->bencina_grainsize_range);
    output_param_range(x, "bencina_pan", &x->scheduler->bencina_pan_range);
}

// @endregion:ligase_pd.pd_external.methods.query.get_ranges

// @region:ligase_pd.pd_external.methods.query.get_generators Get Generators

// Output all generator assignments and settings
static void ligase_get_generators(ligase_t *x) {
    if (!x->scheduler) return;

    t_atom argv[3];

    // Output modulation outlet generator assignments
    output_rand_type(x, "modout1", &x->modout1_range);
    output_rand_type(x, "modout2", &x->modout2_range);
    output_rand_type(x, "modout3", &x->modout3_range);
    output_rand_type(x, "modout4", &x->modout4_range);

    // Output scheduler internal parameter generators
    output_rand_type(x, "speed", &x->scheduler->speed_range);
    output_rand_type(x, "scanrate", &x->scheduler->scanrate_range);
    output_rand_type(x, "organize", &x->scheduler->organize_range);
    output_rand_type(x, "sos", &x->scheduler->sos_range);
    output_rand_type(x, "iot", &x->scheduler->iot_range);
    output_rand_type(x, "maxgrains", &x->scheduler->maxgrains_range);
    output_rand_type(x, "grainsize", &x->scheduler->grainsize_range);
    output_rand_type(x, "grainstart", &x->scheduler->grainstart_range);
    output_rand_type(x, "env_skew", &x->scheduler->env_skew_range);
    output_rand_type(x, "gdelay", &x->scheduler->gdelay_range);
    output_rand_type(x, "gdelay_feed", &x->scheduler->gdelay_feedback_range);
    output_rand_type(x, "gdelay_tone", &x->scheduler->gdelay_tone_range);
    output_rand_type(x, "gdelay_mix", &x->scheduler->gdelay_mix_range);
    output_rand_type(x, "distortion", &x->scheduler->distortion_range);
    output_rand_type(x, "amplitude", &x->scheduler->amplitude_range);
    output_rand_type(x, "pan", &x->scheduler->pan_range);
    output_rand_type(x, "moog_cutoff", &x->scheduler->moog_cutoff_range);
    output_rand_type(x, "moog_resonance", &x->scheduler->moog_resonance_range);
    output_rand_type(x, "moog_mix", &x->scheduler->moog_mix_range);

    // Output distortion enhancement parameter generators
    output_rand_type(x, "dist_emphasis_freq", &x->scheduler->dist_emphasis_freq_range);
    output_rand_type(x, "dist_pregain", &x->scheduler->dist_pregain_range);
    output_rand_type(x, "dist_curve_blend", &x->scheduler->dist_curve_blend_range);
    output_rand_type(x, "dist_drive_pos", &x->scheduler->dist_drive_pos_range);
    output_rand_type(x, "dist_drive_neg", &x->scheduler->dist_drive_neg_range);
    output_rand_type(x, "dist_poly_c1", &x->scheduler->dist_poly_c1_range);
    output_rand_type(x, "dist_poly_c2", &x->scheduler->dist_poly_c2_range);
    output_rand_type(x, "dist_poly_c3", &x->scheduler->dist_poly_c3_range);


    // Output stut and bencina parameter generators
    output_rand_type(x, "stut_reps", &x->scheduler->stut_reps_range);
    output_rand_type(x, "bencina_iot", &x->scheduler->bencina_iot_range);
    output_rand_type(x, "bencina_grainsize", &x->scheduler->bencina_grainsize_range);
    output_rand_type(x, "bencina_pan", &x->scheduler->bencina_pan_range);

    // Output perlin frequency settings for all 4 instances
    for (int i = 0; i < 4; i++) {
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], x->scheduler->perlin_state.noise_frequency_scale[i]);
        outlet_anything(x->x_state_out, gensym("perlin_frequency"), 2, argv);
    }

    // Output N-body settings for all 4 instances
    for (int i = 0; i < 4; i++) {
        nbody_state_t *nb = &x->scheduler->perlin_state.nbody[i];

        // nbody_mode
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], (float)x->scheduler->perlin_state.nbody_output_mode[i]);
        outlet_anything(x->x_state_out, gensym("nbody_mode"), 2, argv);

        // nbody_epsilon
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], nb->epsilon);
        outlet_anything(x->x_state_out, gensym("nbody_epsilon"), 2, argv);

        // nbody_damping
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], nb->damping);
        outlet_anything(x->x_state_out, gensym("nbody_damping"), 2, argv);

        // nbody_pump
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], nb->pump_amount);
        SETFLOAT(&argv[2], (float)nb->pump_interval);
        outlet_anything(x->x_state_out, gensym("nbody_pump"), 3, argv);

        // nbody_G
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], nb->G);
        outlet_anything(x->x_state_out, gensym("nbody_G"), 2, argv);
    }

    // Export sphere modes for all 4 instances
    for (int i = 0; i < 4; i++) {
        // sphere_mode
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], (float)x->scheduler->perlin_state.sphere_output_mode[i]);
        outlet_anything(x->x_state_out, gensym("sphere_mode"), 2, argv);

        // sphere_damping
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], x->scheduler->perlin_state.sphere[i].damping_factor);
        outlet_anything(x->x_state_out, gensym("sphere_damping"), 2, argv);

        // sphere_elasticity
        SETFLOAT(&argv[0], (float)(i + 1));
        SETFLOAT(&argv[1], x->scheduler->perlin_state.sphere[i].elasticity);
        outlet_anything(x->x_state_out, gensym("sphere_elasticity"), 2, argv);
    }
}

// @endregion:ligase_pd.pd_external.methods.query.get_generators

// @region:ligase_pd.pd_external.methods.query.get_state Get Complete State

// Output complete state (params + ranges + generators)
static void ligase_get_state(ligase_t *x) {
    ligase_get_params(x);
    ligase_get_ranges(x);
    ligase_get_generators(x);
}

// @endregion:ligase_pd.pd_external.methods.query.get_state

// @endregion:ligase_pd.pd_external.methods.query

// @endregion:ligase_pd.pd_external.methods

static void ligase_free(ligase_t *x) {
    if (x->smear) grain_smear_destroy(x->smear);
    if (x->moogladder) grain_moogladder_destroy(x->moogladder);
    if (x->grain_delay) grain_delay_destroy(x->grain_delay);
    if (x->delay_bencina) grain_delay_bencina_destroy(x->delay_bencina);
    if (x->delay_stut) grain_delay_stut_destroy(x->delay_stut);
    if (x->recorder) recorder_destroy(x->recorder);
    if (x->scheduler) scheduler_destroy(x->scheduler);
    if (x->envelope) envelope_destroy(x->envelope);
    if (x->reel) reel_destroy(x->reel);
    if (x->morph) freebytes(x->morph, sizeof(morph_state_t));
}

static void *ligase_new(void) {
    ligase_t *x = (ligase_t *)pd_new(ligase_class);

    // Remember the patch we live in, for resolving load/save paths
    x->x_canvas = canvas_getcurrent();

    // Create signal inlets
    x->x_in_right = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_grain_size = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_grain_start = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_speed = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_organize = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_scanrate = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_sos = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_iot = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_maxgrains = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_gdelay_time = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_gdelay_feedback = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_gdelay_tone = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_gdelay_mix = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_smear = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);  // Inlet 15 - smear mix
    x->x_moog_cutoff = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_moog_resonance = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_moog_mix = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_midi = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_env_skew = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_amplitude = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    x->x_pan = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);

    // Create signal outlets
    x->x_out_left = outlet_new(&x->x_obj, &s_signal);
    x->x_out_right = outlet_new(&x->x_obj, &s_signal);
    x->x_splice_end_out = outlet_new(&x->x_obj, &s_bang);  // Bang outlet for splice end
    x->x_grain_bang_out = outlet_new(&x->x_obj, &s_bang);  // Bang outlet for grain onset

    // Create modulation control outlets
    x->x_modout1 = outlet_new(&x->x_obj, &s_float);
    x->x_modout2 = outlet_new(&x->x_obj, &s_float);
    x->x_modout3 = outlet_new(&x->x_obj, &s_float);
    x->x_modout4 = outlet_new(&x->x_obj, &s_float);

    // @region:ligase_pd.pd_external.outlets.state.creation State Outlet Creation
    // Create state query outlet (outlet 9) - outputs lists of messages
    x->x_state_out = outlet_new(&x->x_obj, &s_list);
    // @endregion:ligase_pd.pd_external.outlets.state.creation

    // Initialize components
    x->reel = reel_create();
    x->envelope = envelope_create(ENVELOPE_COSINE, 4096);  // Smoother default
    x->scheduler = scheduler_create(x->envelope, 48000);
    x->recorder = recorder_create(x->reel);
    x->grain_delay = grain_delay_create(48000);
    x->delay_stut = grain_delay_stut_create(48000);
    x->delay_bencina = grain_delay_bencina_create(x->envelope, 48000);
    x->moogladder = grain_moogladder_create(48000);
    x->smear = grain_smear_create(48000);    // allpass smear effect

    // Check for allocation failures
    if (!x->reel || !x->envelope || !x->scheduler || !x->recorder ||
        !x->grain_delay || !x->delay_stut || !x->delay_bencina ||
        !x->moogladder || !x->smear) {
        pd_error(x, "ligase~: failed to allocate memory for components");
        ligase_free(x);
        return NULL;
    }

    // Morph / Metasurface layer (optional; every use is guarded by `if (x->morph)`)
    x->morph = (morph_state_t *)getbytes(sizeof(morph_state_t));
    if (x->morph) morph_state_init(x->morph);

    // Initialize parameters
    x->grain_size = 0.1f;
    x->grain_start = 0.5f;
    x->speed = 1.0f;
    x->organize_cv = 0.0f;
    x->amplitude = 1.0f;  // Default amplitude
    x->pan = 0.5f;         // Default pan (center)
    x->saw_cycles = 0.0f;  // Default: no saw modulation
    x->saw_depth = 0.0f;   // Default: no saw modulation

    // Initialize state
    x->is_playing = 0;
    x->is_triggering = 0;
    x->playback_position = 0.0f;
    x->prev_grain_start = 0.5f;  // Match initial grain_start value
    x->grain_trigger_counter = 0;
    x->grain_trigger_period = 4800;
    x->playhead_mode = PLAYHEAD_MODE_STATIC;  // Default to Morphagene mode
    x->scan_rate = 1.0f;  // Default scan rate for Mode 2

    // Initialize grain bang output
    x->grain_bang_counter = 0;
    x->grain_bang_rate = 0;  // Default: off (0 = disabled)

    // Initialize outlet 3 mode and note change detection
    x->outlet3_mode = 0;  // Default: splice end/wrap mode
    x->prev_midi_note = 60;  // Initialize to middle C
    x->prev_scale_semitone = 0.0f;  // Initialize to 0 semitones

    // Initialize clock advance mode (Mode 3)
    x->clock_advance_use_quantized = 0;  // Default: use current grain length
    x->clock_bang_received = 0;          // No clock bang yet

    // Initialize rate limiting
    x->last_splice_time = 0.0;
    x->splice_cooldown = 0.01;  // 10ms minimum between splices

    // Initialize splice behavior options
    x->splice_behavior.create_position = 0;    // Default: create at playback position
    x->splice_behavior.jump_to_new = 0;        // Default: stay in current splice
    x->splice_behavior.finish_before_nav = 0;  // Default: immediate navigation
    x->splice_behavior.split_current = 0;      // Default: allow splitting current splice
    x->splice_behavior.pending_splice = -1;    // No pending navigation
    x->splice_behavior.send_splice_msg = 0;    // Default: disabled
    x->splice_behavior.loop_mode = 1;          // Default: loop forever (0 = oneshot stop-at-end)

    // Initialize timing and quantization
    x->clock_running = 0;
    x->last_bang_time = 0.0;
    x->bpm = 0.0;  // 0 = not calculated yet

    // IOT quantization defaults
    x->time_sig_numerator = 4;
    x->time_sig_denominator = 4;  // Default 4/4
    x->quant_note = 16;  // Default 1/16 note quantization
    x->quant_amount = 0.0f;  // Default no quantization (0%)
    x->quant_grid_ms = 0.0f;
    x->samples_since_quant = 0;

    // Pattern cycle clock defaults (idle until a pattern_cycle/bang sets it)
    x->cycle_total_sec = 0.0;
    x->cycle_seg_count = 0;
    x->pattern_debug = 0;
    x->pattern_pitch_last_printed = -999.0f;
    x->smear_pitch_debug = 0;
    x->smear_pitch_dbg_last = -999.0f;
    x->midi_msg_active = 0;
    for (int ci = 0; ci < PATTERN_MAX_SEGS; ci++) {
        x->cycle_segments[ci].num = 0;
        x->cycle_segments[ci].den = 0;
    }

    // Grain size quantization defaults
    x->gs_time_sig_numerator = 4;
    x->gs_time_sig_denominator = 4;  // Default 4/4
    x->gs_quant_note = 16;  // Default 1/16 note
    x->gs_quant_amount = 0.0f;  // Default no quantization (0%)
    x->gs_quant_grid_ms = 0.0f;

    // Delay time quantization defaults
    x->delay_time_sig_numerator = 4;
    x->delay_time_sig_denominator = 4;  // Default 4/4
    x->delay_quant_note = 16;  // Default 1/16 note
    x->delay_quant_amount = 0.0f;  // Default no quantization (0%)
    x->delay_quant_grid_ms = 0.0f;

    // Stut slice-length defaults: independent mode, 62.5 ms (= old spacing-tied length)
    x->stut_length_mode = 0;       // 0 = independent length, 1 = grainsize
    x->stut_length_ms = 62.5f;     // 1/16 @120 BPM
    x->stut_len_quant_note = 16;   // Default 1/16 note
    x->stut_len_quant_amount = 0.0f;  // Default no quantization
    x->stut_len_quant_grid_ms = 0.0f;

    // Initialize SOS mode (default to Morphagene mode)
    x->sos_mode = 1;
    x->sos_value = 0.5f;  // Default to 50% mix (0.5)

    // Initialize headless mode (default: enabled for backward compatibility)
    x->headless_mode = 1;  // 1 = epsilon thresholds enabled (headless operation)

    // Initialize modulation outlets (uses unified param_range system)
    // rand_type and rand_instance now stored in modout_range.rand_type/.rand_instance
    x->modout1_range.min = 0.0f;
    x->modout1_range.max = 1.0f;
    x->modout1_range.base_value = 0.5f;
    x->modout1_range.slew = 0.0f;
    x->modout1_range.smoothed_value = 0.0f;
    x->modout1_range.enabled = 0;
    x->modout2_range.min = 0.0f;
    x->modout2_range.max = 1.0f;
    x->modout2_range.base_value = 0.5f;
    x->modout2_range.slew = 0.0f;
    x->modout2_range.smoothed_value = 0.0f;
    x->modout2_range.enabled = 0;
    x->modout3_range.min = 0.0f;
    x->modout3_range.max = 1.0f;
    x->modout3_range.base_value = 0.5f;
    x->modout3_range.slew = 0.0f;
    x->modout3_range.smoothed_value = 0.0f;
    x->modout3_range.enabled = 0;
    x->modout4_range.min = 0.0f;
    x->modout4_range.max = 1.0f;
    x->modout4_range.base_value = 0.5f;
    x->modout4_range.slew = 0.0f;
    x->modout4_range.smoothed_value = 0.0f;
    x->modout4_range.enabled = 0;
    x->modout1_range.rand_type = RAND_TYPE_NONE;
    x->modout2_range.rand_type = RAND_TYPE_NONE;
    x->modout3_range.rand_type = RAND_TYPE_NONE;
    x->modout4_range.rand_type = RAND_TYPE_NONE;
    x->modout1_range.rand_instance = 0;
    x->modout2_range.rand_instance = 0;
    x->modout3_range.rand_instance = 0;
    x->modout4_range.rand_instance = 0;

    x->sample_rate = 48000;

    return (void *)x;
}

// @region:ligase_pd.pd_external.setup Setup Function

LIGASE_PUBLIC void ligase_tilde_setup(void) {
    ligase_class = class_new(gensym("ligase~"),
        (t_newmethod)ligase_new,
        (t_method)ligase_free,
        sizeof(ligase_t),
        CLASS_DEFAULT,
        0);

    CLASS_MAINSIGNALIN(ligase_class, ligase_t, x_f);
    class_addmethod(ligase_class, (t_method)ligase_dsp, gensym("dsp"), A_CANT, 0);
    class_addmethod(ligase_class, (t_method)ligase_load, gensym("load"), A_DEFSYMBOL, 0);
    class_addmethod(ligase_class, (t_method)ligase_save, gensym("save"), A_DEFSYMBOL, 0);
    class_addmethod(ligase_class, (t_method)ligase_play, gensym("play"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_loop, gensym("loop"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_trigger, gensym("trigger"), 0);
    class_addmethod(ligase_class, (t_method)ligase_record, gensym("record"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_shift, gensym("shift"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_organize, gensym("organize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_add_splice, gensym("splice"), 0);
    class_addmethod(ligase_class, (t_method)ligase_rec_splice, gensym("recsplice"), 0);
    class_addmethod(ligase_class, (t_method)ligase_rec_input, gensym("recinput"), 0);
    class_addmethod(ligase_class, (t_method)ligase_clear_splices, gensym("clear_splices"), 0);
    class_addmethod(ligase_class, (t_method)ligase_clear_splices_except_current, gensym("clear_splices_except_current"), 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_join_right, gensym("splice_join_right"), 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_join_all, gensym("splice_join_all"), 0);
    class_addmethod(ligase_class, (t_method)ligase_clear_current_splice, gensym("clear_current_splice"), 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_create_pos, gensym("splice_create_pos"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_jump, gensym("splice_jump"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_finish_nav, gensym("splice_finish_nav"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_split, gensym("splice_split"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_send_splice_msg_mode, gensym("send_splice_msg"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_splice_msg, gensym("splice_msg"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_clear_splice_msg, gensym("clear_splice_msg"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_crossfade, gensym("sos"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_sos_mode, gensym("sos_mode"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_headless, gensym("headless"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_playhead_mode, gensym("playhead"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_scan_rate, gensym("scanrate"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_speed, gensym("speed"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_clock_advance_quant, gensym("clock_advance_quant"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_envelope, gensym("envelope"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_env_skew, gensym("env_skew"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_maxgrains, gensym("maxgrains"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_iot, gensym("iot"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_amplitude, gensym("amplitude"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pan, gensym("pan"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pan_mode, gensym("pan_mode"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_saw_cycles, gensym("saw_cycles"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_saw_depth, gensym("saw_depth"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_grainsize, gensym("grainsize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_grainstart, gensym("grainstart"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_grain_bang_rate, gensym("grain_bang_rate"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_outlet3_mode, gensym("outlet3_mode"), A_DEFFLOAT, 0);
    class_addbang(ligase_class, ligase_bang);
    class_addmethod(ligase_class, (t_method)ligase_clock_stop, gensym("clockstop"), 0);
    class_addmethod(ligase_class, (t_method)ligase_timesig, gensym("timesig"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_quantize, gensym("quantize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_quant_amount, gensym("quant"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pattern, gensym("pattern"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_pattern_cycle, gensym("pattern_cycle"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_pattern_clear, gensym("pattern_clear"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_pattern_debug, gensym("pattern_debug"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gs_timesig, gensym("gs_timesig"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_gs_quantize, gensym("gs_quantize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gs_quant_amount, gensym("gs_quant"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_delay_timesig, gensym("delay_timesig"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_delay_quantize, gensym("delay_quantize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_delay_quant_amount, gensym("delay_quant"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gdelay_time, gensym("gdelay_time"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_delay_glide, gensym("delay_glide"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gdelay_feedback, gensym("gdelay_feed"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gdelay_tone, gensym("gdelay_tone"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gdelay_mix, gensym("gdelay_mix"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_gdelay_clear, gensym("gdelay_clear"), 0);
    class_addmethod(ligase_class, (t_method)ligase_delay_mode, gensym("delay_mode"), A_DEFFLOAT, 0);

    // Stut mode methods
    class_addmethod(ligase_class, (t_method)ligase_stut, gensym("stut"), 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_reps, gensym("stut_reps"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_reduction, gensym("stut_reduction"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_spacing, gensym("stut_spacing"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_length, gensym("stut_length"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_length_mode, gensym("stut_length_mode"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_length_quantize, gensym("stut_length_quantize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_stut_length_quant, gensym("stut_length_quant"), A_DEFFLOAT, 0);

    // Bencina mode methods
    class_addmethod(ligase_class, (t_method)ligase_bencina_iot, gensym("bencina_iot"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_bencina_grainsize, gensym("bencina_grainsize"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_bencina_spread, gensym("bencina_spread"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_bencina_edge, gensym("bencina_edge"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_bencina_level, gensym("bencina_level"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_bencina_wrap, gensym("bencina_wrap"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_bencina_clear, gensym("bencina_clear"), 0);

    // Smear effect methods

    // Allpass smear controls. Mix is the signal inlet (inlet 15).
    class_addmethod(ligase_class, (t_method)ligase_smear_frequency, gensym("smear_frequency"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_resonance, gensym("smear_resonance"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_stages, gensym("smear_stages"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_feedback, gensym("smear_feedback"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_source, gensym("smear_pitch_source"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_semitones, gensym("smear_pitch_semitones"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_note, gensym("smear_note"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_scale, gensym("smear_pitch_scale"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_rand_type, gensym("smear_pitch_rand_type"), A_DEFSYMBOL, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_debug, gensym("smear_pitch_debug"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_fine, gensym("smear_pitch_fine"), A_DEFFLOAT, 0);

    class_addmethod(ligase_class, (t_method)ligase_distortion_enable, gensym("distortion_enable"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_intensity, gensym("distortion"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_oversampling, gensym("distortion_oversampling"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_pre_hp_freq, gensym("distortion_pre_hp_freq"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_pre_hp_mix, gensym("distortion_pre_hp_mix"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_post_lp_freq, gensym("distortion_post_lp_freq"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_post_lp_mix, gensym("distortion_post_lp_mix"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_notch_freq, gensym("distortion_notch_freq"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_notch_bw, gensym("distortion_notch_bw"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_notch_mix, gensym("distortion_notch_mix"), A_DEFFLOAT, 0);
    // Distortion enhancement methods
    class_addmethod(ligase_class, (t_method)ligase_dist_emphasis_mode, gensym("dist_emphasis_mode"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_emphasis_freq, gensym("dist_emphasis_freq"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_pregain, gensym("dist_pregain"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_waveshaper_mode, gensym("dist_waveshaper_mode"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_curve_blend, gensym("dist_curve_blend"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_drive_pos, gensym("dist_drive_pos"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_drive_neg, gensym("dist_drive_neg"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_poly_c1, gensym("dist_poly_c1"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_poly_c2, gensym("dist_poly_c2"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_dist_poly_c3, gensym("dist_poly_c3"), A_DEFFLOAT, 0);
    // Distortion positioning and oversampling control
    class_addmethod(ligase_class, (t_method)ligase_distortion_position, gensym("distortion_position"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_distortion_oversample_factor, gensym("distortion_oversample"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_moog_cutoff, gensym("moog_cutoff"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_moog_resonance, gensym("moog_resonance"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_moog_mix, gensym("moog_mix"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_moog_fb_threshold, gensym("moog_fb_threshold"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_moog_fb_saturation, gensym("moog_fb_saturation"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_moog_enable, gensym("moog_enable"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_param_range, gensym("param_range"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_param_base_value, gensym("param_base_value"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_param_slew, gensym("param_slew"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_param_invert, gensym("param_invert"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_param_lock, gensym("param_lock"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_rand_type, gensym("rand_type"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_noise_freq, gensym("noise_freq"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_noise_freq_1, gensym("noise_freq_1"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_noise_freq_2, gensym("noise_freq_2"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_noise_freq_3, gensym("noise_freq_3"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_noise_freq_4, gensym("noise_freq_4"), A_DEFFLOAT, 0);
    // N-body parameter control methods
    class_addmethod(ligase_class, (t_method)ligase_nbody_epsilon, gensym("nbody_epsilon"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_nbody_damping, gensym("nbody_damping"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_nbody_pump, gensym("nbody_pump"), A_DEFFLOAT, A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_nbody_G, gensym("nbody_G"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_nbody_reset, gensym("nbody_reset"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_perlin_reset, gensym("perlin_reset"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_lorenz_reset, gensym("lorenz_reset"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_nbody_mode, gensym("nbody_mode"), A_DEFFLOAT, A_DEFFLOAT, 0);
    // Sphere physics simulation control methods
    class_addmethod(ligase_class, (t_method)ligase_sphere_kick, gensym("sphere_kick"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_sphere_damping, gensym("sphere_damping"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_sphere_elasticity, gensym("sphere_elasticity"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_sphere_reset, gensym("sphere_reset"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_sphere_mode, gensym("sphere_mode"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_mode, gensym("pitch_mode"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_semitones, gensym("pitch_semitones"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_fine, gensym("pitch_fine"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_range, gensym("pitch_range"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_rand_type, gensym("pitch_rand_type"), A_DEFSYMBOL, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_scale, gensym("pitch_scale"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_midi, gensym("midi"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_midi_channel, gensym("midi_channel"), A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_pitch_channel, gensym("pitch_channel"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_channel, gensym("smear_pitch_channel"), A_DEFFLOAT, 0);
    // Modulation outlet methods
    // Modulation outlets now use unified param_range and rand_type messages (no dedicated methods)
    // @region:ligase_pd.pd_external.methods.query.registration Query System Message Handlers
    // Query and state export methods
    class_addmethod(ligase_class, (t_method)ligase_get_inlets, gensym("get_inlets"), 0);
    class_addmethod(ligase_class, (t_method)ligase_query, gensym("query"), A_DEFSYMBOL, 0);
    class_addmethod(ligase_class, (t_method)ligase_get_params, gensym("get_params"), 0);
    class_addmethod(ligase_class, (t_method)ligase_get_ranges, gensym("get_ranges"), 0);
    class_addmethod(ligase_class, (t_method)ligase_get_generators, gensym("get_generators"), 0);
    class_addmethod(ligase_class, (t_method)ligase_get_state, gensym("get_state"), 0);
    // @endregion:ligase_pd.pd_external.methods.query.registration
}

// @endregion:ligase_pd.pd_external.setup

// @endregion:ligase_pd.pd_external
