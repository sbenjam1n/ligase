# Plan: Modulation matrix + input-listening sources

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — new directions)
**Related:** The whole `param_range_t` modulation system (the per-destination "base" layer this overlays — `sample_param_range` at `grain.c:201`, the `get_param_range_by_name` registry at `ligase~.c:3962`, `perlin_state_t` generator state at `types.h:552`); the **pattern** subsystem (`pattern_eval_slot` at `grain.c:408`, the per-block cache discipline at `ligase~.c:1679-1697`) which is the **precedent** for an input-derived per-block source; the **modulation outlets** (`modout1-4` at `ligase~.c:1724-1742`) as a matrix destination; and the recording/monitor path (`ligase_process_grains`, `ligase~.c:1104+`) which is the **only** existing consumer of `in_left`/`in_right` today.

> **PROVENANCE NOTE (2026-06-24, re-verified by adversarial audit).** Every file:line reference below was re-read against the current tree (`src/grain.c`, `src/ligase~.c`, `src/types.h`) and independently re-confirmed line-by-line in an audit pass; `make clean && make` rebuilds warning-free. The codebase has evolved since the earlier grounding snapshot — the pitch-destination arc (P1/P2/P3) has landed, so `smear_pitch_control`, `pitch_fine_range`, `RAND_TYPE_PATTERN`, and the pattern cycle clock are all **in-tree now**. The registry `get_param_range_by_name` currently enumerates ~38 destinations (`ligase~.c:3963-4011`); the scheduler holds ~41 `param_range_t` members plus the 4 `modout*_range` on the object. Line numbers are current as of this read; treat them as anchors, re-confirm at edit time.

---

## Problem

Today modulation is strictly **1 source → 1 param**. Every modulatable parameter owns its own `param_range_t` (`types.h:383`), which carries min/max **plus exactly ONE source** (`rand_type` + `rand_instance`). `sample_param_range` (`grain.c:201`) reads that single source, maps it into `[min,max]`, applies invert/slew, and returns one float. The binding "source → destination" **is the struct field itself** — there is no routing layer. A source S can reach a param P only by setting `P.range.rand_type = S`; P then has that one source and nothing else, and S cannot simultaneously drive P and Q with independent depths.

**OWNER INTENT**, in concrete terms:

1. **Make modulation an N→M matrix.** Many sources → many destinations, **summed per destination**, with a **per-connection depth** (signed/bipolar). One LFO can drive cutoff *and* pan *and* grain-size, each at its own depth; cutoff can be driven by an LFO *and* the Lorenz attractor *and* an envelope follower, summed. This is a **separate sparse routing list**, additive **on top of** the existing per-destination `param_range` base value — not a replacement for it.

2. **Add input-LISTENING sources** so the instrument modulates itself from what it hears. Start with an **envelope follower** over the live input (`in_left`/`in_right`, available in `ligase_perform` at `ligase~.c:1606-1607` but today consumed **only** by recording + the dry/wet monitor mix — never by modulation). Later extensions: **onset** detection and **pitch** detection. The follower is computed **once per block** in `perform`, beside the existing pattern-slot eval pass (`ligase~.c:1679-1697`), cached as a float, and exposed as a new **matrix source** the routing layer reads.

3. **Additive over the existing `param_range` system.** Backward compat is load-bearing: with zero connections the matrix is inert and every destination behaves **bit-identically** to today. The `param_range` per-param path is **untouched**; the matrix is a thin overlay.

The aesthetic target (tape + granular + Tidal + chaos-modulation + playable FX, CV/signal-first, headless 0) wants self-modulation: feed the input envelope into grain density, cutoff, or pan so the instrument breathes with its input. The matrix makes that one `matrix_connect` message instead of a struct rewrite.

## Mechanics / target surface — the EXISTING code this extends

### The single-source sampler (the choke point — kept as the per-destination base)

- **`sample_param_range`** — `grain.c:201`. `enabled?` (`:203`) → `min==max?` (`:208`) → `switch(rand_type)` (`:225-307`) selects **one** generator (`RAND_TYPE_NONE` 0.5, `RAND`, `PERLIN_1D/2D`, `LORENZ`, `NBODY`, `SPHERE`, `SAW`, `SINE`, `SQUARE`, `PATTERN`) → `random_value` in `[0,1]` → invert (`:310`) → map to `[min,max]` (`:315`) → slew EMA (`:321-329`) → return one float. **One source in, one value out.** Every apply site funnels through this. The matrix does **not** modify it; the matrix value is **added after** this returns.
- **The `RAND_TYPE_PATTERN` case** — `grain.c:296-306`. Reads `perlin_state->pattern[slot].cached_value` (a per-block cache), bounds-checks `slot` and `step_count > 0` (`:302-304`), falls back to neutral 0.5 if unloaded/out-of-range. **This is the exact template for a new input-derived source** (read a cached float, defensive bounds, neutral fallback) and for a `mod_source_value()` helper.
- **`param_range_t`** — `types.h:383`. The 1-source-per-param binding: `min`, `max`, `rand_type` (one enum), `rand_instance` (0-3 for generators, or pattern slot for `RAND_TYPE_PATTERN`), `enabled`, `base_value`, `slew`, `smoothed_value`, `invert`, `saved_*`. **The matrix must NOT repurpose these fields** (see constraints).
- **`rand_type_t`** — `types.h:369-381`. The per-range source enum (`RAND_TYPE_NONE`..`RAND_TYPE_PATTERN`). The matrix source id is a **separate** enum (so the ~41 ranges + 4 modout + the `get_param_range_by_name` registry + the `RAND_TYPE_PATTERN` slot overload are all untouched).

### The two apply tiers (where the matrix sum is added)

- **Per-BLOCK apply — `ligase_update_inlets`** — defined `ligase~.c:396`, called from `ligase_perform` at `ligase~.c:1699`. For each effect/playback destination it samples the base then applies via a setter:
  - gdelay time base at `:802`, applied `grain_delay_set_time` `:807` (enabled-gated `:806`);
  - gdelay feedback/tone/mix `:813-854` (note: feedback/tone **route to stut** in `DELAY_MODE_STUT`, `:814`/`:832`);
  - moog cutoff base `:858`, `grain_moogladder_set_cutoff` `:862`; resonance `:866`; mix below;
  - the **smear stanza** under `if (x->smear)` (`:913-986`): `smear_frequency_range` (`:917-919`, already bypassed when `smear_pitch_control.enabled`), resonance/stages/feedback, then the smear-pitch note→Hz override;
  - plus scanrate/organize/sos/iot/env_skew elsewhere in the `~800-980` region.
  This is the block-rate apply tier. **Per-block matrix destinations are summed here, right after each base sample, before the setter.**
- **Per-GRAIN apply — `scheduler_trigger_grain`** — `grain.c:751`. After `update_perlin_coords` (`:788`) it samples grain-rate params fresh per grain spawn: speed base in the pitch switch (`:795`, then transposed `:802-882`), grain_size `:903`, distortion params `:911-958`, grainstart `:968`, amplitude `:976`, pan `:990`, saw cycles/depth below. **Per-grain matrix destinations are summed here, after each base sample.**
  **NOTE (verified, v1.5 design constraint):** `speed_range` is sampled **only** in `PITCH_MODE_OFF` (`grain.c:793-795`); in every other pitch mode `base_speed = speed` and the range is bypassed. So a v1.5 matrix→`speed` connection is **inert in non-OFF pitch modes** unless the v1.5 wiring adds the sum on the `base_speed` line for **all** modes (the cleaner choice — apply the matrix offset to `final_speed`/`base_speed` regardless of mode, then re-clamp). Also note `grain_size` (`:906-907`), `amplitude` (`:983-984`), and `pan` (`:997-998`) are clamped **in place right after the sample**, so the per-grain matrix sum must be added **before** those existing clamps (or the per-dest bounds table must reproduce them). Flag both for the v1.5 GATE.

### Input audio access (the new source reads this) — currently NOT touched by modulation

- **`in_left` / `in_right`** — extracted in `ligase_perform` at `ligase~.c:1606-1607` (`w[2]`/`w[3]`), validated `:1648-1650`, passed to `ligase_process_grains` at `:1704`. Inside `process_grains` they feed **only** recording (`:1117-1118`, `:1409-1410`, `:1421-1422`, `:1509-1510`) and the `constant_power_mix` monitor / SOS passthrough (`:1383`, `:1462`, `:1492-1493`). **No modulation code reads them today** — so an envelope follower over them is purely additive and touches no existing modulation path. (Audit confirmed: every `in_left`/`in_right` read in `ligase~.c` is one of these recording/monitor sites — none modulation.)

### The per-block cache precedent (the sanctioned perform-safe pattern)

- **The pattern-slot eval pass** — `ligase_perform` at `ligase~.c:1679-1697`. Once per block (guarded `bpm > 1.0 && cycle_total_sec > 0.0`, `:1679`) it advances each slot's phase and calls `pattern_eval_slot(ps, s)` (`:1689`), the **sole writer** of `pattern[slot].cached_value`/`cached_is_rest` (`grain.c:408`). `sample_param_range`'s `RAND_TYPE_PATTERN` case just **reads** that cache. **This is exactly the discipline the envelope follower mirrors:** a per-block writer into a cached float, read downstream, audio-thread-safe (no malloc, no locks, single writer). This pass runs **before** `ligase_update_inlets` (`:1699`) and `ligase_process_grains` (`:1704`), so any cache written here is fresh for both apply tiers this block.
- **`perlin_state_t`** — `types.h:552-600`. The per-scheduler modulation-source state container: `lorenz[4]`, `nbody[4]`, `sphere[4]`, `waveform_phase[4]`, plus `pattern[PATTERN_SLOTS]` + `pattern_phase[]` + `pattern_cycle_index[]` (`:597-599`). Zero-init by the scheduler `memset` (`grain.c:487`). **The envelope-follower state (one-pole coeff + last value per channel) and its cached output belong here**, alongside the pattern slots — so the existing memset zero-inits it and every sample site already has a `perlin_state*` in hand.

### The destination namespace (matrix reuses it verbatim)

- **`get_param_range_by_name`** — `ligase~.c:3962-4013`. The string→`param_range_t*` registry: `speed`, `scanrate`, `organize`, `sos`, `iot`, `maxgrains`, `grainsize`, `grainstart`, `env_skew`, `gdelay`/`gdelay_feed`/`gdelay_tone`/`gdelay_mix`, `distortion`, `amplitude`, `pan`, `moog_cutoff`/`moog_resonance`/`moog_mix`, the `dist_*` cluster, `stut_reps`, `bencina_*`, `smear_frequency`/`smear_resonance`/`smear_stages`/`smear_feedback`, `pitch_fine` (→ `pitch_control.pitch_fine_range`), `smear_pitch_fine`, and `modout1-4` (`:4007-4010`). **This is the canonical DESTINATION namespace** for matrix connection messages — the matrix reuses these exact identifiers with zero new wiring.
- **`modout1-4`** — `ligase~.c:1724-1742`. Per-block modulation OUT: each samples its `param_range` once per block (`:1725` etc.) and `outlet_float`s the result. They are themselves registry destinations (`:4007-4010`) — so a matrix that can target `modout*` lets input-derived sources be **patched out to the rest of the Pd graph**. **NOTE (verified):** the modout gate is **compound** — `if (modoutN_range.enabled && modoutN_range.rand_type != RAND_TYPE_NONE)` (`:1724`), not a bare `enabled`. A matrix-only connection to a modout whose range is disabled (or has `rand_type == RAND_TYPE_NONE`) would be skipped by **both** clauses, so targeting modout is **not** quite "free": the apply here must become `if ((enabled && rand_type != NONE) || matrix_dest_active(DEST_MODOUTn))` and, when only the matrix drives it, emit `outlet_float(0.5 + sum)` (or the range base if enabled) rather than gating on `rand_type`. This is the one site where the enabled-gate change (decision 4) is a compound rewrite, not a one-clause `||` — see GATE A decision 9.

### The message-handler surface (matrix-edit method lives here)

- **`ligase_param_range`** — `ligase~.c:4016`, registered `gensym("param_range")` at `ligase~.c:5588`. The existing param-message parsing style (A_GIMME, validate atoms, store fields on the control thread). The new `matrix_connect` / `matrix_clear` methods mirror this and register near here.
- **`ligase_pattern`** — defined `:3000`, registered `gensym("pattern")` at `ligase~.c:5506`. Another A_GIMME control-thread handler precedent.

## Design

The matrix is a **thin additive overlay**, four pieces, no restructuring of the perform flow, grain pool, or generators:

1. a **sparse connection list** on `scheduler_t` (fixed capacity, no audio-thread alloc);
2. a **per-block input-source cache** in `perlin_state_t` (envelope follower; later onset/pitch);
3. a **`mod_source_value()` + `matrix_sum_for_dest()` helper pair** called right after each existing base sample at the SAME apply sites;
4. **`matrix_connect` / `matrix_clear` message handlers** reusing the `get_param_range_by_name` destination vocabulary.

### Data structures (`src/types.h`)

**Matrix source enum** (new, separate from `rand_type_t` — added near `rand_type_t` at `types.h:369`):

```c
// Matrix source ids — a SEPARATE namespace from rand_type_t so the per-range source
// binding (rand_type/rand_instance) is untouched. Generator instances are enumerated
// explicitly so one connection picks one concrete source; input-derived sources follow.
typedef enum {
    MOD_SRC_NONE = 0,
    // --- generator instances (mirror perlin_state's 4-instance arrays) ---
    MOD_SRC_LFO1, MOD_SRC_LFO2, MOD_SRC_LFO3, MOD_SRC_LFO4,        // waveform_phase[0..3] (saw/sine/square per range cfg)
    MOD_SRC_PERLIN1, MOD_SRC_PERLIN2, MOD_SRC_PERLIN3, MOD_SRC_PERLIN4,
    MOD_SRC_LORENZ1, MOD_SRC_LORENZ2, MOD_SRC_LORENZ3, MOD_SRC_LORENZ4,
    MOD_SRC_NBODY1, MOD_SRC_NBODY2, MOD_SRC_NBODY3, MOD_SRC_NBODY4,
    MOD_SRC_SPHERE1, MOD_SRC_SPHERE2, MOD_SRC_SPHERE3, MOD_SRC_SPHERE4,
    MOD_SRC_RAND1, MOD_SRC_RAND2, MOD_SRC_RAND3, MOD_SRC_RAND4,
    // --- pattern slots (PATTERN_SLOTS == 8 in-tree, NOT 4) ---
    MOD_SRC_PATTERN0,  // ... PATTERN0 .. PATTERN(PATTERN_SLOTS-1) — see GATE A on whether to enumerate all
    // --- input-LISTENING sources (the new direction) ---
    MOD_SRC_ENV_L,     // envelope follower, left input  (v1)
    MOD_SRC_ENV_R,     // envelope follower, right input (v1)
    MOD_SRC_ENV_MONO,  // envelope follower, (L+R) mix   (v1)
    // MOD_SRC_ONSET,  // transient/onset trigger  (v2 — deferred)
    // MOD_SRC_PITCH,  // detected fundamental     (v2 — deferred)
    MOD_SRC_COUNT
} mod_source_t;
```

(The exact instance enumeration vs. an `(kind, instance)` pair is a GATE A decision; the `mod_source_value` helper hides it either way. NOTE: `PATTERN_SLOTS` is **8** — `types.h:465` — so a fully-enumerated pattern run is 0..7, not 0..3.)

**Connection struct + the sparse matrix on the scheduler** (added before the closing brace of `scheduler_t` at `types.h:693`, near `pitch_control`/`smear_pitch_control` at `:681-682`):

```c
#define MOD_MATRIX_MAX 32   // fixed capacity -> no audio-thread allocation (GATE A: size)

typedef struct {
    int   source;    // mod_source_t
    int   dest;      // dest id into the get_param_range_by_name namespace (parallel name->id table)
    float depth;     // signed/bipolar; contribution = depth * (source01 - center)  (see math below)
    int   enabled;   // 0 = inert (allows disable-without-remove)
} mod_conn_t;
```

…and on `scheduler_t`:

```c
    mod_conn_t mod_matrix[MOD_MATRIX_MAX];   // sparse N->M routing (additive overlay)
    int        mod_conn_count;               // 0 => matrix inert => exact backward compat
```

The scheduler `memset` (`grain.c:487`) zero-inits both → `mod_conn_count = 0` → matrix inert on construction → **bit-identical to today**. No explicit init needed (0 is the correct inactive value here, unlike pattern slots where 0 is valid). This mirrors the documented memset rationale at `grain.c:477-487` (zero-init guarantees `enabled = 0` for every range, "including any added in future").

**Envelope-follower state on `perlin_state_t`** (added after the pattern arrays at `types.h:599`, so the scheduler memset zero-inits it):

```c
    // Input envelope follower — per-block input-LISTENING source (mirrors the pattern cache
    // discipline: written once per block in perform, read by the matrix; single writer, no locks).
    float env_follow_state[2];   // one-pole rectified state per channel (L, R); leaky -> denormal-safe
    float env_follow_value[3];   // CACHED block output: [0]=L, [1]=R, [2]=mono mix; read by mod_source_value
    float env_follow_coeff;      // one-pole smoothing coeff (set from env_follow_ms at samplerate; default ~30ms)
```

`env_follow_coeff` needs a non-zero default (see init). The rest is zero-safe (silence → 0).

### The envelope follower (per-block input source — beside `pattern_eval_slot`)

Computed in `ligase_perform`, **after** the signal pointers are validated (`ligase~.c:1673`) and **after** the pattern-slot eval pass (`:1697`), **before** `ligase_update_inlets` (`:1699`) — so the cached value is fresh for both apply tiers this block. It needs `in_left`/`in_right` (already in scope at `:1606-1607`) and `n`/`x->sample_rate`:

```c
    // --- Input envelope follower (input-LISTENING source for the modulation matrix) -------------
    // One-pole rectified-peak envelope over the block. Single writer (perform), no malloc/locks.
    // Mirrors pattern_eval_slot's per-block cache discipline. Output cached in [0,1]-ish (input is
    // typically <=1.0; the matrix clamps the destination anyway, so no hard cap needed here).
    if (x->scheduler) {
        perlin_state_t *ps = &x->scheduler->perlin_state;
        float c = ps->env_follow_coeff;            // 0 => one-pole degenerates to instant follow
        float sl = ps->env_follow_state[0], sr = ps->env_follow_state[1];
        for (int i = 0; i < n; i++) {
            float al = fabsf(in_left[i]);          // rectify
            float ar = fabsf(in_right[i]);
            // leaky one-pole peak follower: rise fast (toward peak), fall at coeff
            sl = (al > sl) ? al : (sl * c + al * (1.0f - c));
            sr = (ar > sr) ? ar : (sr * c + ar * (1.0f - c));
        }
        ps->env_follow_state[0] = sl;
        ps->env_follow_state[1] = sr;
        ps->env_follow_value[0] = sl;
        ps->env_follow_value[1] = sr;
        ps->env_follow_value[2] = 0.5f * (sl + sr);  // mono mix
    }
    // LIGASE_FLUSH_DENORMALS() at :1585 already covers the leaky state; the loop is O(n), once/block.
```

This reads the input buffer once per block, updates two one-pole states + three cached floats — **no malloc, no parse, no gensym, single writer.** (Audit confirmed `LIGASE_FLUSH_DENORMALS` at `:1585` is a process-wide FTZ/DAZ CPU-mode set — `ligase~.c:18-21` — so the leaky state is genuinely denormal-safe with no per-sample work.) The follower's attack/release shape and whether it is peak vs RMS is a GATE A decision (peak is shown; RMS swaps the `(al>sl)?` for an accumulate-and-sqrt). A `env_follow_ms <ms>` message sets `env_follow_coeff = expf(-1.0f / (ms * 0.001f * sr))`.

### The source-value helper (read ANY source as [0,1])

A small helper, modeled on the `RAND_TYPE_PATTERN` read at `grain.c:296-306`, returns a normalized value for any `mod_source_t`. It is the single point that maps a matrix source id to a number, reused by `matrix_sum_for_dest` (and optionally by `sample_param_range` if a range ever wants the follower as its one source):

```c
// Returns the source's current value in [0,1]. Out-of-range / unloaded -> neutral 0.5 (defensive,
// mirroring grain.c:217 instance clamp and grain.c:302 pattern bounds). Generators reuse the SAME
// readouts sample_param_range uses (lorenz_get_normalized, nbody_get_normalized, waveform_phase, ...).
static inline float mod_source_value(perlin_state_t *ps, int source) {
    switch (source) {
        case MOD_SRC_ENV_L:    return ps->env_follow_value[0];
        case MOD_SRC_ENV_R:    return ps->env_follow_value[1];
        case MOD_SRC_ENV_MONO: return ps->env_follow_value[2];
        case MOD_SRC_LORENZ1: return lorenz_get_normalized(&ps->lorenz[0], 0);
        // ... generator instances mirror the sample_param_range switch (grain.c:252-294) ...
        case MOD_SRC_PATTERN0: return (ps->pattern[0].step_count > 0) ? ps->pattern[0].cached_value : 0.5f;
        // ...
        default: return 0.5f;   // MOD_SRC_NONE / unknown -> neutral
    }
}
```

(Verified: `lorenz_get_normalized`/`nbody_get_normalized`/`sphere_get_normalized` exist — `perlin.h:55/78`, `sphere.h:153` — and `pattern_table_t` carries `step_count`/`cached_value`/`cached_is_rest` — `types.h:480/486/487` — so the `step_count > 0 ? cached_value : 0.5f` guard exactly matches the live `RAND_TYPE_PATTERN` case.)

### The sum + clamp (the additive overlay)

```c
// Sum all enabled connections targeting `dest`. Returns a signed OFFSET to add to the base value.
// Center the [0,1] source at 0.5 so a bipolar depth pushes both directions symmetrically; depth
// is in the DESTINATION's own units (the connect message takes depth in dest units -- see API).
static inline float matrix_sum_for_dest(scheduler_t *sched, int dest) {
    float sum = 0.0f;
    for (int i = 0; i < sched->mod_conn_count; i++) {
        mod_conn_t *c = &sched->mod_matrix[i];
        if (!c->enabled || c->dest != dest) continue;
        float s01 = mod_source_value(&sched->perlin_state, c->source);  // [0,1]
        sum += c->depth * (s01 - 0.5f) * 2.0f;   // (s-0.5)*2 -> [-1,1]; depth scales to dest units
    }
    return sum;   // dest with no connections -> 0 -> identical output (backward compat)
}
```

**At each apply site**, after the existing base sample and **before** the setter, add the sum then **clamp to the destination's musical range**:

```c
    // PER-BLOCK example (gdelay time, ligase~.c:802-809):
    float gdelay = sampled_gdelay_time + matrix_sum_for_dest(x->scheduler, DEST_GDELAY);
    gdelay = CLAMP(gdelay, GDELAY_MIN, GDELAY_MAX);     // matrix layer owns the clamp (see constraints)
    if (x->scheduler->gdelay_range.enabled || matrix_dest_active(x->scheduler, DEST_GDELAY))
        grain_delay_set_time(x->grain_delay, gdelay);
```

Three subtleties:
- **Enabled gating.** Today many setters are gated on `range.enabled` (e.g. `:806`). A destination with **only** matrix connections (range disabled) must still apply — so the gate becomes `range.enabled || matrix_dest_active(dest)`. `matrix_dest_active` is a cheap "any enabled connection targets this dest" scan (or a precomputed per-dest bitmask refreshed on `matrix_connect`).
- **The `modout1-4` gate is COMPOUND, not a bare `enabled`** (`:1724`: `enabled && rand_type != RAND_TYPE_NONE`). For a matrix-only connection to a disabled modout to fire, the rewrite is `if ((enabled && rand_type != NONE) || matrix_dest_active(DEST_MODOUTn))`, and when only the matrix drives it emit `outlet_float(0.5 + sum)` rather than calling `sample_param_range` (whose disabled path returns the base and whose `rand_type==NONE` path returns 0.5 anyway). This is the **only** per-block site needing a compound rewrite — call it out in decision 9.
- **Per-grain sites** (`scheduler_trigger_grain`, `grain.c:795-990`) read the **same cached follower value** for all grains spawned in the block (the follower is computed once per block in perform, which runs before grains spawn this block). This is **documented** (grains within a block share the block's envelope) — no per-sample follower for grain-rate destinations. Also: `speed_range` is sampled only in `PITCH_MODE_OFF`, and `grain_size`/`amplitude`/`pan` clamp in place right after their sample — both are v1.5 wiring constraints (see per-grain Mechanics note).

### Destination id table (parallel to the name registry)

The matrix needs an integer `dest` id, not a `param_range_t*`. Add a parallel `name → dest_id` resolution that mirrors `get_param_range_by_name` (`ligase~.c:3962`) one-to-one, plus a `dest_id → clamp bounds` table (each destination's musical `[lo,hi]`, sourced from where the engine already clamps ad hoc — e.g. moog cutoff, smear `[20, 0.45·sr]`, pan `[0,1]`, amplitude `[0,2]`). Keep the two tables adjacent so they never drift. The connect handler resolves `dest_name` once (control thread) and stores the integer `dest` in the connection; the audio thread only reads integers.

### Message API (`src/ligase~.c`, near `ligase_param_range` at `:4016`, registered near `:5588`)

All control-thread, field-store only; the audio thread only reads.

- **`matrix_connect <source_name> <dest_name> <depth>`** → resolves `source_name` to a `mod_source_t` and `dest_name` through the registry namespace to a `dest` id; appends/updates a `mod_conn_t` (if `MOD_MATRIX_MAX` reached, `pd_error` and drop). Depth is signed, in destination units. **Ordering:** write `source`/`dest`/`depth`/`enabled` fields **first**, then `mod_conn_count++` **last** as the publish barrier (so the audio thread never iterates a half-written entry). This mirrors the in-tree `pattern_table_t.step_count`-set-last publish barrier (`types.h:480`: "0 => slot inactive (publish barrier; set LAST on commit)"). A re-connect of the same `(source,dest)` updates depth in place.
- **`matrix_disconnect <source_name> <dest_name>`** → sets that connection's `enabled = 0` (or compacts the list).
- **`matrix_clear`** → `mod_conn_count = 0` (single store; instantly inert).
- **`matrix_dump`** → posts the current connections (state/query aid).
- **`env_follow_ms <ms>`** → sets `env_follow_coeff`; **`env_follow_mode <peak|rms>`** (GATE A).

Registration (next to `param_range` at `:5588`):

```c
    class_addmethod(ligase_class, (t_method)ligase_matrix_connect,    gensym("matrix_connect"),    A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_matrix_disconnect, gensym("matrix_disconnect"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_matrix_clear,      gensym("matrix_clear"),      A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_matrix_dump,       gensym("matrix_dump"),       A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_env_follow_ms,     gensym("env_follow_ms"),     A_DEFFLOAT, 0);
```

### How it composes with the engine (summary)

`base = sample_param_range(&dest_range, ...)` (the per-destination native source, unchanged) **+** `matrix_sum_for_dest(dest)` (the additive overlay) **→ clamp →** setter. With `mod_conn_count == 0` the overlay is 0 and the path is byte-for-byte today. The follower is a new source feeding only the overlay; it never alters an existing `param_range`. `param_range`'s invert/slew/map still apply to the **base**; the matrix depth is the bipolar overlay scale. There is **no** rewrite of `sample_param_range`, the grain pool, the perform flow, or the generator instances.

### Staging (v1 core vs. later)

- **v1 (this plan's build target):** the sparse matrix struct + memset-inert default; the envelope follower (`MOD_SRC_ENV_L/R/MONO`) computed per block; `mod_source_value`/`matrix_sum_for_dest`/`matrix_dest_active`; the dest-id + clamp tables; the per-block apply at the effect/playback sites; the message API; `env_follow_ms`. Generator sources in the matrix are **free** (the helper already wraps them) — include them in v1.
- **v1.5 (cheap follow-on):** wire the **per-grain** apply sites (speed/pitch_fine/grainsize/grainstart/amplitude/pan) so grain-rate destinations also read the matrix (using the block-cached follower). Two verified per-grain subtleties to honor: (a) `speed_range` is sampled only in `PITCH_MODE_OFF` (`grain.c:793-795`) — apply the matrix `speed` offset to `base_speed`/`final_speed` in **all** modes, not just OFF, or it is silently inert under any active pitch mode; (b) `grain_size`/`amplitude`/`pan` clamp **in place** right after their sample (`:906-907`/`:983-984`/`:997-998`) — add the matrix sum **before** those clamps.
- **v2 (deferred extensions):** `MOD_SRC_ONSET` (transient detector — derivative/threshold over the follower, emits a one-block trigger) and `MOD_SRC_PITCH` (fundamental estimate — autocorrelation/YIN, heavier CPU). These add source cases to `mod_source_value` + state to `perlin_state` and need their own GATE A.

## Steps & gates

### GATE A (approval) — open design decisions for owner sign-off

This is a direction plan, so GATE A is substantial. Recommendations marked **[R]**.

1. **Matrix capacity & shape.** `MOD_MATRIX_MAX` fixed size. **[R] 32** (covers generous patches; ~`32 * sizeof(mod_conn_t)` ≈ 512 B/instance, trivial). Confirm vs 16 (leaner) or 64. Also: `mod_conn_t` as `{source, dest, depth, enabled}` (flat int ids) **[R]** vs an `(source_kind, source_instance)` pair. Flat is simpler; the helper hides it.

2. **Depth semantics / units.** **[R]** depth is **signed (bipolar)**, in the **destination's own units**, applied as `depth * (source01 - 0.5) * 2` so a unipolar source swings ±depth around the base. Confirm vs (a) depth in normalized `[0,1]` of the dest range (needs each dest's span), (b) **unipolar** offset `depth * source01` (no centering — simpler, but a quiet LFO never subtracts). Centering is the musically standard choice for bipolar mod.

3. **Source-centering / per-source polarity.** Should each *source* declare whether it is unipolar (envelope: 0→up only) or bipolar (LFO/chaos: centered)? **[R]** treat **all** sources as `[0,1]` and center uniformly at the connection (decision 2); a follower that should only push **up** is expressed with a positive depth and the user accepting the −0.5 floor, OR add a per-connection `unipolar` flag. Confirm whether a per-connection unipolar/bipolar flag is wanted in v1 (the envelope is the natural unipolar case).

4. **Enabled gating change.** A destination with **only** matrix connections (its `param_range` disabled) must still apply. **[R]** change each gated setter from `if (range.enabled)` to `if (range.enabled || matrix_dest_active(dest))`. Confirm this is acceptable at **every** per-block site (it slightly changes the "disabled range = setter not called" invariant — but only when a matrix connection targets that dest, which is new behavior the user opted into). **Note the modout sites are the one exception** — their gate is compound (`enabled && rand_type != NONE`), handled in decision 9.

5. **Follower detector type & default time.** **[R]** **peak** follower (rectify + leaky one-pole, fast attack), default release **~30 ms**, `env_follow_ms` to tune. Confirm vs **RMS** (smoother, needs a sqrt) or a separate attack/release pair (`env_follow_attack_ms`/`env_follow_release_ms`). Also: is the **mono mix** `0.5*(L+R)` the right default source, or per-channel only?

6. **Follower output scaling / headroom.** Input is typically ≤1.0 but can exceed it (gain). **[R]** do **not** hard-cap the follower output (the destination clamp catches overshoot); document that hot input + high depth saturates the dest at its clamp. Confirm vs normalizing/soft-clipping the follower to `[0,1]`.

7. **Clamp ownership.** The matrix sum can overshoot a destination's musical range. **[R]** the **matrix apply site clamps** to the dest's `[lo,hi]` before the setter (a new per-dest bounds table), because the existing setters do **not** all clamp internally (some do ad hoc — moog, smear `smear_update_coeffs`, sos). Confirm the per-dest bounds table is the right home (vs. pushing clamps into each setter — larger surface, rejected). Cross-check each bound against the **in-place** clamps already present at the apply sites (`grain_size` :906-907 [0.01, 2.0], `amplitude` :983-984 [0, 2.0], `pan` :997-998 [0, 1.0], `speed` :895-901 floor −4.0) so the table reproduces them rather than fighting them.

8. **v1 scope: per-block only, or per-block + per-grain?** **[R]** **v1 = per-block destinations** (effect/playback: gdelay/moog/smear/scanrate/organize/sos/iot/env_skew/modout); **v1.5 = per-grain** (speed/pitch_fine/grainsize/grainstart/amplitude/pan). Splitting de-risks the first build (one apply tier) and the per-grain tier is mechanically identical **except** the two verified subtleties (speed only modulated in PITCH_MODE_OFF; in-place clamps on grain_size/amplitude/pan). Confirm vs all destinations in v1.

9. **modout as matrix dest.** **[R]** allow the matrix to target `modout1-4` (they are registry destinations already) so input-derived sources patch out to the Pd graph. **Caveat (verified):** the modout apply gate is **compound** — `enabled && rand_type != RAND_TYPE_NONE` (`:1724`), so a matrix-only connection to a disabled modout is skipped by **both** clauses. Targeting modout therefore requires a small **compound** rewrite at `:1724-1742` (`if ((enabled && rand_type != NONE) || matrix_dest_active(...))`, and when only the matrix drives it emit `outlet_float(0.5 + sum)` rather than calling `sample_param_range`), not the one-clause `||` the other per-block sites get. Still cheap, but call it out so the four modout sites aren't treated as "free." Confirm modout-as-dest with this compound-gate handling.

10. **Source enumeration vs. reuse of `rand_instance`.** Confirm the matrix source enum is **fully separate** from `rand_type_t`/`rand_instance` (decision is load-bearing for backward compat — see constraints). **[R] separate** (non-negotiable for compat, raised here only for the record).

11. **v2 extensions (onset, pitch) — scope now or later?** **[R]** **defer to v2**, separate GATE A (onset is cheap; pitch detection is meaningfully more CPU and needs algorithm choice — YIN/autocorrelation/zero-cross). Confirm the v1/v2 split.

### Step 1 → GATE B (types + inert default, no behavior)

Add `mod_source_t`, `mod_conn_t`, `MOD_MATRIX_MAX`, the `mod_matrix[]`/`mod_conn_count` members on `scheduler_t` (`types.h` near `:693`), and the `env_follow_*` members on `perlin_state_t` (`types.h:599`). Set `env_follow_coeff` default in `scheduler_create` (after the memset at `grain.c:487`); everything else is zero-safe. **GATE:** `make clean && make` warning-free; a fresh object has `mod_conn_count == 0`; `query`/state path unchanged ⇒ identical behavior.

### Step 2 → GATE C (envelope follower)

Add the per-block follower in `ligase_perform` after the pattern eval pass (`ligase~.c:1697`), before `ligase_update_inlets` (`:1699`). Add `env_follow_ms`. **GATE:** `make clean && make` warning-free; with no connections the follower writes the cache but nothing reads it ⇒ audio output unchanged; a headless harness reading `env_follow_value` via a debug post shows it tracks an input envelope (rises on signal, decays on silence) and is denormal-clean after sustained silence.

### Step 3 → GATE D (helpers + per-block apply)

Add `mod_source_value`, `matrix_sum_for_dest`, `matrix_dest_active`, the dest-id + clamp tables. Wire the per-block apply at the effect/playback sites in `ligase_update_inlets` (`~:800-986`) and `modout1-4` (`:1724-1742`, with the **compound**-gate handling from decision 9): `base += matrix_sum_for_dest(dest)` → clamp → setter, with the enabled-gate change (decision 4). **GATE:** `make clean && make` warning-free; with `mod_conn_count == 0` every per-block destination is **byte-for-byte today**; a single `matrix_connect lfo1 moog_cutoff <d>` modulates cutoff at depth d, summed on top of any existing `moog_cutoff_range`.

### Step 4 → GATE E (message API)

Add `ligase_matrix_connect`/`_disconnect`/`_clear`/`_dump`; register near `:5588`. Validate source/dest names (unknown → `pd_error`, no state change); enforce `MOD_MATRIX_MAX`; publish-count-last ordering. **GATE:** `make clean && make` warning-free; `matrix_connect`/`disconnect`/`clear`/`dump` reach the handlers and mutate the list correctly; malformed names are rejected without touching the matrix; `matrix_clear` returns to backward-compat behavior.

### Step 5 → GATE F (verify, headless)

Build; run acceptance patches (below) under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1`), recording output via `writesf~` and reading back the modulated parameter / the follower trace. Confirm all acceptance criteria; confirm no regression in the `param_range` path or the recording/monitor path. Update the manual with the `matrix_connect`/`env_follow_ms` surface, the additive-over-`param_range` model, the per-block follower cadence, and the clamp-owns-bounds note.

### (Later) v1.5 → per-grain apply; v2 → onset/pitch sources

v1.5 wires the per-grain sites (`grain.c:795-990`) identically (own GATE) **plus** the two verified constraints: apply the `speed` offset to `base_speed`/`final_speed` in **all** pitch modes (not just `PITCH_MODE_OFF`, `grain.c:793-795`), and add the `grain_size`/`amplitude`/`pan` sums **before** their in-place clamps (`:906-907`/`:983-984`/`:997-998`). v2 adds `MOD_SRC_ONSET`/`MOD_SRC_PITCH` (own GATE A for detector algorithm + CPU).

## Acceptance criteria

All headless via `pd -nogui -nosound -stderr -path . <patch>.pd` where possible; the two subjective items are flagged for an ear-test.

1. **Backward compat (inert matrix).** A patch that sends **no** `matrix_*` message is byte-for-byte identical to today: record noise, granulate, run `moog_cutoff_range`/`pan_range`/`speed_range` etc. — captured WAV and all `query` values match the pre-change build. `make clean && make` warning-free. **Verify:** diff captured WAVs / `query` dumps against baseline.

2. **Single connection, additive on top of base.** With `moog_cutoff_range` **disabled**, `matrix_connect lfo1 moog_cutoff 2000` makes the cutoff swing ±2000 Hz around its base at the LFO rate (enabled-gate change applies the setter even with the range off). With `moog_cutoff_range` **enabled** too, the matrix swing is summed **on top of** the range's base. **Verify:** FFT/spectral-centroid trace of the captured output shows the expected periodic sweep; the base+overlay case shows both contributions.

3. **N→M: one source → many dests, many sources → one dest.** `matrix_connect lfo1 moog_cutoff d1` + `matrix_connect lfo1 pan d2` drives cutoff **and** pan from one LFO at independent depths. `matrix_connect lfo1 moog_cutoff d1` + `matrix_connect lorenz1 moog_cutoff d3` sums two sources into cutoff. **Verify:** both modulated parameters' traces move; the two-source-into-cutoff trace is the **sum** of the individual contributions (within float tolerance).

4. **Input envelope follower drives a destination.** `matrix_connect env_mono pan 1.0` (or `grainsize`): feed an input with a clear amplitude envelope (e.g. a gated burst); the chosen parameter tracks the **input envelope** (rises with input level, decays with the follower's release). `env_follow_ms 5` vs `env_follow_ms 200` changes the decay slope. **Verify:** the parameter trace correlates with the input envelope; the two `env_follow_ms` settings show measurably different decay times. **(Partly an ear-test for the musical feel of self-modulation.)**

5. **Clamp / overshoot safety.** Set a large depth so `base + sum` overshoots a destination's range (e.g. `matrix_connect lfo1 moog_cutoff 50000`): the setter receives a value **clamped** to the destination bounds — no NaN, no instability, no out-of-range read. A hot input + high follower depth saturates at the clamp gracefully. **Verify:** captured output stable; no NaN in the WAV; trace pins at the clamp bound.

6. **Defensive ids.** `matrix_connect bogus_src moog_cutoff 1` and `matrix_connect lfo1 bogus_dest 1` are rejected with `pd_error` and leave `mod_conn_count` unchanged. A connection whose source/dest somehow holds an out-of-range id (corrupted state) reads the neutral `0.5` fallback in `mod_source_value` and is skipped by dest mismatch — **no out-of-bounds read on the audio thread**. **Verify:** `matrix_dump` shows the matrix unchanged after the bad messages.

7. **Follower is audio-thread-safe + denormal-clean.** With a connection active and input then silenced, the follower state decays to ~0 without denormal CPU climb (LIGASE_FLUSH_DENORMALS at `:1585` covers it process-wide). No malloc/locks on the audio thread (code review + the single-writer cache discipline). **Verify:** sustained-silence CPU stays flat; the follower trace reaches ~0.

8. **No regression in recording/monitor.** The recording path and `constant_power_mix` monitor (the existing `in_left`/`in_right` consumers, `:1117+`/`:1383+`) are unchanged — recording a splice and the dry/wet monitor behave exactly as before (the follower only **reads** the input, never mutates it). **Verify:** record-then-play a splice; compare against baseline.

9. **modout-as-dest (compound gate).** With every `modout1_range` **disabled**, `matrix_connect env_mono modout1 1.0` still emits a per-block float on the modout1 outlet that tracks the input envelope (verifies the compound-gate rewrite from decision 9). With `modout1_range` enabled and a `rand_type` set, the matrix sum is **added on top of** the sampled range value. **Verify:** capture the outlet floats (e.g. via `print`/`writesf~` of a `sig~`-fed value) and confirm both the matrix-only and base+overlay cases.

## Risks / out-of-scope

**Risks**

- **Unbounded sum overshoot.** N contributions into one dest can exceed its musical range. Mitigated by the **mandatory clamp at the apply site** (decision 7 / AC5) using a per-dest bounds table. The existing setters do **not** all clamp internally, so the matrix layer must own this — getting the bounds table wrong (or omitting a dest) is the dominant correctness risk. Cross-check each bound against where the engine already clamps ad hoc (moog, smear `smear_update_coeffs`, sos, pan/amplitude/grain_size in-place at the apply sites).
- **Enabled-gate semantics shift (incl. the compound modout gate).** Changing `if (range.enabled)` → `if (range.enabled || matrix_dest_active)` means a destination can now be driven with its `param_range` disabled. This is intended (decision 4) but is a behavior change at each site — must be applied **consistently** or some dests will silently ignore matrix connections. The `modout1-4` gate is **compound** (`enabled && rand_type != NONE`, `:1724`) and needs the special handling in decision 9, not the one-clause `||`. Audit every per-block site.
- **Half-written connection race.** `matrix_connect` writes on the control thread while perform reads. Mitigated by **fields-first, count-last** publish ordering on a fixed array (no realloc, no pointer churn) — the same barrier as `pattern_table_t.step_count` (`types.h:480`); the worst case is a one-block benign read of a stale-but-valid entry. `matrix_clear` is a single store.
- **Per-grain follower granularity + pitch-mode/clamp coupling.** Grain-rate destinations (v1.5) read the **block-cached** follower for all grains in the block — not a per-sample envelope (documented, acceptable). Additionally `speed` is range-modulated only in `PITCH_MODE_OFF` and grain_size/amplitude/pan clamp in place — both must be handled in the v1.5 wiring (see staging note), or matrix→speed is silently inert under active pitch modes.
- **Source enumeration drift.** `mod_source_value` must mirror the generator readouts `sample_param_range` uses (`grain.c:252-294`) and the dest-id table must mirror `get_param_range_by_name` (`ligase~.c:3963-4011`). Two parallel tables can drift — keep them adjacent and add a build-time count check.
- **CPU.** The follower is O(n) once per block (a few flops/sample) — negligible. `matrix_sum_for_dest` is O(connections) per destination per block; with `MOD_MATRIX_MAX=32` and ~20 per-block dests that is ≤640 int-compares + a handful of float MACs per block — trivial. The per-grain tier (v1.5) adds O(connections) per grain spawn for ~6 dests — bounded by grain density, still small. **v2 pitch detection is the only real CPU concern** (autocorrelation/YIN over a block-history buffer) and is explicitly deferred with its own GATE.

**Out of scope**

- **Onset (`MOD_SRC_ONSET`) and pitch (`MOD_SRC_PITCH`) sources** — v2, separate GATE A (detector algorithm + CPU + state). v1 ships the envelope follower only.
- **Per-sample / audio-rate modulation.** The matrix is control-rate (per block, per grain) — the same cadence as the existing `param_range` apply. No per-sample matrix.
- **Repurposing `rand_type`/`rand_instance` for routing** — strictly forbidden (breaks the per-range source binding, the registry, state dump, and the `RAND_TYPE_PATTERN` slot overload). The matrix is a separate structure.
- **Rewriting `sample_param_range` or the `param_range_t`-per-param design** — the matrix is an overlay, not a replacement.
- **Restructuring the perform flow, grain pool, or generator instances** — untouched.
- **A GUI/matrix editor** — the interface is messages (`matrix_connect`/`dump`); any visual editor is downstream and out of scope.
- **Modulating the matrix itself** (a connection's depth as a destination) — not in v1; depths are control-thread-set constants.