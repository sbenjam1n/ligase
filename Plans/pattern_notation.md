# Plan P1: Notation parser, recursive nesting, and the free-running BPM cycle clock

**Owner:** SLB
**Date:** 2026-06-24
**Status:** ✅ DONE (2026-06-24) — implemented and headless-verified (GATE E); all 8 acceptance criteria pass; `make clean && make` warning-free at every gate; no regression in the existing audio path. **One sanctioned deviation from the GATE A sign-off:** the `RAND_TYPE_PATTERN` / `PITCH_MODE_PATTERN` enum *values* are **deferred to P2/P3** (added atomically with their switch cases) rather than added here — adding them in P1 produced dangling `-Wswitch` warnings with no P1 code using either value, and deferral was the explicitly-offered GATE-A alternative. (Header below predates completion; left as the original plan record. See Progress.)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — pattern subsystem build-out); promote into §2 (ON DECK) when scheduled. (NOT §1 — §1 is the COMPLETE-work changelog.) Verification harness: `AUTOMATED_TEST_PROCEDURE.md` (headless `pd -nogui -nosound -stderr`).
**Related (forthcoming, not yet written):** Plan P2 (`pattern_modulation.md`, attaches a pattern to any `param_range_t` target via `RAND_TYPE_PATTERN`) and Plan P3 (`pattern_pitch.md`, `PITCH_MODE_PATTERN` scale-degree stepper). P1 is the foundation both depend on; it builds the parser + clock + nesting but **attaches to nothing** — a slot can be loaded and advanced, observed only via a debug `post()`.

## Progress (2026-06-24) — IMPLEMENTED + VERIFIED

Built across the four gates; `make clean && make` warning-free at each.
- **Step 1 (types):** `pattern_step_t` / `pattern_table_t` / `pattern_node_t` + `PATTERN_*` caps in `types.h`; `pattern[PATTERN_SLOTS]` / `pattern_phase[]` / `pattern_cycle_index[]` in `perlin_state_t` (memset-covered); `pitch_pattern_slot` in `pitch_control_t` (init `-1` in `scheduler_create`); `cycle_total_sec` / `cycle_seg_count` / `cycle_segments[]` / `pattern_debug` in `ligase_t` (+ `ligase_new` init). Enum-value deferral per Status.
- **Step 2 (clock):** `ligase_recompute_cycle()` helper (dual-recompute, guarded `bpm<=0`), wired into `ligase_bang`; `pattern_cycle` (validate-then-commit `%d/%d` segments) + `pattern_clear` handlers, registered.
- **Step 3 (parser + eval):** `pattern_eval_slot()` (`grain.c`, non-static + `extern` in `ligase~.c`); two-stage `ligase_pattern` (recursive-descent tree → span-descent flatten → validate-then-commit, `step_count` published LAST); per-block phase-advance loop in `ligase_perform` just before `ligase_update_inlets` (guarded `scheduler && bpm>1.0 && cycle_total_sec>0`); `pattern` + `pattern_debug` registered.
- **Step 4 (verify):** 7 headless patches (`tests/pattern/P1*.pd`) under `pd -nogui -nosound`, asserting against the `pattern_debug` stderr trace (logical-time-stamped step changes).

| AC | Test | Result |
|----|------|--------|
| 1 Even timing | `pattern_cycle 4/4` + `pattern 0 0.0 0.25 0.5 0.75` @120BPM | steps 0→1→2→3 at 0.500 s (= 2.0/4); ✓ |
| 2 `4/4 3/8` cycle | `pattern_cycle 4/4 3/8` | cycle 2.750 s, step period 1.375 s; ✓ |
| 3 Nested | `pattern 0 [ 0.2 0.4 ] 0.8` | spans 0.5/0.5/1.0 s (¼,¼,½); ✓ |
| 4 Weighted | `pattern 0 0.2@3 0.8` | spans 1.5/0.5 s (¾,¼); ✓ |
| 5 Alternation | `pattern 0 < 0.1 0.5 0.9 >` (default cycle) | one value/cycle, cycle%3, 2.0 s computed in `ligase_bang`; ✓ |
| 6 Pitch load | `pattern pitch < 0 4 7 >` | slot 7, RAW degrees 0/4/7 (not normalized), pitch mode unchanged; ✓ |
| 7 Robustness | standalone brackets, glued/`foo` token, unbalanced | clean `pd_error`, prior slot preserved; ✓ |
| 8 Guards | pre-BPM load + zero-init | clock frozen at phase 0 (no NaN/div0), resumes once BPM set; no crash; ✓ |

Regression: the existing `test_delay.pd` runs clean and writes its WAV — the audio path is untouched.

**Carried forward to P2/P3 (out of P1 scope, as planned):** the `pattern <name>` param-name target + `RAND_TYPE_PATTERN` attach (P2); the `PITCH_MODE_PATTERN` pitch case + mode-set on `pattern pitch …` commit (P3). Also deferred (grammar reserves the tokens; not needed for P1 acceptance): Euclid `(k,n)`, and group-glued suffixes like `]*2`. Single-level alternation only (ALT-inside-ALT is rejected with a clean error).

## Problem

The user wants a TidalCycles-style mini-notation to sequence ligase~'s parameters and pitch, reusing the engine's existing modulation, BPM, and quantization machinery rather than bolting on a parallel clock:

- **`<,>` / even distribution.** A list like `<a, b, c>` should distribute its members evenly across a cycle — three values each occupying 1/3 of the cycle.
- **A `[4/4, 3/8]` quantization cycle.** The cycle length itself is musical: a `[4/4, 3/8]` segment list means the cycle runs for 4 quarter-notes followed by 3 eighth-notes at the detected tempo, and steps lay out over that total.
- **Nesting.** `[]` subdivides (a group fits inside one parent step), `<>` alternates (one member per cycle); these compose recursively, exactly as Tidal models them as a tree.
- **Pitch via the scale system.** A pattern of scale degrees (`< 0 4 7 >`, `[ 0 4 7 ]`) should drive pitch through the existing `pitch_scale_t.semitones[]` → `semitones_to_speed()` chain, not a new tuning path.
- **Reuse, not reinvent.** BPM is already detected from bang intervals; the four quant grids already derive from `ms_per_whole_note = (60000/bpm)*4`. The pattern clock must be a *fifth, isolated* concept built from the same grid math, overloading none of the existing four.

**Syntax is canonical TidalCycles mini-notation (no commas).** In Tidal, a sequence is **space-separated** (`a b c`), `< >` alternates one member per cycle, and `[ ]` subdivides; `,` means **stack** (parallel/polyphonic layers). Stacking is **not applicable here** — a pattern drives one scalar parameter or one pitch over time, never parallel voices — so comma is intentionally not part of the syntax, exactly per Tidal's meaning for it (simply unused). This also aligns with Pd, where `,` is a binbuf message **delimiter**, not data (`A_COMMA`, `src/m_pd.h:176`; `A_SEMI`, `:175`): a literal comma could never arrive intact at a method anyway. So the even-distribution sequence is the bare space-separated `a b c` form, and the `[4/4 3/8]`-style quantization cycle is a dedicated `pattern_cycle 4/4 3/8` selector. This plan pins that wire syntax precisely.

P1 delivers the parse + clock + nesting **infrastructure only**. No parameter and no pitch is driven yet (that is P2/P3). Done means: `pattern <slot> <tokens>` loads a slot, the cycle clock advances its phase once per DSP block at the detected BPM, nesting compiles to the right weighted step layout, and a debug `post()` shows the right step being selected at the right time.

## Mechanics / target surface

The existing code this extends, verified by reading `src/types.h`, `src/grain.c`, `src/ligase~.c`:

### BPM and the grid formula (reuse verbatim)
- `ligase_bang` (`src/ligase~.c:2512`) derives BPM control-rate from the bang interval: `interval_ms = interval_units / 14112.0` (`:2522`, `14112 = 32*441` Pd time units/ms, SR-independent), then `x->bpm = 60000.0 / interval_ms` (`:2526`). `x->bpm` is a `double` (`src/ligase~.c:242`), initialized `0.0` (`:4525`), and stays 0 until the **second** bang.
- The grid formula appears identically eight times: `ms_per_whole_note = (60000.0 / x->bpm) * 4.0` (`:2532, :2539, :2545, :2551` in `ligase_bang`; `:2614, :2667, :2720, :2765` in the four quant setters). The cycle clock reuses this exact expression.
- **Four independent quant grids** (`quant_grid_ms`, `gs_quant_grid_ms`, `delay_quant_grid_ms`, `stut_len_quant_grid_ms`) are each recomputed both in `ligase_bang` (`:2530-2553`) and in their own setter when `bpm > 0.0`. The cycle clock is a **fifth** grid and follows the same dual-recompute discipline.
- **The dead field.** `samples_since_quant` (`src/ligase~.c:250`, init `:4533`) is declared but never advanced or read (verified: only refs are the declaration and the init) — confirming there is no pre-existing phase state to build on. The cycle clock introduces its own.
- **Guard convention.** Perform-side grid use guards on `x->bpm > 1.0` (`:761, :774, :965, :1170, :2842, :2854`); setters guard `x->bpm > 0.0` (`:2613, :2666, :2719, :2764`). `x->sample_rate` is an `int` (`src/ligase~.c:281`), default `48000` (`:4598`).

### The `N/D` fraction parse precedent
- `ligase_timesig` (`src/ligase~.c:2575`) is the canonical "a fraction survives Pd as one bare symbol" proof: it requires `argc == 1 && argv[0].a_type == A_SYMBOL` (`:2576`) and splits with `sscanf(sig_str, "%d/%d", &num, &denom)` (`:2584`), validating `num > 0 && denom > 0` (`:2585`). `ligase_quantize` (`:2601`) validates the denominator set `{1,2,4,8,16,32,64,128}` (`:2605`). The `pattern_cycle` segment parser clones both.

### The DSP block entry point
- `ligase_perform` (`src/ligase~.c:1493`): block size `n = (int)(w[26])` (`:1542`), bounded `n > 8192` bail with zeroed outputs (`:1547-1554`). The per-block orchestration is `ligase_update_inlets(...)` (`:1587`) then `ligase_process_grains(...)` (`:1592`). The cycle-clock advance is inserted **immediately before** `ligase_update_inlets` (i.e. just after pointer validation completes at `:1585`) so the freshly-advanced phase is visible both to per-block param sampling (P2) and to per-grain pitch reads (P3).
- **Note on `x->scheduler`:** the existing earliest deref of `x->scheduler->perlin_state` in perform is at `:1599`, and that site is NOT NULL-guarded today (the object is constructed even if `scheduler_create` returns NULL). Because the new phase-advance block becomes the *earliest* deref of `x->scheduler` in perform, it adds a cheap `x->scheduler != NULL` term to its guard (defensive; strictly stronger than the existing convention).

### The pattern node state lives in `perlin_state_t`
- `perlin_state_t` (`src/types.h:472`) holds all generator state, last field `float waveform_phase[4]` (`:512`). It is a value member of the scheduler struct (`perlin_state_t perlin_state;` at `src/types.h:587`). The whole scheduler is `memset(sched, 0, sizeof(scheduler_t))` in `scheduler_create` (`src/grain.c:394`) — its comment (`:384-393`) documents that a garbage `enabled` flag silently clobbers a live parameter every block, the root cause of past record/playback/delay bugs. **Any new pattern state added to `perlin_state_t` is automatically zero-init'd by that memset** (it is a value member of the memset'd `scheduler_t`) — the footgun is handled provided the new fields live inside the memset'd struct and `step_count == 0` reliably means "inactive."

### Class-method registration
- All selectors register via `class_addmethod` near `src/ligase~.c:4658` (timing cluster: `timesig`, `quantize`, ...) and `:4732-4737` (the `A_GIMME` `param_*`/`rand_type` cluster). The new `pattern`, `pattern_cycle`, `pattern_clear` selectors register here. All 22 signal inlets (one main signal inlet via `CLASS_MAINSIGNALIN` + 21 `inlet_new(... &s_signal, &s_signal)` from `ligase_new`, `:4418` onward) mean selector messages only reach inlet 0 — the established route.

### Cross-file linkage (there is NO grain.h)
- The codebase has **no `grain.h`**. Grain functions used from `ligase~.c` are declared there via explicit `extern` (e.g. `scheduler_create` `:59`, `scheduler_trigger_grain` `:61`, `sample_param_range` `:63`). The new `pattern_eval_slot` therefore must be **non-static in `grain.c`** with a matching `extern` declaration added in `ligase~.c` beside lines `:59-63` — NOT "exported via grain.h."

### What P1 does NOT touch (deferred to P2/P3, listed so the reader knows the seams)
- `sample_param_range` switch (`src/grain.c:214-284`) — P2 adds `case RAND_TYPE_PATTERN`.
- `scheduler_trigger_grain` pitch switch, `PITCH_MODE_SCALE` case at `src/grain.c:708` — P3 adds `PITCH_MODE_PATTERN`.
- `ligase_rand_type` (`:3467`) / `ligase_pitch_rand_type` (`:3882`) parse branches and the print sites (`get_rand_type_name` `:3991`, the dump switch `:3317-3326`) — P2. (Note: `get_rand_type_name` at `:3991` already has a latent gap — no cases for SAW/SINE/SQUARE, which fall to `default: "none"`; the dump switch at `:3317-3326` is complete. P2 adding `RAND_TYPE_PATTERN` must cover BOTH to stay `-Wswitch`-clean.)
- The outlet-3 note-change mode test (`:1631-1632`, the `PITCH_MODE_SCALE || PITCH_MODE_RANGE` branch) — P3.

P1 **does** add the enum *values* (`RAND_TYPE_PATTERN`, `PITCH_MODE_PATTERN`) and the `pitch_pattern_slot` field so the type surface is complete and the structs compile, but wires no behavior to them. This keeps P2/P3 as pure additive cases against a stable type surface.

## Design

### Data structures (in `src/types.h`)

New region after `pitch_control_t` (after `src/types.h:420`):

```c
#define PATTERN_MAX_STEPS  64    // compiled flat-table cap (runtime); >64-leaf patterns rejected
#define PATTERN_MAX_NODES  256   // parse-time tree node pool cap (scratch, message-thread stack)
#define PATTERN_MAX_DEPTH  8     // recursive-descent open-group stack cap
#define PATTERN_SLOTS      8     // independent pattern slots (>4 generator instances => per-target independence)
#define PATTERN_MAX_SEGS   16    // pattern_cycle segment list cap

// One compiled flat leaf (runtime representation, produced by flattening the parse tree)
typedef struct {
    float value;        // normalized 0..1 (params) OR raw scale degree as float (pitch)
    float weight;       // @-weight; default 1.0
    int   is_rest;      // ~ : hold previous value, emit no fresh step
    int   alt_group;    // -1 = always present; >=0 = member of alternation group G
    int   alt_member;   // index within its alt group (one present per cycle)
} pattern_step_t;

// One pattern slot's compiled table + cached evaluator output (the ONLY thing perform touches)
typedef struct {
    pattern_step_t steps[PATTERN_MAX_STEPS];
    int   step_count;                          // 0 => slot inactive (publish barrier; set LAST on commit)
    int   alt_group_count[PATTERN_MAX_STEPS];  // members per alternation group G (cycle-mod select)
    int   alt_group_total;                     // number of distinct alternation groups
    float cum_weight[PATTERN_MAX_STEPS];       // prefix sums over PRESENT steps (recomputed on reselection)
    float total_weight;                        // sum of present-step weights this cycle
    // evaluator cache (written ONLY by pattern_eval_slot once per block; read by P2/P3):
    float cached_value;        // current normalized value / degree for this block
    int   cached_is_rest;      // current step is a rest
    int   changed;             // 1 on the block where the active present-step index changed
    int   last_step_index;     // for change detection
    long  last_alt_cycle;      // cycle index of last alt reselection (skip recompute when unchanged)
} pattern_table_t;

// Parse-time ONLY (function-local automatic array inside ligase_pattern; never in perform state)
typedef enum { PN_LEAF, PN_SEQ, PN_ALT } pattern_node_kind_t;
typedef struct {
    pattern_node_kind_t kind;
    float value; int is_rest;        // LEAF fields
    int   weight;                    // @N (default 1)
    int   first_child, next_sibling; // index links into the node pool (-1 if none)
} pattern_node_t;
```

Add to `rand_type_t` (after `RAND_TYPE_SQUARE`, `src/types.h:378`) — value only, no behavior in P1:
```c
    , RAND_TYPE_PATTERN     // step-sequence / mini-notation source (wired in P2)
```

Add to `pitch_mode_t` (append after `PITCH_MODE_MIDI`, `src/types.h:402`, becomes value 5 — existing 0-4 stay stable):
```c
    , PITCH_MODE_PATTERN    // pattern-driven scale-degree stepper (wired in P3)
```

Add to `pitch_control_t` (`src/types.h:412`):
```c
    int pitch_pattern_slot;  // which pattern[] slot supplies scale degrees (-1 = none)
```

Add to `perlin_state_t` (after `float waveform_phase[4];`, `src/types.h:512`) — covered by the `scheduler_create` memset (`src/grain.c:394`, since `perlin_state` is a value member of the memset'd `scheduler_t`):
```c
    pattern_table_t pattern[PATTERN_SLOTS];
    float           pattern_phase[PATTERN_SLOTS];        // free-running 0..1 cycle phase per slot
    long            pattern_cycle_index[PATTERN_SLOTS];  // integer cycle counter per slot (<> alternation)
```

Add to the `ligase_t` timing block (next to the dead `samples_since_quant`, `src/ligase~.c:250`):
```c
    double cycle_total_sec;                 // total cycle length in seconds (0 => clock idle)
    int    cycle_seg_count;                 // # pattern_cycle segments (0 => default 1-bar cycle)
    struct { int num; int den; } cycle_segments[PATTERN_MAX_SEGS];
```

`param_range_t` is **unchanged** — P2's attach is purely via `rand_type == RAND_TYPE_PATTERN` + `rand_instance` as the slot index, so no field-widening and no parallel keyed storage. The parse-time node pool is a function-local automatic array — no `malloc`, message thread only.

### Wire syntax (everything survives Pd binbuf: space-separated atoms, NO commas, NO glued brackets)

Three selectors, all on inlet 0, all `A_GIMME`:

```
pattern <slot> <token>...        # load a pattern into slot N (slot = 0..PATTERN_SLOTS-1, or the literal `pitch`)
pattern_cycle <N/D> <N/D> ...    # the quantization-cycle segment list (global; sets cycle_total_sec)
pattern_clear <slot>             # clear a slot (step_count := 0)
```

> **P1 scope note on `<slot>`:** because P1 attaches to nothing, the target is a **bare slot index** `0..PATTERN_SLOTS-1` (plus the literal `pitch`, which loads the pitch slot but does not set the mode yet). P2 generalizes the first argument to a *param name* (`moog_cutoff`, `smear`, ...) resolved through `get_param_range_by_name` (`src/ligase~.c:3241`) and maps it to a slot; P3 wires the `pitch` target to set `PITCH_MODE_PATTERN`. Keeping P1 on a numeric slot means P1 is testable in isolation with zero dependence on the attach plumbing.

**Tokens** (each is exactly one whitespace-delimited atom; brackets/angles are STANDALONE symbols):

| token | atom form | meaning |
|---|---|---|
| value | `A_FLOAT` e.g. `0.7` | leaf; 0..1 for params, raw scale-degree float for pitch |
| rest | symbol `~` | leaf, `is_rest=1` (hold previous value) |
| group-open | symbol `[` | begin subdivision group (one parent step, even split among children) |
| group-close | symbol `]` | |
| alt-open | symbol `<` | begin alternation group (one child per cycle, slowcat) |
| alt-close | symbol `>` | |
| weight | glued suffix `@N` e.g. `0.7@3` | step weight 3 (uneven durations: `weight/Σweights`) |
| fast | glued suffix `*N` e.g. `0.7*2` | desugar at parse to a `PN_SEQ` of N copies |
| replicate | glued suffix `!N` e.g. `0.7!3` | N separate sibling steps |

EBNF (post-atomization; comma already consumed by binbuf):
```
pattern-msg := 'pattern' slot step+
cycle-msg   := 'pattern_cycle' frac+
step        := group | leaf
group       := '[' step+ ']' suffix?    // subdivision (Tidal [])
             | '<' step+ '>' suffix?    // alternation (Tidal <>)
leaf        := (FLOAT | '~') suffix?
suffix      := ('@'INT | '*'INT | '!'INT)?   // glued to the preceding token, survives as one symbol
frac        := INT '/' INT                    // one A_SYMBOL, sscanf %d/%d
```

Tidal-mini-notation consistency (no commas):
- **Sequence = space-separated** (Tidal `a b c`), not comma-separated: `pattern 0 0.2 0.5 0.8` = three 1/3 steps. `< >` is true per-cycle alternation and `[ ]` is subdivision — exactly Tidal.
- **`[4/4 3/8]` musical cycle** → the dedicated `pattern_cycle 4/4 3/8` selector (space-separated fractions, each riding as a glued `N/D` symbol). Distinct from value distribution: it sets `cycle_total_sec`.
- **Tidal comma = stack (chord/polyphony)** is **not applicable** to this feature (one scalar/pitch over time, no parallel voices) and is therefore not supported — consistent with leaving `,` to its Tidal meaning, simply unused. (Impossible in Pd's wire form regardless — `,` is a binbuf delimiter.)

Examples (all survive binbuf unchanged):
```
pattern 0 0.2 0.8 0.5                 # even 3-step sequence into slot 0, default cycle
pattern 1 < 0.2 0.8 > [ 0.1 0.5 ]     # alt (one/cycle) then subdivision, nested, into slot 1
pattern 2 [ 0.2 [ 0.5 0.9 ] 0.7@2 ]   # nested + weighted
pattern pitch < 0 1 2 3 4 5 6 7 >     # pitch slot loaded (P3 makes it walk the scale)
pattern_cycle 4/4 3/8                  # quantization cycle = 4 quarters + 3 eighths at detected BPM
```

### Cycle-clock math

Reuse the verbatim grid formula. From the `pattern_cycle` segment list:
```c
ms_per_whole_note = (60000.0 / bpm) * 4.0;                    // identical to all 4 grid recomputes
// each segment = num notes of value 1/den:  seg_ms = (ms_per_whole_note / den) * num
cycle_total_ms = Σ over segments of (ms_per_whole_note / den_i) * num_i;
cycle_total_sec = cycle_total_ms / 1000.0;
```

Worked, `pattern_cycle 4/4 3/8` (verified numerically):
```
seg0 (4/4) = (mswn/4)*4 = mswn         = 4*(60000/bpm) ms   (4 quarter-notes)
seg1 (3/8) = (mswn/8)*3 = (3/8)*mswn   = 3*(30000/bpm) ms   (3 eighth-notes)
cycle_total_ms = 4*(60000/bpm) + 3*(30000/bpm) = 330000/bpm
bpm=120 -> mswn=2000 ms -> seg0=2000, seg1=750 -> cycle_total_ms = 2750 ms -> cycle_total_sec = 2.75 s.
```
**Default branch (no `pattern_cycle` set, `cycle_seg_count == 0`):** one bar of 4/4 → `cycle_total_sec = ms_per_whole_note / 1000.0 = (240000/bpm)/1000` (= 2.0 s at bpm 120). **This default value MUST be computed in `ligase_bang` whenever `cycle_seg_count == 0`** (and the field left 0 only while `bpm == 0`), exactly alongside the four grid recomputes — otherwise the perform guard `cycle_total_sec > 0.0` keeps the clock frozen and a slot loaded with no `pattern_cycle` (acceptance #5) would never advance.

Each denominator validated against `{1,2,4,8,16,32,64,128}` (the `ligase_quantize` set, `src/ligase~.c:2605`), or at minimum `den > 0` (the `ligase_timesig` rule, `:2585`). `cycle_total_sec` is recomputed **(a)** in `ligase_bang` next to the four grid recomputes — for BOTH the segment-list case AND the `cycle_seg_count == 0` default case (so it tracks BPM in either) — and **(b)** in the `pattern_cycle` handler when the segment list changes — the established dual-recompute discipline.

**Phase advance** (free-running, once per DSP block, inserted at the TOP of `ligase_perform` just before `ligase_update_inlets` at `src/ligase~.c:1587`):
```c
if (x->scheduler && x->bpm > 1.0 && x->cycle_total_sec > 0.0) {  // scheduler NULL + BPM-unset + div0 guards
    perlin_state_t *ps = &x->scheduler->perlin_state;
    double inc = ((double)n / (double)x->sample_rate) / x->cycle_total_sec; // REAL samples; perform has no logical clock
    for (int s = 0; s < PATTERN_SLOTS; s++) {
        if (ps->pattern[s].step_count < 1) continue;            // skip inactive slots
        ps->pattern_phase[s] += inc;
        while (ps->pattern_phase[s] >= 1.0) {                   // wrap; bump cycle counter by INTEGER part
            ps->pattern_phase[s] -= 1.0;                        // (handles >1 cycle/block at tiny cycle_total_sec)
            ps->pattern_cycle_index[s] += 1;                    // long counter drives <> alternation member select
        }
        pattern_eval_slot(ps, s, x->bpm);                       // re-select alt on new cycle, write cached_value + changed
    }
}
```
Guards mirror the existing grid guards (`bpm > 1.0`; `cycle_total_sec > 0.0` and `x->scheduler` added). Until the second bang `bpm == 0`, so the clock freezes at phase 0 — acceptable degrade, never NaN. The `cycle_index` is incremented by floor (never derived from `phase*bigN`), so alternation never drifts.

**Phase → step lookup** (inside `pattern_eval_slot`, both even and `@`-weighted in one path):
- **even** (all weights 1, `total_weight == present_count`): `step = (int)(phase * present_count)`, clamped to `[0, present_count-1]` — literally realizes "N values, each 1/N of the cycle."
- **weighted** (`@`): search `phase * total_weight` against the `cum_weight[]` prefix sums → the present leaf whose `[cum_prev, cum)` span contains it. Realizes `a@3 b -> 3/4 + 1/4`.
- **alternation**: on a new `cycle_index`, for each alt group G the present member is `cycle_index mod alt_group_count[G]`; absent members are excluded from the prefix-sum pass, then `cum_weight`/`total_weight` are recomputed in one bounded `O(<=64)` pass. Skipped when `cycle_index == last_alt_cycle` (no group reselection needed).
- **rest**: `cached_is_rest = 1`; `cached_value` is left unchanged (hold-previous semantics; the *consumer* in P2/P3 decides what hold means).
- `changed` is set when the selected present-step index differs from `last_step_index`.

### Two-stage parser (`ligase_pattern`, message thread)

`pattern` registers via `class_addmethod(... gensym("pattern"), A_GIMME, 0)` beside the `A_GIMME` cluster at `src/ligase~.c:4737`. Because `,`/`;` are consumed by binbuf before dispatch (`A_COMMA`/`A_SEMI`, `m_pd.h:175-176`), `argv` contains only `A_FLOAT`/`A_SYMBOL` — every existing `A_GIMME` handler relies on exactly this.

**Stage 1 (tree).** `argv[0]` is the slot target (a float slot index, or the symbol `pitch`). Walk `argv[1..]` with a recursive-descent loop over a function-local `pattern_node_t pool[PATTERN_MAX_NODES]` and an explicit `int stack[PATTERN_MAX_DEPTH]` of open-group parent indices:
- `[` / `<` allocate a `PN_SEQ` / `PN_ALT` node, link as child of the stack top, push.
- `]` / `>` pop, **validating the bracket type matches** the pushed kind; reject on mismatch.
- `A_FLOAT` / `~` append a `PN_LEAF` child of the stack top.
- glued suffix on the preceding token: `@N` sets that node's `weight`; `*N` desugars to a `PN_SEQ` of N cloned copies; `!N` emits N sibling copies.
- Bound `node_count <= PATTERN_MAX_NODES` and `depth <= PATTERN_MAX_DEPTH`; on overflow or unbalanced brackets, reject **without touching live state**.

Tokenization of glued suffixes/fractions in C: `strtof` up to the suffix char, then `atoi` after `@`/`*`/`!`; fractions split with `sscanf("%d/%d", ...)` (the `ligase_timesig` idiom, `:2584`). No internal spaces inside any logical token.

**Stage 2 (flatten).** Recursively descend the tree assigning each node a weight share of its parent's span (fractions multiply down per Tidal nesting), emitting `pattern_step_t` leaves into a **stack-local scratch `pattern_table_t`** with `alt_group`/`alt_member` tags and `alt_group_count[]`. Bound emitted leaves `<= PATTERN_MAX_STEPS`; reject on overflow.

**Validate-then-commit** (the `ligase_pitch_scale` discipline, `src/ligase~.c:3938-3950` — "validate all args BEFORE modifying, previous scale preserved on error"): everything writes the scratch table; only on a fully successful parse do we `memcpy` the scratch into `perlin_state.pattern[slot]`, reset `pattern_phase[slot] = 0` and `pattern_cycle_index[slot] = 0`, then set `step_count` **LAST** — the publish barrier (perform reads `step_count >= 1` as "table fully built"). On any error the previous slot table is preserved. In P1 the commit sets **only** the slot table — it does **not** set any `range->rand_type` or pitch mode (that is P2/P3). A `post()` reports `slot N: M steps, cycle K.KK s` for headless observation.

**`pattern_cycle` handler.** Clone the `ligase_timesig` `sscanf("%d/%d")` parser over `argv`, fill `cycle_segments[]`, validate denominators, set `cycle_seg_count`, compute `cycle_total_sec` via the grid formula (guarded `bpm > 0.0`, else leave 0 and recompute on next bang). Register beside the timing cluster (`:4658`, `A_GIMME` — same form as `timesig`).

**`pattern_clear` handler.** `argv[0]` = slot; set `pattern[slot].step_count = 0` and zero `pattern_phase`/`pattern_cycle_index` for that slot. Register beside `pattern`.

**Thread safety.** `ligase_pattern` / `pattern_cycle` / `pattern_clear` all run on the Pd main thread and write the fixed arrays; the audio thread only **reads** (and only `pattern_eval_slot` writes `cached_*`, once per block). No `malloc` anywhere — the node pool is automatic stack storage, the tables are fixed-size in `perlin_state_t`. (In stock single-threaded Pd, messages are processed between DSP blocks on the same thread, so the publish-barrier discipline is defensive correctness, not a data-race fix — but it costs nothing and is the right pattern.)

### `pattern_eval_slot` (the once-per-block evaluator)

A new `void pattern_eval_slot(perlin_state_t *ps, int slot, double bpm)` (NON-static) in `src/grain.c` (next to `update_perlin_coords`, `:310`). Since there is **no `grain.h`**, it is made visible to `ligase_perform` by adding a matching `extern void pattern_eval_slot(perlin_state_t *ps, int slot, double bpm);` in `ligase~.c` beside the existing grain externs (`:59-63`). It is the **single writer** of `cached_value` / `cached_is_rest` / `changed` / `last_step_index` / `last_alt_cycle`. P1 reads those only from a debug `post()`; P2/P3 read them in `sample_param_range` / `scheduler_trigger_grain` respectively.

## Steps & gates

- **GATE A (approval) — APPROVED by owner 2026-06-24 (Seq 46).** Recommended option for each item below is confirmed: `PATTERN_SLOTS = 8`; reject-not-truncate at the 64-leaf cap; the strict `{1,2,4,8,16,32,64,128}` denominator set; the P1-staged numeric `<slot>`; and the `RAND_TYPE_PATTERN` / `PITCH_MODE_PATTERN` enum *values* added in P1. Comma/stacking dropped as not-applicable (canonical Tidal: space-separated sequences, `,`=stack unused). Cleared to start at Step 1. Original sign-off items retained for the record:
  1. **`<slot>` is a numeric index in P1** (`pattern 0 ...`), generalized to param names in P2. Confirm this staging (vs. requiring P2's name resolution up front).
  2. **`PATTERN_SLOTS = 8`** (vs. 4). Eight gives per-target independence beyond the 4 generator instances; cost is `8 * sizeof(pattern_table_t)` added to every instance's `perlin_state_t` (measured ≈ 14.3 KB/object: `pattern_table_t` ≈ 1828 B × 8, plus `pattern_phase[8]`/`pattern_cycle_index[8]`). Confirm 8.
  3. **Caps `PATTERN_MAX_STEPS=64`, `PATTERN_MAX_NODES=256`, `PATTERN_MAX_DEPTH=8`.** A pattern flattening to >64 leaves is **rejected** (not truncated). Confirm reject-not-truncate and the 64 ceiling.
  4. **Denominator validation = the strict `{1,2,4,8,16,32,64,128}` set** (matching `ligase_quantize`) rather than the looser `den > 0` (matching `ligase_timesig`). Confirm strict.
  Also surface for the record: this plan adds the `RAND_TYPE_PATTERN` / `PITCH_MODE_PATTERN` enum **values** in P1 (so the type surface is stable) but wires no behavior — confirm that is acceptable (vs. deferring the enum additions to P2/P3).

- **Step 1 → GATE B (types compile).** Add all `src/types.h` structures, enum values, the `pitch_pattern_slot` field, the `perlin_state_t` pattern state, and the `ligase_t` cycle fields. Confirm `make clean && make` is warning-free and the `scheduler_create` memset (`src/grain.c:394`) provably covers the new `perlin_state_t` fields (they are inside the memset'd `scheduler_t`). No behavior yet.

- **Step 2 → GATE C (clock).** Add the `cycle_segments`/`cycle_total_sec` fields' init in `ligase_new` (zero them next to `samples_since_quant` at `:4533`), the `pattern_cycle` + `pattern_clear` handlers, the `cycle_total_sec` recompute in `ligase_bang` (next to `:2530-2553`, covering BOTH the segment-list and `cycle_seg_count == 0` default branches), and the phase-advance block in `ligase_perform` (before `ligase_update_inlets`, `:1587`, with the `x->scheduler` NULL guard). Register `pattern_cycle`/`pattern_clear`. With a hand-loaded slot (a temporary debug `pattern_cycle` + a stub one-step table), verify the phase advances and wraps at the computed `cycle_total_sec`, guarded against `bpm == 0`.

- **Step 3 → GATE D (parser + nesting + eval).** Add `ligase_pattern` (both stages, validate-then-commit), `pattern_eval_slot` (non-static in grain.c + `extern` in `ligase~.c`), and register `pattern`. Add the per-block `pattern_eval_slot` call into the phase-advance loop. Emit a debug `post()` from `pattern_eval_slot` (gated on `LIGASE_DEBUG`) reporting `slot, present-step index, cached_value, cached_is_rest, cycle_index` on the `changed` block.

- **Step 4 → GATE E (verify, headless).** Build; run the acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (the `AUTOMATED_TEST_PROCEDURE.md` convention). Each patch MUST contain `\; pd dsp 1` from `loadbang` (required in `-nosound` mode — Pd does not start DSP automatically without an audio device, per `AUTOMATED_TEST_PROCEDURE.md`). Confirm step timing, nesting, alternation, and the comma-free wire syntax. No attachment is exercised (P2/P3).

## Acceptance criteria

All headless via `pd -nogui -nosound -stderr -path . <patch>.pd` with `timeout`, reading the `LIGASE_DEBUG` `post()` from `pattern_eval_slot` on stderr. Every patch loadbangs `\; pd dsp 1` to drive perform (without it no block advances and every timing assertion silently passes-by-vacuity). No audio is asserted (P1 drives nothing).

1. **Even step timing at a known BPM.** Send two bangs 500 ms apart (`bpm = 120`), then `pattern_cycle 4/4` (cycle = `240000/120/1000 = 2.0 s`) and `pattern 0 0.0 0.25 0.5 0.75` (4 even steps). Drive DSP; the debug post must report the active present-step index advancing `0→1→2→3` at `0.5 s` boundaries (each step = `2.0 s / 4`), and `cached_value` matching the step's value. Verifiable from stderr timestamps of the `changed` posts.

2. **`pattern_cycle 4/4 3/8` cycle length.** At `bpm = 120`, `pattern_cycle 4/4 3/8` must set `cycle_total_sec = 2.75` (the worked example `330000/120/1000`). Confirm via the commit `post()` and via the inter-cycle period of the `cycle_index` increment in the debug stream (one full cycle every 2.75 s).

3. **Nested subdivision subdivides correctly.** `pattern 0 [ 0.2 0.4 ] 0.8` flattens to three leaves with spans `1/4, 1/4, 1/2` (the `[]` group occupies the first half, split evenly into two quarters; the bare `0.8` is the second half). The debug post must show the `0.8` step active for the back half of each cycle and `0.2`/`0.4` each for one quarter. Confirms fraction-multiplying span descent.

4. **Weighted steps.** `pattern 0 0.2@3 0.8` → `total_weight = 4`, spans `3/4 + 1/4`; the `0.2` step must be active for the first 3/4 of the cycle, `0.8` for the last 1/4.

5. **Alternation selects one member per cycle (DEFAULT cycle, NO `pattern_cycle`).** `pattern 0 < 0.1 0.5 0.9 >` after two bangs (default 1-bar cycle = 2.0 s at bpm 120, computed in `ligase_bang` because `cycle_seg_count == 0`): across three consecutive cycles, the single present step's `cached_value` must read `0.1`, then `0.5`, then `0.9` (i.e. `cycle_index mod 3`), then repeat. Confirms the long-counter `<>` member select AND the default-cycle computation in `ligase_bang`.

6. **Scale-degree pitch *loads* (not yet wired).** `pattern pitch < 0 4 7 >` must commit a 1-step alternating table into the pitch slot, post `slot pitch: ... cycle ...`, and leave `pitch_control.mode` unchanged (P3 wires the mode). Confirms pitch values are stored as raw degrees (`0`, `4`, `7`), not normalized to 0..1. (The semitone-yield assertion — degree 7 → root + perfect fifth, degree 8 → root + 1 octave via wrap — is a **P3** acceptance criterion; here we only assert the leaf values committed are the integer degrees.)

7. **Comma / whitespace robustness.** `pattern 0 0.2, 0.8` (a comma, as a user might type) dispatches as `pattern 0 0.2` then a bare `0.8` to inlet 0; the handler must accept the first (a 1-step pattern) and the engine must not crash on the stray `0.8`. The bracket tokens must arrive standalone: `pattern 0 [ 0.2 0.5 ]` parses, while a glued `[0.2` (no space) must be rejected cleanly with a `pd_error`, not crash.

8. **Guards / zero-init.** Before any bang (`bpm == 0`), loading and driving DSP must leave `pattern_phase` frozen at 0 with no NaN and no div-by-zero. `make clean && make` must be warning-free, and a freshly constructed object (relying on the `scheduler_create` memset) must report every slot `step_count == 0` (inactive) before any `pattern` message. Unbalanced brackets (`pattern 0 [ 0.2`) and over-cap inputs (a flatten exceeding `PATTERN_MAX_STEPS`) must `pd_error` and preserve the prior slot table.

## Risks / out-of-scope

**Risks**
- **Memory footprint.** `PATTERN_SLOTS=8` `pattern_table_t` arrays sit in every `perlin_state_t` (one per object). Measured ≈ 14.3 KB per instance — acceptable for a single granular object but pinned at GATE A.
- **No grain.h.** `pattern_eval_slot` MUST be non-static in `grain.c` + `extern`-declared in `ligase~.c` (beside `:59-63`), matching the existing grain-function linkage convention; there is no header to add it to. A reviewer expecting a `grain.h` edit will not find one.
- **Slew smearing (P2 concern, noted here).** When P2 lets a pattern feed `sample_param_range`, a nonzero `slew` will cross-fade stepped values; stair-stepping is preserved only at `slew = 0`. P1 does not touch slew but the cache it produces is what gets smeared, so document it.
- **Sparse-grain pitch (P3 concern, noted here).** Pitch reads the *block-advanced* cache at *grain-trigger* rate, so sparse grains can skip cycle steps. P1's clock is correct; the skip is an inherent consumer-side limit documented for P3.
- **Default-cycle computation site.** `cycle_total_sec` for the `cycle_seg_count == 0` default MUST be set in `ligase_bang` (and recomputed on each bang), not only in the `pattern_cycle` handler — otherwise the perform guard `cycle_total_sec > 0.0` freezes any slot loaded without a `pattern_cycle` (would silently break acceptance #5). Enforced at GATE C.
- **Float cycle_index drift avoided by design.** Incrementing `cycle_index` by the integer wrap (not `floor(phase*bigN)`) prevents long-run drift; the `while` wrap also correctly handles `cycle_total_sec` smaller than one block (multiple cycles per block).
- **Publish-barrier ordering.** `step_count` must be the *last* field written on commit, and the audio thread must gate on `step_count >= 1`. If reordered, perform could read a half-built table. Enforced in code review at GATE D.

**Out of scope (P1)**
- Any **attachment**: no `case RAND_TYPE_PATTERN` in `sample_param_range`, no modulation-outlet gating, no `PITCH_MODE_PATTERN` case in `scheduler_trigger_grain` — that is P2/P3.
- The `ligase_rand_type` / `ligase_pitch_rand_type` parse branches and the `get_rand_type_name` / dump-switch print fixes (including the latent SAW/SINE/SQUARE gap at `get_rand_type_name`, `:3991`) — P2. When P2 adds `RAND_TYPE_PATTERN` it must cover BOTH the `get_rand_type_name` switch (`:3991`, currently incomplete) and the dump switch (`:3317-3326`, currently complete) to stay `-Wswitch`-clean.
- The outlet-3 note-change mode test extension (`:1631-1632`) — P3.
- **Euclid `(k,n)`** Bjorklund precompute and `*N`-into-true-SEQ beyond the basic copy desugar are noted in the architecture but are **not required** for P1's acceptance; if not implemented in P1 they carry forward. (The grammar reserves the tokens; the parser may reject `(k,n)` with a clean `pd_error` in P1.)
- Tidal **comma = stack / polyphony** — **not applicable** to this feature (single scalar/pitch over time); intentionally unsupported, leaving `,` to its Tidal meaning. (Not a comma in the wire form regardless — Pd's binbuf eats it.)
- Param-name → slot resolution and the `pitch` mode-set on commit — deliberately deferred so P1 is testable against a bare numeric slot with zero P2/P3 coupling.