# Plan P3: Pattern as pitch via the existing scale system (PITCH_MODE_PATTERN)

**Owner:** SLB
**Date:** 2026-06-24
**Status:** ✅ DONE (2026-06-24) — implemented and headless-verified (GATE E); all acceptance criteria pass; `make clean && make` warning-free; no regression (SCALE/RANGE/MIDI unchanged). Builds on P1+P2 (landed `79070dd`, `f18b108`). See Progress.
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — pattern subsystem build-out). (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related (NOT YET WRITTEN / NOT YET MERGED):** Plan P1 (intended path `Plans/pattern_notation.md`) — the parser + free-running BPM cycle clock + `pattern_table_t`/`pattern_eval_slot` infrastructure this plan reads from. Plan P2 (intended path `Plans/pattern_modulation.md`) — the `RAND_TYPE_PATTERN`/`rand_instance`-as-slot attach model and the dual-parse-site/print-site discipline. **P3 depends on BOTH** (P1 supplies the cache + clock; P2 establishes the attach/commit conventions and the `pattern <target> …` handler that P3 routes `pattern pitch …` through). **As of 2026-06-24 neither plan file exists and none of their symbols (`pattern_table_t`, `PATTERN_SLOTS`, `ligase_pattern`, `pattern_eval_slot`, `RAND_TYPE_PATTERN`, `cached_value`, `cached_is_rest`, `pattern_cycle`) is in the tree — P3 cannot be coded until P1+P2 land (see GATE A.2).**

---

## Progress (2026-06-24) — IMPLEMENTED + VERIFIED

Built on top of P1+P2; `make clean && make` warning-free.
- **types.h:** `PITCH_MODE_PATTERN` appended to `pitch_mode_t` (the value P1 deferred). (`pitch_pattern_slot` already added in P1.)
- **grain.c:** `case PITCH_MODE_PATTERN:` in `scheduler_trigger_grain` between SCALE and MIDI — reads `pattern[pitch_pattern_slot].cached_value` as a scale degree, applies **wrap + octave** (`idx = ((deg%count)+count)%count`, `oct = floorf(deg/count)`, `semitone = scale.semitones[idx] + 12*oct`), holds the previous semitone on a rest, then `base_speed * semitones_to_speed()`; reuses the existing `last_semitone` store + ±4.0 clamp verbatim. Slot/scale-not-ready → unison (0), never crashes.
- **ligase~.c:** `pattern pitch …` commit now sets `pitch_pattern_slot = slot` + `mode = PITCH_MODE_PATTERN` (P1 had loaded slot 7 with raw degrees; P3 wires the mode); `pattern_clear pitch` restores `mode → OFF` + `pitch_pattern_slot = -1` (GATE A(b)); `ligase_pitch_mode` widened to `0-5` (+ `"pattern"` name); outlet-3 note-change test extended to include `PITCH_MODE_PATTERN`. A `pattern_debug`-gated pitch trace logs the applied semitone on change.

| AC | Test (`/tmp/pat_tests/P3*.pd`; record+play to granulate, `pitch_scale 0 2 4 5 7 9 11`) | Result |
|----|------|--------|
| 1 Wrap+octave | `pattern pitch [ 0 1 2 3 4 5 6 7 ]` | semitones 0,2,4,5,7,9,11,**12** — degree 7 = root+octave (clamp would give 11); ✓ |
| 2 Tempo-locked | 8 degrees over the 2.0 s cycle | advance with the cycle, repeat each cycle; ✓ |
| 3 Alternation | `pattern pitch < 0 4 7 >` | one degree/cycle → semitone 0 (c0), 7 (c1), 12 (c2); ✓ |
| 4 Nested | `pattern pitch [ 0 [ 4 7 ] ]` | 0 (first half), then 4, 7 (quarters) → 0,7,12; ✓ |
| 6 Mode plumbing | `pitch_mode 5`/`6`/`pattern pitch`/`pattern_clear pitch` | 5 ok ("pattern"), 6 rejected (0-5), auto-set on `pattern pitch`, clear→off; ✓ |
| 7 No regression | `pitch_mode 3` + `test_delay.pd` | scale mode works; delay WAV intact; ✓ |

AC5 (outlet-3 note-change bang) is wired via the **same** `last_semitone != prev_scale_semitone` diff that SCALE/RANGE already use (now including `PITCH_MODE_PATTERN`); the verified per-step semitone changes are exactly that signal, and a held rest re-stores the same semitone so it does not bang.

## Problem

The user wants to write [TidalCycles](https://tidalcycles.org)-style mini-notation and have it drive **pitch**, reusing the synth's existing scale system rather than inventing a parallel pitch path. In their words: sequences of scale degrees written as a Tidal-style pattern (`<0 4 7>` walking one-per-cycle, `[0 4 7]` arpeggiating across a cycle, nesting allowed), quantized on the same `[4/4, 3/8]`-style cycle clock as everything else, where the `<,>` notation maps to even per-cycle distribution and `[]` to subdivision — and crucially **pitch should ride the existing scale → speed machinery and the existing BPM/quantization**, not a new tuning engine. A pattern of scale degrees `0 1 2 3 4 5 6 7` on a 7-note scale should walk up the scale and into the next octave (degree 7 = root + 1 octave), which the current scale code does **not** do (it only clamps).

This plan is the **pitch slice**: take the already-parsed, already-cycle-advanced pattern cache (built by P1, attached the P2 way) and feed it into the per-grain pitch switch as scale degrees, adding the wrap+octave the codebase currently lacks, and reusing the existing `scale.semitones[] → semitones_to_speed → ±4.0 clamp → last_semitone` chain verbatim so note-change outlet behavior comes for free.

## Mechanics / target surface — the EXISTING code this extends

**Provenance note:** every reference below into the **existing (pre-pattern) code** was verified by reading `src/grain.c`, `src/ligase~.c`, `src/types.h` on 2026-06-24. References to P1/P2 symbols are **forward-dependencies** on unmerged work and carry no committed line numbers yet.

- **`pitch_mode_t` enum** — `src/types.h:397-403`. Currently `OFF, SEMITONES, RANGE, SCALE, MIDI` (values 0–4; `PITCH_MODE_MIDI` is the last member, at `types.h:402`). We **append** `PITCH_MODE_PATTERN` after `PITCH_MODE_MIDI` so it becomes value **5** and the existing 0–4 stay stable for patches sending numeric modes.
- **`pitch_control_t` struct** — `src/types.h:412-420`. Holds `mode, semitones, semitone_range, scale, midi_note, midi_enabled, last_semitone`. We add `int pitch_pattern_slot;` (which `perlin_state.pattern[]` slot supplies degrees; `-1` = none).
- **The per-grain pitch switch** — `scheduler_trigger_grain` in `src/grain.c:632`, the `switch (sched->pitch_control.mode)` at `src/grain.c:686-726`. The `PITCH_MODE_SCALE` case is at **`src/grain.c:708-716`**; the `PITCH_MODE_MIDI` case is at `src/grain.c:718-725`. We add a sibling `case PITCH_MODE_PATTERN:` between SCALE and MIDI.
- **`semitones_to_speed`** — `src/grain.c:86-88`, `powf(2.0f, semitones/12.0f)`. Reused verbatim. (`math.h` is already included at `grain.c:8`; `powf`/`floorf` already used at `grain.c:87`/`grain.c:1043`.)
- **`sample_scale_semitones`** — `src/grain.c:95-182`. Its index math at `src/grain.c:178-179` does `index=(int)(random_value*count)` then **clamps** `if (index >= count) index = count-1;` — i.e. **no modulo wrap, no octave shift**. This is exactly the gap PITCH_MODE_PATTERN fills (it does NOT call this function; it reads the deterministic pattern cache instead, because this function is hardwired to the stochastic `rand_type` generator).
- **`last_semitone` store** — `src/grain.c:729`, `sched->pitch_control.last_semitone = current_semitone;`. Reused; the new case must keep writing `current_semitone` before this line so the store captures it.
- **The ±4.0 speed clamp** — `src/grain.c:731-748`. Runs after the switch, reused verbatim.
- **Outlet-3 note-change block** — `src/ligase~.c:1620-1644` (this block lives in `ligase_perform`, i.e. the audio thread; it runs once per perform call). At **`src/ligase~.c:1631-1632`** the test is `mode == PITCH_MODE_SCALE || mode == PITCH_MODE_RANGE` (with the MIDI branch above it at 1625). We add `|| mode == PITCH_MODE_PATTERN` so a degree change bangs outlet 3 via the existing `last_semitone != prev_scale_semitone` diff at `src/ligase~.c:1634`.
- **`ligase_pitch_mode`** — `src/ligase~.c:3848-3865`. Bound check `if (m < 0 || m > 4)` at **`src/ligase~.c:3850`**, error text at 3851, `mode_names[]` at **`src/ligase~.c:3858`** (`{"off (speed controls speed)", "semitones", "range", "scale", "midi"}`). Widen to `m > 5` and append `"pattern"`.
- **`ligase_pitch_scale`** — `src/ligase~.c:3931-3953`. The validate-then-commit float-list template (validate all `A_FLOAT` at 3939-3944, bound to `MAX_SCALE_NOTES`, commit last). The pitch degree load reuses this discipline.
- **`get_param_range_by_name`** — `src/ligase~.c:3241-3290`, returns `NULL` for any unknown name (fall-through at `ligase~.c:3289`). `"pitch"` is **not** in this table, so the P3 fork "if `argv[0]` is `pitch` (not a `get_param_range_by_name` hit)" is feasible: `pitch` correctly returns NULL and routes to the pitch path.
- **The P1/P2 `pattern <target> …` handler** (`ligase_pattern`) — **does not exist yet.** P1 will register it via `class_addmethod` adjacent to the existing pitch-method cluster (the pitch `class_addmethod` block is at `src/ligase~.c:4758-4762` today; P1's new registration would go near there). P3 adds the branch: when `<target>` is the literal symbol `pitch`, fill a pitch slot and switch pitch mode.
- **`perlin_state_t`** — `src/types.h:472-513`. P1 adds `pattern_table_t pattern[PATTERN_SLOTS]` here; P3 reads `sched->perlin_state.pattern[slot]` from the pitch case. The whole scheduler struct is zeroed by the `memset(sched, 0, sizeof(scheduler_t))` at `src/grain.c:394`, so `pitch_pattern_slot` (in `pitch_control_t`, inside the scheduler) is zero-init covered — but **0 is a valid slot index**, so see the Risks section: we explicitly set `pitch_pattern_slot = -1` as the inactive sentinel in `scheduler_create` rather than relying on the memset-to-0.

## Design

### Data structures (`src/types.h`)

Append to `pitch_mode_t` (after `PITCH_MODE_MIDI` at `types.h:402`), keeping 0–4 stable:

```c
typedef enum {
    PITCH_MODE_OFF,          // 0
    PITCH_MODE_SEMITONES,    // 1
    PITCH_MODE_RANGE,        // 2
    PITCH_MODE_SCALE,        // 3
    PITCH_MODE_MIDI,         // 4
    PITCH_MODE_PATTERN       // 5  — pattern-driven scale-degree stepper (append; keeps 0-4 stable)
} pitch_mode_t;
```

Add one field to `pitch_control_t` (`types.h:412-420`):

```c
    int pitch_pattern_slot;  // which perlin_state.pattern[] slot supplies scale degrees
                             // for PITCH_MODE_PATTERN; -1 = none / inactive
```

No other struct changes here. `pattern_table_t pattern[PATTERN_SLOTS]` and `pattern_step_t` will live in `perlin_state_t` (added by P1); P3 only **reads** them. The pattern leaf `value` field holds, for a pitch slot, the **raw scale degree as a float** (P1 stores leaf values as `float`; P2 normalizes 0..1 for param targets, P3 keeps them as integer degrees for the `pitch` target — see Loading below).

### The pitch case (`src/grain.c`, between PITCH_MODE_SCALE at 708-716 and PITCH_MODE_MIDI at 718-725)

Insert a new `case PITCH_MODE_PATTERN:` immediately after the `PITCH_MODE_SCALE` case (after `grain.c:716`), before `PITCH_MODE_MIDI` (`grain.c:718-725` stays untouched). It reads the **already-block-advanced cache** written by P1's `pattern_eval_slot` — it does NOT advance any clock and does NOT call `sample_scale_semitones`. **This case runs on the audio thread (`scheduler_trigger_grain` is called from `ligase_perform` at `ligase~.c:1091/1120/1224`), so it must only READ the cache and do plain arithmetic — no malloc, no binbuf parse, no `gensym`.**

```c
case PITCH_MODE_PATTERN:
    {
        int slot = sched->pitch_control.pitch_pattern_slot;
        int count = sched->pitch_control.scale.count;
        // Default: if slot/scale not ready, leave current_semitone = 0 (unison, no transpose)
        if (slot >= 0 && slot < PATTERN_SLOTS && count > 0) {
            pattern_table_t *pt = &sched->perlin_state.pattern[slot];
            if (pt->step_count > 0) {                 // table fully built (publish barrier from P1)
                if (pt->cached_is_rest) {
                    current_semitone = sched->pitch_control.last_semitone; // rest: HOLD last semitone
                } else {
                    int degree = (int)pt->cached_value;                    // leaf value = scale degree
                    // WRAP + OCTAVE (the gap sample_scale_semitones lacks; grain.c:178-179 only clamps)
                    int idx = ((degree % count) + count) % count;          // [0, count-1]
                    int oct = (int)floorf((float)degree / (float)count);   // octave compensation
                    current_semitone = sched->pitch_control.scale.semitones[idx]
                                     + 12.0f * (float)oct;
                }
            }
        }
        final_speed = base_speed * semitones_to_speed(current_semitone);  // verbatim from SCALE case
    }
    break;
```

Notes on the rest path: a rest (`~`) holds the previous note. Since `last_semitone` (written at `grain.c:729`) already stores the prior `current_semitone`, on a rest we set `current_semitone = last_semitone` so the pitch is unchanged AND `grain.c:729` re-stores the same value (so the outlet-3 diff at `ligase~.c:1634` correctly does **not** fire on a held rest). Because P1's evaluator sets `cached_is_rest` and holds `cached_value` across rests, either source is consistent; reading `last_semitone` is the cheapest correct choice. (Read `last_semitone` only on the rest branch and read `cached_value` only on the non-rest branch — no read of a value that won't be used.)

Everything downstream is reused unchanged: `semitones_to_speed` (`grain.c:86`), the `last_semitone` store (`grain.c:729`), the ±4.0 clamp (`grain.c:731-748`). `base_speed` is the raw speed inlet (set at `grain.c:679` because `mode != PITCH_MODE_OFF`), so pitch is a multiplicative transposition from the speed inlet, identical to SCALE.

**Cadence (documented limit):** the pitch case runs **per grain** (`scheduler_trigger_grain` fires at grain onset), but it READS `pattern[slot].cached_value`, which P1 advances **once per DSP block on the BPM-locked cycle clock**. So pitch is **tempo-locked, not grain-locked** — a grain firing at cycle-phase p reads whatever scale step the cycle clock currently sits on. Sparse grains (low density / large IOT) can skip steps; dense grains re-hit the same step until the cycle advances. This is the intended TidalCycles semantics (steps are tempo positions, not grain events) and matches the modulation behavior in P2.

### Loading: routing `pattern pitch …` to the pitch slot

P1/P2's `ligase_pattern` handler parses the token stream into a scratch `pattern_table_t` (on the **message thread**) and resolves `<target>`. P3 adds the `pitch`-target branch at the **commit** step:

1. After argv[0] is read, if `argv[0]` is the symbol `pitch` (and `get_param_range_by_name(x, "pitch")` returns NULL, which it does — `pitch` is not in that table), take the **pitch path**:
   - Parse leaf values as **integer scale degrees** — do NOT normalize to 0..1. (The param path in P2 normalizes; the pitch path keeps `value` as the raw degree float. This is the single behavioral fork keyed on `target == pitch`.)
   - Choose the pitch slot: a fixed dedicated slot for pitch (recommend `PATTERN_SLOTS - 1`, the last slot, so it never collides with the value/mod slots P2 hands out by `rand_instance`). Store it in `sched->pitch_control.pitch_pattern_slot`.
   - Commit the scratch table into `perlin_state.pattern[pitch_pattern_slot]` exactly the P1 way (set `step_count` LAST as the publish barrier).
   - Set `sched->pitch_control.mode = PITCH_MODE_PATTERN`.
   - `post("ligase~: pitch pattern set (%d steps), pitch_mode → pattern", …)`. If `scale.count == 0` at this point, additionally `post` a hint that no `pitch_scale` is loaded (the sound will be unison until one is).
2. `pattern_clear pitch` restores `pitch_control.mode` to `PITCH_MODE_OFF` (unless P1/P2 already save a prior mode — see GATE A.1(b)), sets `pitch_pattern_slot = -1`, and zeroes the pattern slot's `step_count`.

No new `class_addmethod` is needed for pitch — it rides the existing `pattern` selector registered by P1. **Pd wire-syntax feasibility (confirmed):** `pattern pitch < 0 1 2 3 4 5 6 7 >` and `pattern pitch [ 0 4 7 ]` survive binbuf because `<`, `>`, `[`, `]` become ordinary whitespace-separated `A_SYMBOL` atoms (`gensym`) and the degrees are `A_FLOAT`. **No comma is ever typed inside a P3 message**, so the comma-as-message-separator hazard does not apply to P3. (The user's cycle notation `[4/4, 3/8]` does contain a comma that Pd treats as a separator — but that surface belongs to P1's `pattern_cycle`, not P3; P3 routes through P1.)

### Mode setter widening (`src/ligase~.c:3848-3865`)

```c
    if (m < 0 || m > 5) {                                 // was: m > 4 (ligase~.c:3850)
        pd_error(x, "ligase~: pitch_mode must be 0-5 "
                    "(0=off, 1=semitones, 2=range, 3=scale, 4=midi, 5=pattern)");
        return;
    }
    ...
    const char *mode_names[] = {"off (speed controls speed)", "semitones",
                                "range", "scale", "midi", "pattern"};   // appended "pattern" (ligase~.c:3858)
```

Sending `pitch_mode 5` directly is allowed but is a no-op for sound unless a `pattern pitch …` has loaded the slot (the case degrades to unison `current_semitone = 0` when `pitch_pattern_slot < 0`, `count == 0`, or `step_count == 0`). The normal path sets the mode automatically at `pattern pitch …` commit.

### Outlet-3 note-change (`src/ligase~.c:1631-1632`)

```c
        } else if (x->scheduler->pitch_control.mode == PITCH_MODE_SCALE ||
                   x->scheduler->pitch_control.mode == PITCH_MODE_RANGE ||
                   x->scheduler->pitch_control.mode == PITCH_MODE_PATTERN) {   // added
```

This is free: the stepper already writes `last_semitone` at `grain.c:729`, and the existing diff against `prev_scale_semitone` (`ligase~.c:1634-1636`) fires the bang on `x_splice_end_out` whenever the degree changes. A held rest re-stores the same `last_semitone`, so it correctly does not bang.

### Cycle-clock math (inherited from P1, restated for the pitch reader)

P3 introduces no new timing. Pitch reads `pattern[slot].cached_value`, which P1 advances each DSP block by `inc = ((double)n / sample_rate) / cycle_total_sec`, where `cycle_total_sec` derives from the `pattern_cycle` segment list via the existing `ms_per_whole_note = (60000.0 / bpm) * 4.0` grid formula (`ligase~.c:2532`).

**BPM guard (inherited requirement, MUST hold in P1):** every existing BPM-derived computation in the codebase is gated on `x->bpm > 1.0` (e.g. `ligase~.c:761, 965, 1170`), and `ligase_bang` only recomputes when `interval_ms > 0.0` (`ligase~.c:2524`). The cycle clock divides by `bpm` in `(60000.0 / bpm) * 4.0`, so P1's advance MUST guard `bpm > 1.0` (hold the clock / hold the current step when no valid tempo) — otherwise the divide blows up at `bpm == 0`. P3 inherits this; it does not re-implement it, but the dependency is load-bearing for the pitch reader.

Worked examples (all at the default 1-bar cycle): for `pattern pitch [ 0 4 7 ]` at bpm=120: `cycle_total_sec = ((60000/120)*4.0)/1000 = (500*4)/1000 = 2.0 s`, three even steps of 0.667 s each → degrees 0,4,7 each held for 2/3 s, looping. For `pattern pitch < 0 1 2 3 4 5 6 7 >` the alternation advances one held degree per **cycle** (per P1's `pattern_cycle_index mod alt_group_count`), so degree 0 holds for a full 2 s cycle, then 1, etc.

## Steps & gates

- **GATE A (approval) — APPROVED by owner 2026-06-24 (Seq 46).** Recommended options confirmed: (a) dedicate the last `PATTERN_SLOTS-1` slot to pitch; (b) `pattern_clear pitch` → `PITCH_MODE_OFF`; (c) raw-degree leaf values keyed on `target == pitch`. The **P1+P2-merged precondition below is a build-order gate, not a design decision, and still holds.** Original sign-off items retained for the record:
  1. **OpenDecisions to sign off:** (a) pitch slot allocation — dedicate the **last** `PATTERN_SLOTS-1` slot to pitch vs. share the P2 `rand_instance` pool? Recommend **dedicate** to avoid a value-pattern and a pitch-pattern fighting over one slot. (b) `pattern_clear pitch` restore target — revert to the **pre-pattern mode** (requires saving it) vs. unconditionally `PITCH_MODE_OFF`? Recommend **OFF** for simplicity unless P1/P2 already save prior mode. (c) raw-degree vs. normalized leaf values keyed on `target == pitch` — confirm the single fork point is acceptable.
  2. **HARD BLOCKER — Confirm P1 + P2 are written AND merged.** As of 2026-06-24 they are NOT: no `Plans/pattern_notation.md` / `Plans/pattern_modulation.md` exist, and grep finds zero of `pattern_table_t`, `PATTERN_SLOTS`, `pattern_eval_slot`, `cached_value`, `cached_is_rest`, `ligase_pattern`, `RAND_TYPE_PATTERN` in the tree. This plan **does not compile** without them. Do not start P3 until they land.
- **Step 1 → GATE B (types).** Append `PITCH_MODE_PATTERN` to `pitch_mode_t` (after `types.h:402`); add `int pitch_pattern_slot;` to `pitch_control_t` (`types.h:420`). Initialize `pitch_pattern_slot = -1` in `scheduler_create` (after the `memset` at `grain.c:394`, since 0 is a valid slot). Build clean (`make clean && make`, no warnings).
- **Step 2 → GATE C (pitch case + clamp/last_semitone reuse).** Add the `case PITCH_MODE_PATTERN:` in `scheduler_trigger_grain` between SCALE (ends `grain.c:716`) and MIDI (`grain.c:718`), with wrap+octave and the rest-hold. Confirm it leaves `current_semitone` set before `grain.c:729` and that `final_speed` flows into the ±4.0 clamp unchanged. Confirm the case allocates nothing and calls no parse/`gensym` (audio-thread safety).
- **Step 3 → GATE D (loading + mode + outlet).** Route the `pitch` target through `ligase_pattern` (raw degrees, set slot + mode, commit-last, on the message thread). Widen `ligase_pitch_mode` to `0-5` + `mode_names[]` (`ligase~.c:3850,3858`). Add `PITCH_MODE_PATTERN` to the outlet-3 test (`ligase~.c:1631`). Add `pattern_clear pitch` restore.
- **Step 4 → GATE E (verify).** Build; run the headless acceptance tests below with `pd -nogui -send "…"` driving a patch that bangs ligase~ at a known interval to set BPM, captures live output via `writesf~`, and reads back semitone/outlet behavior. Update the manual's pitch section to document `pattern pitch …` and the tempo-locked-not-grain-locked caveat.

## Acceptance criteria (headless-testable with `pd -nogui`)

1. **Wrap + octave correctness.** With a 7-note scale `pitch_scale 0 2 4 5 7 9 11` and `pattern pitch [ 0 1 2 3 4 5 6 7 ]`, the produced `last_semitone` sequence across one cycle is `0,2,4,5,7,9,11,12` — i.e. degree 7 = root + 1 octave (`12.0`), NOT a re-hit of degree 6 (`11`). This is the behavior `sample_scale_semitones` (clamp-only, `grain.c:178-179`) cannot produce; the test fails on the old clamp path and passes on the new wrap path. Verify by logging `last_semitone` (or the resulting `final_speed` = `base_speed * 2^(semitone/12)`) per step.
2. **Tempo-locked step durations.** At a bang interval giving bpm=120 with the default 1-bar cycle (`cycle_total_sec = 2.0 s`), `pattern pitch [ 0 4 7 ]` holds each degree for ~0.667 s before the next degree appears, looping. Measured per-step hold durations (via the timestamps of outlet-3 note-change bangs) match `cycle_total_sec / step_count` within one DSP block. Doubling bpm to 240 halves the hold durations — proving BPM-lock. (Also confirm no divide-by-zero / NaN at bpm=0: with no clock bangs the stepper must hold, not advance.)
3. **Alternation walks one degree per cycle.** `pattern pitch < 0 1 2 3 4 5 6 7 >` at bpm=120 (2 s cycle) holds degree 0 for the entire first 2 s cycle, degree 1 for the second, etc. — one note-change bang per cycle, not per subdivision.
4. **Nested subdivision.** `pattern pitch [ 0 [ 4 7 ] ]` subdivides correctly: degree 0 occupies the first half of the cycle, then 4 and 7 split the second half (degree 0 for 1.0 s, then 4 for 0.5 s, then 7 for 0.5 s at bpm=120). Verified by note-change bang timestamps.
5. **Note-change bangs fire in pattern mode.** Outlet 3 (with `outlet3_mode 1`) bangs exactly once per degree change in `PITCH_MODE_PATTERN`, and does NOT bang on a held rest `~` (e.g. `pattern pitch [ 0 ~ 7 ]` produces 2 bangs per cycle for the 0 and 7 transitions, the rest holds 0's semitone).
6. **Mode plumbing.** `pitch_mode 5` is accepted (no `pd_error`) and `post`s "pattern"; `pitch_mode 6` is rejected with the 0-5 error. Sending `pattern pitch …` auto-sets the mode to 5 (confirmed via `get_state`/query if exposed, else via observed pitch behavior). `pattern_clear pitch` returns pitch behavior to flat/unison.
7. **No regression in SCALE/RANGE/MIDI.** Existing `pitch_mode 3`/`pitch_scale`/`pitch_rand_type` (stochastic scale) still produce the same stochastic semitones; modes 0–4 unchanged numerically. Build is warning-free.

## Risks / out-of-scope

- **P1/P2 not yet built (hard dependency).** This is the top risk: P3's entire data source (`pattern_table_t`, the cycle clock, `cached_value`/`cached_is_rest`, `ligase_pattern`) does not exist as of 2026-06-24. P3 is unbuildable until P1+P2 merge. GATE A.2 blocks on this.
- **`pitch_pattern_slot = 0` ambiguity (zero-init footgun).** The `memset` at `grain.c:394` zeroes `pitch_control`, so an uninitialized `pitch_pattern_slot` would default to **0 — a valid slot index**, not "none." The pitch case guards on `mode == PITCH_MODE_PATTERN` (the switch only enters this case in that mode), so a stray 0 cannot drive pitch unless the mode is explicitly pattern; nonetheless **explicitly set `pitch_pattern_slot = -1` in `scheduler_create`** as the inactive sentinel and guard `slot >= 0` in the case. This is the recurring "garbage-enabled clobbers live value" class the codebase already documents at `grain.c:384-394`.
- **BPM=0 / bpm≤1 in the cycle clock.** The cycle math divides by `bpm` (`(60000/bpm)*4.0`). P1 must guard `bpm > 1.0` (matching the existing `ligase~.c:761,965,1170` guards) and hold the step when there is no valid tempo. P3 inherits this; if P1 omits the guard, pitch will read NaN. Flag for P1 review.
- **Sparse-grain step skipping / re-hitting.** Because pitch reads a tempo clock at grain rate, low grain density can skip degrees and high density can re-trigger the same degree. This is inherent (the same trade-off P2 documents for modulation) and is a documented limit, not a bug. Out of scope to "fix" by per-grain advancing — that would break tempo-lock and the shared-cycle semantics with modulation.
- **Octave stacking can exceed the ±4.0 clamp.** Degrees far above `count` add `12*oct` semitones; combined with a high base speed this can exceed the ±4.0 `final_speed` clamp (`grain.c:731-748`, ≈ ±48 semitones / 4 octaves) and silently clamp/alias-protect. Keep degree ranges within ~±48 semitones of base or expect clamping. Document, do not change the clamp.
- **`scale.count == 0`.** If `PITCH_MODE_PATTERN` is set with no `pitch_scale` loaded, the case leaves `current_semitone = 0` (unison) — degrade, never crash (guarded by `count > 0`). The user must load a scale first; the `pattern pitch` handler `post`s a hint if `scale.count == 0` at load time.
- **Fractional degrees.** Pattern leaf `value` is a float; `(int)deg_f` truncates toward zero. Non-integer degrees (e.g. `2.5`) are truncated to the lower degree — microtonal/quarter-tone degrees are **out of scope** (scale degrees are integers indexing `scale.semitones[]`). If wanted later, a separate "raw semitone pattern" mode could bypass the scale index.
- **Note on the `14112.0` constant.** `14112.0` is Pd's `TIMEUNITPERMSEC = 32*441` (clock-time-units per millisecond, `ligase~.c:2518-2522`), **not** a BPM unit. P3 timing derives entirely from `bpm = 60000.0/interval_ms` and `ms_per_whole_note = (60000.0/bpm)*4.0`; `14112.0` only appears upstream in `ligase_bang` when converting Pd logical time to ms. Stated here to prevent any future misreading.
- **Out of scope for P3:** the parser, the cycle clock, `pattern_cycle`, `pattern_eval_slot`, and `RAND_TYPE_PATTERN` attach machinery (all P1/P2); polyphony / chords (Tidal comma=stack — **not applicable** to a single-pitch stepper, intentionally unsupported); per-step octave glyphs in the notation; MIDI-out of the stepped notes (only the existing speed-transpose + outlet-3 bang are wired).