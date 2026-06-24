# Plan P3: Pitch fine-tune (+/-50 cents, modulatable) for grain + smear

**Owner:** SLB
**Date:** 2026-06-24
**Status:** ✅ DONE (2026-06-24) — implemented (BOTH halves; P1 is landed) and headless-verified; `make clean && make` warning-free; no regression. grain fine per-grain in `scheduler_trigger_grain`, smear fine per-block riding P1's smear stanza; both modulatable `param_range` targets. See Progress.
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — pitch-destination build-out). (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related:** Plan P1 `Plans/smear_pitch.md` (the SMEAR note→Hz destination + its per-block sampling stanza this plan adds a *fine* offset to — **the smear half of P3 depends on P1**, see Mechanics) and Plan P2 `Plans/midi_channel_routing.md` (the channel-aware `midi <note> [vel] [channel]` ingress; orthogonal to the fine offset, which sits *on top of* whatever source each destination resolves). Anchored on three existing sections: the **modulation** section — the B19/Seq 42 `param_range_t`/`get_param_range_by_name`/`rand_type`-all-list five-touchpoint precedent (`smear_frequency`/`_resonance`/`_stages`/`_feedback` ranges, `ligase~.c:909-924`, `:3756-3759`, `:4046-4049`); the **grain pitch** section — `pitch_control_t` (`types.h:416-426`), the per-grain pitch switch + `semitones_to_speed` (`grain.c:779-846`, `:86`), and `ligase_pitch_semitones` (`ligase~.c:4357`); the **smear pitch** section — P1's per-block note→Hz stanza inside `if (x->smear)` (`ligase~.c:908-925`) and the sole frequency clamp in `smear_update_coeffs` (`grain_smear.c:50-51`). This is **P3 of the pitch-destination arc** (siblings P1 smear destination, P2 channel routing); P3 adds a Moog-style ±50-cent fine tune to BOTH pitch destinations.

> **ADVERSARIAL VERIFICATION (2026-06-24).** All load-bearing file:line refs for the GRAIN half were re-read in `src/types.h`, `src/grain.c`, `src/ligase~.c` and confirmed REAL and accurate: the pitch switch closes at `grain.c:843` and the `last_semitone` store is at `grain.c:846` (the insertion point is between them); `semitones_to_speed` is `powf(2,n/12)` at `grain.c:86`; `pitch_control_t` (`types.h:416-426`) ends with `pitch_pattern_slot` at `:424-425`; the `get_param_range_by_name` smear cluster ends at `ligase~.c:3759`; the `rand_type` all-list smear cluster ends at `ligase~.c:4049`; `ligase_pitch_semitones` is at `ligase~.c:4357` and its registration at `ligase~.c:5276`; the `scheduler_create` pitch_control init block runs `grain.c:553-563`. The SMEAR half is a **forward dependency on P1** (the `if (x->smear)` stanza at `ligase~.c:908-925` exists today, but P1's `semitone` accumulator + note→Hz `powf` + `smear_pitch_control_t` into which the fine adds do NOT — they land when P1 merges). The GRAIN half is fully independent and ships now. `sample_param_range` returning `base_value` verbatim when `!enabled` (`grain.c:202-204`) is the un-modulated base path; `&sched->perlin_state` is already passed to it inside `scheduler_trigger_grain` (`grain.c:794-796`), so per-grain modulation of the fine is reachable with zero new plumbing.

---

## Progress (2026-06-24) — IMPLEMENTED + VERIFIED (both halves)

P1 having landed, both the grain (independent) and smear (P1-dependent) halves shipped together; `make clean && make` warning-free; `test_delay.pd` clean.
- **Types/init:** `pitch_fine` + `pitch_fine_range` on `pitch_control_t`; `semitone_fine` on `smear_pitch_control_t`; `smear_pitch_fine_range` on `scheduler_t`. Defaults 0 + disabled range, bounds ±0.5 semitone, in `scheduler_create`.
- **Apply:** grain fine sampled **per grain** in `scheduler_trigger_grain` (after the switch, before `last_semitone`; recompute `final_speed` once so it reaches every mode incl. OFF; base = `pitch_fine`, NOT 0). Smear fine **per block** in P1's smear stanza, added to `semitone` before the `powf` (base = `semitone_fine`).
- **Modulation targets + setters:** `get_param_range_by_name` + `rand_type` all-list entries for `pitch_fine`/`smear_pitch_fine`; `pitch_fine <cents>` / `smear_pitch_fine <cents>` setters (cents→semitones = /100; ±50¢ = ±0.5 semitone).

| AC | Test (`tests/pattern/SM[4-6]*.pd`) | Result |
|----|------|--------|
| Smear fine | `smear_pitch_fine 50 / -50 / 25 / 0` on `smear_pitch_semitones 0` | 452.89 / 427.47 / 446.40 / 440 Hz ✓ |
| Grain fine | `pitch_fine 50 / -50 / 0` on `pattern pitch [0]` (degree 0) | last_semitone 0.50 / -0.50 / 0.00 ✓ (additive on the source) |
| Modulatable | `param_range smear_pitch_fine -0.5 0.5` (rand) | Hz wobbles in [427.5, 452.9] (±50¢) ✓ |
| Units | 50 cents → 0.5 semitone (not 50) | confirmed (no 100× error) ✓ |
| Backward compat | fine 0 + disabled range | no offset, no behavior change ✓ |

## Problem

The owner wants a Moog-style **fine tune** on EACH pitch destination — an overall detune knob that sits on top of whatever the destination is already doing, and that can itself be modulated. In their words:

> "Every synth I love has a fine knob — a little ±50-cent detune on top of the main tuning, so I can drift the grains slightly sharp against the smear, or wobble it with an LFO for chorus. I want one for the grains and one for the smear box. It's an OFFSET — it adds to whatever the pitch source is (a semitone, a scale step, a MIDI note, a pattern), it doesn't replace it. ±50 cents is plenty, that's half a semitone. And like everything else here I want to be able to *modulate* it — point a perlin or a pattern at the fine and have it move."

Decomposed into this slice (P3 — a modulatable ±50-cent fine offset on each of the two pitch destinations):

1. **A fine offset per destination.** A grain fine and a smear fine, each a ±0.5-semitone (±50-cent) overall transpose, added to the destination's resolved pitch *after* the source produces it — so it stacks on top of every source (off/semitone/scale/MIDI/pattern), never replacing the source.
2. **Modulatable, the B19 way.** Each fine is a `param_range_t` modulation TARGET: `param_range pitch_fine …` / `rand_type <type> pitch_fine` / `pattern pitch_fine …` (and the smear equivalents). When the range is disabled the fine is a constant base offset; when enabled the fine is sampled from a generator (perlin / sine / pattern …), so an LFO or a pattern can detune it over time.
3. **Per-grain vs per-block apply.** The GRAIN fine is sampled **per grain** (in `scheduler_trigger_grain`, where the per-grain `&sched->perlin_state` is already in scope) so dense grains get fresh modulation; the SMEAR fine is sampled **per block** (in P1's smear stanza), matching each destination's existing cadence.
4. **Backward compat is load-bearing.** Fine = 0 (the default: disabled range + zero base) is a no-op — `current_semitone += 0` for grains, `semitone += 0` for smear — so a patch that never touches the fine is byte-for-byte identical to today.

This plan adds the GRAIN fine end-to-end now (independent), and specifies the SMEAR fine as a forward-dependent rider on P1's smear note→Hz stanza.

## Mechanics / target surface — the EXISTING code this extends

**Provenance note:** every reference below into the **GRAIN** path was verified by reading `src/types.h`, `src/grain.c`, `src/ligase~.c` on 2026-06-24. The **SMEAR** path references P1 symbols that are forward-dependencies on unmerged work — the `if (x->smear)` stanza exists (`ligase~.c:908-925`), but P1's `semitone` accumulator, `float hz = sp->ref_hz * powf(2.0f, semitone/12.0f)` line, and `smear_pitch_control_t` do not.

### The B19 modulation-target recipe (the five touchpoints this plan replicates twice)

A modulatable param in ligase~ is a `param_range_t` that gets exactly five things. The smear ranges (`smear_frequency`/`_resonance`/`_stages`/`_feedback`) are the live precedent:

- **(a) A `param_range_t` field** on the owning struct. (For grains: on `pitch_control_t`, `types.h:416-426`, next to `semitone_range` — NOT on `scheduler_t`, because the fine is pitch-owned and sampled inside the pitch logic. For smear: on `scheduler_t` next to `smear_feedback_range`, the same place the B19 smear ranges live.)
- **(b) A `= default_range` init** in `scheduler_create` (`grain.c`). Smear ranges init at `grain.c:540-543`; the grain pitch_control init block is `grain.c:553-563`.
- **(c) An apply site** that calls `sample_param_range(&range, &perlin_state, base)`. Smear's per-block applies are `ligase~.c:909-924` (`if (range.enabled) … sample_param_range(&range, &perlin_state, 0.0f)`).
- **(d) A `get_param_range_by_name` entry** (`ligase~.c`), so `param_range`/`rand_type`/`pattern` can resolve the name. Smear entries are `ligase~.c:3756-3759`.
- **(e) Inclusion in the `rand_type` all-list array** (`ligase~.c`), so a bare `rand_type <type>` (no param name) covers it. Smear entries are `ligase~.c:4046-4049`.

`sample_param_range` itself (`grain.c:200`, extern-declared at `ligase~.c:63`, no header) is the single read primitive: **`if (!range->enabled) return base_value;`** (`grain.c:202-204`) — that verbatim-base-return is the un-modulated path. No header change is needed to call it from either apply site (`grain.c` defines it non-static in the same TU; `ligase~.c` has the extern). The only shared-header edit is the two new struct fields in `types.h`.

### Grain pitch — the per-grain semitone apply (independent half)

- **`scheduler_trigger_grain`** — `src/grain.c:725`. The per-grain trigger. Its pitch mode switch (`src/grain.c:779-843`) sets `current_semitone` per mode and bakes `final_speed = base_speed * semitones_to_speed(current_semitone)` inline in each case (OFF leaves `final_speed = base_speed`, `current_semitone = 0`). `&sched->perlin_state` is in scope and is already passed to `sample_param_range` in the `PITCH_MODE_RANGE` case (`grain.c:794-796`).
- **End-of-switch → `last_semitone` store** — switch closes at `src/grain.c:843`; `sched->pitch_control.last_semitone = current_semitone;` is at `src/grain.c:846`. **The fine offset is inserted BETWEEN these two lines** (add to `current_semitone`, recompute `final_speed` once), so `last_semitone` reflects the fine-tuned value (the `ligase~.c` outlet-3 change-detection reads it) and the offset applies uniformly to EVERY mode including `PITCH_MODE_OFF`.
- **`semitones_to_speed`** — `src/grain.c:86`, `powf(2.0f, n/12.0f)`. Confirms the apply is in **SEMITONES** — a ±0.5-semitone (=±50-cent) offset adds directly to `current_semitone` with NO cents→semitone conversion at the apply site.
- **`pitch_control_t`** — `src/types.h:416-426`. Holds `mode, semitones, semitone_range, scale, midi_note, midi_enabled, last_semitone, pitch_pattern_slot`. The new `pitch_fine` (base scalar, semitones) + `pitch_fine_range` (`param_range_t`) co-locate here, next to `semitone_range` — pitch owns them.
- **`ligase_pitch_semitones`** — `src/ligase~.c:4357`, writes `pitch_control.semitones`, registered at `src/ligase~.c:5276`. The template for the plain `pitch_fine` base setter + its `class_addmethod`.

### Smear pitch — P1's per-block note→Hz stanza (forward-dependent half)

- **The per-block smear stanza** — `src/ligase~.c:908-925`, inside `if (x->smear)`, on the block thread, AFTER `pattern_eval_slot` (called `:1611`) and before `grain_smear_process`. Today it samples `smear_frequency_range`/`_resonance`/`_stages`/`_feedback` (`:909-924`). **P1 adds, inside this stanza, a `switch (sp->source)` that resolves a float `semitone` and then `float hz = sp->ref_hz * powf(2.0f, semitone / 12.0f); grain_smear_set_frequency(x->smear, hz);`** (P1 plan text ~`:186-189`). The SMEAR fine adds into that same `semitone` accumulator, immediately BEFORE P1's `powf`. **This line does not exist until P1 merges** — hence the forward dependency.
- **`smear_frequency_range` sample idiom** — `src/ligase~.c:909-911`. The per-block sampling pattern the smear fine copies (`if (range.enabled) … sample_param_range(&range, &x->scheduler->perlin_state, base)`), except the sampled value is ADDED to `semitone`, not set as Hz.
- **`grain_smear_set_frequency`** — `src/grain_smear.c:89`. The single freq mutator; routes through `smear_update_coeffs`. The fine path feeds it raw post-`powf` Hz — no extra clamp.
- **`smear_update_coeffs` clamp** — `src/grain_smear.c:50-51`, `[20, 0.45·sr]`. The SOLE frequency-bounds owner. The fine path must NOT duplicate or re-implement it — a large fine + extreme note folds safely there.
- **`ligase_smear_feedback`** + smear registration — `src/ligase~.c:3445-3447` (last smear base setter, before the `@endregion` at `:3448`) and `src/ligase~.c:5217` (last smear `class_addmethod`). The `smear_pitch_fine` base setter + registration land right after these.

### Why grain ships independently but smear waits on P1

The grain fine touches only existing, in-tree surface (`pitch_control_t`, the `grain.c:779-846` switch, the five B19 touchpoints in `ligase~.c`). The smear fine's apply site is *inside P1's not-yet-merged note→Hz block* and adds to P1's `semitone` variable and reads P1's `smear_pitch_control` base — neither exists until P1 lands. So: **GRAIN half = build now; SMEAR half = gated on P1 merge** (GATE A).

## Design

### Two new modulation targets

| Target name | Owning struct | Base scalar (semitones) | Range field (`param_range_t`) | Apply cadence | Apply site |
|-------------|---------------|--------------------------|-------------------------------|---------------|------------|
| `pitch_fine` | `pitch_control_t` (`types.h:416-426`) | `pitch_fine` | `pitch_fine_range` (min −0.5, max +0.5) | **per grain** | `scheduler_trigger_grain`, between `grain.c:843` and `:846` |
| `smear_pitch_fine` | `smear_pitch_control_t` (P1) + `scheduler_t` | `semitone_fine` (P1's struct) | `smear_pitch_fine_range` (min −0.5, max +0.5, on `scheduler_t`) | **per block** | P1's smear stanza, before P1's `powf` |

Both default to **0** (no offset) and a **disabled** range, so backward compat falls out for free.

### Units: cents ↔ semitones (the one place to get right)

The feature spec is **±50 cents = ±0.5 semitone**. The apply-site math is in SEMITONES on both destinations:

- grain: `current_semitone += fine;` then `semitones_to_speed(current_semitone) = powf(2, current_semitone/12)` (`grain.c:86`).
- smear: `semitone += fine;` then `hz = ref_hz * powf(2, semitone/12)` (P1).

So the stored base value and the `param_range` `min`/`max` are in **SEMITONES** (`−0.5 … +0.5`). The conversion lives in EXACTLY ONE place — the plain base setter — which accepts **CENTS** (Moog-style UI) and divides by 100:

```c
static void ligase_pitch_fine(ligase_t *x, t_floatarg cents) {
    // Moog-style fine: message in CENTS; stored in SEMITONES (cents/100). ±50 cents = ±0.5 semitone.
    if (cents < -50.0f) cents = -50.0f;          // UI sanity clamp (NOT a pitch-path clamp)
    if (cents >  50.0f) cents =  50.0f;
    x->scheduler->pitch_control.pitch_fine = cents / 100.0f;
}
```

Convention to document plainly: the dedicated `pitch_fine <cents>` / `smear_pitch_fine <cents>` MESSAGES take cents; the `param_range pitch_fine -0.5 0.5` modulation target is given directly in **semitones** (consistent with `pitch_range`, which is in semitones). NEVER pass cents into `current_semitone`/`semitone` — that would be a 100× error.

### `pitch_control_t` additions (`src/types.h:416-426`)

After `int pitch_pattern_slot;` (`:424-425`), before the closing `} pitch_control_t;` (`:426`):

```c
    float pitch_fine;             // base fine-tune offset, SEMITONES (±0.5 = ±50 cents); default 0
    param_range_t pitch_fine_range; // modulatable fine-tune, SEMITONES, sampled PER GRAIN; disabled = use pitch_fine
```

Co-located with `semitone_range` because the fine is part of the pitch subsystem and is sampled inside the pitch logic. (The smear fine's range goes on `scheduler_t` next to `smear_feedback_range`, mirroring B19, and its base `semitone_fine` is a new float on P1's `smear_pitch_control_t`.)

### `scheduler_create` init (`src/grain.c`, in the pitch_control block `:553-563`)

After `sched->pitch_control.pitch_pattern_slot = -1;` (`grain.c:563`):

```c
    // Fine-tune offset: default 0 semitones, range disabled -> no offset (backward compat).
    sched->pitch_control.pitch_fine = 0.0f;
    sched->pitch_control.pitch_fine_range = default_range;          // disabled by default
    sched->pitch_control.pitch_fine_range.min = -0.5f;             // -50 cents
    sched->pitch_control.pitch_fine_range.max =  0.5f;             // +50 cents
```

(`default_range` is the local initializer at `grain.c:498`; the `memset(sched, 0, …)` at `grain.c:486` already zeroes the field as a safety net, but the explicit init matches the rest of the block.) The smear fine's `= default_range` (min −0.5, max +0.5) init goes at `grain.c:543` next to the B19 smear ranges, and `smear_pitch_control.semitone_fine = 0.0f` goes in P1's smear_pitch_control init block after `grain.c:563`.

### GRAIN apply — per grain, after the switch (`src/grain.c`, between `:843` and `:846`)

Insert immediately after the switch close (`grain.c:843` `}`) and immediately before the `last_semitone` store (`grain.c:846`):

```c
    }   // end switch (grain.c:843)

    // FINE TUNE (±0.5 semitone = ±50 cents) — overall offset on TOP of whatever the source produced.
    // Sampled per grain (perlin_state already used by the RANGE case above). When the range is
    // disabled, sample_param_range returns pitch_fine (the base, default 0) -> no offset, no behavior
    // change. Recompute final_speed ONCE here: each switch case baked its own multiply and PITCH_MODE_OFF
    // baked none, so a single post-switch recompute makes the fine apply uniformly across ALL modes
    // (including OFF) and keeps last_semitone consistent with the audible pitch.
    {
        float fine = sample_param_range(&sched->pitch_control.pitch_fine_range,
                                        &sched->perlin_state,
                                        sched->pitch_control.pitch_fine);   // base = the plain setter's value
        current_semitone += fine;
        final_speed = base_speed * semitones_to_speed(current_semitone);
    }

    // Store the semitone value for change detection in ligase~.c (grain.c:846)
    sched->pitch_control.last_semitone = current_semitone;
```

Critical: pass `sched->pitch_control.pitch_fine` (NOT `0.0f`) as the `base_value` arg — that is how the plain `pitch_fine <cents>` setter applies a constant offset when the range is disabled (mirrors how speed/amplitude thread their base through `sample_param_range`). Passing `0.0f` like the smear B19 sites do would silently discard the plain setter. The recompute overrides each case's inline `final_speed` and gives `PITCH_MODE_OFF` (which previously left `final_speed = base_speed`) a fine offset too. The block reads only `pitch_fine_range`/`pitch_fine` + calls the allocation-free `sample_param_range` — no malloc, no parse, no `gensym`; audio-thread safe.

### SMEAR apply — per block, in P1's stanza (FORWARD DEP on P1)

Inside the `if (x->smear)` stanza (`ligase~.c:908-925`), in P1's `SMEAR_PITCH_*` branch, immediately BEFORE P1's `float hz = sp->ref_hz * powf(2.0f, semitone / 12.0f);` line:

```c
    // FINE TUNE (±0.5 semitone = ±50 cents) on the smear note. Per BLOCK (matches the smear cadence).
    // base = sp->semitone_fine (default 0). Range disabled -> returns base -> semitone unchanged ->
    // hz identical -> byte-for-byte backward compatible. Sampled with the same &perlin_state as
    // smear_frequency_range (:909-911). NO clamp here — the [20,0.45*sr] clamp stays in smear_update_coeffs.
    float fine = sp->semitone_fine;
    if (x->scheduler->smear_pitch_fine_range.enabled)
        fine = sample_param_range(&x->scheduler->smear_pitch_fine_range,
                                  &x->scheduler->perlin_state, fine);
    semitone += fine;
    float hz = sp->ref_hz * powf(2.0f, semitone / 12.0f);   // existing P1 line (unchanged)
```

The raw post-`powf` Hz is fed to `grain_smear_set_frequency`; the `[20, 0.45·sr]` clamp (`grain_smear.c:50-51`) is the sole bounds owner and is NOT duplicated. Per-block (not per-grain): the smear destination is sampled once per DSP block, unlike the grain fine.

### How the fine composes with every source

The fine is added to the *resolved* semitone, after the source switch, on both destinations:

- **Grain** `PITCH_MODE_OFF` (`current_semitone=0`) → `0 + fine`; `SEMITONES` → `semitones + fine`; `RANGE` → `sampled + fine`; `SCALE` → `scale_semitone + fine`; `MIDI` → `(note−60) + fine`; `PATTERN` → `scale_degree_semitone + fine`. In every case the destination's source is preserved and the fine is a pure additive detune.
- **Smear** (P1 sources): `SMEAR_PITCH_SEMITONE`/`SCALE`/`MIDI`/`PATTERN` each set `semitone`; the fine adds before the single `powf`, so it detunes the resonator note by the cents regardless of source.

Because the grain fine is sampled per grain and the source (e.g. a per-grain `PITCH_MODE_RANGE`) is also per grain, a modulated fine layers cleanly on a modulated source — two independent perlin/pattern streams summing in semitone space.

### Modulation-target registration (the (d) + (e) touchpoints), both destinations

`get_param_range_by_name` (`ligase~.c`), after the smear cluster ending at `:3759`:

```c
    if (strcmp(name, "pitch_fine") == 0)       return &x->scheduler->pitch_control.pitch_fine_range;
    if (strcmp(name, "smear_pitch_fine") == 0) return &x->scheduler->smear_pitch_fine_range;   // P1 field
```

`rand_type` all-list array (`ligase~.c`), after the `smear_feedback_range` entry at `:4049`:

```c
            &x->scheduler->pitch_control.pitch_fine_range,
            &x->scheduler->smear_pitch_fine_range,        // P1 field
```

These make `param_range pitch_fine -0.5 0.5`, `rand_type perlin_2d_3 pitch_fine`, `pattern pitch_fine …`, and the bare `rand_type <type>` (all-params) all resolve to the fine — identical treatment to the B19 smear params. (The smear-fine entries land with P1, since they reference the P1 `smear_pitch_fine_range` field.)

### Plain base setters + class registration

Grain — after `ligase_pitch_semitones` (`ligase~.c:4357-4360`), register near `:5276`:

```c
static void ligase_pitch_fine(ligase_t *x, t_floatarg cents) {       // CENTS in, SEMITONES stored
    if (cents < -50.0f) cents = -50.0f;
    if (cents >  50.0f) cents =  50.0f;
    x->scheduler->pitch_control.pitch_fine = cents / 100.0f;
    post("ligase~: pitch fine set to %.1f cents (%.4f semitone)", cents, cents / 100.0f);
}
// registration (near ligase~.c:5276):
//   class_addmethod(ligase_class, (t_method)ligase_pitch_fine, gensym("pitch_fine"), A_DEFFLOAT, 0);
```

Smear — after `ligase_smear_feedback` (`ligase~.c:3445-3447`), register at `:5217` (FORWARD DEP on P1's `smear_pitch_control`):

```c
static void ligase_smear_pitch_fine(ligase_t *x, t_floatarg cents) {
    if (cents < -50.0f) cents = -50.0f;
    if (cents >  50.0f) cents =  50.0f;
    x->scheduler->smear_pitch_control.semitone_fine = cents / 100.0f;   // P1 field
}
// registration (near ligase~.c:5217):
//   class_addmethod(ligase_class, (t_method)ligase_smear_pitch_fine, gensym("smear_pitch_fine"), A_DEFFLOAT, 0);
```

No selector collision: `pitch_fine`/`smear_pitch_fine` resolve through `get_param_range_by_name` for `param_range`/`rand_type`/`pattern` (writing the RANGE), while the dedicated `pitch_fine <cents>` / `smear_pitch_fine <cents>` messages have their own `class_addmethod` handlers (writing the BASE) — distinct write targets, no conflict.

## Steps & gates

### GATE A (approval) — owner decisions 2026-06-24 (Seq 51)

Recommended options applied (they directly implement the owner's ±50-cent Moog-style spec; revisit at implementation if desired): **(1)** dedicated `pitch_fine`/`smear_pitch_fine` MESSAGES take **cents**, the `param_range` target is in **semitones**; **(2)** default range **±0.5 semitone** for both fines (the field accepts wider if the user sets it); **(3)** **ship the GRAIN half now** (fully independent) and **land the SMEAR half with/after P1**; **(4)** clamp the cents message to ±50 in the setter, strictly out of the Hz/speed path. Original sign-off items retained for the record:

1. **Units at the message API: cents vs semitones.** Recommend the dedicated `pitch_fine`/`smear_pitch_fine` MESSAGES take **CENTS** (Moog convention; the setter divides by 100 to store semitones), while `param_range pitch_fine -0.5 0.5` is given in **semitones** (consistent with `pitch_range`). Confirm vs a single all-semitones API (message also in semitones, no cents anywhere) — simpler but less Moog-intuitive.
2. **Range bounds: ±0.5 semitone for BOTH fines.** Recommend both `pitch_fine_range` and `smear_pitch_fine_range` default to `min=-0.5, max=0.5` (the ±50-cent spec), and the grain fine reuses the same ±0.5 default as the smear fine. Confirm vs a wider modulation range (e.g. allow `param_range pitch_fine -1 1` for ±1-semitone vibrato) — the field allows any min/max the user sets; the question is only the DEFAULT.
3. **P1 dependency for the smear half.** The SMEAR fine's apply site is inside P1's not-yet-merged note→Hz block (`ligase~.c:908-925`) and adds to P1's `semitone` accumulator + reads P1's `smear_pitch_control.semitone_fine`. Recommend: **ship the GRAIN half now (fully independent), land the SMEAR half with or after P1.** Confirm the split (grain-now / smear-with-P1) vs holding all of P3 until P1 merges.
4. **UI sanity clamp on the cents message.** Recommend clamping the `pitch_fine`/`smear_pitch_fine` message arg to ±50 cents in the setter (UI sanity), kept strictly OUT of the Hz/speed path (the smear `[20,0.45·sr]` clamp remains the sole frequency bound; the grain ±4.0 `final_speed` clamp at `grain.c:848-` remains the sole speed bound). Confirm vs no message clamp (let `param_range` go arbitrarily wide and only the message be free-form).

### Step 1 → GATE B (types + init, no behavior) — GRAIN

Add `float pitch_fine;` + `param_range_t pitch_fine_range;` to `pitch_control_t` (`types.h:424-425`, before the brace at `:426`). Init in `scheduler_create` (`grain.c`, after `:563`): `pitch_fine = 0`, `pitch_fine_range = default_range` with `min=-0.5, max=0.5`, enabled left 0. **GATE:** `make clean && make` warning-free; a fresh object has `pitch_fine == 0` and `pitch_fine_range.enabled == 0`, and the pitch switch is unchanged ⇒ identical behavior.

### Step 2 → GATE C (per-grain apply) — GRAIN

Add the fine block between the switch close (`grain.c:843`) and the `last_semitone` store (`grain.c:846`): `sample_param_range(&pitch_fine_range, &perlin_state, pitch_fine)` → `current_semitone += fine` → recompute `final_speed`. **GATE:** `make clean && make` warning-free; with the range disabled and `pitch_fine == 0`, `final_speed` is byte-for-byte the old value for every mode (the recompute of `base_speed * semitones_to_speed(current_semitone)` reproduces each case's inline multiply, and OFF stays `base_speed * 2^(0/12) = base_speed`); the block allocates nothing and calls no parse/`gensym` (audio-thread safe).

### Step 3 → GATE D (target + message) — GRAIN

Add the `get_param_range_by_name` `pitch_fine` entry (after `:3759`), the `rand_type` all-list entry (after `:4049`), `ligase_pitch_fine` (after `:4360`), and its `class_addmethod` (near `:5276`). **GATE:** `make clean && make` warning-free; `pitch_fine 50` sets `pitch_control.pitch_fine = 0.5`; `param_range pitch_fine -0.5 0.5` resolves (no "unknown parameter"); `rand_type perlin_2d_3 pitch_fine` and a bare `rand_type sine` both reach the range.

### Step 4 → GATE E (smear half) — FORWARD DEP on P1

**Precondition: P1 (`Plans/smear_pitch.md`) is merged** — its `smear_pitch_control_t`, `semitone` accumulator, and note→Hz `powf` exist in-tree. Then: add `param_range_t smear_pitch_fine_range;` to `scheduler_t` (next to `smear_feedback_range`) + `float semitone_fine;` to `smear_pitch_control_t`; init both in `scheduler_create` (`grain.c:543` for the range, P1's smear init block for `semitone_fine = 0`); add the per-block fine into P1's `semitone` before its `powf`; add the `smear_pitch_fine` `get_param_range_by_name` entry, the `rand_type` all-list entry, `ligase_smear_pitch_fine` (after `:3447`), and its `class_addmethod` (near `:5217`). **GATE:** `make clean && make` warning-free; `smear_pitch_fine 50` detunes the resonator note by ~+50 cents; `param_range smear_pitch_fine -0.5 0.5` resolves; with the range disabled + `semitone_fine == 0`, the resonator Hz is byte-for-byte P1's behavior.

### Step 5 → GATE F (verify, headless)

Build; run the acceptance patches under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1`), recording live output via `writesf~` and measuring pitch/playback-rate (grain) and resonant-peak Hz (smear) via FFT. Confirm all acceptance criteria; confirm no regression in the bare pitch modes (0–5), in `smear_frequency`/`smear_frequency_range`, and in `pattern pitch`. Update the manual's pitch + smear sections to document `pitch_fine`/`smear_pitch_fine` (cents), the `param_range`/`rand_type`/`pattern` targets, the per-grain vs per-block cadence, and the fine-as-offset semantics.

## Acceptance criteria (headless-testable with `pd -nogui -nosound`)

All via `pd -nogui -nosound -stderr -path . <patch>.pd`; record noise into the reel, granulate / ring the smear, capture output via `writesf~`, and measure pitch (grain playback-rate via FFT peak ratio, or the per-grain `final_speed`/semitone trace if exposed) and the smear resonant peak (FFT peak Hz).

1. **Grain fine raises pitch by ~half a semitone.** With `pitch_mode 1` (SEMITONES) + `pitch_semitones 0` (so the source contributes 0), `pitch_fine 50` raises grain pitch by **+0.5 semitone** — the playback-rate / pitch ratio is `2^(0.5/12) ≈ 1.0293` vs the `pitch_fine 0` baseline (measured as the FFT-peak frequency ratio or the `final_speed` ratio). `pitch_fine -50` lowers it by the same (ratio `≈ 0.9715`). `pitch_fine 25` ⇒ ratio `2^(0.25/12) ≈ 1.0145`. Verifies the per-grain offset + the cents→semitones conversion (50 cents → 0.5 semitone, NOT 50 semitones — a 100× error would give an absurd ratio).
2. **Fine stacks on the source, every mode.** `pitch_mode 1` + `pitch_semitones 12` + `pitch_fine 50` ⇒ total `12.5` semitones (ratio `2^(12.5/12)`), i.e. the fine ADDS to the 12-semitone source, not replaces it. `pitch_mode 0` (OFF) + `pitch_fine 50` ⇒ pitch rises +0.5 semitone above the raw speed inlet (confirming the post-switch recompute reaches OFF, which previously left `final_speed = base_speed`).
3. **Modulating `pitch_fine` detunes over time.** `param_range pitch_fine -0.5 0.5` + `rand_type sine pitch_fine` (or `pattern pitch_fine [ -0.5 0 0.5 ]`) makes the grain pitch wobble/step within ±50 cents over time — the measured FFT peak drifts sinusoidally / steps across the captured WAV, vs a static peak when the range is disabled. Verifies the per-grain `sample_param_range` path and the modulation-target registration (`param_range`/`rand_type`/`pattern` all resolve `pitch_fine`).
4. **Smear fine shifts the resonator Hz by the cents** (after P1). `smear_pitch_source 1` + `smear_pitch_semitones 0` (rings at the 69/440 reference, ~440 Hz) + `smear_pitch_fine 50` ⇒ the resonant peak moves to `440 · 2^(0.5/12) ≈ 452.9 Hz` (+50 cents); `smear_pitch_fine -50` ⇒ `440 · 2^(-0.5/12) ≈ 427.5 Hz` (−50 cents). `param_range smear_pitch_fine -0.5 0.5` + `rand_type sine smear_pitch_fine` makes the resonant peak vibrato within ±50 cents. Verifies the per-block smear offset feeds raw Hz through the sole `smear_update_coeffs` clamp.
5. **Fine 0 = no change / backward compat.** A patch that never sends `pitch_fine`/`smear_pitch_fine` and never enables their ranges produces byte-for-byte the same grain pitch and resonator Hz as today (and as P1 alone for smear). Explicitly: `pitch_fine 0` then `pitch_fine 50` then `pitch_fine 0` returns to the exact baseline; the disabled range returns `pitch_fine` (the base) verbatim via `sample_param_range`. `make clean && make` warning-free; `test_delay.pd` clean; pitch modes 0–5, `smear_frequency`/`smear_frequency_range`, and `pattern pitch` all unregressed.

## Risks / out-of-scope

**Risks**

- **Units 100× error (cents vs semitones).** Passing cents into `current_semitone`/`semitone` instead of semitones would detune by 100× (50 "cents" → 50 semitones). Mitigated by the single conversion point (the setter divides by 100), the SEMITONES-typed base field + range bounds (`min=-0.5, max=0.5`), and AC1 (which fails loudly on a 100× error). The `param_range` API is documented as semitones to keep the apply-site math pure.
- **Per-grain Perlin reachability.** The grain fine samples `&sched->perlin_state` per grain. Confirmed reachable: `&sched->perlin_state` is a direct member of `scheduler_t` and is already passed to `sample_param_range` in the `PITCH_MODE_RANGE` case (`grain.c:794-796`) — zero new plumbing. No risk, stated to forestall a "how do I reach perlin from the trigger" question.
- **`final_speed` recompute correctness.** The fine block recomputes `final_speed = base_speed * semitones_to_speed(current_semitone)` once after the switch, overriding each case's inline multiply. With fine 0 this must reproduce every case's value exactly (it does: same `current_semitone`, same `base_speed`; OFF gives `base_speed * 2^0 = base_speed`). AC2/AC5 verify across modes including OFF. The recompute is the deliberate mechanism for "fine applies to OFF too" — not a regression.
- **Base-value arg must be the scalar, not 0.** Unlike the B19 smear sites (which pass `0.0f` as `base_value` and rely on the `.enabled` guard), the fine apply MUST pass `pitch_fine` / `semitone_fine` as `base_value`, or the plain `pitch_fine <cents>` setter is silently discarded when the range is disabled. Called out in the apply snippets and in AC5.
- **Two-writer interaction on smear freq (after P1).** The smear fine adds to `semitone` *inside* P1's single `grain_smear_set_frequency` call — it does NOT add a second writer. It composes with P1's OVERRIDE precedence (the note→Hz path is already the last writer vs `smear_frequency_range`); the fine just shifts the note. No new race.
- **Zero-init footgun.** `pitch_fine`/`semitone_fine` default to 0 and the ranges to disabled (memset + explicit init), so an uninitialized fine cannot detune. Same "garbage-enabled clobbers live value" class the codebase documents at `grain.c:480-486`; mitigated by the explicit `scheduler_create` init.

**Out of scope (P3)**

- **The SMEAR note→Hz destination itself** (P1) — P3 only adds the fine RIDER to it; the destination, its sources, and its precedence are P1's.
- **Channel-aware MIDI routing** (P2) — the fine is an offset on top of whatever source resolves, including a P2 MIDI note; P3 does not touch the MIDI ingress.
- **Per-destination fine beyond grain + smear** — only the two existing pitch destinations get a fine; no third destination is introduced.
- **A fine wider than the modulatable range allows** — the DEFAULT range is ±0.5 semitone; the field accepts any `param_range min/max` the user sets, but "make ±50 cents the hardware spec" means the message/UI clamp is ±50 cents. Wider vibrato via `param_range pitch_fine -1 1` is allowed by the field but not the advertised default (GATE A.2).
- **Modifying the smear `[20,0.45·sr]` clamp or the grain ±4.0 `final_speed` clamp** — strictly forbidden; the fine feeds through both existing clamps, never duplicating or contradicting them.
- **Microtonal precision claims beyond float `powf`** — the fine is a float semitone added before a single `powf`; no special tuning-table machinery.
