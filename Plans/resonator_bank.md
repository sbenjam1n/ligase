# Plan: Resonator bank — granular-excited tuned resonant body (distinct mode)

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — new directions). (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related:** the **smear** subsystem (`grain_smear.c`, the single resonant allpass cascade — this plan replicates its voice N times) and its **pitch destination** (`Plans/smear_pitch.md` / P1, DONE — supplies the `smear_pitch_control_t` note→Hz path + the `pitch_scale_t` chord/scale this plan reads per-voice); the **delay-mode selector** (`grain_delay_mode_t` + `ligase_delay_mode`, the canonical MODE-selector pattern this plan mirrors as `smear_mode 0/1`); the **pattern** subsystem (`Plans/pattern_pitch.md` / P3, DONE — a future bank-pitch source could ride a pattern slot per voice); the **fine-tune** rider (`Plans/pitch_fine_tune.md` / P3 — `smear_pitch_control.semitone_fine`, optionally applied to the whole bank).

> **PROVENANCE.** Every file:line below was verified by reading `src/grain_smear.c`, `src/grain_smear.h`, `src/ligase~.c`, `src/types.h`, and `src/grain.c` on 2026-06-24. This is a **forward-direction design plan** (scoping + architecture); no code is written. The smear-pitch destination (`smear_pitch_control_t`, the note→Hz path, the `smear_pitch_scale` loader) is **already in-tree and DONE** (P1, see `Plans/smear_pitch.md`) — this plan builds the BANK on top of that finished work, it does not re-derive it.

---

## Problem

**The direction.** Today the smear is a *single* tuned resonator: one stereo voice you can play at one pitch (P1 made it note-driven). The owner wants a **distinct new mode** in which a whole **BANK of N tuned resonators** is excited by the granular+delay output — the grains become the **exciter** (broadband noise, transients, clouds) and the bank of tuned voices becomes **the instrument**. Strike the body with grain dust and it rings a chord. This is the classic sympathetic-string / modal-resonator / excited-physical-body topology: a physical-modeling flavour layered on top of the existing granular engine.

**Owner intent (concrete).**

> "A distinct MODE where a bank of tuned resonators — extending the playable smear — is excited by the granular output. The grains are the exciter, the resonant body is the instrument. Pitches come from a chord/scale, the same note→Hz and scale machinery I already built for the smear. And I want Karplus-Strong weighed as an alternative voice."

Decomposed:

1. **A distinct mode, single stays default.** Add a `smear_mode 0/1` selector (single | bank) mirroring the `delay_mode 0/1/2` pattern (`ligase~.c:3431`). `smear_mode == SINGLE` (default) runs the **identical code path** as today — `grain_smear_process(x->smear, …)` at the head of `ligase_process_effects` (`ligase~.c:1538-1540`), byte-for-byte. The bank is reachable only when explicitly switched.
2. **Grains = exciter, bank = instrument.** In bank mode the granular+delay bus arriving at `ligase_process_effects` (`out_left`/`out_right`) is the **exciter**. N tuned voices each ring on that exciter; their (pre-scaled) wet sum is mixed back in place. No new audio source — the exciter *is* the existing engine output.
3. **Pitches from a chord/scale, reusing P1's machinery.** Each voice's center frequency comes from the **already-loaded** `smear_pitch_control.scale` (`pitch_scale_t`, `types.h:412`/`:452`, set by the `smear_pitch_scale` message at `ligase~.c:3634`) via the **exact same** note→Hz formula P1 uses: `hz = ref_hz * 2^(semitone/12)` (`ligase~.c:975`). N = the loaded scale's `count`. Chord = scale degrees. We do **not** invent a second pitch path.
4. **Each bank voice keeps the unity-gain topology.** Every voice is an allpass-cascade-plus-global-feedback (the `grain_smear` topology), so loop-gain magnitude == `|feedback|` and `|fb| < 0.99` is **unconditionally stable** (the `fmaxf(-0.99f, fminf(0.99f, fb))` clamp lives in `grain_smear_set_feedback`, `grain_smear.c:108` / clamp expr at `:110`; the header itself states "allpass cascade is exactly unity-gain, so any |fb|<1 is unconditionally stable", `grain_smear.h:6-7`). The only new gain risk is *summing* N near-unity wet voices, handled by a `1/N`-class pre-scale before the existing output clamp (`ligase~.c:1569`).
5. **Karplus-Strong as a weighed alternative voice.** A KS / waveguide string is the cited alternative voice type (cheaper per voice, more plucked/struck timbre, but needs a per-voice delay buffer + fractional-delay tuning). It is **scoped as a later extension (v2)**, behind a GATE-A decision; the **allpass-cascade bank is the lower-risk v1** because it reuses `grain_smear` verbatim with no buffer-length / tuning-resolution problem.

Honest scope statement up front: **this is a sizable feature.** It is staged — **v1 core** (an allpass-cascade bank that reuses `grain_smear`'s section math, tuned from the existing scale, gain-staged and stable) ships first; **v2 extensions** (Karplus-Strong voice type, per-voice damping/stages, per-voice pattern pitch, exciter-tap options) are explicitly deferred and gated.

## Mechanics / target surface — the EXISTING code this extends

All references verified 2026-06-24 against the named files.

### The single smear voice this plan replicates N times (`src/grain_smear.c`)

- **`grain_smear_process`** — `grain_smear.c:122`. The per-sample stereo engine. Dry-skip guard `mix < 0.0001f || stages <= 0` (`:126`). Per channel: `xl = dl + fb * s->fb_l` (`:139`), run `stages` allpass sections (`:140-142`), `s->fb_l = smear_flush(xl)` (`:143`), `left[i] = dry*dl + wet*xl` (`:144`). **This is the exact loop the bank replicates (or wraps N of).**
- **`smear_allpass`** — `grain_smear.c:114`. One 2nd-order allpass section, Direct Form I: 4 mul + 4 add per section per channel, with `smear_flush()` (`:41`) denormal/NaN kill on the `y` states. **CPU unit cost: bank ≈ `N_voices * stages * 8 flops * 2 channels` per sample**, plus one feedback MAC per voice per channel.
- **`smear_update_coeffs`** — `grain_smear.c:47`. Computes the **shared** `a1 = -2r·cos(w0)`, `a2 = r²` (`:56-57`) from `freq_hz` + `resonance`; clamps `f` to **`[20, 0.45·sr]`** (`:50-51`) and `r` to `[0, 0.999]` (`:54-55`). **THE sole freq/radius bounds owner.** Each bank voice needs its OWN `a1`/`a2` from its OWN `freq_hz` — but routed through this *same* clamp logic, never a duplicated/relaxed copy.
- **`grain_smear_create` / `grain_smear_destroy`** — `grain_smear.c:60` / `:74`. `calloc`-based alloc / `free` of one voice; `magic = SMEAR_MAGIC` (`:13`, `:63`) guards every entry point. Defaults: `freq_hz=800`, `resonance=0.7`, `stages=12`, `feedback=0.0` (`:66-69`). **Bank allocation must call these (or a dedicated bank-create) only on a message/dsp setter, never per-sample.**
- **`grain_smear_set_frequency` / `_set_resonance` / `_set_stages` / `_set_feedback` / `_set_mix`** — `grain_smear.c:89` / `:95` / `:101` / `:108` / `:84` (function entry points; the `|fb|<0.99` clamp expr is at `:110`). Allocation-free, `magic`-guarded, message/block-thread-safe coefficient setters. Per-voice tuning calls the freq setter (one `cosf` recompute) on the block thread, not per sample.
- **The `grain_smear_t` struct** — `grain_smear.c:15-39`. Holds per-voice: `a1,a2`, the four section-state arrays `xL1/xL2/yL1/yL2[GRAIN_SMEAR_MAX_STAGES]` (and the R-channel mirror), and the global feedback memory `fb_l,fb_r`. `GRAIN_SMEAR_MAX_STAGES = 48` (`:12`). Per-voice state does not interact — **N independent stable voices stay stable.**

### The effect chain this inserts into (`src/ligase~.c`)

- **`ligase_process_effects`** — `ligase~.c:1529`. Effect chain: Grains→Mix→Delay→[REC]→**SMEAR**→DISTORTION→MOOG→clamp. Smear is **first** (`:1538-1540`), in-place on `out_left`/`out_right`. The final per-sample clamp to `[-1,1]` + NaN/Inf flush is at **`ligase~.c:1569-1577`** — the backstop for any summed bank gain.
- **`x->smear` field + create / free** — field at `ligase~.c:204` (`grain_smear_t *smear;`); created `x->smear = grain_smear_create(48000)` at **`ligase~.c:5295`** with the alloc-failure check at `:5298-5304`; destroyed `if (x->smear) grain_smear_destroy(x->smear);` in `ligase_free` at **`ligase~.c:5229`**. **The bank's ownership field (`x->smear_bank`) slots in beside `x->smear` at `:204`, is created NULL, and is destroyed alongside the smear at `:5229`.**
- **Sample-rate propagation** — `ligase_set_sample_rate` at `ligase~.c:1781`; the smear line is `grain_smear_set_sample_rate(x->smear, sr)` at **`ligase~.c:1789`** (main thread, dsp graph locked, only on actual rate change). **The bank must propagate SR the same way here** (re-derive every voice's `a1/a2` at the new rate).

### The smear-pitch machinery the bank's voice tuning reuses (`src/ligase~.c`, `src/types.h`) — DONE by P1

- **The per-block smear-pitch resolver** — `ligase~.c:938-985`, inside the `if (x->smear)` stanza of `ligase_update_inlets` (the `if (x->smear)` block opens at `:913`). Resolves the source (SEMITONE/MIDI/SCALE/PATTERN) → a semitone, then `hz = sp->ref_hz * powf(2.0f, semitone / 12.0f)` (**`:975`**) → `grain_smear_set_frequency` (`:977`). Runs once per DSP block on the block thread, AFTER the pattern cache is fresh. **The bank tuning reuses this exact `powf` formula, once per voice, in (or beside) this stanza.**
- **`smear_pitch_control_t`** — `types.h:442-456`. Holds `ref_hz` (440, `:448`), `ref_note` (69, `:449`), `source` (`:444`), `enabled` (`:443`), the `pitch_scale_t scale` (`:452`), `semitone_fine` (`:455`). **The `scale` array IS the chord/scale that supplies the bank's per-voice pitches** — `scale.semitones[0..count-1]`.
- **`pitch_scale_t`** — `types.h:412-415`. `{ float semitones[MAX_SCALE_NOTES]; int count; }` where `MAX_SCALE_NOTES = 128` (`types.h:410`). Populated by the `smear_pitch_scale` message handler **`ligase_smear_pitch_scale`** at `ligase~.c:3634-3651` (validates `1..MAX_SCALE_NOTES` float args at `:3637`, stores into `smear_pitch_control.scale` at `:3647-3649`). **Reuse its `count` notes as the bank's N voice pitches.** Default `count = 0` (`grain.c:582`).
- **Init site** — `scheduler_create` sets the smear-pitch defaults at `grain.c:573-585` (A440 ref `:578`, `pattern_slot = -1` `:580`, `scale.count = 0` `:582`). **Any bank-control defaults that live on the scheduler go here; the bank module's own state defaults live in its `_create`.**

### The MODE-selector pattern this plan mirrors (`src/ligase~.c`, `src/types.h`)

- **`grain_delay_mode_t`** — `types.h:32-36`. `enum { DELAY_MODE_DD4, DELAY_MODE_BENCINA, DELAY_MODE_STUT }`. **The canonical mode enum to mirror** as `smear_mode_t { SMEAR_MODE_SINGLE, SMEAR_MODE_BANK }`.
- **`ligase_delay_mode`** — `ligase~.c:3431-3440`. `int m = (int)mode; if (m >= 0 && m <= 2) { setter; post(name); } else { pd_error(…); }`. **Copy this verbatim** into `ligase_smear_mode(ligase_t*, t_floatarg)` validating `0..1`.
- **Registration template** — `class_addmethod(ligase_class, (t_method)ligase_delay_mode, gensym("delay_mode"), A_DEFFLOAT, 0)` at **`ligase~.c:5522`**. The smear-method cluster is at **`ligase~.c:5546-5556`** (`smear_frequency`/`_resonance`/`_stages`/`_feedback`/`_pitch_source`/`_pitch_semitones`/`smear_note`/`_pitch_scale`/`_pitch_rand_type`/`_pitch_debug`/`_pitch_fine`). **`smear_mode` and the bank's control methods register right here.**

## Design

The design is split into a **v1 core** (allpass-cascade bank, ship first) and **v2 extensions** (Karplus-Strong, per-voice controls, deferred). Everything in v1 reuses `grain_smear`'s section math and P1's note→Hz path; nothing in v1 touches the single-smear code path.

### v1 — the allpass-cascade resonator bank

#### New module: `src/grain_smear_bank.c` / `.h`

A new self-contained module (mirroring `grain_smear.c`'s shape: `magic`-guarded, calloc-on-create, no per-sample allocation). It owns N voices. **Recommended representation (a): an array of `grain_smear_t*`** — reuse the *whole* `grain_smear` voice (struct + `smear_allpass` + `smear_flush` + the coefficient setters) verbatim, so there is zero re-implementation of the section math and the unity-gain/denormal guarantees come for free. (Alternative (b): a flat `grain_smear_bank_t` carrying N inlined coefficient/state sets — saves a layer of indirection and one `magic` check per voice, but duplicates the section loop. **Recommend (a) for v1; (b) is a v2 micro-opt if profiling demands it** — see GATE A.)

```c
// grain_smear_bank.h
#define GRAIN_SMEAR_BANK_MAX_VOICES 16   // hard cap (CPU + scale-count ceiling); GATE A.2

typedef struct grain_smear_bank grain_smear_bank_t;

grain_smear_bank_t *grain_smear_bank_create(int sample_rate, int max_voices);
void grain_smear_bank_destroy(grain_smear_bank_t *b);
void grain_smear_bank_set_sample_rate(grain_smear_bank_t *b, int sample_rate);

void grain_smear_bank_set_count(grain_smear_bank_t *b, int n);          // active voices (0..max)
void grain_smear_bank_set_voice_freq(grain_smear_bank_t *b, int i, float hz);
void grain_smear_bank_set_resonance(grain_smear_bank_t *b, float r);    // shared across voices (v1)
void grain_smear_bank_set_stages(grain_smear_bank_t *b, int stages);    // shared across voices (v1)
void grain_smear_bank_set_feedback(grain_smear_bank_t *b, float fb);    // shared across voices (v1)
void grain_smear_bank_set_mix(grain_smear_bank_t *b, float mix);        // bank dry/wet

// In-place stereo: out = (1-mix)*exciter + mix * (1/N) * sum_v(voice_v(exciter)).
void grain_smear_bank_process(grain_smear_bank_t *b, float *left, float *right, int n);
```

```c
// grain_smear_bank.c (sketch)
#define SMEAR_BANK_MAGIC 0xBA11C0DE
struct grain_smear_bank {
    unsigned int  magic;
    int           sample_rate;
    int           max_voices;
    int           count;                 // active voices (0..max_voices)
    float         mix;                   // bank dry/wet
    grain_smear_t *voices[GRAIN_SMEAR_BANK_MAX_VOICES];  // representation (a)
    float        *scratchL, *scratchR;   // per-voice exciter copy (sized to max block at create)
    float        *accL, *accR;           // wet accumulator (sized to max block at create)
    int           scratch_n;             // allocated scratch/accumulator length (samples)
};
```

**Why a scratch copy of the exciter.** Each voice must ring the *same* dry exciter, but `grain_smear_process` works **in place** (it overwrites its input with `dry*in + wet*xl`). So `_process` (a) snapshots `left`/`right` into `scratchL`/`scratchR` once, (b) for each voice copies the snapshot into a working buffer, runs that voice with `mix = 1.0` (pure wet — verified to clear the dry-skip guard at `grain_smear.c:126` and yield `dry*dl + wet*xl = xl`) — or, cleaner, runs the voice and reads its wet `xl` — and accumulates, (c) writes `out = (1-mix)*exciter + mix*(1/N)*sum` back into `left`/`right`. Two concrete implementation shapes:

- **Shape A (reuse `grain_smear_process` unchanged):** set each voice's internal `mix = 1.0` (pure wet) and `feedback`/`stages`/`a1,a2` per voice; for each voice memcpy `scratch → tmp`, call `grain_smear_process(voice, tmpL, tmpR, n)`, accumulate `tmp` into `accL/accR`. Then `out[i] = (1-bankmix)*scratch[i] + bankmix*(1/N)*acc[i]`. Cost: one extra memcpy + one accumulate add per voice per sample (cheap relative to `stages*8`). **No change to `grain_smear.c`.** This is the safest v1 — recommended.
- **Shape B (a thin per-voice wet helper):** add a `grain_smear_process_wet(voice, inL, inR, accL, accR, n)` that reads from `in*` and *adds* the wet result into `acc*` (no dry term, no in-place overwrite). Saves the per-voice memcpy and the separate dry term. Requires one small new function in `grain_smear.c` exposing the inner loop. **A v1.1 optimization** — start with Shape A.

**Allocation discipline.** `grain_smear_bank_create` allocates the `max_voices` array of `grain_smear_t*` (each via `grain_smear_create`) and the scratch/accumulator buffers (sized to a generous max block, e.g. 8192 samples, reallocated in `set_sample_rate`/`dsp` if the block grows). `set_count(n)` only changes the *active* count `count` (0..max_voices) — **it does not allocate** (all voices pre-allocated at create). Per-sample/per-block code only reads pre-allocated state and writes coefficients. `magic`/NULL validated at the top of every entry point exactly as `grain_smear.c` does. **All `_create`/`set_sample_rate` calls happen on the main/dsp thread (mirroring `x->smear`'s create at `ligase~.c:5295` and SR-prop at `:1789`), never from the audio callback.**

#### Gain staging (stability)

Each voice keeps the allpass-cascade + global-feedback topology → loop-gain magnitude == `|feedback|`, so the existing `|fb| < 0.99` clamp (`grain_smear_set_feedback`, entry `grain_smear.c:108` / clamp expr `:110`) makes each voice **unconditionally stable** (the header asserts the cascade is exactly unity-gain, `grain_smear.h:6-7`). The bank pre-scales the voice sum by **`1/N`** before mixing with dry (`out = (1-mix)*exciter + mix*(1/N)*sum`), so N near-unity wet voices do not constantly slam the output clamp at `ligase~.c:1569`. **`1/N` vs `1/sqrt(N)` is a GATE-A decision** (`1/N` is conservative / quietest; `1/sqrt(N)` assumes decorrelated voices and stays louder but can clip on correlated transients). The final `[-1,1]` clamp (`ligase~.c:1569-1577`) remains the hard backstop regardless.

#### State on `ligase_t` + mode selector

- **New mode enum** (near `grain_delay_mode_t` at `types.h:32`):
  ```c
  typedef enum { SMEAR_MODE_SINGLE, SMEAR_MODE_BANK } smear_mode_t;
  ```
- **New fields on `ligase_t`** (beside `x->smear` at `ligase~.c:204`):
  ```c
  int                 smear_mode;   // smear_mode_t; default SMEAR_MODE_SINGLE
  grain_smear_bank_t *smear_bank;   // NULL until bank mode allocates it
  ```
  `smear_mode` defaults to `SMEAR_MODE_SINGLE` (set in `ligase_new`, mirroring the other scalar inits ~`ligase~.c:5306+`). `smear_bank` is created at `ligase_new` (`:5295`, beside `x->smear = grain_smear_create(48000)`) so the buffers exist before any mode switch — or lazily on the first `smear_mode 1` (GATE A.4: **eager-create recommended** — bank create is a few KB + N small structs, and lazy-create-in-message-handler is fine but eager avoids a NULL branch in the hot path). Destroyed in `ligase_free` (`:5229`) beside `grain_smear_destroy(x->smear)`.

- **`smear_mode` message** (mirror `ligase_delay_mode` at `:3431`, register beside the smear cluster at `:5546-5556`):
  ```c
  static void ligase_smear_mode(ligase_t *x, t_floatarg mode) {
      int m = (int)mode;
      if (m >= 0 && m <= 1) {
          x->smear_mode = m;
          const char *names[] = {"single (resonator)", "bank (excited body)"};
          post("ligase~: smear mode set to %d (%s)", m, names[m]);
      } else {
          pd_error(x, "ligase~: invalid smear mode %d (use 0=single, 1=bank)", m);
      }
  }
  // class_addmethod(ligase_class, (t_method)ligase_smear_mode, gensym("smear_mode"), A_DEFFLOAT, 0);
  ```

#### Chain insertion (the exciter tap)

In `ligase_process_effects` (`ligase~.c:1538-1540`), branch on `x->smear_mode`:

```c
    if (x->smear_mode == SMEAR_MODE_BANK && x->smear_bank) {
        grain_smear_bank_process(x->smear_bank, out_left, out_right, n);  // grains = exciter, bank = body
    } else if (x->smear) {
        grain_smear_process(x->smear, out_left, out_right, n);            // UNCHANGED single path
    }
```

`smear_mode == SINGLE` (default) runs the **identical** `grain_smear_process` call as today. The bank treats `out_left`/`out_right` (the granular+delay bus) as the exciter, runs N voices, writes the summed pre-scaled wet+dry in place. Smear stays the **first** effect; the final clamp (`:1569`) still backstops the summed gain.

#### Per-voice tuning from the existing scale (the chord)

In the block-rate smear stanza (`ligase~.c:938`, alongside the existing P1 resolver), add a bank branch gated on `smear_mode == BANK`:

```c
    if (x->smear_mode == SMEAR_MODE_BANK && x->smear_bank) {
        smear_pitch_control_t *sp = &x->scheduler->smear_pitch_control;
        int count = sp->scale.count;
        if (count > GRAIN_SMEAR_BANK_MAX_VOICES) count = GRAIN_SMEAR_BANK_MAX_VOICES;
        grain_smear_bank_set_count(x->smear_bank, count);
        for (int i = 0; i < count; i++) {
            float semitone = sp->scale.semitones[i] + sp->semitone_fine;   // reuse P1 scale + fine
            float hz = sp->ref_hz * powf(2.0f, semitone / 12.0f);          // SAME formula as :975
            grain_smear_bank_set_voice_freq(x->smear_bank, i, hz);         // routes through smear_update_coeffs clamp
        }
    }
```

This reuses `sp->scale` (loaded by the existing `smear_pitch_scale` message at `:3634`), the exact `powf` note→Hz at `:975`, and (optionally) the P1 `semitone_fine` to detune the whole body. **No new pitch path, no new scale loader** — the bank's pitches ARE the smear-pitch scale's degrees. Each voice's freq goes through `grain_smear_bank_set_voice_freq` → the per-voice `grain_smear_set_frequency` → `smear_update_coeffs`'s `[20, 0.45·sr]` clamp, so a high chord degree can't push `w0` past Nyquist. Updating coefficients once per block (not per sample) means no sub-block coefficient thrash. (Note: this bank branch reads `sp->scale` directly and is independent of the existing `if (sp->enabled)` single-voice resolver at `:940`, so loading a chord and enabling the single-voice pitch source are decoupled.)

> **Tuning the bank to a chord, in practice:** `smear_pitch_scale 0 4 7 12` then `smear_mode 1` → a 4-voice bank ringing root / major-3rd / 5th / octave above `ref_hz` (440 Hz default). Change the scale, the bank re-tunes next block. `smear_mode 0` returns to the single voice.

#### Bank control messages (v1)

Shared (not per-voice) controls in v1, mirroring the single-smear setters, registered beside the smear cluster (`:5546-5556`):

- `smear_bank_resonance <r>` → `grain_smear_bank_set_resonance` (shared pole radius / ring length, 0..0.999).
- `smear_bank_stages <n>` → `grain_smear_bank_set_stages` (shared stages per voice; **default LOWER than the single smear**, e.g. 8–12, to keep `N*stages` in budget — see CPU below).
- `smear_bank_feedback <fb>` → `grain_smear_bank_set_feedback` (shared sustain/decay, -0.99..0.99).
- `smear_bank_mix <0..1>` → `grain_smear_bank_set_mix` (bank dry/wet). (Could alternatively reuse inlet 15's smear-mix value — confirmed: inlet 15 = smear mix, driven via `grain_smear_set_mix` at `ligase~.c:679` — so the bank tracks the single smear's mix knob — GATE A.5.)

Per-voice resonance/stages/detune are **v2** (below). N (voice count) is **not** a message — it is driven by `scale.count` so the chord defines the bank size; a separate cap message is unnecessary (the `GRAIN_SMEAR_BANK_MAX_VOICES` constant + the clamp above bound it).

### v2 — deferred extensions (gated, not built in v1)

- **Karplus-Strong / waveguide voice type.** A `smear_bank_voice_type 0/1` (allpass | KS) selector. A KS voice is a per-voice delay line of length `sr/freq_hz` + a one-pole lowpass loop filter + fractional-delay interpolation for accurate tuning. **Cheaper per voice** (one delay read/write + one filter MAC) but needs: a per-voice delay buffer sized to the **lowest** pitch (largest period), allpass fractional-delay tuning (else pitch quantizes to integer-sample periods), and its own denormal/excitation gating. **Strictly v2** — it is a second voice engine, not a reuse of `grain_smear`. The allpass bank is the v1 because it has no buffer-length / tuning-resolution issue and reuses `grain_smear` verbatim. **GATE A.6** decides whether v2 is greenlit and which timbre the owner prefers after ear-testing v1.
- **Per-voice resonance / stages / damping.** Voice `i` brighter/longer than voice `j` (e.g. lower notes ring longer — physical). Needs per-voice setters and a control surface (a list message `smear_bank_voice_res <i> <r>`, or a curve). v2.
- **Per-voice / per-degree pattern pitch.** Drive bank degrees from a pattern slot (the P3 machinery) rather than a static chord — an arpeggiating body. v2; reuses `pattern_table_t` (`step_count` at `types.h:480`, `cached_value` `:486`, `cached_is_rest` `:487`; `PATTERN_SLOTS=8` `:465`) but needs a per-voice slot policy.
- **Exciter-tap options.** Tap the exciter pre-delay vs post-delay, or high-pass the exciter so only transients excite the body. v2.
- **Representation (b) flat bank + Shape B wet helper** — the CPU micro-opts above, only if v1 profiling shows the bank is the budget bottleneck.

## Steps & gates

### GATE A (approval) — open design decisions for owner sign-off

This is a direction plan, so GATE A is substantial. Recommendations are flagged **[R]**.

1. **Voice engine for v1.** Allpass-cascade bank (reuse `grain_smear` verbatim, representation (a) + Shape A) **[R]** vs. start directly on Karplus-Strong. Recommend the allpass bank: zero new DSP math, inherits unity-gain stability + denormal flushing, no buffer-length/tuning issue. KS is weighed as **v2** (decision 6).
2. **Max voice count `GRAIN_SMEAR_BANK_MAX_VOICES`.** Recommend **16** as the hard cap, with the *practical* musical N driven by `scale.count` (a 3–6-note chord is typical). 16 voices × 8 stages × 2 ch × 8 flops ≈ 2048 flops/sample — see CPU note. Confirm the cap, or set lower (e.g. 8) to be conservative.
3. **Default stages-per-voice in bank mode.** Recommend **8** (vs the single smear's default 12), so `N*stages` stays bounded; a denser body costs more. Owner may prefer a different default ring density. **[R] 8.**
4. **Bank allocation timing.** Eager-create `x->smear_bank` in `ligase_new` (buffers ready, no hot-path NULL branch) **[R]** vs. lazy-create on first `smear_mode 1`. Eager costs a few KB + N small structs always; lazy keeps the default-single instance leaner. Recommend eager for simplicity.
5. **Bank dry/wet source.** Dedicated `smear_bank_mix` message **[R]** vs. reuse inlet-15's smear-mix value (bank tracks the single smear's mix knob, one fewer control). Recommend a dedicated message so the bank's wetness is independent of the single voice's; the owner may prefer the shared knob for a smaller control surface.
6. **Voice-sum pre-scale.** `1/N` (conservative, quietest, never clips on correlated transients) **[R]** vs. `1/sqrt(N)` (louder, assumes decorrelation, can clip — backstopped by the `:1569` clamp). Recommend `1/N` for v1; revisit after ear-test.
7. **v2 greenlight + timbre preference.** Is Karplus-Strong (and per-voice damping / pattern-pitch) in scope at all, and does the owner want to A/B the allpass-body timbre vs a KS-string timbre before committing v2? Recommend: **ship + ear-test v1 first, then decide v2 from the actual sound.** This is the "weigh KS as an alternative" item — the weighing happens *after* v1 exists to compare against.
8. **Mode-switch click policy.** On `smear_mode` toggle, do we crossfade single↔bank over a few ms, or hard-switch and accept a possible click? Recommend **hard-switch for v1** (simplest; the exciter is continuous so the discontinuity is small), crossfade as a v2 nicety if it clicks audibly.

### Step 1 → GATE B (mode scaffold, no bank DSP)

Add `smear_mode_t` (`types.h:32` region), the `int smear_mode` + `grain_smear_bank_t *smear_bank` fields on `ligase_t` (`:204`), default `smear_mode = SMEAR_MODE_SINGLE` in `ligase_new`, and the `ligase_smear_mode` message (mirror `:3431`) + registration (`:5546-5556`). Stub `grain_smear_bank.h`/`.c` with create/destroy/process that, for now, leaves the bank a no-op (or NULL). The `ligase_process_effects` branch (`:1538`) routes `BANK → bank_process`, `SINGLE → grain_smear_process` unchanged. **GATE:** `make clean && make` warning-free; `smear_mode 0` is byte-for-byte today (single path untouched); `smear_mode 1` does not crash (bank no-op); `smear_mode 2` errors.

### Step 2 → GATE C (bank module: N voices, summed, gain-staged)

Implement `grain_smear_bank.c` (representation (a), Shape A): N pre-allocated `grain_smear_t*` voices, scratch + accumulator buffers sized at create, `set_count`/`set_voice_freq`/`set_resonance`/`set_stages`/`set_feedback`/`set_mix`, and `_process` doing the exciter-snapshot → per-voice-wet → `1/N` sum → dry/wet mix. `magic`/NULL guards on every entry; **all allocation in `_create`, none in `_process`**. Wire `grain_smear_bank_set_sample_rate` into `ligase_set_sample_rate` (`:1789` region). **GATE:** `make clean && make` warning-free; with a fixed chord and feedback, the bank rings N distinct partials (verified by FFT, AC2); no allocation in the perform path (audio-thread-safe); summed output stays in `[-1,1]` (AC4); a decaying tail does not pin CPU (denormal flush per voice, AC5).

### Step 3 → GATE D (per-voice tuning from the scale)

Add the bank tuning loop in the smear stanza (`:938` region), gated on `smear_mode == BANK`: read `smear_pitch_control.scale` (loaded by the existing `smear_pitch_scale` message), set `count = min(scale.count, MAX_VOICES)`, and per voice compute `hz = ref_hz * 2^((semitone+fine)/12)` (reuse `:975`) → `set_voice_freq`. Add the v1 control messages (`smear_bank_resonance`/`_stages`/`_feedback`/`_mix`) + registration. **GATE:** `make clean && make` warning-free; `smear_pitch_scale 0 4 7 12` + `smear_mode 1` tunes the bank to that chord (FFT peaks at `440·2^{0,4,7,12 /12}` ≈ 440 / 554.4 / 659.3 / 880 Hz, AC1); changing the scale re-tunes next block; extreme degrees clamp at `0.45·sr` via the unmodified `smear_update_coeffs` (AC3).

### Step 4 → GATE E (verify + ear-test)

Build; run the headless acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1`), recording the bank output via `writesf~` and measuring FFT peaks. Confirm the no-regression criteria (single mode byte-identical, `test_delay.pd` clean). **Then an explicit ear-test** of the excited body (the timbre is the deliverable and is subjective — see Acceptance). Update the manual's smear section to document `smear_mode`, the bank controls, and the grains-as-exciter model. **This GATE also produces the v1-vs-KS comparison input for GATE A.7.**

## Acceptance criteria

Headless where possible (`pd -nogui -nosound -stderr -path . <patch>.pd`): record broadband noise into the reel, drive dense grains so the granular bus is a rich exciter, enable `smear_mode 1` with feedback high enough for audible ring, capture `out_left`/`out_right` via `writesf~`, and FFT the WAV.

1. **Bank tunes to the chord/scale.** `smear_pitch_scale 0 4 7 12` + `smear_mode 1` → the captured spectrum shows resonant peaks at **≈ 440, 554.4, 659.3, 880 Hz** (`ref_hz·2^(degree/12)`), within the resonator's bandwidth tolerance — i.e. N=4 voices each ring at their degree. Reuses P1's note→Hz and the existing `smear_pitch_scale` loader. *(headless, FFT)*
2. **Grains excite the body.** With the granular engine **silent** (no grains firing) the bank output is silent/decayed (the exciter is the only energy source); with grains firing the tuned peaks appear and sustain per the feedback. Confirms the excited-resonator model (grains = exciter, not an independent oscillator). *(headless: compare silent-exciter vs active-exciter captures)*
3. **Extreme degrees clamp, no instability.** `smear_pitch_scale -60 60` at a low sample rate where `440·2^{60/12}` exceeds `0.45·sr`: the high voice clamps to `0.45·sr` and the low to ≥20 Hz, no NaN/Inf, no runaway — confirming the **sole** clamp in `smear_update_coeffs` (`grain_smear.c:50-51`) governs every voice and is not duplicated/relaxed. *(headless: assert finite output + peak ≤ 0.45·sr)*
4. **Summed gain stays bounded + stable.** With N=8 voices at high shared feedback (e.g. 0.95) on a loud exciter, the captured output stays within `[-1,1]` (the `1/N` pre-scale + the `:1569` clamp) and does not diverge over a 10 s capture (every voice keeps `|fb|<0.99` → unconditionally stable). *(headless: assert max|sample| ≤ 1.0 and bounded RMS over time)*
5. **No CPU pathology on decaying tails / denormals.** After the exciter goes silent, the bank's decaying tails flush to zero (per-voice `smear_flush`) — measured by a stable/declining CPU and no self-sustaining garbage in the capture (silence floor, not a noise floor that grows). *(headless: capture post-exciter tail, assert it decays to ≈0; CPU observation is qualitative)*
6. **No regression in the single smear.** `smear_mode 0` (default): manual `smear_frequency 800` + `smear_feedback` rings exactly as today; a patch that never sends `smear_mode`/`smear_bank_*` is **byte-for-byte identical** to current behavior (single voice, mix, feedback, stages unchanged). `make clean && make` warning-free; `test_delay.pd` clean. *(headless + build gate)*
7. **CPU budget honored.** At the chosen defaults (N≤16, bank stages 8), the bank adds CPU proportional to `N·stages·16` flops/sample and stays within an audible-without-dropout budget at 48 kHz / 64-sample blocks on the target headless-0 hardware. *(measured: report added % DSP load vs single smear; a regression here re-opens GATE A.2/A.3)*
8. **Subjective timbre (ear-test, NOT headless).** The excited body sounds like a *resonant instrument struck by grain dust* — a recognizable chord/body ring, not a ring-modulator buzz or a comb-filter artifact. **This is the load-bearing aesthetic acceptance and requires an owner ear-test** (it is the input to the GATE A.7 v1-vs-Karplus-Strong decision). No automated proxy substitutes for it.

## Risks / out-of-scope

**Risks**

- **CPU blow-up.** Single-voice cost is `stages(≤48)·8 flops·2 ch` per sample + a feedback MAC; the bank multiplies by N. 8 voices × 24 stages × 2 ch ≈ 3072 flops/sample is already heavy; 16 × 48 would be ruinous. **Mitigated** by capping N (`GRAIN_SMEAR_BANK_MAX_VOICES`), defaulting bank stages **lower** than the single smear (8 vs 12), and the AC7 budget gate. If the budget still fails, the Karplus-Strong voice (one delay + one filter MAC per voice) becomes the v2 escape hatch. **The plan stages this explicitly — do not ship a 16×48 bank.**
- **Summed-gain clipping.** N near-unity wet voices can exceed `[-1,1]`. **Mitigated** by the `1/N` pre-scale (GATE A.6) + the existing `:1569` clamp as a hard backstop. Correlated transients (all voices excited by the same grain hit) are the worst case — `1/N` (not `1/sqrt(N)`) is the safe default for v1.
- **Audio-thread allocation.** Resizing per voice-count or scale change on the audio thread would be a bug. **Mitigated** by pre-allocating `max_voices` voices + scratch/accumulator at `_create` (message/dsp thread only, mirroring the `x->smear` create at `:5295` and SR-prop at `:1789`); `set_count` only flips the active count, never allocates; `_process` reads pre-allocated state only. `magic`/NULL guard every entry, exactly as `grain_smear.c`.
- **Mode-switch / re-tune clicks.** Toggling `smear_mode` or re-tuning a voice mid-ring can click. **Mitigated** for v1 by accepting a hard switch (GATE A.8) — the exciter is continuous so the discontinuity is small; a crossfade is a v2 nicety if it audibly clicks. (Note: a voice that goes inactive via `set_count` and later re-activates retains its prior feedback/section state — a benign transient absorbed by `smear_flush` + the continuous exciter; not a stability hazard.)
- **Scale-count = 0 footgun.** If `smear_mode 1` is set with no `smear_pitch_scale` loaded (`scale.count == 0`, the default), the bank has N=0 voices → it passes the exciter through dry (degrade, never crash). The mode setter should `post` a hint ("bank mode: load a smear_pitch_scale to tune the voices") — same pattern as the P1 `pattern smear_pitch` hint at `ligase~.c:3202`.
- **Stability is per-voice, not automatic for the sum.** Each voice is unconditionally stable (`|fb|<0.99`, unity-gain cascade), but the *design* must keep that topology — **do NOT** substitute resonant biquads / peaking filters (gain > 1 at resonance) for the voices without explicit normalization. The plan's reuse of `grain_smear` voices verbatim is what preserves this; representation (b) must replicate the exact section math.

**Out of scope (v1)**

- **Karplus-Strong / waveguide voice type** — v2 (GATE A.6/A.7); it is a second voice engine (per-voice delay buffer + fractional-delay tuning + loop filter), not a reuse of `grain_smear`. v1 ships the allpass bank as the lower-risk first cut and the comparison baseline.
- **Per-voice resonance / stages / damping / detune** — v1 shares these across voices; per-voice control surfaces are v2.
- **Per-voice / per-degree pattern pitch** (arpeggiating body via the P3 pattern slots) — v2; v1 tunes from the static `scale` chord.
- **Exciter-tap options** (pre/post-delay tap, high-passed transient-only excitation) — v2.
- **Touching the single-smear code path** — `grain_smear_process(x->smear, …)` at `ligase~.c:1538-1540` and the entire `grain_smear.c` single-voice path must remain byte-for-byte unchanged when `smear_mode == SINGLE` (the bank is purely additive; Shape A adds no new function to `grain_smear.c`).
- **Modifying or duplicating the `[20, 0.45·sr]` clamp** in `smear_update_coeffs` (`grain_smear.c:47`) — strictly forbidden; every bank voice routes its freq through that single bounds owner.
- **A second pitch path / scale loader for the bank** — the bank's pitches ARE the smear-pitch `scale` (loaded by `smear_pitch_scale`, `ligase~.c:3634`) via the existing `ref_hz·2^(semitone/12)` formula (`:975`); inventing a parallel tuning path is out of scope.