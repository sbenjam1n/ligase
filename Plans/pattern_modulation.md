# Plan P2: Pattern as a modulation source on any param_range target

**Owner:** SLB
**Date:** 2026-06-24
**Status:** ✅ DONE (2026-06-24) — implemented and headless-verified (GATE E); all acceptance criteria pass; `make clean && make` warning-free at every gate; no regression in the existing audio path. Builds on P1 (landed `79070dd`). See Progress.
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — pattern subsystem build-out); promote into §2 (ON DECK) when scheduled. (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related:** Plan P1 (`pattern_notation.md`) — the parser + clock + nesting + `pattern_eval_slot` foundation this attaches to (HARD DEPENDENCY). Plan P3 (`pattern_pitch.md`) — the `PITCH_MODE_PATTERN` sibling that reuses the same slot cache for scale-degree stepping.

> **ADVERSARIAL-VERIFY NOTE (2026-06-24):** verified line-by-line against `src/ligase~.c` (4778 lines), `src/types.h` (630), `src/grain.c` (1130). Two code-accuracy defects corrected (see ⚠ markers): (1) the `pattern*` selectors are **not** registered at `4732-4737` — those lines are `param_range`..`rand_type`; P1 has not landed, so all "P1-owned surface" refs are TO-BE-CREATED, not existing reads; (2) **`smear` is not a modulatable range name** — only `smear_frequency/resonance/stages/feedback` are; all examples/tests retargeted. A `bpm>0` guard and a `min==max`-short-circuit attach-guard were added. Everything else checked out and is accurate.

## Progress (2026-06-24) — IMPLEMENTED + VERIFIED

Built across GATE C (read path) and GATE D (wire path); `make clean && make` warning-free at each.
- **GATE C (sampler + print):** `RAND_TYPE_PATTERN` added to `rand_type_t` (the value P1 deferred); `saved_rand_type`/`saved_rand_instance` appended to `param_range_t` (default initializer extended). `case RAND_TYPE_PATTERN:` added to **both** `sample_param_range` and `sample_scale_semitones` (reads `pattern[rand_instance].cached_value`, reading `rand_instance` directly to bypass the 0..3 clamp; neutral 0.5 when the slot is unloaded/out-of-range). Print fixes: `ligase_param_range` dump labels `"pattern"`; **`get_rand_type_name` SAW/SINE/SQUARE gap fixed** (they were mislabeled `"none"`) plus `"pattern"`.
- **GATE D (wire path):** `pattern_alloc_param_slot()` (reuse-or-first-free via `step_count==0`; slot `PATTERN_SLOTS-1` reserved for pitch). `ligase_pattern` first arg generalized: **param name** → resolve + auto-slot + attach; **`pitch`** → slot 7 (P3 wires mode); **numeric** → raw load (testing / two-step). Attach saves the prior source, sets `rand_type=PATTERN`/`rand_instance=slot`/`enabled=1`, and resets a collapsed `min==max` span to `[0,1]`. `ligase_pattern_clear` generalized (numeric slot | `pitch` | param-name restore-then-free). `pattern_N` branch added to **both** `ligase_rand_type` and `ligase_pitch_rand_type` ladders with an `is_pattern`-guarded relaxation of the 0..3 instance check (slots 0..PATTERN_SLOTS-1).

| AC | Test (`tests/pattern/P2*.pd`, via `modout` outlet → `[change]` → `[print]`) | Result |
|----|------|--------|
| Even map | `param_range modout1 100 900` + `pattern modout1 0.0 0.5 1.0` | outlet emits 100→500→900 at 0.667 s (= 2.0/3); ✓ |
| Nested + invert | `param_invert modout1 1` + `pattern modout1 [ 0.2 0.4 ] 0.8` (0..100) | 80→60→20 (inverted) at ¼,¼,½; ✓ |
| Outlets fire | all tests drive a `modout` target | `enabled && rand_type != NONE` gate passes pattern sources; ✓ |
| Clear/restore | `rand_type sine_1 modout1` → attach → `pattern_clear modout1` | "restored type sine", sine resumes (also proves the `get_rand_type_name` fix); ✓ |
| Slot ≥4 | `pattern 5 …` + `rand_type pattern_6 modout1` | reads slot 5 → 20/80 (bypasses the 0..3 clamp); ✓ |
| min==max guard | `param_range modout2 0.5` + `pattern modout2 0.0 1.0` | "reset map span to [0,1]", steps 0/1; ✓ |
| Slot allocation | modout2→slot 0, then `smear_frequency`→slot 1 | independent auto-allocated slots; ✓ |
| Wrong name | `pattern smear …` | `unknown parameter 'smear'` (only `smear_frequency`/etc. are valid); ✓ |

Alternation-on-a-param is covered by composition (P1 alternation eval + this attach read the same `cached_value`); the invert/slew tail is reused byte-identically (the pattern case only sets `random_value`). Regression: `test_delay.pd` clean, WAV intact.

**Scope notes:** the numeric-slot path stays load-only (no attach) for testing/two-step; the `ligase_pitch_rand_type` `pattern_N` branch + the `sample_scale_semitones` pattern case were both added (full ladder parity), letting `PITCH_MODE_RANGE/SCALE` optionally draw its selector from a pattern — distinct from P3's dedicated `PITCH_MODE_PATTERN` scale-degree stepper.

## Problem
The user wants to drive any modulatable parameter from a TidalCycles-style mini-notation pattern, not just the stochastic generators (rand/perlin/lorenz/nbody/sphere/saw/sine/square). Their stated requirements:

- A bare sequence of values distributes **evenly** over a cycle: `pattern smear_frequency 0.2 0.5 0.8` = three steps, each occupying 1/3 of the cycle. (This is canonical Tidal mini-notation: sequences are **space-separated**, not comma-separated; in Tidal `,` means *stack* (parallel voices), which is not applicable here and is intentionally unsupported. Pd's binbuf eats commas regardless.)
- **Nesting** subdivides: `pattern moog_cutoff [ 0.2 [ 0.5 0.9 ] 0.7 ]` — the inner `[ 0.5 0.9 ]` splits its parent's slot in half.
- **Alternation** (`<` `>`) walks one member per cycle (Tidal slowcat): `pattern smear_frequency < 0.2 0.8 >` plays 0.2 on even cycles, 0.8 on odd.
- The `[4/4, 3/8]`-style **quantization cycle** sets the musical length of one pattern cycle (`pattern_cycle 4/4 3/8`); when unset, the cycle is one bar and steps distribute evenly.
- Pitch-via-scale (`pattern pitch ...`) — deferred to P3; this plan is value/modulation targets only.
- **Reuse** the existing modulation pipeline (invert/min-max-map/slew), the existing BPM detection, and the existing quant-grid math. No new clock concepts beyond P1's fifth cycle clock.

P1 already delivers the parser, the `pattern` / `pattern_cycle` / `pattern_clear` selectors, the data structures, the free-running BPM-locked cycle clock, nesting, alternation, and the once-per-block `pattern_eval_slot()` evaluator that writes `pattern[slot].cached_value`. **What P1 does NOT do is attach a loaded slot to anything** — a slot can be filled and its phase advances, but no parameter reads it yet. P2 is the attachment slice: make `RAND_TYPE_PATTERN` a real modulation source that any `param_range_t` can select.

> ⚠ **PARAM-NAME CONSTRAINT (verified):** A "modulatable parameter" means a name that resolves in `get_param_range_by_name` (`ligase~.c:3241-3290`). The valid value/mod targets are: `speed`, `scanrate`, `organize`, `sos`, `iot`, `maxgrains`, `grainsize`, `grainstart`, `env_skew`, `gdelay`, `gdelay_feed`, `gdelay_tone`, `gdelay_mix`, `distortion`, `amplitude`, `pan`, `moog_cutoff`, `moog_resonance`, `moog_mix`, the `dist_*` set, `stut_reps`, the `bencina_*` set, **`smear_frequency` / `smear_resonance` / `smear_stages` / `smear_feedback`** (there is NO bare `smear` range — `smear` is only a READABLE value in the separate query dispatch at `3975`), and `modout1..4`. Any `pattern <name> ...` whose `<name>` is not in this list MUST error `unknown parameter` (mirroring the existing handlers) — and the user must not be told `smear` works.

## Mechanics / target surface — the EXISTING code this extends
All line numbers verified by reading `src/grain.c`, `src/ligase~.c`, and `src/types.h` on 2026-06-24.

- **The sampler** `sample_param_range(range, perlin_state, base_value)` — `src/grain.c:190`. Disabled → returns `base_value` (`grain.c:192`); **`min==max` → returns `min`** (`grain.c:197-199`) — this short-circuits BEFORE the `rand_type` switch (see the attach caveat in Design §2); reads `instance = range->rand_instance` into a LOCAL (`grain.c:202`), then clamps that local to 0..3 (`grain.c:206-208`, it does NOT write back to `range->rand_instance`, verified); a `switch(range->rand_type)` at `grain.c:214-284` sets `random_value` in [0,1]; then invert (`grain.c:287-289`), map `min + random_value*(max-min)` (`grain.c:292`), slew EMA (`grain.c:298-306`). The switch's **last case is `RAND_TYPE_SQUARE` ending at `grain.c:283`**, and the switch has **no `default:`** (an unknown enum leaves `random_value` at its init `0.0f`, `grain.c:201`) — the new `case RAND_TYPE_PATTERN:` goes immediately after `grain.c:283`. The 0..3 clamp at `grain.c:206-208` applies only to the LOCAL `instance` used by the stochastic cases; the pattern case reads `range->rand_instance` directly (slots 0..PATTERN_SLOTS-1) and never touches that local.
- **The rand_type enum** `rand_type_t` — `src/types.h:368-379`, last member `RAND_TYPE_SQUARE` at line 378 (no trailing comma). P1 adds `, RAND_TYPE_PATTERN` after it. As of 2026-06-24 this value is **absent**; P2's first step confirms P1 added it.
- **The per-block apply site** `ligase_update_inlets` — `src/ligase~.c:382`, called from `ligase_perform` at `ligase~.c:1587`. One `sample_param_range` call per parameter; result applied only when `range->enabled`. No change needed here — pattern attaches by setting `rand_type`/`rand_instance`, which this code already honours generically.
- **The 4 modulation outlets** — `src/ligase~.c:1598-1616`. Each gated on `range->enabled && range->rand_type != RAND_TYPE_NONE` (verified at 1598/1603/1608/1613). This gate already lets `RAND_TYPE_PATTERN` through (it is `!= RAND_TYPE_NONE`), so the outlets fire for pattern sources with **no code change** — the plan VERIFIES this and documents it as an explicit acceptance gate rather than assuming it.
- **The name→range dispatch** `get_param_range_by_name` — `src/ligase~.c:3241`. Already generic (see the verified key list above). A pattern attaches to ANY of these with zero per-param plumbing. **It does NOT contain `smear`, `pitch`, `grain_size`, or any name not listed above.**
- **DUAL parse sites (the footgun):**
  - `ligase_rand_type` — `src/ligase~.c:3467` (`A_GIMME`, signature `(x, s, argc, argv)`). Parses `<type>_<N> [param]`; the `strncmp` ladder is `ligase~.c:3478-3508` (last branch `square_` at 3502-3504, final `else` error at 3505-3508). Commit to a specific param at `ligase~.c:3526-3527` (`range->rand_type = rand_type; range->rand_instance = instance;`); the all-list loop at `ligase~.c:3568-3571`. **Instance validated 0..3 at `ligase~.c:3511-3514`** — this gate rejects pattern slots 0..7, so the pattern branch must take a separate validation path.
  - `ligase_pitch_rand_type` — `src/ligase~.c:3882`. **Registered `A_DEFSYMBOL`, signature `(x, s)`** (single symbol, NOT argc/argv — verified at 3882 and registration 4761). It reads only `s->s_name`, so a `pattern_N` branch that only inspects `type_str` transplants directly. Second, independent `strncmp` ladder at `ligase~.c:3888-3918` (same shape, instance check 0..3 at 3920-3923). Must learn the identical `pattern_` branch for parity.
- **TWO print sites (one with a latent bug):**
  - The `switch` in `ligase_param_range` at `src/ligase~.c:3316-3327` — has all 10 current cases (NONE..SQUARE); its `type_name` defaults to `"unknown"` (3315), so it does not mislabel as `"none"`. Needs `RAND_TYPE_PATTERN`.
  - `get_rand_type_name` at `src/ligase~.c:3991-4002` — **CONFIRMED latent gap: MISSING `RAND_TYPE_SAW`, `RAND_TYPE_SINE`, `RAND_TYPE_SQUARE`** (cases 3993-3998 cover RAND..SPHERE, then `RAND_TYPE_NONE`/`default: return "none"` at 3999-4000). State-dumps of saw/sine/square params already mislabel as `"none"`. P2 fixes this gap in the same edit that adds `RAND_TYPE_PATTERN`.
- **⚠ P1-owned surface this plan reads (DOES NOT EXIST as of 2026-06-24 — P1 must land it first; do NOT redefine, just consume):** `pattern_table_t pattern[PATTERN_SLOTS]` + `pattern_phase[]` + `pattern_cycle_index[]` in `perlin_state_t`; `pattern_eval_slot()`; the `cached_value` / `cached_is_rest` / `step_count` fields; `PATTERN_SLOTS` (=8, P1's choice); the `pattern` / `pattern_cycle` / `pattern_clear` A_GIMME selectors and their `class_addmethod` registrations. **CORRECTION: these registrations are NOT at `ligase~.c:4732-4737` — grep confirms NO `pattern*` selector is registered anywhere. Lines 4732-4737 are `param_range`, `param_base_value`, `param_slew`, `param_invert`, `param_lock`, `rand_type`.** P1 must ADD the three `class_addmethod(... gensym("pattern"...), A_GIMME, 0)` calls into the registration block (which currently ends ~`4761` at `pitch_scale`). P2 only ADDS attachment logic to the `pattern`/`pattern_clear` handlers; it does not own their parsing or registration.

## Design

### 1. The `RAND_TYPE_PATTERN` sampler case (`src/grain.c`, after `grain.c:283`)
A thin reader of the P1 cache. It must NOT be subject to the 0..3 instance clamp (pattern slots run 0..PATTERN_SLOTS-1 = 0..7), so it reads `range->rand_instance` independently and validates against `PATTERN_SLOTS`:

```c
case RAND_TYPE_PATTERN: {
    int slot = range->rand_instance;            // NOTE: pattern slots are 0..PATTERN_SLOTS-1, NOT 0..3
    if (slot < 0 || slot >= PATTERN_SLOTS) {     // out-of-range slot -> neutral, never read OOB
        random_value = 0.5f;
        break;
    }
    pattern_table_t *pt = &perlin_state->pattern[slot];
    if (pt->step_count < 1) {                    // slot not loaded -> neutral passthrough
        random_value = 0.5f;
        break;
    }
    // P1's pattern_eval_slot() has already written cached_value (normalized 0..1) and
    // cached_is_rest this block. A rest HOLDS the previous value: cached_value is left
    // unchanged by pattern_eval_slot on a rest step, so reading it verbatim is the hold.
    random_value = pt->cached_value;             // already in [0,1] per the P1 contract
    break;
}
```

Contract notes (enforced, not assumed):
- `cached_value` is guaranteed in [0,1] by P1's normalization, so the existing invert/map/slew tail (`grain.c:287-306`) runs **verbatim** and is correct: invert flips the step value, the map scales it into `[range->min, range->max]`, and `slew` cross-fades between steps (a stair-step at `slew=0`, a glide for `slew>0` — the same behaviour every other source enjoys).
- A **rest** (`~`) holds the previous value. P1's `pattern_eval_slot` does NOT overwrite `cached_value` on a rest step (sets `cached_is_rest=1`, leaves `cached_value` from the last non-rest step), so the sampler reads the held value with no extra branch. `cached_is_rest` is consumed by P3 (pitch) for `last_semitone` hold; for modulation the held `cached_value` is sufficient and `cached_is_rest` is read-but-not-acted-on here.
- The switch has no `default:`; this case is reached only when `range->rand_type == RAND_TYPE_PATTERN`, set exclusively on the main thread.

### 2. The selector path — set `rand_type`/`rand_instance` at commit (`src/ligase~.c`)
P1's `pattern <target> <token>...` handler parses + flattens into a scratch table and, on success, publishes into `perlin_state->pattern[slot]` (setting `step_count` LAST as the publish barrier). The slot index is chosen by P1. P2 adds the attachment write at the SAME commit point (after `step_count` is set), for value/mod targets:

```c
// inside ligase_pattern, AFTER the scratch table is published into pattern[slot]
// and step_count is set, when target is NOT the literal "pitch":
param_range_t *range = get_param_range_by_name(x, target_name);
if (!range) { pd_error(x, "ligase~: pattern: unknown parameter '%s'", target_name); return; }
range->saved_rand_type     = range->rand_type;       // remember prior source for pattern_clear
range->saved_rand_instance = range->rand_instance;
range->rand_type     = RAND_TYPE_PATTERN;
range->rand_instance = slot;                          // slot is the pattern[] index, 0..PATTERN_SLOTS-1
range->enabled       = 1;                             // a pattern is only audible when enabled
// IMPORTANT: a range left at min==max short-circuits in sample_param_range (grain.c:197) and
// will NEVER read the pattern cache. Default ranges are {0,1} (grain.c:406) so a fresh attach
// works, but a range previously collapsed by `param_range X <v>` (sets min==max, ligase~.c:3332-3334)
// would be silently inert. Guard it:
if (range->min == range->max) {
    range->min = 0.0f; range->max = 1.0f;             // restore a usable mapping span
    post("ligase~: pattern: %s had min==max; reset map span to [0,1]", target_name);
}
post("ligase~: pattern attached to %s (slot %d, %d steps)", target_name, slot, pattern[slot].step_count);
```

Two small additions this requires:
- **`saved_rand_type` / `saved_rand_instance` fields** on `param_range_t` (appended after the current last field `int invert;` at `src/types.h:390`; struct spans `381-391`), so `pattern_clear` can restore the prior source rather than blindly resetting to NONE. These are covered by the `scheduler_create` memset (`grain.c:394`) → zero-init = `RAND_TYPE_NONE` / instance 0, a safe default. **NOTE:** `default_range` is brace-initialized at `grain.c:406` with a fixed field list; adding trailing fields to `param_range_t` zero-fills them by C aggregate rules (safe default) and the initializer still compiles. (If P1 already added these as part of slot bookkeeping, reuse them; otherwise P2 owns them.)
- **`range->enabled = 1`** on attach: a `param_range` only modulates when `enabled` (the documented footgun at the apply sites). Setting it here makes `pattern <param> ...` self-sufficient. (The `modout*` outlets additionally require `rand_type != RAND_TYPE_NONE`, which `RAND_TYPE_PATTERN` satisfies.)

### 3. `pattern_clear <target>` — restore the prior source (`src/ligase~.c`)
P1 registers the `pattern_clear` selector. P2 fills/extends its body for value targets:

```c
// inside ligase_pattern_clear, for a value/mod target:
param_range_t *range = get_param_range_by_name(x, target_name);
if (!range) { pd_error(x, "ligase~: pattern_clear: unknown parameter '%s'", target_name); return; }
if (range->rand_type != RAND_TYPE_PATTERN) {
    post("ligase~: pattern_clear: %s has no pattern attached", target_name);
    return;
}
int slot = range->rand_instance;
range->rand_type     = range->saved_rand_type;       // restore prior generator
range->rand_instance = range->saved_rand_instance;
if (slot >= 0 && slot < PATTERN_SLOTS)
    x->scheduler->perlin_state.pattern[slot].step_count = 0;  // free the slot (publish barrier: 0 = inactive)
post("ligase~: pattern cleared from %s (restored type %s)", target_name,
     get_rand_type_name(range->saved_rand_type));
```

Order matters: restore the range's `rand_type` FIRST (so the audio thread stops reading the slot), THEN zero `step_count`. Both writes are on the Pd main thread; the audio thread only reads. Zeroing `step_count` is the inactive-slot signal P1's clock and the sampler both honour. `enabled` is left as-is (do NOT clear it — see test 8 and Risks).

### 4. The DUAL `pattern_N` parse branches
Add to BOTH ladders, with their OWN instance validation (slots 0..PATTERN_SLOTS-1, not 0..3). The `pattern <target> ...` selector is the primary user path (loads the table AND attaches); the `rand_type pattern_N <param>` path is the secondary parity path that re-points a param at an already-loaded slot. Both must exist so the two parsers do not silently diverge.

**`ligase_rand_type` (`ligase~.c`), in the ladder before the final `else` at 3505:**
```c
} else if (strncmp(type_str, "pattern_", 8) == 0) {
    rand_type = RAND_TYPE_PATTERN;
    instance = atoi(type_str + 8) - 1;          // 1-based wire -> 0-based slot
    is_pattern = 1;                             // bypass the 0..3 gate below
    if (instance < 0 || instance >= PATTERN_SLOTS) {
        pd_error(x, "ligase~: pattern slot must be 1-%d", PATTERN_SLOTS);
        return;
    }
}
```
Cleanest implementation: introduce a local `int is_pattern = 0;`, set it in this branch, and change the existing validation at `ligase~.c:3511` from `if (instance < 0 || instance > 3)` to `if (!is_pattern && (instance < 0 || instance > 3))`. The rest of the function (param resolve at 3517-3524, commit at 3526-3527, all-list at 3568-3571) then works unchanged for pattern instances 0..7.

**`ligase_pitch_rand_type` (`ligase~.c`), same branch before the final `else` at 3915,** with the same `is_pattern`-guarded relaxation of the 0..3 check at `ligase~.c:3920`. (Signature `(x, s)`/`A_DEFSYMBOL`; reads only `type_str`, so the branch transplants directly. Keeps the two ladders byte-for-byte parallel — the stated footgun fix.)

### 5. The TWO print sites
- **`ligase_param_range` switch (`ligase~.c:3316-3327`):** add `case RAND_TYPE_PATTERN: type_name = "pattern"; break;` so range dumps label it correctly.
- **`get_rand_type_name` (`ligase~.c:3991-4002`):** add `case RAND_TYPE_PATTERN: return "pattern";` AND, in the same edit, the three missing cases that currently fall through to `"none"`:
  ```c
  case RAND_TYPE_SAW:    return "saw";
  case RAND_TYPE_SINE:   return "sine";
  case RAND_TYPE_SQUARE: return "square";
  case RAND_TYPE_PATTERN:return "pattern";
  ```
  This closes the latent SAW/SINE/SQUARE mislabel (confirmed missing, cases at 3993-3998 then default at 3999-4000) in the same change (no separate task).

### 6. The 4 modulation outlets (`ligase~.c:1598-1616`)
The gate is `range->enabled && range->rand_type != RAND_TYPE_NONE` (verified at 1598/1603/1608/1613). `RAND_TYPE_PATTERN != RAND_TYPE_NONE` is true, so a `modout*` range attached to a pattern (e.g. `pattern modout1 0.2 0.8`) already fires through this gate with **no source change**. The plan's action here is to VERIFY (not edit) and to add an acceptance test. The codebase uses `!= RAND_TYPE_NONE` exclusively (no `>`/`<` enum comparisons as gates), so it is robust to where P1 places `RAND_TYPE_PATTERN` in the enum.

### Cycle-clock math (consumed from P1, restated for acceptance tests)
P1 advances each slot's phase once per DSP block by `inc = (n / sample_rate) / cycle_total_sec`, where `cycle_total_sec` is derived from the `pattern_cycle` segment list via the existing grid formula `ms_per_whole_note = (60000/bpm)*4.0` (`ligase~.c:2532`). **GUARD (mandatory):** this divides by `bpm`, and `bpm` initializes to `0.0` ("not calculated yet", `ligase~.c:4525`); EVERY real grid call site guards `x->bpm > 0.0` (2613/2666/2719/2764) or `x->bpm > 1.0` (761/774/965/1170). P1's cycle clock MUST likewise guard `bpm > 0` and hold the phase at 0 (no advance) until a BPM is computed; P2's acceptance harness establishes BPM via two bangs BEFORE any `pattern` message and asserts no NaN / no advance before that. Default (no `pattern_cycle`) = one bar of 4/4 = `cycle_total_sec = (240000/bpm)/1000` (only valid for `bpm>0`). Even distribution: an N-step bare sequence puts step `(int)(phase*N)` active, each step occupying `cycle_total_sec / N` seconds. P2 does not re-derive this; it relies on `cached_value` being correct per-block and asserts the per-step durations in acceptance tests.

## Steps & gates

- **GATE A (approval) — APPROVED by owner 2026-06-24 (Seq 46).** Recommended options confirmed: A1 `enabled = 1` on attach = YES; A3 the `is_pattern`-guarded relaxation of the 0..3 check at both ladders; A4 the `min==max` → reset-to-`[0,1]` attach-guard; A5 the `smear_frequency`/etc. naming (no bare `smear`). A2 field ownership = shared with P1 (P1 owns slot bookkeeping; reuse `saved_rand_*` if already present, else P2 adds them). The **P1-landed precondition below is a build-order gate, not a design decision, and still holds.** Original sign-off items retained for the record:
  - **(A1)** Does `pattern <param> ...` auto-set `range->enabled = 1` (Design §2)? Recommended YES (self-sufficient attach). If NO, the user must also send `param_range <param> <min> <max>` to enable. **Owner decides.**
  - **(A2)** Do `saved_rand_type`/`saved_rand_instance` live on `param_range_t` (P2-owned, appended after `int invert;` at `types.h:390`) or were they already added by P1? Confirm ownership to avoid a double-declare; confirm the `default_range` initializer at `grain.c:406` still compiles with the added trailing fields (it will, trailing-zero rule). **Owner/P1 confirm.**
  - **(A3)** Confirm the `is_pattern`-guarded relaxation of the 0..3 instance check (Design §4) at BOTH ladders. Recommended: the guard (minimal diff, keeps ladders parallel). **Owner decides.**
  - **(A4 — NEW)** Confirm the `min==max` attach-guard (Design §2): a pattern attached to a collapsed range silently does nothing because of the `grain.c:197` short-circuit. Recommended: reset to [0,1] + post a warning. **Owner decides.**
  - **(A5 — NEW)** Confirm the param-name policy: `smear` is NOT a valid target; `smear_frequency`/etc. are. All user docs/examples for "smear" must say `smear_frequency`. **Owner confirms.**
  - **BLOCKING:** Confirm P1 has LANDED — as of 2026-06-24 it has NOT: `RAND_TYPE_PATTERN`, `pattern[]` state, `pattern_eval_slot`, `PATTERN_SLOTS`, `pattern_table_t`, and the three `pattern*` selectors/registrations are ALL absent from `types.h`/`grain.c`/`ligase~.c`. P2 cannot build until P1 lands them. **Blocking.**

- **Step 1 → GATE B** — P1-landed precondition check. Build the current tree (`make clean && make`); confirm `RAND_TYPE_PATTERN` is in `types.h`, `pattern_eval_slot` + `PATTERN_SLOTS` + `pattern_table_t` are defined, and the `pattern`/`pattern_cycle`/`pattern_clear` selectors are registered (grep the `class_addmethod` block). If any is absent, STOP — P2's dependency is unmet. (No P2 code yet.)

- **Step 2 → GATE C** — the sampler + print sites (the read path). Add the `case RAND_TYPE_PATTERN:` to `sample_param_range` (Design §1); add the two print-site labels and the SAW/SINE/SQUARE gap fix (Design §5). Build clean (no warnings). At this point a slot loaded by P1 and manually pointed via `rand_type pattern_1 moog_cutoff` (Step 3 wiring not yet in) would read the cache — but the parse branch doesn't exist yet, so this gate verifies only the switch compiles and `get_rand_type_name`/the param_range switch print `"pattern"`/`"saw"`/`"sine"`/`"square"` correctly via a state dump.

- **Step 3 → GATE D** — the wire path. Add the dual `pattern_N` parse branches with `is_pattern`-guarded slot validation (Design §4); add the attach-at-commit block (with the `min==max` guard) to `ligase_pattern` (Design §2); add/extend the `pattern_clear` restore (Design §3); add the `saved_rand_*` fields to `param_range_t` if not already present (Design §2), covered by the `scheduler_create` memset. Build clean.

- **Step 4 → GATE E (verify)** — headless acceptance (see below). `pd -nogui`, capture ligase~'s live output via `writesf~` the way `sample_rate_buffering.md` Step 3 does, drive bangs at a known interval to set BPM, send `pattern <param> ...`, and assert per-step values/durations in the rendered WAV. Then verify the 4 modulation outlets fire for `pattern modout1 ...` (Design §6), and verify `pattern_clear` restores the prior source. Update `QUEUE.md` §1 with a `verification_ready/VR-N` entry per the r14-verify discipline.

## Acceptance criteria
All headless-testable with `pd -nogui` + `writesf~` capture at a fixed `-r` rate; BPM established by two bangs at a known logical-time interval BEFORE the `pattern` message (so `bpm>0` before any cycle math runs).

1. **Even step distribution at a known BPM.** With bpm=120 and default cycle (one bar of 4/4 → `cycle_total_sec = 240000/120/1000 = 2.0 s`), `param_range moog_cutoff 200 4000` then `pattern moog_cutoff 0.0 1.0` must show the cutoff at the step-0 value for the first 1.0 s of each cycle and the step-1 value for the second 1.0 s. Captured: a square-wave-like alternation with a 2.0 s period, 50/50 duty. (`slew=0` for a clean step.)
2. **Three-step even distribution.** `param_range smear_frequency <min> <max>` (min≠max), then `pattern smear_frequency 0.2 0.5 0.8` at bpm=120, default cycle → each step occupies 2.0/3 ≈ 0.667 s; the captured `smear_frequency`-modulated signal steps 0.2→0.5→0.8 at 0.667 s boundaries, repeating every 2.0 s. **(`smear_frequency`, not `smear` — `smear` is not a range.)**
3. **Nested subdivision.** `pattern moog_cutoff [ 0.2 [ 0.5 0.9 ] 0.7 ]` at bpm=120 default cycle: three top-level slots of 2.0/3 s each; the middle slot is split → 0.5 for 1/3 s then 0.9 for 1/3 s within it. Assert four distinct value plateaus with durations 0.667, 0.333, 0.333, 0.667 s.
4. **Alternation walks one member per cycle.** `pattern smear_frequency < 0.2 0.8 >` at bpm=120: cycle 0 holds 0.2 for the whole 2.0 s, cycle 1 holds 0.8 for the whole 2.0 s, alternating. Assert the captured value is constant within each cycle and toggles at cycle boundaries.
5. **`pattern_cycle` changes cycle length.** `pattern_cycle 4/4 3/8` at bpm=120 → `cycle_total_sec = (2000 + 750)/1000 = 2.75 s` (4/4 whole note 2000 ms + 3 eighths at 250 ms = 750 ms; equivalently `330000/120/1000`). A 2-step `pattern smear_frequency 0.2 0.8` then steps every 1.375 s. Assert the step period is 1.375 s. (Tokenization check: `4/4` and `3/8` survive as single symbol atoms.)
6. **Modulation outlets fire (Design §6).** `pattern modout1 0.2 0.8` with `modout1` enabled: outlet 1 emits floats stepping 0.2↔0.8 at the cycle-step boundaries (capture via a `print` or a `writesf~` driven by the float→sig path in the test patch). Confirms the `enabled && rand_type != RAND_TYPE_NONE` gate passes pattern sources unchanged.
7. **Invert + slew tail runs unchanged.** `param_invert smear_frequency 1` after attaching a pattern flips each step value (0.2→0.8 maps inverted); `param_slew smear_frequency 0.9` smooths the steps into glides. Both must work with zero pattern-specific code (proves the existing tail is reused verbatim).
8. **`pattern_clear` restores prior source.** Send `param_range moog_cutoff 200 4000`, `rand_type sine_1 moog_cutoff` (sine LFO), then `pattern moog_cutoff 0.2 0.8` (attach pattern), then `pattern_clear moog_cutoff`; the captured `moog_cutoff` modulation must return to a sine LFO, not freeze or go silent. Assert the post-clear signal is the sine, that `enabled` is still 1 (clear does not disable), and a `query`/state dump labels it `"sine"` (also exercising the get_rand_type_name fix).
9. **State-dump labels are correct.** A `query` after attaching patterns to several params and setting saw/sine/square on others must print `"pattern"`, `"saw"`, `"sine"`, `"square"` — never `"none"` for a non-none source (the latent-gap regression test).
10. **Slot ≥ 4 works (the 0..3 footgun).** Load and attach a pattern on a slot ≥4 (e.g. `rand_type pattern_5 moog_cutoff` after P1 loads slot 5, or whichever slot P1 assigns) and confirm it modulates — proving the `is_pattern` guard and the direct `range->rand_instance` read in the sampler bypass the 0..3 clamp at all three sites.
11. **Build is clean.** `make clean && make` produces no warnings; the new switch case, parse branches, and print cases compile under the project's existing flags.

## Risks / out-of-scope
- **Risk: instance/slot clamp collision.** The sampler clamps a LOCAL `instance` to 0..3 (`grain.c:206-208`, set from `range->rand_instance` at 202, never written back) and both ladders validate 0..3 (`3511`, `3920`). Pattern slots run 0..7. The mitigation (read `range->rand_instance` directly in the pattern case; `is_pattern`-guard the ladder checks) must be applied at ALL THREE sites or a slot ≥4 is silently rejected/mis-read. Acceptance test 10 catches this.
- **Risk: `min==max` short-circuit silently disables the pattern.** `sample_param_range` returns `min` when `min==max` (`grain.c:197-199`) BEFORE the rand_type switch. A pattern attached to a collapsed range never reads the cache. Mitigation: the attach-guard in Design §2 (A4).
- **Risk: wrong param name.** `smear` is not a modulatable range (only in the query dispatch at `3975`). Using it errors `unknown parameter`. All examples/tests use `smear_frequency`/`moog_cutoff`/`modout1`. Enforced by the param-name policy (A5).
- **Risk: P1 not landed / fabricated reads.** As of 2026-06-24 P1's surface is entirely absent. The "registrations near 4732-4737" claim was wrong (those are `param_range`/`rand_type`). GATE A/Step 1 are hard-blocking on P1.
- **Risk: bpm==0 div-by-zero in the cycle clock.** Restated formula divides by `bpm` (init 0). P1's clock and all P2 math must guard `bpm>0` (mirroring 2613/2666/2719/2764). The harness sets BPM first.
- **Risk: reading the cache before P1 writes it.** P1 must advance `pattern_eval_slot` at the TOP of `ligase_perform` (before `ligase_update_inlets` at 1587). If that ordering regresses, the sampler reads a stale/zero `cached_value`. Documented dependency; acceptance tests would catch a one-block lag.
- **Risk: `enabled` footgun.** If A1 is NO, every pattern test must also send `param_range`; if A1 is YES, `pattern_clear` must NOT spuriously disable a range the user enabled independently (clear restores `rand_type`/`instance` but leaves `enabled` as-is — verified in test 8).
- **Risk: slew smears intended hard steps.** Documented, not a bug: stair-step sequencing requires `slew=0`. Tests 1-6 use `slew=0`; test 7 explicitly exercises `slew>0`.
- **Out of scope:** the parser, the cycle clock, nesting/alternation evaluation, `pattern_eval_slot`, AND the `class_addmethod` registration of the three pattern selectors (all P1). Pitch via `pattern pitch ...` and `PITCH_MODE_PATTERN` (all P3). True polyphony / Tidal comma-as-STACK — **not applicable** (single param over time); intentionally unsupported, leaving `,` to its Tidal stack meaning (and never a comma in the wire form — Pd binbuf eats it). Per-param independent slots beyond PATTERN_SLOTS=8 (slot reuse/aliasing is a P1 allocation concern). No changes to `ligase_update_inlets`, the apply sites, or `get_param_range_by_name` (already generic).