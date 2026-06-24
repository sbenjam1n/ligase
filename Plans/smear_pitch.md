# Plan P1: SMEAR pitch destination — note→Hz controller with off / fixed-semitone / (scale) / pattern sources

**Owner:** SLB
**Date:** 2026-06-24
**Status:** ✅ DONE (2026-06-24) — implemented and headless-verified; `make clean && make` warning-free at every gate; no regression in the existing audio path or `smear_frequency`/`pattern pitch`. Full source menu (off/semitone/scale/MIDI-via-`smear_note`/pattern) shipped incl. the de-static'd SCALE source. See Progress.
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — pitch-destination build-out). (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related:** Plan P2 `Plans/midi_channel_routing.md` (the channel-aware `midi <note> [vel] [channel]` ingress that *feeds* the `source=MIDI` field this plan reserves but does not drive — P2 **depends on** P1). The **smear** section (`ligase_smear_frequency` + the per-block `smear_frequency_range` modulation at `ligase~.c:908-925`), the **pitch** section (the grain `pitch_control_t` + `PITCH_MODE_*` machinery this plan deliberately does NOT reuse), and the **pattern** section (`Plans/pattern_pitch.md` / P3, whose `pattern <target>` handler + degree→scale→semitone math this plan mirrors for a second destination). This is the first of a two-plan arc that splits "MIDI + modulation for the resonator pitch" from the existing grain pitch: **P1 builds the smear destination + its non-MIDI sources; P2 adds the channel-aware MIDI routing across both destinations.**

> **ADVERSARIAL VERIFICATION (2026-06-24).** All load-bearing file:line refs and symbols were re-read in `src/types.h`, `src/grain.c`, `src/ligase~.c`, `src/grain_smear.c`/`.h` and confirmed REAL and accurate, with three corrections folded in below (flagged ⟦V⟧): (1) the `scheduler_t` member-placement note was wrong — `pitch_control` is **not** the last member before the brace (see Data structures); (2) the optional SCALE source's `sample_scale_semitones` is **`static`** in `grain.c` and cannot be called from `ligase~.c` as written — needs de-static-ing/wrapper (SCALE is deferred, so lean P1 is unaffected); (3) slot-6 "reservation" via the two `pattern_alloc_param_slot` guards is only partial — the `pattern_N` manual instance-bind bypasses it (same as slot 7 today). MIDI-channel handling, the note→Hz math + clamp, backward-compat, and block-thread/slot threading all verified sound. Baseline `make clean && make` is warning-free (`-Wall -O2`), so the build gates are grounded.

---

## Progress (2026-06-24) — IMPLEMENTED + VERIFIED

Built across the five gates; `make clean && make` warning-free at each; no regression (`test_delay.pd` clean).
- **Step 1 (types):** `smear_pitch_source_t` enum + `smear_pitch_control_t` (a dedicated controller, NOT `pitch_control_t`) in `types.h`; `smear_pitch_control` member on `scheduler_t`; A440 musical defaults (`ref_hz=440`, `ref_note=69`, `pattern_slot=-1`, `enabled=0`) in `scheduler_create`.
- **Step 2 (per-block apply + override):** de-static'd `sample_scale_semitones` (+ extern in `ligase~.c`) so the SCALE source links; the source→semitone→Hz branch in the smear stanza (`ref_hz·2^(semitone/12)` → `grain_smear_set_frequency`); **override** = the `smear_frequency_range` branch is gated on `!smear_pitch_control.enabled` (true bypass, consistent with grain's `speed_range` bypass); clamp stays solely in `smear_update_coeffs`; rest/no-note holds the previous Hz.
- **Step 3 (messages):** `smear_pitch_source <0-4>`, `smear_pitch_semitones` (auto-selects SEMITONE), `smear_note <note> [ref_note] [ref_hz]`, `smear_pitch_scale`, `smear_pitch_rand_type` (+ `smear_pitch_debug`).
- **Step 4 (pattern target):** slot 6 reserved (param auto-allocator shrunk to 0..PATTERN_SLOTS-3); `pattern smear_pitch` dispatch + commit (`SMEAR_PITCH_PATTERN`) + `pattern_clear smear_pitch`, mirroring the grain `pattern pitch` path with per-block wrap+octave reads.
- **Step 5 (verify):** headless patches (`tests/pattern/SM*.pd`), asserting the resolved Hz via the `smear_pitch_debug` stderr trace.

| AC | Test | Result |
|----|------|--------|
| Fixed semitone | `smear_pitch_semitones 0 / 12 / -12` | 440 / 880 / 220 Hz ✓ |
| MIDI note (A440 ref) | `smear_note 60 / 69` | 261.63 / 440 Hz (middle C / A4) ✓ |
| Pattern (slot 6, wrap+octave) | `pattern smear_pitch [ 0 4 7 ]` on scale `0 2 4 5 7 9 11` | 440 / 659.26 / 880 Hz, tempo-locked ✓ |
| SCALE (random) | `smear_pitch_source 2` + scale | random picks land on scale notes (493.88/554.37/587.33/659.26/739.99/830.61) ✓ |
| Override | `smear_frequency_range 200-2000` enabled + `smear_pitch_semitones 0` | resonator pinned to 440 (range bypassed) ✓ |
| Backward compat | `smear_pitch_source 0` | trace stops; manual/modulated `smear_frequency` resume ownership ✓ |

**Carried forward:** the `source=MIDI` field is fed channel-free by `smear_note` in P1; the channel-aware `midi` ingress is **P2**. The ±50-cent fine-tune (`smear_pitch_fine`) is **P3** (rides this stanza's `semitone`). `smear_pitch_range` was dropped (functionally unused — `sample_scale_semitones` ignores min/max).

## Problem

The owner wants the synth to have **two independent pitch destinations**, each with a selectable source, so the resonator (smear) can be *played* like an instrument alongside the grains — in their words:

> "The smear box rings at a pitch. I want to drive that pitch from a note, not just a raw Hz number — a fixed transpose, or a mini-notation pattern stepping a scale, the same way the grains can. And I want to be able to run a scale/pattern on one destination while MIDI plays the other: same MIDI channel = unison (both follow the one note), different channels = they're separate. Whatever I had before — `smear_frequency` and the smear frequency modulation — has to keep working exactly as it does now if I don't touch any of the new stuff."

Decomposed into this slice (P1 — the smear destination and its off/fixed/(scale)/pattern sources; the channel-routed MIDI ingress is P2):

1. **Second pitch destination.** Smear/resonator pitch becomes an addressable destination with its own small controller, **independent of the shared grain `pitch_control_t`** — emitting **Hz** (`ref_hz · 2^((note − ref_note)/12)`), not a grain-playback speed ratio.
2. **Sources off / fixed-semitone / pattern (and optional scale).** A `smear_pitch_source` menu picks the producer: OFF (backward-compat, manual `smear_frequency` owns the freq), SEMITONE (fixed transpose), PATTERN (mini-notation scale-degree stepper, dedicated slot), and an optional stochastic SCALE source (GATE A). A `source=MIDI` field is reserved and fed by P2.
3. **Either destination runs a scale/pattern while the other takes MIDI.** Because the smear source is independently selectable from the grain pitch source, smear-on-PATTERN while grain-on-MIDI (or vice versa) "just works" — this plan delivers the smear half; P2 wires the channel routing that makes the MIDI half channel-aware (same channel = unison, different = separate).
4. **Backward compat is load-bearing.** With the smear pitch controller disabled (default), the existing manual `smear_frequency <hz>` message and the per-block `smear_frequency_range` modulation behave bit-for-bit as today.

This plan is the **smear-destination slice**: add a minimal `smear_pitch_control_t`, a note→Hz conversion applied once per block in the existing smear sampling stanza, three control messages, and a `pattern smear_pitch …` target on a dedicated pattern slot — reusing the proven P3 degree→scale→semitone math but emitting Hz through the existing `grain_smear_set_frequency` (whose `[20, 0.45·sr]` clamp is the sole bounds owner).

## Mechanics / target surface — the EXISTING code this extends

**Provenance note:** every reference below was verified by reading `src/types.h`, `src/grain.c`, `src/ligase~.c`, and `src/grain_smear.c` on 2026-06-24 (and re-verified adversarially the same day). The pattern/pitch infrastructure (P1+P2+P3 of the pattern subsystem) is **already merged and in-tree** — `pattern_table_t`, `PATTERN_SLOTS`, `ligase_pattern`, `pattern_eval_slot`, `cached_value`/`cached_is_rest`, `PITCH_MODE_PATTERN` all exist and are referenced live below.

### Smear (resonator) pitch — the destination this plan adds a note interface to

- **`grain_smear_set_frequency`** — `src/grain_smear.c:89`. `s->freq_hz = hz; smear_update_coeffs(s);` — the **single mutation point** for resonator pitch. Allocation-free, `magic`-guarded (`src/grain_smear.c:90`), safe from the message/block thread, **not** per-sample (it recomputes `cosf`).
- **`smear_update_coeffs`** — `src/grain_smear.c:47-58`. THE freq→coefficient math: clamps `f` to **`[20, 0.45·sr]`** (`:50-51`), `w0 = 2π·f/sr`, `a1 = -2r·cos(w0)`, `a2 = r²`. **The clamp lives ONLY here** — the note→Hz path feeds raw Hz and reuses this clamp; it does NOT re-clamp.
- **The per-block smear sampling stanza** — `src/ligase~.c:908-925`, inside `ligase_update_inlets` (defined `:392`). Under `if (x->smear)` it samples `smear_frequency_range` and calls `grain_smear_set_frequency` (**`:909-911`**), then `smear_resonance_range`/`smear_stages_range`/`smear_feedback_range` (`:913-924`). This runs on the block thread, **once per DSP block**, and `ligase_update_inlets` is called at **`src/ligase~.c:1621`**, which is **AFTER** `pattern_eval_slot` (called `:1611`) — so any pattern cache read here is fresh this block. **This is the single injection point for the note→Hz override.** ⟦V: ordering 1611 < 1621 re-verified — the slot-6 cache is fresh when the smear stanza reads it.⟧
- **`ligase_smear_frequency`** — `src/ligase~.c:3436-3438`. Manual `smear_frequency <hz>` → `grain_smear_set_frequency`. Runs on the Pd message thread. The new note path must coexist with this (precedence below).
- **`smear_current` mirror field** — `src/ligase~.c:318` (tracks inlet-15 smear *mix*, distinct from freq). The state-query path: `get_current_value("smear")` at `:4474`, query `post` at `:4593-4594`. A `last_hz` mirror for smear pitch slots in next to this for the state dump.
- **smear ctor + registration anchors** — `x->smear = grain_smear_create(48000)` at `src/ligase~.c:4969`; smear message registrations (`smear_frequency`/`_resonance`/`_stages`/`_feedback`) at `src/ligase~.c:5214-5217`. New `smear_pitch_*` / `smear_note` handlers register right here. (Note: `get_param_range_by_name`, `ligase~.c:256-259`, recognizes `smear_frequency/_resonance/_stages/_feedback` but NOT `smear_pitch` — confirming the `pattern smear_pitch` dispatch branch MUST precede the generic param branch, else it falls through to "unknown parameter".)

### Grain pitch + pattern infrastructure this plan MIRRORS but does NOT reuse

- **`pitch_control_t`** — `src/types.h:416-426`. The single shared **grain-speed** controller. Its fields (`semitones`, `semitone_range`, `scale`, `midi_note`/`midi_enabled`, `last_semitone`, `pitch_pattern_slot`) all encode grain-playback-speed / scale-degree / change-detection semantics, and `midi_note` is already owned by the signal-inlet writer (`src/ligase~.c:495-503`, P2 territory). **HARD CONSTRAINT: do NOT reuse this struct for smear.** Smear gets its own minimal `smear_pitch_control_t`.
- **`pitch_control` member on the scheduler** — `src/types.h:650`. ⟦V: the plan previously called this "the last functional member before the closing `} scheduler_t;` at `types.h:659`" — that is FALSE: `pan_mode` (`:653`), `delay_stut` (`:656`), and `delay_bencina` (`:657`) all sit between `pitch_control` and the closing brace at `:659`. The closing brace line (`:659`) is correct. **Place the new `smear_pitch_control_t` member adjacent to `pitch_control` at `:650`** — anywhere before the brace at `:659` compiles, but keeping it next to `pitch_control` is clearest.⟧
- **`scheduler_create` pitch init** — `src/grain.c:553-563` (after the zeroing `memset(sched, 0, sizeof(scheduler_t))` at `:486`). `pitch_control` defaults are set explicitly here, including `pitch_pattern_slot = -1` (`:563`) "because 0 is a valid slot." The new smear-pitch defaults go in this same block.
- **`PITCH_MODE_PATTERN` grain stepper (the math to mirror)** — `src/grain.c:811-833` in `scheduler_trigger_grain`. Reads `pattern[slot].cached_value` as a scale degree and does **wrap + octave**: `idx = ((degree % count) + count) % count` (`:825`), `oct = (int)floorf((float)degree / (float)count)` (`:826`), `current_semitone = scale.semitones[idx] + 12·oct` (`:827`); on `cached_is_rest` it holds `last_semitone` (`:821-822`). The smear per-block reader copies this wrap+octave verbatim, then emits **Hz = `ref_hz · 2^(semitone/12)`** instead of `semitones_to_speed` (a speed ratio).
- **`semitones_to_speed`** — `src/grain.c:86`, `powf(2.0f, n/12.0f)`. The smear path needs the **same exponent shape** but interpreted as a frequency multiplier (`ref_hz · 2^(n/12)`), NOT a playback speed. (`powf`/`floorf` and `math.h` are already in `grain.c`; the smear per-block reader lives in `ligase~.c`, where `#include <math.h>` (`:11`) and `powf` (used `:640`) — and therefore `floorf` — are likewise available.)
- **`sample_scale_semitones`** — `src/grain.c:95`, **declared `static`** (file-local to `grain.c`). The stochastic scale-degree sampler used by `PITCH_MODE_SCALE`. ⟦V: because it is `static`, it is **NOT** callable from `ligase~.c` as the optional SCALE smear source (GATE A) currently implies — calling it from the per-block stanza in `ligase~.c` would fail to link. To enable SCALE, first make it non-static + extern-declare it in `ligase~.c` (exactly how `pattern_eval_slot` is exposed at `ligase~.c:64`), or add a small extern wrapper in `grain.c`. The in-scope PATTERN source does NOT call it (it inlines the wrap+octave math reading `cached_value` directly), so **lean P1 is unaffected**; this only gates the deferred SCALE source.⟧

### Pattern handler surface (the `pattern smear_pitch …` target rides these)

- **`ligase_pattern`** — `src/ligase~.c:2880`. Message-thread `pattern <slot|pitch|param> <tokens>` handler. The `pitch` branch (**`:2888-2890`**) selects `slot = PATTERN_SLOTS - 1` (=7) and sets `attach_pitch = 1`; the generic param branch begins `:2891` (`else if (argv[0].a_type == A_SYMBOL)`) and routes through `pattern_alloc_param_slot` (`:2898`); the two-stage parse→flatten→commit body below (`step_count` published LAST as the barrier) is **target-agnostic and reused verbatim**. The `attach_pitch` commit block is at **`:3064-3070`**. The new `smear_pitch` symbol-dispatch MUST go **before** the generic `:2891` param branch (a bare `A_SYMBOL` "smear_pitch" otherwise falls into `get_param_range_by_name` → "unknown parameter").
- **`pattern_alloc_param_slot`** — `src/ligase~.c:2680-2690`. Hands out param slots; its guards loop `i < PATTERN_SLOTS - 1` at **`:2686`** (the first-free scan) and the `rand_instance` reuse check at **`:2683`** (`rand_instance < PATTERN_SLOTS - 1`), so slot 7 is excluded for grain pitch. To reserve slot 6 for smear pitch, both guards change `PATTERN_SLOTS - 1` → `PATTERN_SLOTS - 2`. ⟦V: this is **necessary but not fully sufficient** — see the slot-budget risk: the `rand_type … pattern_N` manual instance-bind path validates `instance < PATTERN_SLOTS` (0..7) at `ligase~.c:3996`, **bypassing** these guards, so a user can still point a param at slot 6 (and at slot 7 today). That is a pre-existing tolerated foot-gun, identical for the grain-pitch slot, not a new regression.⟧
- **`ligase_pattern_clear`** — `src/ligase~.c:2744`. The `pitch` clear branch (**`:2772-2782`**) zeroes `pattern[7].step_count`/`pattern_phase`/`pattern_cycle_index`, sets `pitch_pattern_slot = -1`, reverts `PITCH_MODE_PATTERN → OFF`. The new `smear_pitch` clear branch mirrors this for slot 6 and MUST precede the generic `get_param_range_by_name` lookup at `:2785`.
- **`PATTERN_SLOTS`** — `src/types.h:435` (`==8`). Slot 7 = grain pitch (hard-reserved by convention). Smear pitch claims slot 6. The `perlin_state_t` arrays `pattern[PATTERN_SLOTS]` / `pattern_phase[PATTERN_SLOTS]` / `pattern_cycle_index[PATTERN_SLOTS]` (`types.h:567-569`) are all sized to `PATTERN_SLOTS`, so slot-6 access is in-bounds.
- **`pattern_eval_slot`** — `src/grain.c:407` (extern-declared `src/ligase~.c:64`), the **sole writer** of `cached_value`/`cached_is_rest`; rests hold the previous value. Called per active slot at `src/ligase~.c:1611`. The smear reader is **READ-only** of this cache.

### Backward-compat anchors (must stay bit-identical when smear pitch is OFF)

- The **inlet-19 signal MIDI** path (`src/ligase~.c:495-503`, gate `midi_note >= 1 && midi_note <= 127` at `:499`, writes `pitch_control.midi_note` only in `PITCH_MODE_MIDI`) and the **inlet-15 smear-mix** path (`:669-674`) — untouched by P1 (the inlet MIDI clobber policy is P2's GATE A). ⟦V: `midi_in` is a `t_sample *` signal pointer (`w[20]`, `:1546`); `midi_in[0]` is read **only as a note number**, never a channel. There is **no** existing `midi` input MESSAGE handler (the lone `gensym("midi")` at `:4705` is a state OUTPUT via `outlet_anything`). P1 therefore correctly reserves `source=MIDI` / `note` / `midi_channel` as inert fields and leaves the channel-aware `midi <note> [vel] [channel]` MESSAGE path to P2 — it never pretends the signal inlet carries a channel.⟧
- The manual **`smear_frequency`** message (`:3436`) and the per-block **`smear_frequency_range`** modulation (`:909-911`) — untouched when `smear_pitch_control.enabled == 0`.
- Smear defaults in `grain_smear_create` (`freq_hz=800`, `resonance=0.7`, `stages=12`, `feedback=0.0`) and the dry/short-circuit early-out (process returns when `mix < 0.0001` or `stages <= 0`) — untouched; a note source only changes `freq_hz`.

## Design

### Data structures (`src/types.h`)

**New source enum** (added in the pitch types region near `pitch_mode_t` at `types.h:400`, kept fully separate from `pitch_mode_t` so the two destinations never share a value space):

```c
typedef enum {
    SMEAR_PITCH_OFF,        // 0 — default; manual smear_frequency + smear_frequency_range own freq (backward compat)
    SMEAR_PITCH_SEMITONE,   // 1 — fixed transpose: hz = ref_hz * 2^(semitone/12)
    SMEAR_PITCH_SCALE,      // 2 — stochastic scale-degree (OPTIONAL in P1, GATE A) via sample_scale_semitones
    SMEAR_PITCH_MIDI,       // 3 — note from the channel-aware 'midi' message (FED IN P2; field reserved here)
    SMEAR_PITCH_PATTERN     // 4 — mini-notation scale-degree stepper on a dedicated pattern slot
} smear_pitch_source_t;
```

**New minimal controller** — added to `scheduler_t` **adjacent to `pitch_control` (`types.h:650`)**, anywhere before the closing `} scheduler_t;` at `types.h:659`. ⟦V: `pitch_control` is NOT the last member — `pan_mode`/`delay_stut`/`delay_bencina` follow it; placing the new member right after `pitch_control` at `:650` is the clearest spot.⟧ It carries ONLY what a single resonator Hz needs (no grain-speed / per-grain / change-detection fields):

```c
// SMEAR pitch destination — an independent note->Hz controller for the resonator (allpass) pitch.
// Deliberately NOT pitch_control_t (that is the shared GRAIN-speed controller). Emits a single Hz
// via grain_smear_set_frequency. Zeroed by the scheduler_create memset; musical defaults set there.
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
```

…and one member on the scheduler (adjacent to `pitch_control` at `types.h:650`):

```c
    smear_pitch_control_t smear_pitch_control;  // SMEAR (resonator) pitch destination — independent of pitch_control
```

`smear_pitch_control_t` reuses the existing `pitch_scale_t` (`types.h:411`) and `param_range_t` types — no new pattern table is added (smear pitch reads the shared `perlin_state.pattern[]` pool via its reserved slot).

### Initialization (`src/grain.c`, in `scheduler_create` near the pitch_control init at `grain.c:553-563`)

The `memset` at `grain.c:486` zeroes the whole struct, so `enabled=0`, `source=SMEAR_PITCH_OFF`, `midi_enabled=0` come free. But **0 is a valid slot and a valid note**, and `ref_hz`/`ref_note` must be musical — so set explicit defaults immediately after the `pitch_control` block:

```c
    // SMEAR pitch destination defaults (memset already zeroed enabled/source/note/midi_enabled).
    // Explicit non-zero musical defaults: A440 reference, no slot bound (0 is a valid slot).
    sched->smear_pitch_control.enabled      = 0;                  // off -> backward compat (manual smear_frequency)
    sched->smear_pitch_control.source       = SMEAR_PITCH_OFF;
    sched->smear_pitch_control.semitone     = 0.0f;
    sched->smear_pitch_control.ref_hz       = 440.0f;            // A4
    sched->smear_pitch_control.ref_note     = 69;                // note 69 -> 440 Hz (standard A440 MIDI)
    sched->smear_pitch_control.pattern_slot = -1;                // -1 = no slot bound (0 is a valid slot)
    sched->smear_pitch_control.midi_channel = 2;                 // default smear MIDI channel (used by P2)
    sched->smear_pitch_control.scale.count  = 0;                 // no scale loaded
    sched->smear_pitch_control.semitone_range = default_range;   // disabled by default
    sched->smear_pitch_control.last_hz      = 0.0f;
```

(`default_range` is the local initializer already defined at `grain.c:498`.)

### Note → Hz mapping

```
hz = ref_hz * 2^((note − ref_note) / 12)
```

Defaults `ref_hz = 440`, `ref_note = 69` ⇒ note 69 = 440 Hz (A4), standard A440 MIDI tuning. ⟦V: spot-checked — note 60 → 440·2^(−9/12) = 261.6 Hz (matches AC1); note 127 → 440·2^(58/12) ≈ 12.5 kHz (clamps below Nyquist at low SR per AC4).⟧ The `smear_note <note> [ref_note] [ref_hz]` message lets the user re-reference (e.g. `ref_note = 60` to align with the "middle C = 60" convention noted for `PITCH_MODE_MIDI` at `types.h:405`). The conversion is **one `powf`** at message/block rate, never per-sample. Raw Hz is fed to `grain_smear_set_frequency`, whose `smear_update_coeffs` clamp (`grain_smear.c:50-51`) is the **sole** bounds owner — a high note at low sample rate folds safely there; **we do NOT duplicate or contradict that clamp.**

### The per-block application point (audio-thread-safe, once per block)

All source resolution to Hz happens in the existing smear stanza in `ligase_update_inlets` (`src/ligase~.c:908-925`), which runs on the block thread, AFTER `pattern_eval_slot` (so the pattern cache is fresh), and BEFORE `grain_smear_process`. Inside the existing `if (x->smear) { … }`, **after** the `smear_frequency_range` branch (`:909-911`), add the smear-pitch override:

```c
    if (x->smear) {
        if (x->scheduler->smear_frequency_range.enabled) {                 // EXISTING (:909-911) — unchanged
            grain_smear_set_frequency(x->smear,
                sample_param_range(&x->scheduler->smear_frequency_range, &x->scheduler->perlin_state, 0.0f));
        }
        // ... resonance / stages / feedback branches unchanged (:913-924) ...

        // NEW: smear PITCH destination. Placed AFTER smear_frequency_range so, when enabled, the
        // note-derived Hz is the LAST writer of freq this block -> it OVERRIDES the range modulation
        // (precedence per GATE A). When disabled, this whole branch is skipped (backward compat).
        smear_pitch_control_t *sp = &x->scheduler->smear_pitch_control;
        if (sp->enabled) {
            float semitone;
            int have_note = 1;
            switch (sp->source) {
                case SMEAR_PITCH_SEMITONE:
                    semitone = sp->semitone;                                // fixed transpose vs ref
                    break;
                case SMEAR_PITCH_MIDI:                                      // FED IN P2; field already present
                    if (sp->midi_enabled) semitone = (float)(sp->note - sp->ref_note);
                    else                  have_note = 0;                    // no note yet -> hold
                    break;
                case SMEAR_PITCH_SCALE:                                     // OPTIONAL (GATE A) — see linkage note below
                    semitone = sample_scale_semitones(&sp->scale,
                                   &x->scheduler->perlin_state, &sp->semitone_range);
                    break;
                case SMEAR_PITCH_PATTERN: {
                    int slot = sp->pattern_slot, count = sp->scale.count;
                    pattern_table_t *pt = (slot >= 0 && slot < PATTERN_SLOTS)
                                          ? &x->scheduler->perlin_state.pattern[slot] : NULL;
                    if (pt && pt->step_count > 0 && count > 0 && !pt->cached_is_rest) {
                        int degree = (int)pt->cached_value;                 // leaf value = scale degree
                        int idx = ((degree % count) + count) % count;       // wrap  (mirror grain.c:825)
                        int oct = (int)floorf((float)degree / (float)count);// octave (mirror grain.c:826)
                        semitone = sp->scale.semitones[idx] + 12.0f * (float)oct;
                    } else {
                        have_note = 0;                                      // rest / not ready -> HOLD previous Hz
                    }
                    break;
                }
                default:  /* SMEAR_PITCH_OFF reached with enabled set */ have_note = 0; break;
            }
            if (have_note) {
                float hz = sp->ref_hz * powf(2.0f, semitone / 12.0f);      // note->Hz (one powf, block rate)
                sp->last_hz = hz;                                          // raw Hz; clamp lives in smear_update_coeffs
                grain_smear_set_frequency(x->smear, hz);                   // OVERRIDES range modulation this block
            }
            // have_note == 0 (rest / no MIDI note / not ready) -> skip the setter, hold previous Hz.
        }
    }
```

> ⟦V — SCALE linkage caveat: the `SMEAR_PITCH_SCALE` case above calls `sample_scale_semitones`, which is **`static` in `grain.c` (`:95`)** and therefore not linkable from `ligase~.c` as written. SCALE is a deferred GATE A item; **lean P1 ships OFF/SEMITONE/PATTERN and never compiles this call**. If/when SCALE is enabled, first de-static `sample_scale_semitones` and extern-declare it in `ligase~.c` (mirroring `pattern_eval_slot` at `:64`), or add an extern wrapper in `grain.c`. The PATTERN case (in-scope) inlines its own degree→semitone math and has no such dependency.⟧

This branch only **reads** `pattern[].cached_value`/`cached_is_rest` (sole writer is `pattern_eval_slot`) and calls the allocation-free, `magic`-guarded `grain_smear_set_frequency`. No malloc, no parse, no `gensym` on the audio thread. The `powf` is one block-rate op. It does not touch `mix`/`feedback`/`stages` or the early-out (`grain_smear.c` process guards), so the dry/short-circuit behavior is preserved.

**Cadence (documented limit, inherited from the pattern subsystem):** the PATTERN source reads `cached_value`, which advances once per DSP block on the BPM-locked cycle clock — so smear pitch is **tempo-locked**, identical semantics to grain `PITCH_MODE_PATTERN`. Since the setter is once per block (not per grain), there is no sub-block coefficient thrash.

### Precedence with manual + modulated `smear_frequency` (consistent with grain)

**OVERRIDE — the same rule grain already uses.** On the grain side, engaging any pitch source (`PITCH_MODE != OFF`) bypasses the continuous `speed_range` modulation (`grain.c:766-773`): the source owns the pitch. Smear is identical — when `smear_pitch_control.enabled`, the note→Hz path owns `freq_hz` and the continuous `smear_frequency_range` is bypassed. Implement it as a true bypass for clarity: gate the existing `smear_frequency_range` branch on `!smear_pitch_control.enabled` (equivalently, the smear-pitch write is the sole writer of `freq_hz` for the block). When `smear_pitch` is **disabled** (default), the stanza behaves exactly as today (`smear_frequency` + `smear_frequency_range` own the frequency). One owner per block, selected by `enabled` — no two-writer race.

There is **no "combine" mode.** Modulatable detune / vibrato around the played note is delivered by the **fine-tune** (`smear_pitch_fine`, Plan P3) — a `param_range` you point an LFO or pattern at — NOT by repurposing `smear_frequency_range`. That keeps smear consistent with grain (where the same modulatable-offset role is `pitch_fine`) and avoids inventing a second mechanism for one behavior.

### Message interface (`src/ligase~.c`, handlers near `ligase_smear_frequency` at `:3436`, registered near `:5214`)

All run on the message thread; simple field stores only (no DSP, no alloc). Application happens in the per-block stanza, so a message just sets fields.

- **`smear_pitch_source <0-4>`** → `ligase_smear_pitch_source(ligase_t *x, t_floatarg src)`. Validate `0..4`; set `smear_pitch_control.source` and `smear_pitch_control.enabled = (src != SMEAR_PITCH_OFF)`; `post` the mode name; `pd_error` on out-of-range.
- **`smear_pitch_semitones <n>`** → `ligase_smear_pitch_semitones(ligase_t *x, t_floatarg n)`. Store `smear_pitch_control.semitone = n`. (Setting it does not implicitly enable; the user selects `smear_pitch_source 1` for SEMITONE — or document that it auto-selects SEMITONE for convenience, a GATE A nicety.)
- **`smear_note <note> [ref_note] [ref_hz]`** → `ligase_smear_note(ligase_t *x, t_symbol *s, int argc, t_atom *argv)` (A_GIMME, arg count varies). Validate `note` 1..127 (mirror the perform gate at `:499`). Store `note`, `midi_enabled = 1`, `source = SMEAR_PITCH_MIDI`, `enabled = 1`; if 2 args also set `ref_note` (0..127), if 3 args also set `ref_hz` (> 0). This gives a **channel-free** immediate note source in P1 (a single explicit note); the **channel-aware** `midi` ingress arrives in P2 and writes the same `note`/`midi_enabled` fields by channel.

Registration (next to the smear cluster at `:5214-5217`):

```c
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_source,    gensym("smear_pitch_source"),    A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_pitch_semitones, gensym("smear_pitch_semitones"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_smear_note,            gensym("smear_note"),            A_GIMME,    0);
```

### PATTERN source: `pattern smear_pitch <tokens>` (dedicated slot 6)

Mirrors the proven P3 `pattern pitch` mechanism but emits Hz per block (above), not a grain-speed ratio per grain. No new `class_addmethod` — it rides the existing `pattern` / `pattern_clear` A_GIMME selectors.

1. **Reserve the slot.** Change `pattern_alloc_param_slot`'s guards at **`ligase~.c:2683`** and **`:2686`** from `PATTERN_SLOTS - 1` to `PATTERN_SLOTS - 2`. The param pool shrinks from slots 0..6 to **0..5**; **slot 6 (`PATTERN_SLOTS - 2`) = smear pitch**, slot 7 (`PATTERN_SLOTS - 1`) = grain pitch. ⟦V: this guards only the *auto-allocator*. The explicit `pattern_N` instance-bind path (`ligase~.c:3996`, validates `instance < PATTERN_SLOTS`) is NOT guarded and can still target slot 6 — identical to slot 7's existing exposure; see Risks.⟧
2. **Dispatch branch** in `ligase_pattern` (mirror the `pitch` branch at `:2888-2890`), **before** the generic param branch at `:2891`:

```c
    } else if (argv[0].a_type == A_SYMBOL &&
               strcmp(argv[0].a_w.w_symbol->s_name, "smear_pitch") == 0) {
        slot = PATTERN_SLOTS - 2;            // smear pitch: dedicated slot 6
        attach_smear_pitch = 1;
    }
```

(Add `int attach_smear_pitch = 0;` alongside `attach_pitch` at `:2887`.)

3. **Attach commit** (mirror `attach_pitch` at `:3064-3070`):

```c
    if (attach_smear_pitch) {
        x->scheduler->smear_pitch_control.pattern_slot = slot;
        x->scheduler->smear_pitch_control.source       = SMEAR_PITCH_PATTERN;
        x->scheduler->smear_pitch_control.enabled      = 1;
        if (x->scheduler->smear_pitch_control.scale.count == 0)
            post("ligase~: pattern smear_pitch set, but no smear_pitch_scale loaded yet (unison until you send one)");
        post("ligase~: smear pitch pattern set (slot %d), smear_pitch_source -> pattern", slot);
    }
```

(SCALE-source loading — `smear_pitch_scale` — is GATE A. If SCALE is deferred, the PATTERN source still needs a degree→semitone scale; provide `smear_pitch_scale <semitones…>` as the minimal scale loader for the pattern degrees, or reuse the grain `pitch_scale` — a GATE A item.)

4. **Clear branch** in `ligase_pattern_clear` (mirror the `pitch` clear at `:2772-2782`), **before** the generic `get_param_range_by_name` lookup at `:2785`:

```c
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
```

5. **Per-block read** is the `SMEAR_PITCH_PATTERN` case already shown in the application stanza: read `pattern[6].cached_value` as a degree, wrap+octave (mirror `grain.c:825-827`) → Hz; **hold previous Hz on `cached_is_rest`** (skip the setter), mirroring the cache hold.

### Optional SCALE source (GATE A)

`SMEAR_PITCH_SCALE` reuses `smear_pitch_control.scale` + `.semitone_range` via `sample_scale_semitones` (`grain.c:95`), then degree→semitone→Hz. ⟦V: **this is NOT a trivial drop-in** — `sample_scale_semitones` is `static` in `grain.c` and must first be made externally visible (de-static + extern decl in `ligase~.c`, as `pattern_eval_slot` already is at `:64`) before the stanza can call it; otherwise the link fails. Budget this small refactor into the SCALE add.⟧ Loaders (`smear_pitch_scale …`, `smear_pitch_range …`, `smear_pitch_rand_type …`) parallel the grain `pitch_scale`/`pitch_range`/`pitch_rand_type` handlers. **Recommendation: lean P1 ships OFF/SEMITONE/PATTERN; SCALE deferred** (PATTERN already gives scale-degree sequencing for smear).

### State / mirror (optional)

`smear_pitch_control.last_hz` holds the last applied Hz for an optional state report next to the existing smear-mix query (`get_current_value("smear")` at `:4474`, query post at `:4593-4594`). Not load-bearing for P1; a small reporting nicety.

## Steps & gates

### GATE A (approval) — owner decisions 2026-06-24 (Seq 51)

**Owner-confirmed:** **(2)** note→Hz reference = **A440** (`ref_note=69`, `ref_hz=440`) + the settable 3-arg `smear_note`; **(4)** scope = **FULL menu including the stochastic SCALE source** (NOT lean) — so the `sample_scale_semitones` de-static refactor (make it non-static in `grain.c` + extern-declare in `ligase~.c`, mirroring `pattern_eval_slot` at `:64`) is **IN SCOPE for P1**, and the `SMEAR_PITCH_SCALE` case + the `smear_pitch_scale`/`smear_pitch_range`/`smear_pitch_rand_type` loaders ship in P1. **Defaulted (not separately raised):** **(1)** precedence = **OVERRIDE, locked** — consistent with grain (a pitch source bypasses the continuous modulation, exactly as `PITCH_MODE != OFF` bypasses `speed_range` at `grain.c:766-773`); there is **no "combine" mode** (modulatable detune/vibrato is the P3 `smear_pitch_fine`, not a repurposed `smear_frequency_range`); **(3)** slot 6 reserved (shrink the auto-allocator); **(5)** minimal `smear_pitch_scale` loader (its own scale, not coupled to grain); **(6)** `smear_pitch_semitones` auto-selects SEMITONE. **NEW (owner):** a modulatable ±50-cent **fine tune** per pitch destination is split into sibling **Plan P3** (`Plans/pitch_fine_tune.md`) — P1 does not build it; P3 adds `smear_pitch_fine` on top of P1's smear semitone. Original sign-off items retained for the record:

1. **Smear-pitch precedence vs manual/modulated `smear_frequency`.** **OVERRIDE (locked).** When `smear_pitch_control.enabled`, the note→Hz path owns `freq_hz` and `smear_frequency_range` is bypassed (gate the range branch on `!smear_pitch_control.enabled`); disabled = today's behavior. This is the **same rule grain uses** — `PITCH_MODE != OFF` bypasses `speed_range` (`grain.c:766-773`). **No "combine" mode** (rejected as redundant): modulatable detune/vibrato around the note is the P3 fine-tune (`smear_pitch_fine`), not a repurposed `smear_frequency_range`.
2. **note→Hz reference default.** Recommend **`ref_note = 69`, `ref_hz = 440`** (A440 standard) AND the settable 3-arg `smear_note <note> [ref_note] [ref_hz]`. Confirm vs `ref_note = 60` (middle-C convention at `types.h:405`).
3. **Dedicated smear-pattern slot.** Recommend **reserve slot 6** (`pattern_alloc_param_slot` guards `PATTERN_SLOTS-1 → PATTERN_SLOTS-2` at `:2683`/`:2686`; param pool shrinks 7→6). Confirm vs **bump `PATTERN_SLOTS` 8→9** (keep 7 param slots, one extra `pattern_table_t`). Flag: a patch needing all 7 former param slots simultaneously would get "no free pattern slots." ⟦V: note the auto-allocator guard does not stop a deliberate `pattern_N`-style bind to slot 6 (`:3996`) — same pre-existing exposure as slot 7.⟧
4. **Scope of non-MIDI/non-pattern sources in P1.** Recommend **lean P1 = OFF/SEMITONE/PATTERN**; defer the stochastic SCALE source (PATTERN covers scale-degree sequencing; SCALE additionally needs the `sample_scale_semitones` de-static refactor). Confirm vs full menu including SCALE+random now.
5. **Degree→semitone scale for the PATTERN source** (if SCALE is deferred). Recommend a minimal **`smear_pitch_scale <semitones…>`** loader feeding `smear_pitch_control.scale`. Confirm vs reusing the grain `pitch_control.scale` (couples the two destinations' scales — not recommended).
6. **`smear_pitch_semitones` auto-select.** Recommend it **auto-sets `source = SEMITONE` + `enabled = 1`** for convenience. Confirm vs requiring an explicit `smear_pitch_source 1`.

*(P2 GATE A items — the inlet-19 clobber policy, channel→destination defaults, note-off/velocity semantics — are NOT in scope here; the `source=MIDI` field and `midi_channel` are reserved but inert in P1.)*

### Step 1 → GATE B (types + init, no behavior)

Add `smear_pitch_source_t` and `smear_pitch_control_t` to `src/types.h` (enum near `:400`; struct + the `smear_pitch_control` member adjacent to `pitch_control` at `:650`, before the closing brace at `:659` — note `pan_mode`/`delay_stut`/`delay_bencina` sit between `pitch_control` and the brace). Set explicit defaults in `scheduler_create` (`src/grain.c`, after the `pitch_control` init at `:553-563`). **GATE:** `make clean && make` warning-free (baseline already is); a fresh object's `smear_pitch_control.enabled == 0` and the smear stanza is unchanged ⇒ identical behavior.

### Step 2 → GATE C (per-block application + precedence)

Add the smear-pitch override branch in the `if (x->smear)` stanza (`src/ligase~.c:908-925`), **after** the `smear_frequency_range` branch (`:911`), implementing SEMITONE + PATTERN (+ MIDI field-read) resolution → one `powf` → `grain_smear_set_frequency`, gated on `enabled`, holding on rest/no-note. (Leave the SCALE case out of the lean build, or gate it behind the `sample_scale_semitones` de-static refactor.) **GATE:** `make clean && make` warning-free; with `enabled == 0` the resonator freq path is byte-for-byte the old behavior; the branch reads only the pattern cache + calls the allocation-free setter (audio-thread safe).

### Step 3 → GATE D (messages)

Add `ligase_smear_pitch_source`, `ligase_smear_pitch_semitones`, `ligase_smear_note` near `:3436`; register near `:5214`. Validate ranges (`source` 0..4, `note` 1..127, `ref_note` 0..127, `ref_hz` > 0). **GATE:** `make clean && make` warning-free; the three messages reach the handlers and set the expected fields; `smear_pitch_source 0` disables (manual `smear_frequency` resumes ownership).

### Step 4 → GATE E (pattern target)

Change the two `pattern_alloc_param_slot` guards (`:2683`, `:2686`) to `PATTERN_SLOTS - 2`; add the `smear_pitch` dispatch branch (before the `:2891` param branch) + `attach_smear_pitch` commit (mirror `:3064-3070`) + clear branch (mirror `:2772-2782`, before the `:2785` lookup); the per-block PATTERN read landed in Step 2. (If SCALE is deferred per GATE A.5, add the minimal `smear_pitch_scale` loader.) **GATE:** `make clean && make` warning-free; `pattern smear_pitch [ 0 4 7 ]` loads slot 6 and steps the resonator pitch; existing `pattern pitch` (slot 7) and named-param patterns (now slots 0..5) unregressed; `pattern_clear smear_pitch` restores manual freq ownership.

### Step 5 → GATE F (verify, headless)

Build; run the acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1` so perform actually runs), recording the live granular+smear output via `writesf~` and reading back the spectral peak / splice-end outlet. Confirm all acceptance criteria; confirm no regression in `smear_frequency` / `smear_frequency_range` and in `pattern pitch`. Update the manual's smear section to document `smear_pitch_source` / `smear_note` / `pattern smear_pitch …` and the OVERRIDE precedence + tempo-lock caveat.

## Acceptance criteria (headless-testable with `pd -nogui -nosound`)

All via `pd -nogui -nosound -stderr -path . <patch>.pd`; record noise into the reel, drive the smear at high feedback so it rings at `freq_hz`, capture output via `writesf~`, and measure the dominant resonant frequency (FFT peak of the captured WAV) and the splice-end outlet.

1. **Fixed-semitone smear pitch.** `smear_pitch_source 1` (SEMITONE) + `smear_pitch_semitones 0` with default ref ⇒ the resonator rings at **440 Hz** (note 69) — measured spectral peak within smear's bandwidth tolerance. `smear_pitch_semitones 12` ⇒ **880 Hz** (one octave up); `smear_pitch_semitones -12` ⇒ **220 Hz**. `smear_note 60` ⇒ ~**261.6 Hz** (middle C via the default 69/440 reference). Verifies the note→Hz `powf` and the message path.
2. **Pattern-stepped smear pitch (tempo-locked).** Load a degree scale, then `pattern smear_pitch [ 0 4 7 ]` at a bang interval giving bpm=120 (default 1-bar cycle, 2.0 s). The resonant peak steps through the three scale degrees, each held ~0.667 s, looping; doubling bpm to 240 halves the hold durations (BPM-lock). A rest token `~` holds the previous Hz (no peak shift during the rest). Verifies the dedicated slot-6 read, the wrap+octave, and the per-block Hz emission.
3. **Override-vs-range precedence.** Enable BOTH `smear_frequency_range` (a sweep) AND `smear_pitch_source 1` (fixed semitone). The measured resonant peak is **pinned to the note's Hz**, NOT the swept range — confirming smear pitch (placed after the range branch) is the last writer and OVERRIDES. Then `smear_pitch_source 0` ⇒ the sweep returns (range resumes ownership).
4. **Clamp at extreme notes.** `smear_note 127` at a low sample rate where `2^((127−69)/12)·440` exceeds `0.45·sr`: the resonator frequency is **clamped to `0.45·sr`** (peak at the Nyquist-fold bound), no NaN/instability — confirming the sole clamp in `smear_update_coeffs` (`grain_smear.c:50-51`) governs and is not duplicated. A very low note clamps to 20 Hz.
5. **No regression in `smear_frequency` / `smear_frequency_range`.** With `smear_pitch_source 0` (default): manual `smear_frequency 800` rings at 800 Hz; an enabled `smear_frequency_range` sweeps exactly as before. A patch that never sends any `smear_pitch_*` / `smear_note` / `pattern smear_pitch` message is byte-for-byte identical to today (resonator freq, mix, feedback, stages all unchanged). `make clean && make` warning-free; `test_delay.pd` clean.
6. **No regression in `pattern pitch` (slot 7) and named-param patterns.** With slot 6 reserved for smear pitch, `pattern pitch < 0 4 7 >` still drives the grains on slot 7, and named-param patterns allocate across slots 0..5 (a 6th simultaneous param pattern correctly reports "no free pattern slots"). Smear-on-PATTERN + grain-on-PATTERN run **independent** sequences (the two destinations read different slots).

## Risks / out-of-scope

**Risks**

- **Two-writer race on resonator freq.** `smear_frequency_range` and the new smear-pitch path both call `grain_smear_set_frequency` in one block. Mitigated by the **documented OVERRIDE ordering** (smear-pitch branch after the range branch, gated on `enabled`). If the branch is placed BEFORE the range branch, range silently wins — the ordering is load-bearing, not cosmetic. (GATE A.1.)
- **`pattern_slot = 0` / `note = 0` zero-init footgun.** The `memset` zeroes the controller, so an un-initialized `pattern_slot` would default to **slot 0** (valid) and `note` to **0**. Mitigated by the explicit `pattern_slot = -1`, `ref_note = 69`, `enabled = 0` defaults in `scheduler_create` and the `enabled`/`source` gating in the stanza — same "garbage-enabled clobbers live value" class the codebase documents at `grain.c:480-486`.
- **Param-slot budget shrink.** Reserving slot 6 drops the param pool from 7 to 6 simultaneous param patterns. A patch needing all 7 former param slots at once gets "no free pattern slots." Flagged as the GATE A.3 tradeoff vs bumping `PATTERN_SLOTS` to 9.
- ⟦V⟧ **Slot-6 reservation is partial.** The two `pattern_alloc_param_slot` guard edits only protect the *auto-allocator*. The explicit `rand_type … pattern_N` instance-bind path validates `instance < PATTERN_SLOTS` (0..7) at `ligase~.c:3996` and is NOT guarded, so a user can still deliberately bind a param to slot 6 (clobbering smear pitch). This is **identical to slot 7's (grain pitch) existing exposure today** — a tolerated user foot-gun, not a new regression — and matches the codebase's "dedicated by convention, not hard-locked" treatment. If a hard lock is wanted, the `pattern_N` validator would also need to reject reserved slots (out of scope for P1).
- **PATTERN source with no scale loaded.** `SMEAR_PITCH_PATTERN` with `scale.count == 0` degrades to "hold previous Hz" (the `count > 0` guard) — never crashes; the attach handler `post`s a hint. If SCALE is deferred (GATE A.4), a minimal `smear_pitch_scale` loader (GATE A.5) is required or the pattern has no degrees to map.
- ⟦V⟧ **SCALE source link dependency.** `sample_scale_semitones` is `static` in `grain.c` and cannot be called from `ligase~.c` without first making it externally visible. The lean P1 (OFF/SEMITONE/PATTERN) does not compile that call; enabling SCALE later requires the small de-static/extern refactor (mirroring `pattern_eval_slot`). Budget it with the SCALE add, not P1.
- **High note at low SR aliases near Nyquist.** Mitigated by relying on the existing `[20, 0.45·sr]` clamp (do NOT add a second clamp). AC4 verifies the fold is graceful.

**Out of scope (P1)**

- **Channel-aware MIDI ingress and routing** — the `midi <note> [vel] [channel]` message, the `grain_midi_channel`/`smear_midi_channel` map, same-channel-unison / different-channel-separate, and the inlet-19 clobber policy are **all P2** (`Plans/midi_channel_routing.md`). P1 reserves the `source = SMEAR_PITCH_MIDI`, `note`/`midi_enabled`, and `midi_channel` fields but leaves them inert (the `smear_note` message is the only note source in P1, and it is channel-free). ⟦V: confirmed no existing `midi` input message handler — the signal inlet (`w[20]`) carries only a note number, never a channel — so P1 does not (and must not) treat the inlet as channel-bearing.⟧
- **A "combine" precedence mode** (repurposing `smear_frequency_range` as vibrato around the note) — **not built; rejected as redundant** with the P3 fine-tune (`smear_pitch_fine` is the modulatable detune). Precedence is plain override, consistent with grain.
- **Stochastic SCALE source** — deferred (GATE A.4); the application stanza stubs the case for a later add reusing `sample_scale_semitones` (after de-static-ing it).
- **Microtonal / fractional degrees** — pattern leaf values truncate to integer scale degrees, same as the grain pattern path (out of scope, consistent with P3).
- **Modifying the `[20, 0.45·sr]` clamp** in `smear_update_coeffs` or duplicating it — strictly forbidden (single bounds owner).
- **Touching grain `pitch_control_t`, the inlet-19/inlet-15 signal paths, `semitones_to_speed`, or `PITCH_MODE_PATTERN` grain stepper** — the smear destination is a fully independent controller by construction.