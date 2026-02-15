// ligase_fuzzer.c - Pure Data external for fuzzing ligase~
//
// This fuzzer generates adversarial inputs to test ligase~ for:
// - Memory safety issues (buffer overflows, use-after-free)
// - Undefined behavior (integer overflow, NaN/Inf handling)
// - State machine bugs (splice management, recording modes)
// - Edge cases in parameter validation

#include "m_pd.h"
#include "corpus_manager.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>

static t_class *ligase_fuzzer_class;

typedef struct _ligase_fuzzer {
    t_object x_obj;
    t_outlet *signal_out_l;
    t_outlet *signal_out_r;
    t_outlet *midi_out;
    t_outlet *msg_out;
    t_outlet *status_out;

    // Corpus management
    corpus_t *corpus;

    // Mutation state
    uint32_t rng_state;

    // Test configuration
    int iterations_per_run;
    int current_iteration;
    int signal_fuzzing_enabled;
    int midi_fuzzing_enabled;
    int message_fuzzing_enabled;

    // DSP state
    int signal_mode;  // Current signal generation mode
    int mode_counter; // Samples until mode change
} t_ligase_fuzzer;

// =============================================================================
// SIGNAL INPUT FUZZING
// =============================================================================

// Generate adversarial audio signals
static void fuzzer_generate_signal(t_ligase_fuzzer *x, t_sample *out_l,
                                    t_sample *out_r, int n) {
    // Change mode occasionally
    if (x->mode_counter <= 0) {
        x->signal_mode = xorshift32(&x->rng_state) % 8;
        x->mode_counter = 1024 + (xorshift32(&x->rng_state) % 4096);
    }
    x->mode_counter -= n;

    for (int i = 0; i < n; i++) {
        t_sample sample_l, sample_r;

        switch (x->signal_mode) {
            case 0: // Normal range [-1, 1]
                sample_l = ((t_sample)xorshift32(&x->rng_state) / UINT32_MAX) * 2.0f - 1.0f;
                sample_r = ((t_sample)xorshift32(&x->rng_state) / UINT32_MAX) * 2.0f - 1.0f;
                break;

            case 1: // Extreme values
                sample_l = (xorshift32(&x->rng_state) & 1) ? 1e38f : -1e38f;
                sample_r = (xorshift32(&x->rng_state) & 1) ? 1e38f : -1e38f;
                break;

            case 2: // Denormals
                sample_l = 1e-45f * (xorshift32(&x->rng_state) & 0xFF);
                sample_r = 1e-45f * (xorshift32(&x->rng_state) & 0xFF);
                break;

            case 3: // NaN
                sample_l = NAN;
                sample_r = NAN;
                break;

            case 4: // Infinity
                sample_l = (xorshift32(&x->rng_state) & 1) ? INFINITY : -INFINITY;
                sample_r = INFINITY;
                break;

            case 5: // DC offset
                sample_l = 100.0f;
                sample_r = -100.0f;
                break;

            case 6: // Impulse train
                sample_l = (i % 64 == 0) ? 1.0f : 0.0f;
                sample_r = sample_l;
                break;

            case 7: // Bit patterns (reinterpreted floats)
                {
                    uint32_t bits = xorshift32(&x->rng_state);
                    memcpy(&sample_l, &bits, sizeof(float));
                    bits = xorshift32(&x->rng_state);
                    memcpy(&sample_r, &bits, sizeof(float));
                }
                break;

            default:
                sample_l = sample_r = 0.0f;
        }

        out_l[i] = sample_l;
        out_r[i] = sample_r;
    }
}

// =============================================================================
// MIDI INPUT FUZZING
// =============================================================================

// MIDI fuzzing - target inlet 19 (MIDI pitch: 1-127, 0=inactive)
static void fuzzer_generate_midi(t_ligase_fuzzer *x) {
    uint32_t mode = xorshift32(&x->rng_state) % 6;
    float midi_value = 0.0f;

    switch (mode) {
        case 0: // Valid range
            midi_value = (xorshift32(&x->rng_state) % 127) + 1;
            break;

        case 1: // Boundary values
            midi_value = (xorshift32(&x->rng_state) & 1) ? 0 : 127;
            break;

        case 2: // Out of range high
            midi_value = 128 + (xorshift32(&x->rng_state) % 1000);
            break;

        case 3: // Negative values
            midi_value = -(float)(xorshift32(&x->rng_state) % 1000);
            break;

        case 4: // Float edge cases
            midi_value = (xorshift32(&x->rng_state) & 1) ? NAN : INFINITY;
            break;

        case 5: // Rapid note changes
            for (int i = 0; i < 10; i++) {
                midi_value = xorshift32(&x->rng_state) % 128;
                outlet_float(x->midi_out, midi_value);
            }
            return;
    }
    outlet_float(x->midi_out, midi_value);
}

// =============================================================================
// MESSAGE CORPUS MUTATION
// =============================================================================

void mutate_message(uint8_t *buf, size_t *len, size_t max_len, uint32_t *rng_state) {
    if (*len == 0) return;

    mutation_type_t mut = xorshift32(rng_state) % 8;
    size_t pos = xorshift32(rng_state) % *len;

    switch (mut) {
        case MUT_BIT_FLIP:
            buf[pos] ^= (1 << (xorshift32(rng_state) % 8));
            break;

        case MUT_BYTE_FLIP:
            buf[pos] = xorshift32(rng_state) & 0xFF;
            break;

        case MUT_ARITHMETIC: {
            int delta = (xorshift32(rng_state) % 71) - 35;
            buf[pos] = (uint8_t)((int)buf[pos] + delta);
            break;
        }

        case MUT_INTERESTING_VALUES: {
            // Interesting integers that often trigger bugs
            static const char *interesting[] = {
                "0", "-1", "2147483647", "-2147483648",
                "4294967295", "1e308", "-1e308", "nan", "inf",
                "0.0", "-0.0", "1e-45"
            };
            // Just append an interesting value
            size_t idx = xorshift32(rng_state) % 12;
            size_t istr_len = strlen(interesting[idx]);
            if (*len + istr_len + 1 < max_len) {
                buf[*len] = ' ';
                memcpy(buf + *len + 1, interesting[idx], istr_len);
                *len += istr_len + 1;
            }
            break;
        }

        case MUT_SPLICE:
            // Skip for now, would need corpus access
            break;

        case MUT_INSERT:
            if (*len < max_len - 1) {
                memmove(buf + pos + 1, buf + pos, *len - pos);
                buf[pos] = xorshift32(rng_state) & 0xFF;
                (*len)++;
            }
            break;

        case MUT_DELETE:
            if (*len > 1) {
                memmove(buf + pos, buf + pos + 1, *len - pos - 1);
                (*len)--;
            }
            break;

        case MUT_DICTIONARY:
            // Insert known message fragments
            {
                static const char *fragments[] = {
                    "param_range", "rand_type", "nbody_", "perlin_",
                    "lorenz_", "distortion_", "moog_", "gdelay_",
                    "splice_", "pitch_", "grain"
                };
                size_t idx = xorshift32(rng_state) % 11;
                size_t frag_len = strlen(fragments[idx]);
                if (*len + frag_len < max_len) {
                    memcpy(buf + *len, fragments[idx], frag_len);
                    *len += frag_len;
                }
            }
            break;
    }
}

// Send mutated message from corpus
static void fuzzer_send_mutated_message(t_ligase_fuzzer *x) {
    if (!x->corpus || x->corpus->count == 0) return;

    // Select a corpus entry
    size_t idx = corpus_select_weighted(x->corpus, &x->rng_state);
    corpus_entry_t *entry = &x->corpus->entries[idx];

    // Copy and mutate
    uint8_t mutated[1024];
    size_t len = entry->length < 1000 ? entry->length : 1000;
    memcpy(mutated, entry->data, len);

    // Apply 1-5 mutations
    int num_mutations = 1 + (xorshift32(&x->rng_state) % 5);
    for (int i = 0; i < num_mutations; i++) {
        mutate_message(mutated, &len, 1024, &x->rng_state);
    }
    mutated[len] = '\0';

    // Parse and send message
    t_atom atoms[128];
    int atom_count = 0;
    char *token = strtok((char *)mutated, " ");

    if (!token) return;

    t_symbol *selector = gensym(token);

    // Parse remaining tokens as atoms
    while ((token = strtok(NULL, " ")) && atom_count < 127) {
        // Try to parse as float
        char *endptr;
        float val = strtof(token, &endptr);
        if (endptr != token && *endptr == '\0') {
            SETFLOAT(&atoms[atom_count], val);
        } else {
            SETSYMBOL(&atoms[atom_count], gensym(token));
        }
        atom_count++;
    }

    // Send message
    if (atom_count > 0) {
        outlet_anything(x->msg_out, selector, atom_count, atoms);
    } else {
        outlet_anything(x->msg_out, selector, 0, NULL);
    }
}

// =============================================================================
// STATE TRANSITION FUZZING
// =============================================================================

// Rapid state changes to find race conditions and state machine bugs
static void fuzzer_trigger_state_change(t_ligase_fuzzer *x) {
    static const char *state_messages[] = {
        "play 1", "play 0",
        "record 1", "record 0",
        "recsplice", "recinput",
        "playhead 1", "playhead 2", "playhead 3",
        "clear_splices",
        "sos_mode 0", "sos_mode 1"
    };

    int count = 1 + (xorshift32(&x->rng_state) % 5);
    for (int i = 0; i < count; i++) {
        size_t idx = xorshift32(&x->rng_state) % 12;

        // Parse and send message
        char msg_copy[64];
        strncpy(msg_copy, state_messages[idx], 63);
        msg_copy[63] = '\0';

        t_atom atoms[2];
        char *token = strtok(msg_copy, " ");
        if (!token) continue;

        t_symbol *selector = gensym(token);
        int atom_count = 0;

        token = strtok(NULL, " ");
        if (token) {
            SETFLOAT(&atoms[0], atof(token));
            atom_count = 1;
        }

        outlet_anything(x->msg_out, selector, atom_count, atoms);
    }
}

// =============================================================================
// DSP PERFORM ROUTINE
// =============================================================================

static t_int *fuzzer_perform(t_int *w) {
    t_ligase_fuzzer *x = (t_ligase_fuzzer *)(w[1]);
    t_sample *out_l = (t_sample *)(w[2]);
    t_sample *out_r = (t_sample *)(w[3]);
    int n = (int)(w[4]);

    // Generate fuzzed signal data if enabled
    if (x->signal_fuzzing_enabled) {
        fuzzer_generate_signal(x, out_l, out_r, n);
    } else {
        // Silent
        for (int i = 0; i < n; i++) {
            out_l[i] = out_r[i] = 0.0f;
        }
    }

    // Occasionally fuzz control inlets during DSP
    if (x->midi_fuzzing_enabled && (xorshift32(&x->rng_state) % 64 == 0)) {
        fuzzer_generate_midi(x);
    }

    return (w + 5);
}

static void fuzzer_dsp(t_ligase_fuzzer *x, t_signal **sp) {
    dsp_add(fuzzer_perform, 4, x,
            sp[0]->s_vec,  // output left
            sp[1]->s_vec,  // output right
            sp[0]->s_n);
}

// =============================================================================
// CONTROL MESSAGES
// =============================================================================

// Main fuzzing loop
static void fuzzer_run(t_ligase_fuzzer *x, t_floatarg iterations) {
    x->iterations_per_run = (int)iterations;
    if (x->iterations_per_run <= 0) x->iterations_per_run = 10000;

    post("ligase_fuzzer: starting %d iterations", x->iterations_per_run);

    for (x->current_iteration = 0;
         x->current_iteration < x->iterations_per_run;
         x->current_iteration++) {

        // Randomly choose input type
        uint32_t input_type = xorshift32(&x->rng_state) % 10;

        if (input_type < 5 && x->message_fuzzing_enabled) {
            // 50% - Message fuzzing
            fuzzer_send_mutated_message(x);
        } else if (input_type < 7 && x->midi_fuzzing_enabled) {
            // 20% - MIDI fuzzing
            fuzzer_generate_midi(x);
        }
        // 30% - Signal fuzzing (handled in perform routine)

        // Occasionally trigger state transitions
        if (xorshift32(&x->rng_state) % 100 == 0) {
            fuzzer_trigger_state_change(x);
        }

        // Report progress
        if (x->current_iteration % 1000 == 0) {
            t_atom status[2];
            SETSYMBOL(&status[0], gensym("progress"));
            SETFLOAT(&status[1], (float)x->current_iteration / x->iterations_per_run);
            outlet_anything(x->status_out, gensym("status"), 2, status);
        }
    }

    post("ligase_fuzzer: completed %d iterations", x->iterations_per_run);

    t_atom done[1];
    SETSYMBOL(&done[0], gensym("complete"));
    outlet_anything(x->status_out, gensym("status"), 1, done);
}

// Enable/disable fuzzing types
static void fuzzer_enable_signal(t_ligase_fuzzer *x, t_floatarg f) {
    x->signal_fuzzing_enabled = (f != 0);
    post("ligase_fuzzer: signal fuzzing %s", x->signal_fuzzing_enabled ? "enabled" : "disabled");
}

static void fuzzer_enable_midi(t_ligase_fuzzer *x, t_floatarg f) {
    x->midi_fuzzing_enabled = (f != 0);
    post("ligase_fuzzer: MIDI fuzzing %s", x->midi_fuzzing_enabled ? "enabled" : "disabled");
}

static void fuzzer_enable_messages(t_ligase_fuzzer *x, t_floatarg f) {
    x->message_fuzzing_enabled = (f != 0);
    post("ligase_fuzzer: message fuzzing %s", x->message_fuzzing_enabled ? "enabled" : "disabled");
}

// Set RNG seed (float version - limited precision for large numbers)
static void fuzzer_seed(t_ligase_fuzzer *x, t_floatarg f) {
    x->rng_state = (uint32_t)f;
    post("ligase_fuzzer: RNG seed set to %u", x->rng_state);
}

// Set RNG seed from symbol (string) - full precision for large integers
static void fuzzer_seed_symbol(t_ligase_fuzzer *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 1) {
        pd_error(x, "seed_str: requires one argument");
        return;
    }

    if (argv[0].a_type == A_SYMBOL) {
        t_symbol *seed_sym = atom_getsymbol(&argv[0]);
        unsigned long seed_val = strtoul(seed_sym->s_name, NULL, 10);
        x->rng_state = (uint32_t)seed_val;
        post("ligase_fuzzer: RNG seed set to %u (from symbol '%s')", x->rng_state, seed_sym->s_name);
    } else {
        // Fallback: treat as float
        unsigned long seed_val = (unsigned long)atom_getfloat(&argv[0]);
        x->rng_state = (uint32_t)seed_val;
        post("ligase_fuzzer: RNG seed set to %u (from float)", x->rng_state);
    }
}

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

static void *ligase_fuzzer_new(void) {
    t_ligase_fuzzer *x = (t_ligase_fuzzer *)pd_new(ligase_fuzzer_class);

    // Create outlets
    x->signal_out_l = outlet_new(&x->x_obj, &s_signal);
    x->signal_out_r = outlet_new(&x->x_obj, &s_signal);
    x->midi_out = outlet_new(&x->x_obj, &s_float);
    x->msg_out = outlet_new(&x->x_obj, &s_anything);
    x->status_out = outlet_new(&x->x_obj, &s_anything);

    // Initialize corpus
    x->corpus = corpus_init_from_seeds();
    if (!x->corpus) {
        pd_error(x, "ligase_fuzzer: failed to initialize corpus");
    } else {
        post("ligase_fuzzer: loaded %zu seed messages", x->corpus->count);
    }

    // Initialize RNG with time
    // (For reproducibility testing, use fixed seed via seed or seed_str message)
    x->rng_state = (uint32_t)time(NULL);

    // Default configuration
    x->iterations_per_run = 10000;
    x->current_iteration = 0;
    x->signal_fuzzing_enabled = 1;
    x->midi_fuzzing_enabled = 1;
    x->message_fuzzing_enabled = 1;
    x->signal_mode = 0;
    x->mode_counter = 1024;

    post("ligase_fuzzer: initialized with seed %u", x->rng_state);

    return (void *)x;
}

static void ligase_fuzzer_free(t_ligase_fuzzer *x) {
    corpus_free(x->corpus);
}

// =============================================================================
// CLASS SETUP
// =============================================================================

void ligase_fuzzer_tilde_setup(void) {
    ligase_fuzzer_class = class_new(gensym("ligase_fuzzer~"),
        (t_newmethod)ligase_fuzzer_new,
        (t_method)ligase_fuzzer_free,
        sizeof(t_ligase_fuzzer),
        CLASS_DEFAULT,
        0);

    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_dsp, gensym("dsp"), A_CANT, 0);

    // Control methods
    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_run, gensym("run"), A_FLOAT, 0);
    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_seed, gensym("seed"), A_FLOAT, 0);
    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_seed_symbol, gensym("seed_str"), A_GIMME, 0);
    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_enable_signal, gensym("enable_signal"), A_FLOAT, 0);
    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_enable_midi, gensym("enable_midi"), A_FLOAT, 0);
    class_addmethod(ligase_fuzzer_class, (t_method)fuzzer_enable_messages, gensym("enable_messages"), A_FLOAT, 0);

    CLASS_MAINSIGNALIN(ligase_fuzzer_class, t_ligase_fuzzer, signal_mode);

    post("ligase_fuzzer~: Pure Data fuzzing harness for ligase~");
    post("  Usage: [ligase_fuzzer~] -> [ligase~]");
    post("  Messages: run <iterations>, seed <value> or seed_str <symbol>, enable_signal/midi/messages <0|1>");
}
