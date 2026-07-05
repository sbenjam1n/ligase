// @region:ligase_pd.core.grain.smear_bank Resonator Bank Implementation
//
// Representation (a) + Shape A from Plans/resonator_bank.md: an array of grain_smear_t*
// reusing the WHOLE grain_smear voice verbatim. Each voice runs with mix = 1.0 (pure wet:
// dry*dl + wet*xl == xl), so grain_smear_process is called UNCHANGED; the bank snapshots
// the exciter, runs each voice on a copy, accumulates, and writes
//   out = (1 - mix) * exciter + mix * (1/N) * acc
// back in place. All allocation happens in _create (message/dsp thread); _process only
// reads pre-allocated state — audio-thread-safe by construction.

#include "grain_smear_bank.h"
#include "grain_smear.h"
#include <stdlib.h>
#include <string.h>

#define SMEAR_BANK_MAGIC 0xBA11C0DE
#define SMEAR_BANK_SCRATCH 8192   // scratch/accumulator length (samples); > any Pd block

struct grain_smear_bank {
    unsigned int magic;
    int sample_rate;
    int max_voices;                // allocated voices (1..GRAIN_SMEAR_BANK_MAX_VOICES)
    int count;                     // active voices (0..max_voices); flipped by set_count, no alloc
    float mix;                     // bank dry/wet 0..1

    grain_smear_t *voices[GRAIN_SMEAR_BANK_MAX_VOICES];   // representation (a): whole voices
    float last_hz[GRAIN_SMEAR_BANK_MAX_VOICES];           // skip redundant per-block cosf recomputes

    float *scratchL, *scratchR;    // exciter snapshot (grain_smear_process is in-place)
    float *tmpL, *tmpR;            // per-voice working copy
    float *accL, *accR;            // wet accumulator
    int scratch_n;                 // allocated scratch length (samples)
};

grain_smear_bank_t *grain_smear_bank_create(int sample_rate, int max_voices) {
    if (max_voices < 1) max_voices = 1;
    if (max_voices > GRAIN_SMEAR_BANK_MAX_VOICES) max_voices = GRAIN_SMEAR_BANK_MAX_VOICES;

    grain_smear_bank_t *b = (grain_smear_bank_t *)calloc(1, sizeof(grain_smear_bank_t));
    if (!b) return NULL;
    b->magic = SMEAR_BANK_MAGIC;
    b->sample_rate = (sample_rate > 0) ? sample_rate : 48000;
    b->max_voices = max_voices;
    b->count = 0;                  // no scale loaded yet -> dry passthrough (degrade, never crash)
    b->mix = 0.0f;                 // dry by default (mirrors the single smear's mix default)
    b->scratch_n = SMEAR_BANK_SCRATCH;

    b->scratchL = (float *)calloc(SMEAR_BANK_SCRATCH, sizeof(float));
    b->scratchR = (float *)calloc(SMEAR_BANK_SCRATCH, sizeof(float));
    b->tmpL     = (float *)calloc(SMEAR_BANK_SCRATCH, sizeof(float));
    b->tmpR     = (float *)calloc(SMEAR_BANK_SCRATCH, sizeof(float));
    b->accL     = (float *)calloc(SMEAR_BANK_SCRATCH, sizeof(float));
    b->accR     = (float *)calloc(SMEAR_BANK_SCRATCH, sizeof(float));
    if (!b->scratchL || !b->scratchR || !b->tmpL || !b->tmpR || !b->accL || !b->accR) {
        grain_smear_bank_destroy(b);
        return NULL;
    }

    for (int v = 0; v < max_voices; v++) {
        grain_smear_t *voice = grain_smear_create(b->sample_rate);
        if (!voice) {
            grain_smear_bank_destroy(b);
            return NULL;
        }
        // Shape A: each voice runs PURE WET (mix = 1.0 clears the dry-skip guard and yields xl);
        // the bank owns the dry/wet blend. Bank default stages = 8 (vs the single voice's 12)
        // so N*stages stays in CPU budget.
        grain_smear_set_mix(voice, 1.0f);
        grain_smear_set_stages(voice, GRAIN_SMEAR_BANK_DEF_STAGES);
        b->voices[v] = voice;
        b->last_hz[v] = 800.0f;    // grain_smear_create's default freq_hz
    }
    return b;
}

void grain_smear_bank_destroy(grain_smear_bank_t *b) {
    if (!b) return;
    for (int v = 0; v < GRAIN_SMEAR_BANK_MAX_VOICES; v++) {
        if (b->voices[v]) grain_smear_destroy(b->voices[v]);
    }
    free(b->scratchL); free(b->scratchR);
    free(b->tmpL);     free(b->tmpR);
    free(b->accL);     free(b->accR);
    b->magic = 0;
    free(b);
}

void grain_smear_bank_set_sample_rate(grain_smear_bank_t *b, int sample_rate) {
    if (!b || b->magic != SMEAR_BANK_MAGIC || sample_rate <= 0) return;
    b->sample_rate = sample_rate;
    for (int v = 0; v < b->max_voices; v++) {
        grain_smear_set_sample_rate(b->voices[v], sample_rate);   // re-derives each a1/a2 at the new rate
    }
}

void grain_smear_bank_set_count(grain_smear_bank_t *b, int n) {
    if (!b || b->magic != SMEAR_BANK_MAGIC) return;
    if (n < 0) n = 0;
    if (n > b->max_voices) n = b->max_voices;
    b->count = n;                  // active count only — never allocates
}

void grain_smear_bank_set_voice_freq(grain_smear_bank_t *b, int i, float hz) {
    if (!b || b->magic != SMEAR_BANK_MAGIC) return;
    if (i < 0 || i >= b->max_voices) return;
    if (hz == b->last_hz[i]) return;   // unchanged -> skip the cosf recompute (called every block)
    b->last_hz[i] = hz;
    grain_smear_set_frequency(b->voices[i], hz);   // -> smear_update_coeffs' [20, 0.45*sr] clamp (sole bounds owner)
}

void grain_smear_bank_set_resonance(grain_smear_bank_t *b, float r) {
    if (!b || b->magic != SMEAR_BANK_MAGIC) return;
    for (int v = 0; v < b->max_voices; v++) grain_smear_set_resonance(b->voices[v], r);
}

void grain_smear_bank_set_stages(grain_smear_bank_t *b, int stages) {
    if (!b || b->magic != SMEAR_BANK_MAGIC) return;
    for (int v = 0; v < b->max_voices; v++) grain_smear_set_stages(b->voices[v], stages);
}

void grain_smear_bank_set_feedback(grain_smear_bank_t *b, float fb) {
    if (!b || b->magic != SMEAR_BANK_MAGIC) return;
    for (int v = 0; v < b->max_voices; v++) grain_smear_set_feedback(b->voices[v], fb);   // |fb|<0.99 clamp per voice
}

void grain_smear_bank_set_mix(grain_smear_bank_t *b, float mix) {
    if (!b || b->magic != SMEAR_BANK_MAGIC) return;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    b->mix = mix;
}

void grain_smear_bank_process(grain_smear_bank_t *b, float *left, float *right, int n) {
    if (!b || b->magic != SMEAR_BANK_MAGIC || !left || !right || n <= 0) return;

    // Fully dry or no tuned voices: leave the exciter untouched (degrade, never crash).
    if (b->count <= 0 || b->mix < 0.0001f) return;

    const int count = b->count;
    const float dry = 1.0f - b->mix;
    const float wet = b->mix / (float)count;   // bank wet * 1/N voice-sum pre-scale (GATE A.6)

    int off = 0;
    while (off < n) {                          // sliced so any block size fits the fixed scratch
        int chunk = n - off;
        if (chunk > b->scratch_n) chunk = b->scratch_n;
        size_t bytes = (size_t)chunk * sizeof(float);

        memcpy(b->scratchL, left + off, bytes);    // snapshot: every voice rings the SAME exciter
        memcpy(b->scratchR, right + off, bytes);
        memset(b->accL, 0, bytes);
        memset(b->accR, 0, bytes);

        for (int v = 0; v < count; v++) {
            memcpy(b->tmpL, b->scratchL, bytes);
            memcpy(b->tmpR, b->scratchR, bytes);
            grain_smear_process(b->voices[v], b->tmpL, b->tmpR, chunk);   // UNCHANGED voice; mix=1 -> pure wet
            for (int i = 0; i < chunk; i++) {
                b->accL[i] += b->tmpL[i];
                b->accR[i] += b->tmpR[i];
            }
        }

        for (int i = 0; i < chunk; i++) {
            left[off + i]  = dry * b->scratchL[i] + wet * b->accL[i];
            right[off + i] = dry * b->scratchR[i] + wet * b->accR[i];
        }
        off += chunk;
    }
}

// @endregion:ligase_pd.core.grain.smear_bank
