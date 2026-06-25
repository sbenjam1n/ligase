# Plan: Event-firing patterns + Euclid / Tidal grammar

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — new directions)
**Related:** the **pattern subsystem** (P1 `Plans/pattern_notation.md` — parser + the fifth free-running BPM cycle clock + `pattern_table_t`/`pattern_eval_slot`; P2 `Plans/pattern_modulation.md` — `RAND_TYPE_PATTERN` attach + the per-slot cache contract; P3 `Plans/pattern_pitch.md` — `PITCH_MODE_PATTERN` scale-degree stepper), all **merged and in-tree**; the **granular** path (`scheduler_trigger_grain`, the grain pool, `grain_trigger_counter`/`grain_trigger_period`); the **splice navigation** path (`splice_behavior.pending_splice`, `ligase_shift`/`ligase_organize`); the **transport** (`ligase_trigger`, `is_playing`/`is_triggering`, one-shot `loop_mode`); and the **outlets** (`x_grain_bang_out`, `x_splice_end_out`). This plan adds a new pattern TARGET TYPE (event slots) plus a parse-time grammar extension (Euclid); it depends on the whole pattern subsystem already in the tree.

> **GROUNDING NOTE (2026-06-24, re-verified adversarially).** Every file:line ref below was read against `src/ligase~.c`, `src/grain.c`, `src/types.h` and confirmed real and accurate this session. **Three corrections to the brief's grounding were folded in (flagged ⟦G⟧):** (1) the smear-pitch and grain-pitch pattern slots are **already landed** — slot 7 (`PATTERN_SLOTS-1`) = grain pitch, slot 6 (`PATTERN_SLOTS-2`) = smear pitch, and `pattern_alloc_param_slot` (`ligase~.c:2785-2797`) **already** hands out only slots **0..5** (loops `i < PATTERN_SLOTS - 2`) — so an event target must NOT claim a third reserved slot by default; it rides the **existing 0..5 param pool** as a new target *kind* (cleaner than the brief's "reserve a slot range" and needs no allocator-reservation edit). (2) The `smear_pitch`/`attach_smear_pitch` branch the brief described as "to add" is **already present** at `ligase~.c:3012-3014`; the event branch is a new sibling beside it (and beside `pitch` at `:3009`). (3) Pattern leaf values are **NOT normalized at flatten time** — `pattern_flatten` stores `st->value = n->value` verbatim (`ligase~.c:2953`); params read `cached_value` *as if* already in [0,1] (`grain.c:304`, comment "cached_value is already in [0,1]"), pitch reads it as a raw degree (`grain.c:850`). So an event target reusing `value` as a raw discrete arg (burst count / splice index) is the same "the consumer interprets the raw leaf value" idiom already in the tree — no new normalization. Confirmed: **there is no `t_clock`/`clock_new`/`clock_delay` anywhere in the object** (grep clean), so the "events ride the audio-thread action idiom, no new clock" constraint is real.

---

## Problem

The pattern subsystem today is a **continuous-value engine**: a step's leaf value only ever *sets* something. A param pattern writes `cached_value` into a `param_range` (`RAND_TYPE_PATTERN`, `grain.c:296-306`); a pitch pattern writes a scale degree into the grain pitch switch (`PITCH_MODE_PATTERN`, `grain.c:837-858`); smear pitch the same on its own slot. Nothing in the pattern path *fires* — the cycle clock advances the slot, the evaluator caches a value, and a downstream consumer samples that value when it happens to need it.

**OWNER INTENT.** Let a pattern step *fire an event* instead of (only) setting a value — turning the BPM cycle clock into a real rhythmic **sequencer**. A step on an event pattern should be able to: **burst a cluster of grains**, **jump/select a splice**, **retrigger playback from the splice start**, **gate** the transport on/off, or **bang an outlet** — on the beat, quantized to the same cycle grid as everything else. Concretely: `pattern trigger grain [ 3 ~ 1 1 ]` would fire a 3-grain burst on step 1, nothing on the rest, then one grain on steps 3 and 4, looping on the cycle. Plus: **fill in Euclidean `(k,n)`** — the grammar's research reserved the idea but the parser has no token for it — so `pattern trigger grain [ 1(3,8) ]` lays a 3-of-8 Bjorklund rhythm; and, **if cheap, a couple of Tidal transforms** (`fast`/`slow`/`rev`/`degrade`) as parse-time rewrites.

This is a **direction** plan: it adds a new pattern *target type* (events) and a *grammar* extension (Euclid), staged as a **v1 core** (the event dispatcher + the four/five base actions + Euclid) plus **later extensions** (Tidal transforms, probabilistic steps, per-step event args). It is honest about the one genuinely new thing — firing discrete actions from the per-block eval loop — and reuses, verbatim, the fact that the engine **already** calls `scheduler_trigger_grain`, `outlet_bang`, and the splice/transport flag-writes directly from the audio thread.

The key enabling fact: `pattern_eval_slot` **already computes a per-block `changed` flag** (`grain.c:465`, `pt->changed = (idx != pt->last_step_index)`) that marks the exact block where the active step index advances — and today **only debug-logging reads it** (`ligase~.c:1690`). That `changed` edge is the ready-made event boundary. We are not building a sequencer clock; one already runs. We are giving its step-advance edge an action to take.

## Mechanics / target surface — the EXISTING code this extends

**The cycle clock + per-slot cache (the timing we reuse, do NOT duplicate):**

- **`ligase_perform` pattern eval loop** — `src/ligase~.c:1679-1697`. Once per DSP block, guarded on `x->scheduler && x->bpm > 1.0 && x->cycle_total_sec > 0.0` (`:1679`): for each slot with `step_count >= 1` (`:1683`) it advances `pattern_phase[s]` by `inc = (n/sr)/cycle_total_sec` (`:1681,1684`), wraps and bumps `pattern_cycle_index[s]` (`:1685-1688`, drives `<>` alternation), calls `pattern_eval_slot(ps, s)` (`:1689`), then `if (x->pattern_debug && ps->pattern[s].changed)` logs (`:1690-1695`). **This is THE insertion point** — the event dispatch goes right after `:1689`, reusing the `changed` flag the very next line already reads.
- **`pattern_eval_slot`** — `src/grain.c:408-467`. Sole once-per-block writer of the slot cache; allocation-free, perform-safe. Sets `cached_value`/`cached_is_rest` (`:459-463`) and `changed = (idx != last_step_index)` (`:465`). A rest **holds** the previous `cached_value` and sets `cached_is_rest=1` (`:459-460`). The event target consumes the `changed` it already computes — fire when `changed && !cached_is_rest`.
- **`pattern_table_t`** — `src/types.h:478-491`. The per-slot compiled flat step table + cache. `step_count` (`:480`) is the publish barrier (0 = inactive). `cached_value`/`cached_is_rest`/`changed`/`last_step_index` (`:486-489`) are the evaluator cache. The event target reads `changed` (`:488`) and `cached_value` (`:486`).
- **`pattern_step_t`** — `src/types.h:469-475`. One compiled leaf: `value` (`:470`, "normalized 0..1 (params) OR raw scale degree (pitch)"), `weight`, `is_rest` (`:472`), `alt_group`/`alt_member`. For an EVENT target, `value` becomes the discrete event arg (burst count / splice index); `is_rest=1` leaves are the silent steps Euclid emits for the `n-k` off-positions.
- **`PATTERN_SLOTS`** — `src/types.h:465` (`== 8`). Per-slot arrays `pattern[]`/`pattern_phase[]`/`pattern_cycle_index[]` (`types.h:597-599`) are all sized to it. ⟦G⟧ **Current slot map (landed):** slot 7 (`PATTERN_SLOTS-1`) = grain pitch, slot 6 (`PATTERN_SLOTS-2`) = smear pitch, slots **0..5** = the param/event auto-pool (`pattern_alloc_param_slot`, `:2785-2797`, loops `i < PATTERN_SLOTS - 2`).

**The pattern handler surface (the event target + Euclid ride these):**

- **`ligase_pattern`** — `src/ligase~.c:3000-3165+`. Message-thread `pattern <target> <tokens>` handler. Stage-0 target resolution at **`:3009-3036`**: `"pitch"` → slot 7 + `attach_pitch` (`:3009-3011`); `"smear_pitch"` → slot 6 + `attach_smear_pitch` (`:3012-3014`); `A_SYMBOL` param name → `get_param_range_by_name` + `pattern_alloc_param_slot` (`:3015-3026`); `A_FLOAT` → raw slot (`:3027-3028`). The two-stage parse (stage-1 loop `for (int i = 1; i < argc; i++)` at `:3051`, through `:3133`) → flatten (`:3135-3142`) → commit with `step_count` published LAST (`:3155-3163`) is **target-agnostic and reused verbatim**. The new `trigger`/`event` keyword target is **one more else-if** here.
- **Stage-1 leaf-suffix dispatch** — `src/ligase~.c:3074-3077`. Where `@N` (weight, `:3074`), `*N` (multiply, `:3075`), `!N` (replicate, `:3076`) are parsed off a token, with a bad-suffix guard (`:3077`). **Euclid `(k,n)` parses here**, as a new suffix form beside these (inserted before the catch-all `else if (*p != '\0')` at `:3077`).
- **`*N` expansion (the tree-builder Euclid reuses)** — `src/ligase~.c:3104-3118`. The `if (leaf_mult > 1)` block expands one leaf into a `PN_SEQ` of `leaf_mult` child leaves, respecting `PATTERN_MAX_NODES` (`:3105,3111`). **Euclid reuses this exact shape:** expand one leaf into a `PN_SEQ` of `n` children, `k` value-leaves + `n-k` `is_rest` leaves per the Bjorklund bitmap. `pattern_flatten` (`:2942`) and `pattern_eval_slot` need **zero** changes — an Euclid pattern is just a step table with rests, which the evaluator already handles.
- **`pattern_flatten`** — `src/ligase~.c:2942-2969`. Stores `st->value = n->value` **raw** (`:2953`), `st->is_rest = n->is_rest` (`:2955`). ⟦G⟧ No normalization — the consumer interprets `value`. Unchanged by this plan.
- **`ligase_pattern_clear`** — `src/ligase~.c:2851-2924`. Per-target clear branches: numeric (`:2861-2872`), `"pitch"` (`:2879-2889`), `"smear_pitch"` (`:2890-2902`), named param (`:2904-2923`). The event-target clear is a kind-reset added into these branches.

**The discrete actions (all ALREADY fired from the audio thread — the idiom we reuse):**

- **`scheduler_trigger_grain`** — `src/grain.c:751`. Signature `(scheduler_t*, float position, float speed, uint32_t splice_start, uint32_t splice_end, float amplitude, float pan, float saw_cycles, float saw_depth)`. Spawns a grain from the pool via `scheduler_allocate_grain` (`:769`) — **no heap alloc**, already called from perform at `ligase~.c:1162,1191,1312`. The grain-burst action calls it `N` times inline. (Note: `splice_start`/`splice_end` are `uint32_t` — pass the `s0,e0` that `splice_get_bounds` writes, no cast needed.)
- **`ligase_process_grains` trigger+outlet block** — `src/ligase~.c:1153-1173` (and the SCANNING/CLOCK_ADVANCE twins around `:1180-1216`, `:1300-1322`). **Proof the audio thread already fires events:** it calls `scheduler_trigger_grain` (`:1162`) and `outlet_bang(x->x_grain_bang_out)` (`:1169`) directly; `outlet_bang(x->x_splice_end_out)` likewise (`:1149,1215`). A grain-burst / bang event reuses this exact idiom.
- **`splice_behavior.pending_splice`** — `src/types.h:332` (int, `-1` = none). The perform loop applies it at the next wrap: `x->reel->splices.current_splice = x->splice_behavior.pending_splice; pending_splice = -1;` (`ligase~.c:1221-1222`, twin at `:1275-1276`). The splice-jump action just **writes this int** — atomic, no alloc. `ligase_shift` (`:2044`) / `ligase_organize` (`:2067`) are the message-side equivalents; the canonical wrap idiom `((target % count) + count) % count` is at `:2055`.
- **`ligase_trigger`** — `src/ligase~.c:1943-1953`. One-shot trigger: `playback_position = (float)s; is_playing = 1; is_triggering = 1;` (`:1950-1952`, `s` from `splice_get_bounds` at `:1949`). It touches no active grains (verified: the function body has no grain-clearing code; the "active grains play out" guarantee is documented at the sibling `ligase_play` comment, `:1958-1959`). The retrigger action sets these same flags inline — pure int/float writes.
- **`splice_behavior_t.loop_mode`** — `src/types.h:334` (1 = loop, 0 = one-shot). The gate action flips `is_triggering` (and `is_playing`) — the same flags `ligase_play`/`ligase_trigger` write (`:1956-1957`, `:1951-1952`).
- **Outlets** — `x_splice_end_out` / `x_grain_bang_out` declared `src/ligase~.c:181-182`. `outlet_bang` on either is the bang action.

**No-`t_clock` constraint (verified):** grep for `t_clock`/`clock_new`/`clock_delay` in `ligase~.c`/`grain.c`/`types.h` returns nothing. Events MUST stay on the existing audio-thread action idiom; this plan introduces no clock and no deferred-message queue.

## Design

### Overview

Two orthogonal additions, each landing on an existing seam:

1. **Event target type** — a new pattern *kind*. A slot can be tagged `EVENT_*` at commit; in the per-block eval loop, after `pattern_eval_slot`, if the slot is an event slot and `changed && !cached_is_rest`, the loop dispatches a discrete action inline. The slot's `cached_value` supplies the event argument. **The clock, the step table, the evaluator, and the cache are all reused unchanged** — an event slot advances on the same `pattern_phase`/`pattern_eval_slot` machinery; it just consumes `changed` instead of being pulled for a value.

2. **Euclid `(k,n)` grammar** — a **parse-time-only** extension. A `(k,n)` suffix/token runs Bjorklund on the message thread into a stack bitmap, then expands one leaf into a `PN_SEQ` of `n` children (`k` value-leaves, `n-k` rests) exactly like `*N`. The runtime path is untouched; an Euclid pattern is just a step table with rests. Euclid composes with **both** continuous and event targets (a `(3,8)` rhythm of values, or of grain bursts).

v1 ships: the event dispatcher + actions {grain-burst, splice-select, retrigger, gate, bang} + Euclid. Later extensions (own GATE A item): Tidal transforms (`fast`/`slow`/`rev`/`degrade`), probabilistic steps (`?`), and a richer two-number event arg (e.g. burst-count *and* grain-position).

### Data structures (`src/types.h`)

**New target-kind enum** (near the pattern types region, after `pattern_table_t` at `:491`):

```c
// What a pattern slot DRIVES. Default 0 = VALUE preserves every current pattern (param/pitch read
// cached_value pull-style). EVENT_* slots instead FIRE a discrete action on the changed edge.
typedef enum {
    PATTERN_KIND_VALUE = 0,   // 0 — continuous value (param via RAND_TYPE_PATTERN, or pitch). DEFAULT.
    PATTERN_KIND_EVENT_GRAIN, // fire cached_value grains (a burst) on each step edge
    PATTERN_KIND_EVENT_SPLICE,// write cached_value -> splice_behavior.pending_splice (jump at next wrap)
    PATTERN_KIND_EVENT_RETRIG,// retrigger playback from splice start (mirror ligase_trigger)
    PATTERN_KIND_EVENT_GATE,  // toggle/set transport: cached_value != 0 -> play, == 0 -> stop
    PATTERN_KIND_EVENT_BANG   // outlet_bang(x_grain_bang_out)
} pattern_target_kind_t;
```

**Per-slot tag.** ⟦G⟧ A slot can be VALUE or EVENT independent of *which* slot it is, so the tag is a **parallel per-slot array** on `perlin_state_t` (next to the pattern arrays at `types.h:597-599`), NOT a field inside `pattern_table_t` (keeping the table a pure compiled-steps record, and keeping the commit `memcpy` of the scratch table — `ligase~.c:3160` — from carrying a stale kind):

```c
    int pattern_target_kind[PATTERN_SLOTS];   // pattern_target_kind_t per slot; 0 = VALUE (default)
```

This is covered by the `scheduler_create` `memset(sched, 0, sizeof(scheduler_t))` (`grain.c:487`), so **every slot defaults to `PATTERN_KIND_VALUE` (0)** on construction — all current behavior is bit-identical until a slot is explicitly tagged an event slot. No init line is even strictly required, but Step 1 adds an explicit comment-anchored zero for clarity, mirroring the pattern arrays.

**Optional event-arg refinement (later extension, NOT v1):** if a second event parameter is wanted (e.g. grain-burst position as well as count), add `float pattern_event_arg[PATTERN_SLOTS]` set at commit from a message arg. v1 derives the single arg from `cached_value` only.

### Where the event fires (the one behavioral insertion point, `src/ligase~.c:1689`)

Right after `pattern_eval_slot(ps, s);` (`:1689`), before/around the existing debug log (`:1690-1695`):

```c
            pattern_eval_slot(ps, s);

            // NEW: event dispatch. If this slot is an EVENT slot and the active step just advanced
            // to a non-rest step, FIRE the action inline. Reuses the `changed` flag pattern_eval_slot
            // already set (read again at :1690 for debug). cached_value supplies the event arg.
            // All actions below are things perform ALREADY does (scheduler_trigger_grain at :1162,
            // outlet_bang at :1169, pending_splice write at :1221, ligase_trigger flag-writes at :1950)
            // -> no t_clock, no alloc, audio-thread-safe under the default Pd scheduler.
            int kind = ps->pattern_target_kind[s];
            if (kind != PATTERN_KIND_VALUE &&
                ps->pattern[s].changed && !ps->pattern[s].cached_is_rest) {
                ligase_pattern_fire_event(x, kind, ps->pattern[s].cached_value);
            }

            if (x->pattern_debug && ps->pattern[s].changed) { /* ...existing log unchanged... */ }
```

`ligase_pattern_fire_event(ligase_t *x, int kind, float arg)` is a small static dispatcher (defined near the perform helpers) that does ONLY perform-safe work — the same int/flag writes and pool/outlet calls the perform routine already issues:

```c
static void ligase_pattern_fire_event(ligase_t *x, int kind, float arg) {
    switch (kind) {
    case PATTERN_KIND_EVENT_GRAIN: {
        // Burst: fire (int)arg grains at the current playhead, reusing the exact call + args
        // ligase_process_grains uses (:1162/1191/1312). Clamp to a sane cap; pool-bounded anyway.
        int burst = (int)arg; if (burst < 1) burst = 1; if (burst > PATTERN_EVENT_MAX_BURST) burst = PATTERN_EVENT_MAX_BURST;
        uint32_t s0, e0;
        if (x->reel && x->reel->length) {
            splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice, x->reel->length, &s0, &e0);
            float pos = (float)s0 + x->grain_start * (float)(e0 - s0);
            for (int b = 0; b < burst; b++)
                scheduler_trigger_grain(x->scheduler, pos, x->speed, s0, e0,
                                        x->amplitude, x->pan, x->saw_cycles, x->saw_depth);
            if (x->grain_bang_rate > 0) outlet_bang(x->x_grain_bang_out);   // optional, mirrors :1165-1169 policy
        }
        break;
    }
    case PATTERN_KIND_EVENT_SPLICE: {
        // Jump/select a splice: write pending_splice (perform applies it at the next wrap, :1221).
        if (x->reel && x->reel->splices.count > 0) {
            int idx = (int)arg, c = x->reel->splices.count;
            idx = ((idx % c) + c) % c;                      // wrap, like ligase_shift (:2055)
            x->splice_behavior.pending_splice = idx;        // atomic int write, no alloc
        }
        break;
    }
    case PATTERN_KIND_EVENT_RETRIG: {
        // Retrigger from splice start WITHOUT silencing active grains (mirror ligase_trigger :1949-1952).
        if (x->reel && x->reel->length) {
            uint32_t s0, e0;
            splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice, x->reel->length, &s0, &e0);
            x->playback_position = (float)s0;
            x->is_playing = 1; x->is_triggering = 1;
        }
        break;
    }
    case PATTERN_KIND_EVENT_GATE: {
        // Gate transport: arg!=0 -> play, arg==0 -> stop new triggering (active grains finish, :1327).
        int on = (arg != 0.0f);
        x->is_triggering = on; x->is_playing = on;          // same flags ligase_play writes (:1956-1957)
        break;
    }
    case PATTERN_KIND_EVENT_BANG:
        outlet_bang(x->x_grain_bang_out);                   // same call already at :1169
        break;
    }
}
```

`PATTERN_EVENT_MAX_BURST` (e.g. 16) is a new `#define` in `types.h` next to the pattern caps (`:462-466`); the grain pool already bounds the real ceiling (`scheduler_allocate_grain` returns NULL when full, handled gracefully by `scheduler_trigger_grain` `grain.c:769-776`). The dispatcher does **no** malloc, no `gensym`, no binbuf — every branch is the perform-safe subset already proven at the cited lines.

**Why `changed && !cached_is_rest` is correct.** `changed` is true exactly on the block where `last_step_index` advances (`grain.c:465`) — one fire per step entry, no re-fire while the step is held (dense blocks within a step do not re-trigger). A rest step sets `cached_is_rest=1` and holds `cached_value` (`grain.c:459-460`), so `!cached_is_rest` makes rests **silent** — which is precisely what makes Euclid's `n-k` off-positions correctly *not* fire. This is the v1 semantics: one action per non-rest step boundary, quantized to the cycle.

### Message API (`src/ligase~.c`, target resolution at `:3009-3036`)

⟦G⟧ The event target is **one more else-if** beside `pitch` (`:3009`) and `smear_pitch` (`:3012`), BEFORE the generic `A_SYMBOL` param branch (`:3015`) so `trigger`/`event` doesn't fall through to `get_param_range_by_name`:

```c
    } else if (argv[0].a_type == A_SYMBOL &&
               (strcmp(argv[0].a_w.w_symbol->s_name, "trigger") == 0 ||
                strcmp(argv[0].a_w.w_symbol->s_name, "event") == 0)) {
        // pattern trigger <action> <tokens...> ; action in {grain,splice,retrig,gate,bang}
        if (argc < 3 || argv[1].a_type != A_SYMBOL) {
            pd_error(x, "ligase~: pattern trigger needs <grain|splice|retrig|gate|bang> then tokens");
            return;
        }
        event_kind = pattern_event_kind_from_name(argv[1].a_w.w_symbol->s_name);  // -> PATTERN_KIND_EVENT_*
        if (event_kind == PATTERN_KIND_VALUE) { pd_error(x, "ligase~: unknown trigger action"); return; }
        slot = pattern_alloc_event_slot(x);           // ride the 0..5 auto-pool (see slot note below)
        if (slot < 0) { pd_error(x, "ligase~: pattern: no free pattern slots"); return; }
        // tokens are argv[2..]; shift the parse start by one so the action keyword is consumed
```

Two parse details:

- **Token offset.** `pattern trigger grain [ 3 ~ 1 ]` puts the action keyword at `argv[1]` and tokens at `argv[2..]`. The pitch/param targets start the stage-1 parse at `argv[1]` (`:3051`, `for (int i = 1; ...)`). For the event target, start at `argv[2]`. Cleanest: introduce `int tok_start` (default 1; set to 2 for the event branch) and change the stage-1 loop bound to `for (int i = tok_start; ...)` — a one-line, target-agnostic change.
- **Slot allocation.** ⟦G⟧ `pattern_alloc_param_slot` currently takes a `param_range_t *` and **dereferences it unconditionally** — `range->rand_type` / `range->rand_instance` at `:2789-2790`, with **no NULL guard** — before falling through to the free-slot scan (`for i<PATTERN_SLOTS-2`, `:2793-2794`). So an event slot (no `param_range`) needs one of two clean options (GATE A): **(a) [R]** add a sibling `pattern_alloc_event_slot(x)` that runs only the free-slot scan (`:2793-2795`), skipping the reuse branch entirely — the pseudocode above uses this; or (b) generalize the existing fn to accept `NULL` by wrapping the reuse branch in `if (range && range->rand_type == RAND_TYPE_PATTERN && ...)`. **NB:** calling `pattern_alloc_param_slot(x, NULL)` against the *current* code would dereference NULL at `:2789` and crash — the sibling fn (option a) or the guard (option b) MUST land first. Either way the event slot lives in the **same 0..5 pool** as param patterns — they share capacity, which is correct (a patch trades a param pattern for an event pattern).

**Commit-time tag (publish discipline).** ⟦G⟧ The lock-free barrier is: commit scratch→live via `memcpy` with `scratch.step_count` zeroed (`:3159-3160`), then set `live->step_count = committed_steps` LAST (`:3163`). The per-slot kind MUST be written **before** `step_count`, so the audio thread never sees an active slot (`step_count>0`) that is still tagged stale:

```c
    /* ...after memcpy(live, &scratch, ...) at :3160, BEFORE live->step_count = committed_steps at :3163: */
    if (event_kind != PATTERN_KIND_VALUE)
        ps->pattern_target_kind[slot] = event_kind;   // publish kind BEFORE step_count (barrier)
    else
        ps->pattern_target_kind[slot] = PATTERN_KIND_VALUE;  // value patterns reset the tag (slot reuse)
    ...
    live->step_count = committed_steps;               // publish barrier (existing :3163)
```

Resetting to VALUE on a non-event commit matters because slots are pooled: a slot that was an event slot and is later loaded with a value pattern must clear its kind, or it would mis-fire. (The `memcpy` of the scratch table does NOT carry the kind, since the kind lives in the parallel array, not the table — this is why the parallel array is the right home.)

**Clear (`ligase_pattern_clear`, `:2851`).** The named-param clear branch (`:2904-2923`) already frees a pooled slot. Add a kind reset there: when clearing a slot, set `ps->pattern_target_kind[slot] = PATTERN_KIND_VALUE`. Add an explicit `pattern_clear trigger <action>`? Not needed for v1 — an event pattern is cleared by `pattern_clear <slot-number>` (numeric branch, `:2861-2872`; add the kind reset there too) or by re-loading the slot. GATE A: whether to add a symbolic `pattern_clear trigger` convenience.

**Registration.** No new `class_addmethod` — `pattern` and `pattern_clear` are already `A_GIMME` selectors (`:5506`, `:5508`); the event target rides them. ⟦G⟧ **Name-overlap note (not a collision):** the bare `trigger` *selector* is already a class method (`ligase_trigger`, registered `:5462`). Here `trigger` is only the **sub-keyword `argv[0]` inside the `pattern` GIMME handler** — `pattern trigger ...` routes through `ligase_pattern`, while a bare `trigger` still routes to `ligase_trigger`; the two never alias at the dispatch layer. The overlap is purely a docs/UX footgun — mitigated by also accepting `event` as the target keyword (GATE A.3), which the manual should lead with to avoid user confusion with the transport `trigger`.

### Euclid `(k,n)` — parse-time only (`src/ligase~.c:3074-3118`)

**Bjorklund helper** (message-thread, stack array, no alloc) — a small static fn near `ligase_pattern`:

```c
// Bjorklund(k,n): write a length-n on/off bitmap (1 = pulse) distributing k pulses as evenly as
// possible. Standard pair-and-remainder construction; n bounded by PATTERN_MAX_STEPS (64).
static void bjorklund(int k, int n, unsigned char *out /* [n] */);
```

**Suffix parse** — extend the stage-1 suffix dispatch (after `!` at `:3076`, before the bad-suffix guard at `:3077`):

```c
                else if (*p == '(') {
                    int kk, nn;
                    if (sscanf(p, "(%d,%d)", &kk, &nn) != 2 || nn < 1 || kk < 0 || nn > PATTERN_MAX_STEPS)
                        PAT_FAIL("ligase~: pattern: bad Euclid suffix in '%s' (want (k,n))", t);
                    if (kk > nn) kk = nn;
                    leaf_euclid_k = kk; leaf_euclid_n = nn;   // signal the expansion below
                }
```

This accepts the canonical Tidal `1(3,8)` form (the leaf value `1` is what the `k` pulses carry). It survives Pd binbuf as one `A_SYMBOL` atom (`(`,`,`,`)` are ordinary symbol chars, not Pd separators — only top-level `,`/`;` separate messages, and these are inside a single token). GATE A confirms the surface form: `1(3,8)` suffix vs a separate `( 3 8 )` token group.

**Expansion** — mirror the `*N` block (`:3104-3118`). When `leaf_euclid_n > 0`, build a `PN_SEQ` of `n` children: run `bjorklund(k,n,bits)`, then for each position `j`, if `bits[j]` emit a value-leaf (`value = leaf_val`, `is_rest = leaf_rest`), else emit a rest leaf (`is_rest = 1`). Respect `PATTERN_MAX_NODES` exactly as the existing loop (`:3105,3111`); reject over-cap with `PAT_FAIL` (`:3105`). **Downstream is unchanged** — `pattern_flatten` (`:2942`) and `pattern_eval_slot` already handle rests. Euclid combines freely with `@N` weights and with the event target: `pattern trigger grain [ 2(3,8) ]` is a 3-of-8 burst pattern firing 2 grains per pulse.

**Interaction with `*`/`!`/`@`.** Euclid is mutually exclusive with `*N` on the same leaf (both expand one leaf into a SEQ) — reject `3*2(3,8)` at parse time, or define a precedence (GATE A; recommend reject as ambiguous). `!N` (replicate) and `@N` (weight) compose fine (replicate the whole Euclid group; weight the group).

### Tidal transforms (later extension — GATE A scope decision)

If cheap, add parse-time **rewrites** that operate on the already-built node tree, before flatten:

- **`rev`** — reverse a group's child order. Trivial pointer relink on the `PN_SEQ` children.
- **`fast N` / `slow N`** — `fast 2 [ a b ]` ≈ `[ a b ]*2` (already expressible); implement as sugar that sets `leaf_mult`/group-mult. Largely redundant with `*N`; low value.
- **`degrade` / `?`** — drop a step with probability p. This needs a *runtime* random read (per-cycle), which the current evaluator doesn't do — so it is **not** purely parse-time; it would add a per-step RNG check in `pattern_eval_slot`. Heavier; defer hard, and only if owner wants probabilistic patterns.

Recommendation: v1 ships **`rev`** only (genuinely cheap, parse-time, no runtime change) as a taste of the transform grammar; `fast`/`slow` are redundant with `*N`/the cycle; `degrade`/`?` are a separate probabilistic-steps mini-plan. This is a GATE A scope call.

### CPU / cost (honest accounting)

- **Event dispatch:** one `int` compare + two flag reads per active slot per block (`:1689` insertion), then the action only on the `changed` edge — i.e. a few times per second at musical tempos, not per sample. Grain-burst fires `scheduler_trigger_grain` up to `PATTERN_EVENT_MAX_BURST` times **on that one block** — pool-bounded, the same cost as the existing grain trigger loop. Negligible average CPU; the only spike is a burst, which is exactly the grain trigger work the engine already does.
- **Euclid:** zero runtime cost — Bjorklund runs once at parse time on the message thread; the result is an ordinary step table. The only runtime effect is more steps (rests included) up to `PATTERN_MAX_STEPS` (64), which the evaluator already walks.
- **Memory:** `+ PATTERN_SLOTS` ints (`pattern_target_kind[]`, 32 bytes/instance) + an optional `float[PATTERN_SLOTS]` if the event-arg extension lands. Trivial.

## Steps & gates

### GATE A (approval) — open design decisions for owner sign-off

This is a direction plan; GATE A is substantial. Recommendations marked **[R]**.

1. **v1 action set.** Ship which of {grain-burst, splice-select, retrigger, gate, bang} in v1? **[R] all five** — each is a perform-safe write already proven in the tree, so the marginal cost per action is tiny, and together they make the sequencer expressive. Confirm, or trim (e.g. drop `gate` if it overlaps too much with the one-shot transport).
2. **Event-arg semantics.** v1 derives the single arg from `cached_value`: for grain = **burst count**, for splice = **splice index**, for gate = **on/off (≠0)**, for retrig/bang = **ignored** (any non-rest step fires). **[R] confirm** this mapping. Alternative: reserve `0` as "rest-equivalent / no-op" so `[ 3 0 1 ]` fires bursts of 3 and 1 with a silent middle (distinct from `~`). Decide whether `0` is a no-op or a literal zero-arg.
3. **Surface syntax.** Target keyword `trigger` vs `event` (support both? **[R] both**, alias — and lead the docs with `event` to disambiguate from the transport `trigger` selector). Action sub-keyword set names (`grain`/`splice`/`retrig`/`gate`/`bang`). Euclid form `1(3,8)` suffix **[R]** vs a `( 3 8 )` token group.
4. **Slot policy.** ⟦G⟧ Event slots ride the **existing 0..5 param pool** (shared capacity with param patterns) via a free-scan-only `pattern_alloc_event_slot(x)` **[R](a)** — no new reserved slot, no `PATTERN_SLOTS` bump. Confirm vs (a-alt) a dedicated event slot (shrinks the param pool to 0..4, bumps the reservation count) or (b) `PATTERN_SLOTS` 8→9+ for headroom. The shared pool means a patch can run at most 6 simultaneous {param + event} patterns; flag if that ceiling is too low for the intended sequencer use.
5. **Grain-burst targeting.** A burst fires at the current playhead / `grain_start` position with the current `speed`/`pan`/`amplitude`. **[R] confirm** — or should an event pattern carry its own grain params (a much bigger surface; defer)? Also: should a burst respect the **splice the event pattern is "on"** (if combined with a splice-select event) — i.e. event ordering within a block. Recommend documenting block-order as: splice-select writes `pending_splice` (applies next wrap), grain-burst fires at the *current* splice this block.
6. **Gate vs one-shot transport interaction.** The gate action writes `is_triggering`/`is_playing` — the same flags the one-shot `loop`/`trigger` plan owns. Confirm the gate action is allowed to override the transport (recommend yes; the pattern is the sequencer), and whether `gate 0` should also stop active grains (recommend **no** — let them finish, consistent with `ligase_play`'s documented behavior at `:1958-1959`).
7. **Tidal transforms scope.** **[R] v1 = `rev` only** (cheap, parse-time); `fast`/`slow` deferred (redundant with `*N`); `degrade`/`?` split into a separate probabilistic-steps plan (needs a runtime RNG read in the evaluator). Confirm, or pull `degrade` into v1 if probabilistic patterns are wanted now.
8. **Euclid × `*N` collision.** Reject `v*N(k,n)` as ambiguous **[R]**, or define precedence. Confirm.

### Step 1 → GATE B (types + tag, no behavior)

Add `pattern_target_kind_t` enum and `PATTERN_EVENT_MAX_BURST` to `src/types.h` (near `:462-491`); add `int pattern_target_kind[PATTERN_SLOTS];` to `perlin_state_t` (near `:597-599`). Confirm the `scheduler_create` `memset` (`grain.c:487`) zeroes it (default VALUE); add an explicit comment-anchored note. **GATE:** `make clean && make` warning-free; a fresh object has every slot VALUE; the eval loop is unchanged ⇒ behavior bit-identical to today.

### Step 2 → GATE C (event dispatcher, the one behavioral change)

Add `ligase_pattern_fire_event` (static, perform-safe; the dispatcher above) and the insertion at `ligase~.c:1689` (the `kind != VALUE && changed && !cached_is_rest` branch). Implement v1 actions per GATE A.1. **GATE:** `make clean && make` warning-free; with no slot tagged event, the loop's new branch is never entered ⇒ no regression; the dispatcher does only int/flag writes + `scheduler_trigger_grain` + `outlet_bang` (audio-thread safe, no alloc — code-review the branch against the proven call sites `:1162/1169/1221/1950`).

### Step 3 → GATE D (event target message + commit tag)

Add the `trigger`/`event` else-if in target resolution (`:3009-3036`), the `pattern_event_kind_from_name` helper, the `tok_start` token-offset, the event slot allocation — **`pattern_alloc_event_slot(x)` (the free-scan-only sibling, GATE A.4(a)); do NOT call `pattern_alloc_param_slot(x, NULL)`, which would NULL-deref at `:2789`** — the commit-time `pattern_target_kind[slot]` write **before** `step_count` (insert between the `memcpy` at `:3160` and the `live->step_count = committed_steps` barrier at `:3163`), and the kind reset in the clear branches (`:2861-2872`, `:2904-2923`). **GATE:** `make clean && make` warning-free; `pattern trigger grain [ 3 ~ 1 1 ]` loads a 0..5 slot tagged `EVENT_GRAIN`; `pattern_clear <slot>` resets it to VALUE; existing param/pitch/smear_pitch patterns unregressed (publish-barrier ordering preserved).

### Step 4 → GATE E (Euclid grammar)

Add the `bjorklund` helper, the `(k,n)` suffix parse (`:3074-3077`), and the expansion mirroring `*N` (`:3104-3118`). Enforce caps with `PAT_FAIL`; handle the `*N` collision (GATE A.8). **GATE:** `make clean && make` warning-free; `pattern <param> [ 1(3,8) ]` produces the canonical `x..x..x.` 3-of-8 step table (verify by `pattern_debug` step trace); rests land on the off-positions; combines with the event target (`pattern trigger grain [ 1(3,8) ]`); over-cap `(k,n)` is rejected on the message thread; downstream flatten/eval unchanged.

### Step 5 → GATE F (Tidal `rev` + verify + docs)

Add `rev` (parse-time group reversal) per GATE A.7. Build; run the headless acceptance patches; update the manual's pattern section to document `pattern trigger <action>` (lead with `event`), the `(k,n)` Euclid form, `rev`, the "fires on the changed non-rest edge, tempo-locked" semantics, and the shared-slot-pool ceiling. **GATE:** all acceptance criteria pass; no regression in any existing pattern target; `make clean && make` warning-free.

## Acceptance criteria

Headless where possible via `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1` so perform runs). Event *firing* is observable headless through the **outlets** (`x_grain_bang_out`, `x_splice_end_out`) and the splice-message outlet; grain *count* and audio character are partly subjective and need an **ear-test** (noted).

1. **Event fires on the step edge, once per non-rest step (headless).** With `pattern_debug` on, `pattern trigger bang [ 1 ~ 1 1 ]` at a known BPM bangs `x_grain_bang_out` exactly on the 1st/3rd/4th steps and **not** on the rest, one bang per step entry (not per block). Count the bangs over N cycles; expect `3·N`. Verifies `changed && !cached_is_rest` and no re-fire while a step is held.
2. **Grain burst (headless count + ear-test character).** `pattern trigger grain [ 4 ~ 1 ]` over noise loaded into the reel: the grain-onset bang (`x_grain_bang_out`, if `grain_bang_rate` set) or a `print` on a downstream tap shows a 4-grain cluster on step 1, silence on step 2, one grain on step 3, looping. Headless: assert the cluster timing via outlet bangs. **Ear-test:** the burst *sounds* like a rhythmic grain cluster (subjective density/transient).
3. **Splice jump (headless).** With ≥3 splices and `send_splice_msg` on, `pattern trigger splice [ 0 1 2 ]` cycles the current splice 0→1→2 at cycle steps; the splice-message outlet reports the new index after each wrap (since `pending_splice` applies at the next wrap, `:1221`). Assert the reported splice sequence.
4. **Retrigger + gate (headless transport state).** `pattern trigger retrig [ 1 ~ ~ ~ ]` re-bangs `x_splice_end_out`-style retrigger at step 1 each cycle (playhead jumps to splice start; verify via the splice-end / position trace). `pattern trigger gate [ 1 0 ]` toggles `is_triggering` on/off per step — verify grain output starts/stops on the half-cycle while active grains finish (no hard cut), per GATE A.6.
5. **Euclid expansion (headless, exact).** `pattern <param> [ 1(3,8) ]` with `pattern_debug`: the step trace shows the 8-step table with pulses at the canonical Bjorklund 3-of-8 positions (`x..x..x.`) and rests elsewhere. `(5,8)`, `(4,16)`, edge cases `(0,4)` (all rest) and `(4,4)` (all pulse) produce the expected tables. Over-cap `(3,128)` is rejected with a `pd_error` on the message thread (no crash). Combine: `pattern trigger grain [ 1(3,8) ]` fires bursts on exactly the 3 pulse steps.
6. **Tempo-lock (headless).** An event pattern's fire timestamps (outlet-bang times) match `cycle_total_sec / step_count` within one DSP block, and doubling BPM halves the inter-event interval — same BPM-lock the continuous pattern targets already satisfy. At `bpm ≤ 1` the clock holds (`:1679` guard) and **no** events fire (no div-by-zero, no spurious bang).
7. **No regression (headless).** A patch that sends no `pattern trigger` / `(k,n)` message behaves bit-for-bit as today: param patterns (`RAND_TYPE_PATTERN`), `pattern pitch` (slot 7), `pattern smear_pitch` (slot 6) all unregressed; `make clean && make` warning-free; `test_delay.pd` clean. Loading a value pattern into a slot that previously held an event pattern correctly resets the kind to VALUE (no stray fires).
8. **`rev` (headless).** `pattern <param> [ a b c ] rev` (or the chosen syntax) produces the reversed step order `c b a` in the trace; round-tripping `rev rev` restores the original.

## Risks / out-of-scope

**Risks**

- **Audio-thread action safety is load-bearing.** The whole design rests on the fact that perform already calls `scheduler_trigger_grain`/`outlet_bang`/splice-flag-writes directly under the default Pd scheduler (`:1162/1169/1221/1950`). The dispatcher must contain **only** that proven subset — any malloc/`gensym`/binbuf in an action would break the contract. Mitigation: code-review every action branch against its cited perform call site; no new primitives. **If the object ever runs under a thread-split scheduler, this (and the existing perform `outlet_bang`s) would need a clock — out of scope, same exposure as today.**
- **Publish-barrier ordering for the kind tag.** If `pattern_target_kind[slot]` is written *after* `step_count`, the audio thread can observe an active slot with a stale kind for one block → a mis-fire. Mitigation: write the kind **before** `step_count` (Step 3), mirroring the existing barrier (`:3159-3163`); the parallel-array home (not inside the `memcpy`'d table) makes this a single ordered write.
- **Slot reuse mis-tag.** A pooled slot that was an event slot, then reloaded as a value pattern, must reset its kind to VALUE on commit and on clear — else it mis-fires. Mitigation: explicit reset in both the commit (Step 3) and the clear branches (`:2861-2872`, `:2904-2923`); AC7 tests it.
- **Allocator NULL-deref footgun.** `pattern_alloc_param_slot` dereferences its `range` arg with no NULL guard (`:2789`). The event path MUST use the free-scan-only `pattern_alloc_event_slot(x)` (or first add a `range && ...` guard) — never call `pattern_alloc_param_slot(x, NULL)`. Mitigation: Step 3 mandates the sibling fn; code-review the call site.
- **Burst CPU spike.** A large `(int)cached_value` burst fires many `scheduler_trigger_grain` on one block. Mitigation: `PATTERN_EVENT_MAX_BURST` cap + the pool's own NULL-on-full guard (`grain.c:769-776`). Average CPU is unaffected (events are sparse); the worst case equals the existing grain-trigger loop's cost.
- **Shared 0..5 pool ceiling.** Event and param patterns share six slots; a sequencer-heavy patch could exhaust them. Flagged as GATE A.4 (vs a `PATTERN_SLOTS` bump). The error path is graceful ("no free pattern slots").
- **`changed` granularity vs dense tempo.** One fire per step *entry*. A very fast cycle (multiple steps per block) is handled by the phase wrap-while-loop (`:1685`), but `pattern_eval_slot` writes only the *final* step's cache per block — so intra-block intermediate steps would be skipped (only the last fires). This is the same tempo-vs-block-rate limit the continuous targets already document; sub-block step resolution is out of scope (would need a per-sample evaluator).
- **`trigger` keyword overlap.** The `pattern trigger` sub-keyword shares a name with the bare `trigger` transport selector (`:5462`); they do not collide at dispatch (different routing) but can confuse users. Mitigation: docs lead with the `event` alias (GATE A.3).
- **Euclid × other suffixes.** `(k,n)` and `*N` both expand a leaf; ambiguity rejected (GATE A.8). Bjorklund correctness must match the canonical Tidal output — AC5 pins specific tables.

**Out of scope (v1)**

- **`degrade` / `?` probabilistic steps** — needs a runtime per-cycle RNG read in `pattern_eval_slot` (not parse-time); split into a separate mini-plan.
- **`fast`/`slow` transforms** — redundant with `*N` and the cycle clock; deferred.
- **Per-event grain parameters** (an event pattern carrying its own size/pan/envelope) — large surface; v1 bursts use the live params. Possible later via the `pattern_event_arg[]` extension.
- **Polymeter / `{a b c}%n`** — a separate grammar feature; not in this plan.
- **A new `t_clock` / deferred-message queue** — explicitly forbidden; events ride the audio-thread idiom.
- **Sub-block (per-sample) step resolution** — out of scope; tempo-locked to the block-rate evaluator, like every other pattern target.
- **Touching the continuous-value read paths** (`RAND_TYPE_PATTERN` at `grain.c:296`, `PITCH_MODE_PATTERN` at `grain.c:837`, smear pitch) — the event target is a sibling *kind*, not a change to value reads; default VALUE preserves them exactly.