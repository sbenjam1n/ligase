// @region:ligase_pd.core.grain.smear Allpass Smear Effect
//
// A time-domain cascade of 2nd-order allpass sections (à la Smoothie Audio "Smear" — a 98-pole allpass). 
// Each section is a tunable allpass whose group delay peaks at a center frequency; 
// cascading N of them smears transients and disperses the spectrum. A global feedback loop turns
// it into a resonator (allpass cascade is exactly unity-gain, so any |fb|<1 is
// unconditionally stable). No FFT, no overlap-add — a handful of MACs per sample.

#ifndef GRAIN_SMEAR_H
#define GRAIN_SMEAR_H

typedef struct grain_smear grain_smear_t;

grain_smear_t *grain_smear_create(int sample_rate);
void grain_smear_destroy(grain_smear_t *s);
void grain_smear_set_sample_rate(grain_smear_t *s, int sample_rate);

void grain_smear_set_mix(grain_smear_t *s, float mix);          // 0..1 dry/wet
void grain_smear_set_frequency(grain_smear_t *s, float hz);     // allpass center freq
void grain_smear_set_resonance(grain_smear_t *s, float r);      // 0..0.999 pole radius
void grain_smear_set_stages(grain_smear_t *s, int stages);      // 0..GRAIN_SMEAR_MAX_STAGES
void grain_smear_set_feedback(grain_smear_t *s, float fb);      // -0.99..0.99

// In-place stereo processing.
void grain_smear_process(grain_smear_t *s, float *left, float *right, int n);

#endif // GRAIN_SMEAR_H
