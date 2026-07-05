// @region:ligase_pd.core.grain.smear_bank Resonator Bank (allpass smear voices)
//
// A bank of N tuned grain_smear voices (allpass-cascade resonators) excited by a shared
// stereo bus — the exciter→body model: the granular+delay output strikes the bank, the
// bank rings a chord. Each voice IS a grain_smear_t reused verbatim (struct + section
// math + coefficient setters), so per-voice unity-gain stability (|fb| < 0.99) and
// denormal flushing are inherited, not re-implemented.
//
//   out = (1 - mix) * exciter + mix * (1/N) * sum_v( voice_v(exciter) )
//
// The 1/N voice-sum pre-scale is conservative: N correlated near-unity wet voices can
// never exceed the exciter's own level, so the downstream [-1,1] clamp stays a backstop.

#ifndef GRAIN_SMEAR_BANK_H
#define GRAIN_SMEAR_BANK_H

#define GRAIN_SMEAR_BANK_MAX_VOICES   16  // hard cap; practical N is driven by scale.count
#define GRAIN_SMEAR_BANK_DEF_STAGES    8  // default stages/voice (vs single smear's 12) — keeps N*stages bounded

typedef struct grain_smear_bank grain_smear_bank_t;

grain_smear_bank_t *grain_smear_bank_create(int sample_rate, int max_voices);
void grain_smear_bank_destroy(grain_smear_bank_t *b);
void grain_smear_bank_set_sample_rate(grain_smear_bank_t *b, int sample_rate);

void grain_smear_bank_set_count(grain_smear_bank_t *b, int n);           // active voices (0..max); never allocates
void grain_smear_bank_set_voice_freq(grain_smear_bank_t *b, int i, float hz);  // per-voice tuning (raw Hz;
                                                                         // smear_update_coeffs' [20, 0.45*sr] clamp owns bounds)
void grain_smear_bank_set_resonance(grain_smear_bank_t *b, float r);     // shared across voices (v1)
void grain_smear_bank_set_stages(grain_smear_bank_t *b, int stages);     // shared across voices (v1)
void grain_smear_bank_set_feedback(grain_smear_bank_t *b, float fb);     // shared across voices (v1)
void grain_smear_bank_set_mix(grain_smear_bank_t *b, float mix);         // bank dry/wet 0..1

// In-place stereo: exciter in, (1-mix)*dry + mix*(1/N)*voice-sum out.
void grain_smear_bank_process(grain_smear_bank_t *b, float *left, float *right, int n);

#endif // GRAIN_SMEAR_BANK_H
