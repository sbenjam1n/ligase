# Plan: Morph / Metasurface — snapshot interpolation + coordinate routes

**Owner:** SLB
**Date:** 2026-06-24
**Status:** IN PROGRESS — Step 1/GATE B done (module `src/morph.{c,h}` + types + alloc/free, warning-free, no behavior). Next: Step 2/GATE C (capture + recall). GATE-A decisions taken = the `[R]` recommendations (IDW v1 kernel behind a kernel-agnostic API; float `morph_x`/`morph_y` now, signal inlets v1.x; argmax discretes; shadow FX capture; stepper before `update_inlets`; full-param capture; caps 64).
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — new directions). (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related:** the **modulation** subsystem (`param_range_t` + `sample_param_range`, `src/grain.c:201`; the `get_param_range_by_name` dispatch, `src/ligase~.c:3962`); the **state read/write** API (`ligase_get_state`/`get_params`/`get_ranges`/`get_generators`, `src/ligase~.c:5216-5219`; the ~160 `class_addmethod` setters registered in `ligase_tilde_setup`, `src/ligase~.c:5448`); the **pattern** subsystem (the per-block cycle clock at `src/ligase~.c:1679-1697`, which this plan's morph clock is modelled on); the **pitch** destinations (`pitch_control_t` / `smear_pitch_control_t`, `src/types.h:417-462`) and **effects** (smear/Bencina/distortion/Moog) as snapshot targets. This is the first **control-rate automation/morph layer** plan; it sits ON TOP OF the existing param set rather than inside any one subsystem.

> **GROUNDING NOTE (2026-06-24, adversarially re-verified).** All file:line refs below were re-read in `src/ligase~.c`, `src/types.h`, `src/grain.c`, `src/grain_smear.c`, the four FX headers, and the `Makefile` this session and confirmed against the LIVE tree. The codebase has advanced past the original capture: `PITCH_MODE_PATTERN` (`types.h:407`), the smear-pitch destination (`smear_pitch_control_t`, `types.h:417-456`; `smear_pitch_control` member `types.h:682`), `pitch_fine`/`smear_pitch_fine` ranges, and the per-block pattern cycle clock (`ligase~.c:1679-1697`) ALL already exist. There is still **no** snapshot/morph/metasurface/preset/interpolation-of-presets code anywhere in `src/` (verified by grep — `snapshot|metasurface|morph_|idw|shepard|sibson` ⇒ 0 hits; the only `morph` token besides *Morphagene* is a `WAVESHAPER_MODE_BLEND` comment at `types.h:161`). This plan builds that layer from scratch on the existing read (`get_state`) + write (setters) APIs.

---

## Problem

The owner wants a **2-to-many morph surface** modelled on Ross Bencina's **Metasurface** (AudioMulch, NIME 2005): capture full-patch **snapshots**, drop each as a point at an `(x,y)` location on a 2D plane, then move a **cursor** around the plane; at any cursor position every (included) parameter is set to a spatially-weighted blend of the surrounding snapshots. On top of Bencina's bare X/Y cursor, the owner additionally wants **routes** — a series of waypoints, each with its own transition **rate** and **curve**, that the cursor traverses over time (an automation path the engine plays back). In the owner's framing:

> Place snapshots as points on a 2D surface; a cursor position interpolates between them (distance-weighted). Capture a snapshot (`snapshot <id>`), place it at a coordinate (`morph_point <id> <x> <y>`), move the cursor (`morph <x> <y>` → interpolate), AND create routes across the surface as a series of waypoint messages describing transition RATE, x/y coordinate, and CURVE (`morph_route <x> <y> <rate> <curve>` appended into a path the cursor traverses over time). Snapshots may be by-id OR raw coordinates + interpolation params. Per-param interpolation in the param's natural space; discrete params threshold/step. This is a control-rate automation/morph layer over the existing param set.

Decomposed into what this plan delivers:

1. **Full-patch snapshots.** Capture the whole modulatable + scalar + discrete patch state into a `morph_snapshot_t` buffer, keyed to the existing param namespace. A snapshot stores, for every modulatable param, BOTH the scalar base AND the modulation `{min, max, enabled, …}` — because `sample_param_range` (`grain.c:201`) reads base when disabled and `[min,max]` live when enabled, a morph must interpolate the **band**, not freeze it.
2. **Snapshots as 2D points.** Place each snapshot at an `(x,y)` on a normalized `[0,1]²` surface (`morph_point`/`morph_place`). Arbitrary count, freely repositioned. No per-point radius/gravity — placement is the only spatial parameter (faithful to Bencina).
3. **Cursor → spatial blend.** A live cursor (`morph <x> <y>`, the analogue of Bencina's `Interpolate_X`/`Interpolate_Y`) computes a **distance-weighted** blend of the surrounding snapshots and re-emits every included param through its existing setter. The weighting kernel is **pluggable** behind `morph_interp` — v1 ships Shepard/IDW (a distance loop, cheap, no mesh); true Natural-Neighbour (Sibson stolen-area) is a later extension because it needs a maintained 2D Delaunay/Voronoi mesh.
4. **Routes.** `morph_route <x> <y> <rate> <curve>` appends a waypoint to a path; `morph_run` plays the path back, driving the cursor leg-by-leg at each leg's rate with each leg's curve shape. This is the deliberate **value-add over AudioMulch** (the NIME paper lists "automated interpolation trajectories" and "variable damping of interpolation velocity" as explicit FUTURE work — there is no reference implementation to copy, only the conceptual model).
5. **Per-param natural-space interpolation; discrete params step.** Continuous params lerp inside their own clamp range (Hz in Hz-space, semitones in semitone-space — never through one shared 0..1 axis). Discrete/enum params (envelope type, `*_mode`, `*_source`, rand_type/instance, MIDI channels, scale lists) **cannot** be linearly blended: they snap to the nearest-weighted snapshot's value (threshold at the dominant weight), and mode switches that re-route the signal path re-emit their dependent scalars after the switch.
6. **Parameter selection.** `morph_include`/`morph_exclude` (the Parameter Selection Tree analogue) choose which params the morph recalls; excluded params are left to other control (manual messages, modulation, inlets/CV).

**CV-driven invariant (project memory).** Per the ligase~ hardware invariant, the live cursor must be **signal/CV-drivable**, not message-only — `morph_x`/`morph_y` as a pair (or a 2-channel `morph~` mode) must be readable from signal inlets so a physical XY joystick/CV can drive the surface. v1 ships the message API + a **planned signal path** (GATE A decides whether the signal inlets land in v1 or v1.1); the control model is identical either way.

## Mechanics / target surface — the EXISTING code this extends

**Provenance:** every ref verified by reading the live `src/*.c`/`src/*.h` on 2026-06-24.

### The read half (capture primitives)

- **`sample_param_range`** — `src/grain.c:201`. THE crux of morph semantics. Returns `base_value` when `range->enabled == 0` (`:203-205`), else maps a generator output into `[range->min, range->max]` every grain/block. ⟹ a snapshot must store **both** the scalar base AND `min`/`max`/`enabled` for every modulatable param; a morph interpolates all three **in place**, and the LFO/chaos/pattern generators keep sampling the interpolated band — the morph **widens/shifts the modulation window, it never freezes it.**
- **`get_param_range_by_name`** — `src/ligase~.c:3962-4013`. Name→`param_range_t*` dispatch for the modulatable params. The CANONICAL enumeration to drive a capture/restore loop. Live list (verified, exactly 41 entries): `speed, scanrate, organize, sos, iot, maxgrains, grainsize, grainstart, env_skew, gdelay, gdelay_feed, gdelay_tone, gdelay_mix, distortion, amplitude, pan, moog_cutoff, moog_resonance, moog_mix, dist_emphasis_freq, dist_pregain, dist_curve_blend, dist_drive_pos, dist_drive_neg, dist_poly_c1, dist_poly_c2, dist_poly_c3, stut_reps, bencina_iot, bencina_grainsize, bencina_pan, smear_frequency, smear_resonance, smear_stages, smear_feedback, pitch_fine, smear_pitch_fine, modout1, modout2, modout3, modout4`. **It does NOT cover** `saw_cycles_range`/`saw_depth_range` (`types.h:633-634`) nor the two embedded `pitch_control.semitone_range` (`types.h:420`) / `smear_pitch_control.semitone_range` (`types.h:453`) — a *complete* snapshot must include these or those params will snap rather than morph (see Completeness constraint). [Verified: all 41 present, all 4 omissions genuinely absent.]
- **`param_range_t`** — `src/types.h:383-395`. Per-param modulation record: `{min, max, rand_type, rand_instance, enabled, base_value, slew, smoothed_value, invert, saved_rand_type, saved_rand_instance}`. A snapshot of one modulatable param = `(scalar base) + min + max + enabled + rand_type + rand_instance + base_value + slew + invert`. ⚠ **`smoothed_value` is live state — do NOT capture or restore it** (it is a momentary LFO position). `saved_rand_type`/`saved_rand_instance` (`:393-394`) is pattern-attach bookkeeping — skip.
- **`get_current_value`** — `src/ligase~.c:4779-4813`. Reports a param's CURRENT value: explicit `*_current` fields for ~23 inlet-tracked params (`:4780-4803`), else falls back to `range->smoothed_value` for any modulatable param (`:4811`). ⚠ This is the WRONG primitive for a morph capture — the fallback returns a momentary modulated sample. Capture the **target BASE** (from `x->` scalar fields / `range->min` when disabled), not `get_current_value`.
- **`ligase_get_state`** — `src/ligase~.c:5216-5219`. Calls `get_params` (`:5217`, the scalar dump) + `get_ranges` (`:5218`, **enabled ranges only** — verified: `output_param_range` `:4834` emits `if(range->enabled)`) + `get_generators` (`:5219`). The TEMPLATE for the wire format of an externalizable snapshot, but `get_ranges` omits disabled ranges and many discrete modes — a complete snapshot captures MORE than `get_state` dumps.
- **`_ligase` struct** — `src/ligase~.c:145`. Holds scalar bases + discrete modes a snapshot must capture: `playhead_mode` (`:223`), `clock_advance_use_quantized` (`:231`), `stut_length_mode` (`:269`), `sos_mode` (`:287`), `headless_mode` (`:291`), `outlet3_mode` (`:303`), plus the `*_current` tracking fields, `bpm`, and quant grid settings.
- **`scheduler_t`** — `src/types.h:610-693`. Holds the ~41 `param_range_t` ranges (`:624-672`) + scalars (`iot :621`, `max_grains :620`, `grain_size :616`), the `pitch_control_t` (`:681`) and `smear_pitch_control_t` (`:682`) sub-structs (their own modes/semitones/scales/fine + embedded `semitone_range`), `pan_mode` (`:687`), `grain_midi_channel`/`smear_midi_channel` (`:683-684`), and `perlin_state` (generator internal state — live chaos, NOT snapshotted; but nbody/sphere/perlin TUNING params + output modes ARE part of a full patch snapshot).

### The write half (restore / per-tick re-emit primitives)

- **`ligase_param_range`** — `src/ligase~.c:4016-4061`. The restore/set primitive for a modulation range: `param_range <name> <min> <max>` sets `min`/`max`/`enabled=1` (`:4035-4037`); `param_range <name> <v>` sets `min=max=v, enabled=0` (`:4056-4058`). A morph tick re-emits this per modulatable range with interpolated `min`/`max`, directly feeding `sample_param_range`. Companions: `ligase_param_base_value` (`:4064`), `ligase_param_slew` (`:4084`).
- **`ligase_envelope`** — `src/ligase~.c:2513`. Discrete enum setter (envelope type 0/1/2), rebuilds the envelope IN PLACE. Canonical NON-interpolable param: morph must STEP it; it is in no `param_range`; capture via the envelope sub-object, restore via the `envelope <n>` message.
- **`ligase_pitch_mode` / `ligase_delay_mode` / `ligase_smear_pitch_source`** — `src/ligase~.c:4584` / `:3431` / `:3585`. Discrete source/mode selectors. These switch entire signal-path branches and change the meaning of other params (e.g. `delay_mode` flips inlets between gdelay and stut). Non-interpolable; STEP them, and re-emit dependent scalars AFTER the switch.
- **`ligase_tilde_setup`** — `src/ligase~.c:5448`. The full settable-message registry (~160 `class_addmethod` — verified 159, e.g. `envelope :5488`, `delay_mode :5522`, `param_range :5588`, `pitch_mode :5614`). Authoritative list of every restore message a snapshot can replay — every `ligase_*` setter maps 1:1 to a gensym message name. The new `morph_*`/`snapshot` handlers register HERE.

### The clock to model the morph stepper on (no `t_clock` exists today)

- **The per-block pattern cycle clock** — `src/ligase~.c:1679-1697`. ligase~ uses **no `t_clock`** anywhere (verified: `clock_new` appears only in `m_pd.h`, never in `src/*.c`). Instead the pattern subsystem advances a phase **once per DSP block** inside `ligase_perform`: guarded by `x->bpm > 1.0 && x->cycle_total_sec > 0.0` (`:1679`), it computes `inc = ((double)n / sample_rate) / cycle_total_sec` (`:1681`) and advances `pattern_phase[s]` per slot (`:1684`), calling `pattern_eval_slot` (`:1689`). **This is the model for the morph stepper:** a control-rate advance driven per-block from the same place, BEFORE `ligase_update_inlets` (`:1699`) so the morphed values are applied the same block. (GATE A weighs this vs a real `clock_new` `t_clock` — both are control-thread-safe; the per-block approach is consistent with the existing subsystem and needs no new timer object.)
- **`ligase_update_inlets`** — called at `src/ligase~.c:1699`, AFTER the pattern clock advance. The morph re-emit (writing `x->` scalar fields + `param_range_t` min/max/base) must land BEFORE this so the block's audio reads the morphed state. The audio thread then reads min/max/base through `sample_param_range` (`grain.c:201`) on its own schedule. **⚠ Note (verified, load-bearing):** `ligase_update_inlets` re-derives the scalar BASES of the ~23 inlet/headless-tracked params each block (it writes `x->scheduler->grain_size :786`, `iot`, `max_grains`, speed/scanrate/sos/env_skew/amplitude/pan, gdelay/moog/smear scalars). It does NOT touch any `param_range_t` band field. See GATE A.6 + Risks for the precedence consequence.

### The CV-driven invariant anchor

- **Signal inlets** are created in the constructor and read in `ligase_perform` (e.g. the smear-mix signal inlet `x_smear`, `ligase~.c:170`; the amplitude signal inlet `x_amplitude`, `:176`; the MIDI note signal inlet `x_midi`, `:174`, read only when in `PITCH_MODE_MIDI`). The perform callback is registered `dsp_add(ligase_perform, 26, …)` at `:1841`, with `CLASS_MAINSIGNALIN` at `:5456`. The project invariant requires the morph cursor to be CV-drivable: the `morph_x`/`morph_y` pair must be readable as signals (two new signal inlets, or a `morph~`-mode reuse), not message-only. The plan reserves this; GATE A decides v1-vs-v1.1 timing (adding two inlets means changing the `dsp_add` arg count from 26 and the perform unpacking — a real, bounded surface).

## Design

The layer is a new module **`src/morph.c`** + **`src/morph.h`**, holding the snapshot buffers, the surface (points), the route (waypoints), and the kernel; plus ~12 new `class_addmethod` handlers in `ligase~.c` and one per-block stepper call in `ligase_perform`. Nothing in the audio inner loop changes — the morph only writes the same fields the message setters already write.

### Data structures (`src/types.h` / `src/morph.h`)

> These are design sketches for GATE B; the implementer adds them as real types there. Define `MORPH_SCALAR_COUNT` / `MORPH_DISCRETE_COUNT` alongside the other caps when fixing the capture tables.

**A captured modulatable-param slot** (mirrors the snapshot-worthy subset of `param_range_t`):

```c
typedef struct {
    float base;          // scalar target value (NOT smoothed_value)
    float min, max;      // modulation band
    int   enabled;       // band active?
    int   rand_type;     // generator (categorical -> step, not lerp)
    int   rand_instance; // generator instance (categorical -> step)
    float base_value;    // PERLIN_2D Y base
    float slew;          // smoothing coeff (continuous -> lerp)
    int   invert;        // categorical -> step
} morph_range_slot_t;
```

**A full snapshot** — three field classes, each tagged continuous|discrete so the blend loop knows lerp vs step:

```c
#define MORPH_MAX_SNAPSHOTS 64     // arbitrary cap; surface stays cheap (IDW is O(snapshots))
#define MORPH_RANGE_COUNT   45     // get_param_range_by_name list (41) + saw_cycles + saw_depth
                                   //   + pitch.semitone_range + smear_pitch.semitone_range
// + define MORPH_SCALAR_COUNT / MORPH_DISCRETE_COUNT to the fixed capture-table sizes at GATE B

typedef struct {
    int   in_use;                          // slot occupied
    // (a) modulatable ranges — keyed 1:1 to the morph range-name table (below)
    morph_range_slot_t ranges[MORPH_RANGE_COUNT];
    // (b) continuous scalars (lerp in natural space) — grain_size, grain_start, speed,
    //     amplitude, pan, saw_cycles, saw_depth, scan_rate, iot, bpm, quant fields,
    //     pitch.semitones, pitch.pitch_fine, smear_pitch.semitone/ref_hz/ref_note/fine,
    //     and the SHADOWED effect scalars (smear_*, moog_*, dist_*, gdelay_*, bencina_*)
    float scalars[MORPH_SCALAR_COUNT];
    // (c) discrete ints (STEP at dominant weight) — envelope type, pitch_mode, delay_mode,
    //     smear_pitch_source, pan_mode, sos_mode, headless_mode, outlet3_mode,
    //     clock_advance_use_quantized, stut_length_mode, waveshaper/emphasis/position/
    //     oversample, enable flags, MIDI channels, playhead_mode
    int   discretes[MORPH_DISCRETE_COUNT];
    // scale lists (categorical block) — pitch & smear scales captured + restored whole
    float pitch_scale[MAX_SCALE_NOTES];      int pitch_scale_count;
    float smear_pitch_scale[MAX_SCALE_NOTES]; int smear_pitch_scale_count;
} morph_snapshot_t;
```

**The surface, route, and engine state** (`morph_state_t`, owned by `_ligase` or `scheduler`):

```c
typedef struct { int snap_id; float x, y; int in_use; } morph_point_t;   // a placed snapshot

typedef struct {
    float x, y;        // target cursor coordinate for this leg
    float rate;        // seconds to traverse this leg (transition RATE)
    int   curve;       // 0=linear, 1=ease-in, 2=ease-out, 3=ease-in-out, 4=hold/step
} morph_waypoint_t;

#define MORPH_MAX_WAYPOINTS 64

typedef struct {
    morph_snapshot_t snaps[MORPH_MAX_SNAPSHOTS];
    morph_point_t    points[MORPH_MAX_SNAPSHOTS];   // surface placement (one per placed snap)
    int              point_count;

    float cursor_x, cursor_y;       // live cursor (also written by morph_x/morph_y / signal inlets)
    int   interp_kind;              // 0 = IDW/Shepard (v1), 1 = natural-neighbour (v1.x)
    float idw_power;                // Shepard exponent p (default 2.0)

    int   included[MORPH_RANGE_COUNT + MORPH_SCALAR_COUNT + MORPH_DISCRETE_COUNT]; // selection tree

    // route playback
    morph_waypoint_t route[MORPH_MAX_WAYPOINTS];
    int   route_len;
    int   route_active;             // morph_run engaged
    int   route_leg;                // current leg index
    float route_leg_t;              // 0..1 progress within the current leg
    float route_from_x, route_from_y; // leg start coordinate
    int   route_loop;               // loop the path?
} morph_state_t;
```

**The morph range-name table** (`morph.c`, static): a 45-entry array of `{name, kind}` mirroring `get_param_range_by_name`'s order plus the four omitted ranges, so capture/restore iterate a single source of truth. Restore re-uses `get_param_range_by_name(x, name)` for the 41 dispatched ones; the four omitted ranges (`saw_cycles`, `saw_depth`, `pitch.semitone_range`, `smear_pitch.semitone_range`) get direct pointer access in the capture/restore loop.

### Capture (`morph_capture_snapshot`, message thread)

For each of the 45 modulatable ranges: read straight from the `param_range_t` (`min`/`max`/`enabled`/`rand_type`/`rand_instance`/`base_value`/`slew`/`invert`) and pair it with the scalar base from the corresponding `x->` field (NEVER `get_current_value`/`smoothed_value`). For continuous scalars: read the `x->`/`scheduler->` field directly. For discretes: read the mode/enum/channel field. For the two scale lists: `memcpy` the `pitch_scale_t.semitones[]` + `count`.

**Opaque effect sub-objects** (`grain_delay`, `moogladder`, `smear`, `distortion`) are SET through `grain_*_set_*` with no readback (verified: the four FX headers expose only `*_set_*`, zero `*_get_*` accessors). v1 resolves this by **shadowing**: the morph layer keeps a `last-set` mirror updated whenever the corresponding setter message fires (a one-line write added in each effect setter handler), and captures the shadow. (Alternative, GATE A: add `grain_*_get_*` accessors. Shadowing is lower-risk and local to `ligase~.c`.)

### Placement & the surface (message thread, instant)

`morph_point <id> <x> <y>` (alias `morph_place`) writes/updates `points[]`: find the entry with `snap_id == id` (or allocate one), set `x`,`y` (clamped `[0,1]`). `morph_unplace <id>` removes it. The surface is just this point list; no mesh in v1.

### The blend (`morph_apply_at(x, cx, cy)`, control thread)

Given cursor `(cx,cy)`:

1. **Weights (pluggable kernel).** v1 = **Shepard/IDW**: for each placed snapshot `i`, `d_i = hypotf(cx - x_i, cy - y_i)`; if any `d_i < ε` (cursor sits on a point) → weight 1 to that snapshot, 0 to others (exact reproduction, the no-overshoot property); else `w_i = 1 / d_i^p` (default `p = 2`), normalize `w_i /= Σw`. (v1.x swaps in Sibson natural-neighbour behind `interp_kind == 1` — same call site, different weight computation; needs the Delaunay mesh — see Out-of-scope.)
2. **Continuous fields** (ranges' `base`/`min`/`max`/`slew`/`base_value`, and the scalar block): `v = Σ_i w_i · field_i`, each **in the field's own natural space** (Hz blends in Hz, semitones in semitones — there is NO shared 0..1 normalization). Re-emit via the field's setter / `param_range`.
3. **Discrete fields** (modes, enums, rand_type/instance, invert, channels, scales): NO lerp. Pick the snapshot with the **maximum weight** (`argmax w_i`) and take ITS value. For mode switches that re-route the path (`delay_mode`, `pitch_mode`, `smear_pitch_source`), re-emit the dependent scalars AFTER setting the mode. Scale lists copy whole from the argmax snapshot.
4. **Selection tree:** skip any field whose `included[]` flag is 0 — excluded params keep whatever manual/modulation/inlet control owns them.

Because step 2 writes `min`/`max`/`base` and the modulation generators keep running, **modulation is never frozen** — the morph slides the band, the LFO/chaos/pattern keeps sampling it (`sample_param_range`, `grain.c:201`).

### The live cursor — `morph <x> <y>` and the CV path

- **Message:** `morph <x> <y>` sets `cursor_x/cursor_y` and calls `morph_apply_at` immediately (instant jump — this is Bencina's `Interpolate_X`/`Interpolate_Y`).
- **CV (invariant):** `morph_x`/`morph_y` float messages set one axis each; and (GATE A) two **signal inlets** (or a `morph~` mode) write `cursor_x`/`cursor_y` per block from `morph_x_in[0]`/`morph_y_in[0]`, applied in the per-block stepper. This satisfies the "delay-mode-style params must be CV-driven, not message-only" project invariant for the cursor.

### Routes — `morph_route` + `morph_run` (the value-add over AudioMulch)

- `morph_route <x> <y> <rate> <curve>` appends a `morph_waypoint_t` to `route[]` (`rate` in seconds, `curve` 0-4 per the enum above). `morph_route_clear` empties it.
- `morph_run [loop]` sets `route_active = 1`, `route_leg = 0`, `route_leg_t = 0`, records `route_from_{x,y} = cursor_{x,y}`. `morph_stop` halts; `morph_pause` freezes at the current `(cursor_x, cursor_y)`.
- **Per-block advance** (in the stepper, same place as the pattern clock at `ligase~.c:1679-1697`): when `route_active`, advance `route_leg_t += ((double)n / sample_rate) / route[leg].rate` (guarded `rate > 0`); apply the leg's curve to get eased `t' = curve(route_leg_t, route[leg].curve)`; set `cursor = lerp(route_from, route[leg].{x,y}, t')`; on `route_leg_t >= 1` advance to the next leg (`route_from = route[leg].{x,y}`), or loop / stop at the end. Then call `morph_apply_at(cursor_x, cursor_y)`. This makes the route a control-rate automation envelope on the cursor — the per-leg rate is the transition rate and the per-leg curve is the route's shape, exactly the future-work Bencina flagged.

### Curve functions (`morph.c`, static, control-rate)

`curve(t, kind)`: `0` linear `t`; `1` ease-in `t²`; `2` ease-out `1-(1-t)²`; `3` ease-in-out `smoothstep`; `4` hold (snap to 1 at leg end — useful for stepped routes). Cheap scalar ops, control rate only.

### Message interface (`src/ligase~.c`, handlers near the other setters; registered in `ligase_tilde_setup` `:5448`)

All run on the message thread; field stores + (for `morph`/`snapshot`/`morph_run`) an immediate or armed apply. No DSP, no alloc in the hot loop.

- `snapshot <id>` → capture current patch into `snaps[id]` (alias `morph_snap`).
- `snapshot_recall <id>` → apply `snaps[id]` directly (no blend; jump). `snapshot_clear <id>`.
- `morph_point <id> <x> <y>` (alias `morph_place`) → place/replace point. `morph_unplace <id>`.
- `morph <x> <y>` → live cursor jump + apply. `morph_x <v>` / `morph_y <v>` → single-axis.
- `morph_route <x> <y> <rate> <curve>` → append waypoint. `morph_route_clear`.
- `morph_run [loop]` / `morph_stop` / `morph_pause`.
- `morph_interp <0|1>` → kernel select (0=IDW v1, 1=NN reserved). `morph_power <p>` → IDW exponent.
- `morph_include <name…>` / `morph_exclude <name…>` → selection tree (by param name; `all` keyword).
- `morph_state` → dump the surface (points, cursor, route) for save/restore, mirroring `get_state`'s wire format.

Registration block goes adjacent to the query cluster (`get_state` at `:5633`), e.g.:

```c
    class_addmethod(ligase_class, (t_method)ligase_snapshot,       gensym("snapshot"),    A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_morph_point,    gensym("morph_point"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_morph,          gensym("morph"),       A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_morph_route,    gensym("morph_route"), A_GIMME, 0);
    class_addmethod(ligase_class, (t_method)ligase_morph_run,      gensym("morph_run"),   A_GIMME, 0);
    /* …morph_x/_y, morph_interp, morph_include/_exclude, snapshot_recall, etc… */
```

### Per-block stepper hook (`src/ligase~.c`, in `ligase_perform`)

Add one call right after the pattern cycle clock block (`:1697`) and BEFORE `ligase_update_inlets` (`:1699`):

```c
    if (x->morph && x->morph->route_active)
        morph_step(x, n);   // advance route_leg_t, recompute cursor, morph_apply_at(...)
    else if (x->morph && morph_cursor_is_signal_driven(x))
        morph_apply_at(x, x->morph->cursor_x, x->morph->cursor_y);  // CV cursor (GATE A)
```

`morph_step` and `morph_apply_at` only WRITE the same `x->`/`scheduler->` fields and `param_range_t` members the message setters write — atomic single-writer stores, no alloc, no `post()` in the loop, no pool restructure. The audio thread reads them via `sample_param_range` on its own schedule. **All interpolation is strictly control-thread; all modulation is strictly audio-thread.**

### Composition with the existing engine

- **Modulation:** the morph slides `min`/`max`/`base`; `sample_param_range` keeps running — vibrato/LFO/chaos/pattern modulation survives a morph (it morphs the *band*).
- **Inlet precedence (verified):** the stepper writes BEFORE `ligase_update_inlets` (`:1699`). For the modulation **band** (`min`/`max`/`enabled`/`base_value`/`slew`) this is correct — `update_inlets` never touches band fields, so the morph's band always wins. For the scalar BASES of the ~23 inlet/headless-tracked params, `update_inlets` re-derives them each block from the connected inlet (or headless generator) AFTER the morph, so a connected inlet overrides the morph's base that block. This is the engine's standard live-CV-wins precedence (a manual scalar message hits the same fate); see GATE A.6 + Risks. Net: morph the **band** for inlet-tracked params (always effective); morph of their scalar **base** is effective only while the inlet is unpatched.
- **Pattern subsystem:** orthogonal — patterns write `cached_value` per block; if a param is pattern-attached, leave its range to the pattern (exclude it from the morph, or let the morph set the band the pattern is unused over). GATE A confirms precedence (recommend: morph and pattern are independent; a pattern-attached param's `rand_type` is categorical so the morph won't lerp it away).
- **Pitch destinations:** `pitch.semitones`/`pitch_fine` and `smear_pitch` scalars are continuous (lerp); `pitch_mode`/`smear_pitch_source` are discrete (step). The pitch SCALE lists step whole.
- **CV/headless:** with `headless_mode`, the morph just runs (it is control-rate, no GUI). The cursor signal inlets make it a first-class CV target.

## Steps & gates

### GATE A (approval) — open design decisions for owner sign-off

This is a direction plan; GATE A is substantial. Recommendations marked **[R]**.

1. **v1 weighting kernel.** **[R] Shepard/IDW** (`w = 1/dᵖ`, default `p=2`, exact-reproduction on a point) for v1 — a distance loop, O(snapshots), no mesh, ships now. **Natural-Neighbour (Sibson)** is the faithful Bencina kernel but needs a maintained 2D Delaunay/Voronoi mesh + stolen-area coords (a sizable subproject). Confirm: v1 = IDW behind `morph_interp 0`, NN deferred to v1.x behind `morph_interp 1`. (Tradeoff: IDW is global/smooth-but-less-local; NN is local/predictable/no-overshoot. The API is kernel-agnostic so the swap is internal.)
2. **Surface coverage outside the convex hull.** IDW covers the whole plane natively (every point has a weight). NN is only defined inside the convex hull → AudioMulch reflects points around the rectangle edges. For v1 IDW this is a non-issue; flag it as an NN-era decision. **[R]** ship IDW, defer the edge topology (reflect/cylinder/torus) with NN.
3. **Discrete-param blend policy.** **[R] argmax-weight step** (snap each discrete to the highest-weighted snapshot's value). Alternative: threshold at the cursor crossing the perpendicular bisector (Voronoi-of-discretes). Confirm argmax for v1; confirm that mode switches re-emit dependent scalars after switching.
4. **CV cursor timing.** **[R] ship `morph_x`/`morph_y` float messages in v1; signal inlets (`morph_x_in`/`morph_y_in`) in v1.1** — adding two signal inlets touches the DSP signature (`ligase_dsp`, the `dsp_add(ligase_perform, 26, …)` arg count at `:1841`, `CLASS_MAINSIGNALIN` at `:5456`) and the perform arg list, a bigger surface than the message API. Confirm vs landing signal inlets in v1 to honor the CV invariant immediately. (Either way the control model is identical.)
5. **Opaque-FX capture: shadow vs getters.** **[R] shadow last-set values** in the morph layer (a one-line mirror write in each `grain_*_set_*` message handler) — local, low-risk, no changes to `grain_smear.c`/`grain_delay*.c`/`grain_moogladder.c`/`grain_distortion.c`. Alternative: add `grain_*_get_*` accessors (cleaner but touches four modules — verified those modules ship setters only). Confirm shadow for v1.
6. **Stepper mechanism + write-ordering vs the inlets.** **[R] per-block advance in `ligase_perform`** (model on the pattern cycle clock at `:1679-1697`) — consistent with the existing subsystem, no new `t_clock`. Alternative: a real `clock_new` `t_clock` (decouples morph rate from block size, but adds a timer object + lifecycle). Confirm per-block. **Sub-decision (load-bearing, see Risks — verified):** the hook sits BEFORE `ligase_update_inlets` (`:1699`). That is correct for the modulation **band** (`min`/`max`/`enabled`/`base_value`/`slew`) — `ligase_update_inlets` never touches those, and `sample_param_range` reads them, so the morph's band write survives. But `ligase_update_inlets` RE-DERIVES the scalar BASES of the inlet/headless-tracked params each block — it writes `x->scheduler->grain_size` (`:786`), `x->scheduler->iot`, `x->scheduler->max_grains`, speed/scanrate/sos/env_skew/amplitude/pan and the gdelay/moog/smear scalars from the connected signal inlets (or, in `headless_mode`, from the internal generators) AFTER the stepper runs. So a morph that writes one of THOSE scalar bases is overwritten the same block whenever its inlet is patched (or headless owns it). This is the engine's standard live-CV-wins precedence (identical to what a manual scalar message hits), not a morph bug — but confirm the intended behavior: morph the **band** for inlet-tracked params (always effective), and treat morph of an inlet-tracked **scalar base** as effective only while that inlet is unpatched / not headless-driven. (Placing the hook AFTER `ligase_update_inlets` instead would invert this — morph would override live CV — which violates the CV-driven invariant; keep it BEFORE.)
7. **Route rate units.** **[R] seconds per leg** (`rate` = traverse time). Alternatives: a velocity (units/sec across the surface) or BPM-locked (legs quantized to the cycle clock like patterns). Confirm seconds; note BPM-locked routes are a natural v1.x extension (reuse `cycle_total_sec`).
8. **Snapshot completeness scope for v1.** **[R] capture the full three-class set** (all 45 ranges incl. the 4 omitted, all scalars, all discretes, both scale lists) so morphs are complete from day one. Alternative: a lean v1 capturing only the `get_param_range_by_name` 41 + core scalars, deferring the omitted ranges/scales (those params snap). Confirm full capture (the omitted-range completeness constraint is the documented footgun).
9. **`MORPH_MAX_SNAPSHOTS` / `MORPH_MAX_WAYPOINTS` caps + CPU budget.** **[R] 64 each** (snapshot is a few KB; 64 ≈ low hundreds of KB total; IDW is O(64) distance ops per control tick = negligible). Confirm caps; confirm a static array (no malloc on the audio path) is acceptable vs a growable pool.

### Step 1 → GATE B (module skeleton + types, no behavior)

Add `src/morph.c`/`src/morph.h`; add `morph_snapshot_t`/`morph_state_t`/`morph_point_t`/`morph_waypoint_t` (in `types.h` or `morph.h`) and define `MORPH_SCALAR_COUNT`/`MORPH_DISCRETE_COUNT` to the fixed capture-table sizes; add `morph_state_t *morph` to `_ligase`; allocate/zero in the constructor (`ligase_new` `:5240`), free in the destructor (`ligase_free` `:5228`, alongside the other `if (x->…) …_destroy()` guards); add `morph.c` to the Makefile `SOURCES` (`Makefile:26`). **GATE:** `make clean && make` warning-free; a fresh object has `morph->point_count == 0`, `route_active == 0`, and zero behavior change (no stepper call active, no message handlers wired yet).

### Step 2 → GATE C (capture + recall)

Implement `morph_capture_snapshot` (the 45-range + scalar + discrete + scale-list capture, reading bases not `smoothed_value`) and `morph_snapshot_recall` (direct apply via setters / `get_param_range_by_name`). Add the FX shadow mirrors (GATE A.5). Register `snapshot`/`snapshot_recall`/`snapshot_clear`. **GATE:** `make clean && make` warning-free; `snapshot 0` then mutate params then `snapshot_recall 0` restores the captured state (headless: dump `get_state` + `morph_state` before/after, diff — `get_state` alone only round-trips ENABLED ranges, so disabled-range/discrete fields assert via `morph_state`/per-field reports); modulation keeps running after recall (an enabled range still sweeps).

### Step 3 → GATE D (surface + blend + live cursor)

Implement `morph_apply_at` (IDW weights, continuous lerp in natural space, discrete argmax-step, selection tree) and the placement/cursor handlers (`morph_point`, `morph`, `morph_x`/`morph_y`, `morph_interp`, `morph_include`/`morph_exclude`). **GATE:** `make clean && make` warning-free; with 3 snapshots placed at corners, `morph` at a snapshot's coord reproduces it exactly; `morph` at the centroid yields the weighted blend (headless: assert a few continuous params land at the expected IDW value — for inlet-tracked params assert with the inlet UNPATCHED per GATE A.6); excluded params are untouched.

### Step 4 → GATE E (routes)

Implement `morph_step` + the per-block hook in `ligase_perform` (after `:1697`, before `:1699`) + curve functions; register `morph_route`/`morph_route_clear`/`morph_run`/`morph_stop`/`morph_pause`. **GATE:** `make clean && make` warning-free; a 3-waypoint route with per-leg rates traverses the cursor over the right durations (headless: log cursor `(x,y)` per block, assert leg timings within one block); curves shape the trajectory; `loop` repeats; no audio-thread alloc/`post` in `morph_step`.

### Step 5 → GATE F (CV cursor — per GATE A.4 timing)

If v1: add `morph_x_in`/`morph_y_in` signal inlets, widen `ligase_dsp`/perform (the `dsp_add` arg count + perform unpacking), read them per block in the stepper. Else defer to v1.1. **GATE:** `make clean && make` warning-free; a signal/CV ramp into `morph_x_in` sweeps the surface; message `morph` and route playback still work; the CV invariant is satisfied (cursor is signal-drivable, not message-only).

### Step 6 → GATE G (verify + docs)

Build; run the acceptance patches headless; confirm no regression in the existing param/modulation/pattern paths. Document the `snapshot`/`morph_*` API, the IDW-vs-NN kernel note, the discrete-step policy, the route rate/curve semantics, the inlet-precedence note (band-always / base-when-unpatched), and the modulation-survives-morph behavior in the manual.

## Acceptance criteria

All headless-runnable under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1`). Most criteria are STATE-level (assert via `get_state`/`morph_state` dumps + the `*_current`/`last_*` reports) and need no ear; the sound-quality ones are flagged for an ear-test. NB `get_state` dumps ENABLED ranges only — use `morph_state`/per-field reports to assert disabled-range and discrete fields.

1. **Capture/recall round-trip (headless).** `snapshot 0`; mutate `speed`/`amplitude`/`smear_frequency`/an enabled `iot_range`/`envelope`/`pitch_mode`; `snapshot_recall 0`; the pre-mutation state is restored for captured fields (assert via `get_state` for enabled ranges + `morph_state`/reports for the rest). Verifies the three-class capture incl. discretes and the band (not `smoothed_value`).
2. **Exact reproduction on a point (headless).** Place `snapshot 0` at `(0,0)`, `snapshot 1` at `(1,1)`; `morph 0 0` reproduces snap 0 exactly; `morph 1 1` reproduces snap 1; verifies the IDW exact-reproduction (no-overshoot) property at points.
3. **Weighted blend at an interior point (headless).** Two snapshots differing only in `amplitude` (0.2 vs 0.8) placed at `(0,0)`/`(1,0)`, with the `amplitude` inlet (`x_amplitude`, `:176`) UNPATCHED (per GATE A.6); `morph 0.5 0` yields `amplitude` = the IDW blend (= 0.5 for `p=2` at the midpoint by symmetry); `morph 0.25 0` weights toward 0.2. Assert via `get_current_value("amplitude")` / the `amplitude_current` report.
4. **Modulation survives a morph (headless + ear).** With `amplitude_range` enabled (a sweep) in both snapshots but different bands, `morph` at an interior point sets the interpolated band AND the sweep keeps running (the `amplitude_current` report still varies block-to-block, not frozen). Headless: confirm the value changes across blocks at a fixed cursor; ear: the tremolo continues through the morph.
5. **Discrete params step, not lerp (headless).** Snap A has `envelope 0` + `pitch_mode 1`, snap B has `envelope 2` + `pitch_mode 3`, placed at `(0,0)`/`(1,0)`. `morph 0.4 0` yields A's discretes (argmax weight on A), `morph 0.6 0` yields B's — never an invalid intermediate enum. Assert via `get_state`.
6. **Route traversal timing (headless).** `morph_route 1 0 2.0 0` then `morph_route 1 1 1.0 0`; `morph_run`. The cursor reaches `(1,0)` after ~2.0 s and `(1,1)` after ~3.0 s total (log cursor per block; assert leg end times within one DSP block). Doubling all rates doubles the durations. `morph_run loop` repeats.
7. **Curve shapes the trajectory (headless).** A single leg with `curve 1` (ease-in) vs `curve 0` (linear): the cursor's mid-leg position is nearer the start for ease-in. Assert the cursor `(x,y)` at the leg's half-time differs by the expected curve delta.
8. **CV cursor (headless, if GATE A.4 = v1).** A signal ramp 0→1 into `morph_x_in` sweeps `amplitude` (or any blended param) across the two snapshots; assert the swept `*_current` value tracks the ramp.
9. **Selection tree (headless).** `morph_exclude amplitude`; a morph that would change `amplitude` leaves it at the manually-set value while other params blend. Assert via the report.
10. **No regression (headless).** A patch that sends NO `snapshot`/`morph_*` message behaves byte-for-byte as today (no stepper active, `morph == NULL`-guarded); `make clean && make` warning-free; existing modulation/pattern/pitch tests unchanged.
11. **Audio-quality smoothness (ear-test).** Running a route across snapshots that differ in pitch/filter/grain density should glide without zipper noise on continuous params (the per-block re-emit + each param's own slew handle smoothing) and click only where a discrete steps (expected). Subjective; document any audible stepping that wants slew.

## Risks / out-of-scope

**Risks**

- **Audio-thread safety.** The stepper runs in `ligase_perform` but must only WRITE fields the message setters already write (atomic single-writer stores) — **no malloc, no `post()`, no pool restructure** in `morph_step`/`morph_apply_at`. Capture (which may touch scale lists) stays on the message thread. The whole module is `x->morph != NULL`-guarded so an un-morphed patch never enters the path.
- **Completeness footgun (the documented one).** `get_param_range_by_name` omits `saw_cycles`/`saw_depth`/`pitch.semitone_range`/`smear_pitch.semitone_range`, and `get_ranges` only dumps ENABLED ranges (verified: `output_param_range` `:4834` gates on `if(range->enabled)`). A capture that iterates only the 41 dispatched ranges will make those params SNAP instead of morph. GATE A.8 mandates the full 45-range capture; the morph range-name table must include the 4 omitted ones with direct pointers.
- **`smoothed_value` capture mistake.** Capturing `get_current_value`/`smoothed_value` for modulated params snapshots a momentary LFO position. The capture MUST read the target base (`x->` fields / `range->min` when disabled), per `sample_param_range`'s base semantics.
- **Opaque-FX capture incompleteness.** Without shadows or getters, `smear_*`/`moog_*`/`dist_*`/`gdelay_*`/`bencina_*` scalars can't be captured (write-only setters — verified, no `*_get_*` in any FX header). GATE A.5's shadow mirrors are load-bearing for a complete FX snapshot; an un-mirrored setter silently leaves that param uncaptured.
- **Discrete-mode signal-path coupling.** `delay_mode`/`pitch_mode`/`smear_pitch_source` re-route the signal path and change other params' meaning. The morph must re-emit dependent scalars AFTER stepping the mode, or a stepped mode reads stale dependents.
- **Stepper-before-`update_inlets` overwrites inlet-tracked scalar bases (verified).** The stepper hook runs BEFORE `ligase_update_inlets` (`:1699`), which is correct for the modulation band but means the scalar BASES that `ligase_update_inlets` re-derives every block from connected signal inlets / headless generators (`x->scheduler->grain_size :786`, `iot`, `max_grains`, speed/scanrate/sos/env_skew/amplitude/pan, gdelay/moog/smear scalars — the ~23 inlet-tracked params per `get_current_value` `:4780-4803`) are overwritten the same block when their inlet is patched. NOT a regression (this is the existing live-CV-wins precedence — a manual scalar message hits the identical fate), but the acceptance tests for those params must run with the inlet UNPATCHED (or headless OFF for that param), and the manual must document: morph the **band** for inlet-tracked params (always effective); morph of an inlet-tracked **scalar base** is effective only while the inlet is unpatched. See GATE A.6 sub-decision. (Acceptance criterion 3 uses `amplitude`, whose inlet is `x_amplitude` `:176` — assert with that inlet unpatched.)
- **Natural-space vs shared-axis interpolation.** Blending Hz and semitones through one normalized axis distorts the perceptual mapping. Every continuous field blends in its OWN clamp range; the setters' existing clamps (e.g. `iot 0.0005-2.0`, `moog_resonance 0-4`, `smear` clamp in `smear_update_coeffs` `grain_smear.c:51` `0.45f*sr`) remain the sole bounds owners — the morph feeds in-range values and never re-clamps.
- **IDW is not natural-neighbour.** v1's Shepard kernel is global (every point pulls everywhere) and less locally predictable than Bencina's Sibson NN. It still gives exact reproduction at points and no overshoot, but the surface shape differs. This is an accepted v1 simplification (GATE A.1); the API is kernel-agnostic so NN drops in later.
- **CPU.** IDW is O(snapshots) distance ops per control tick (≤64) — negligible. A route adds a few scalar curve ops per block. The cost is in the **per-block re-emit of all included params** every morph tick — bounded by the param count (~45 ranges + ~30 scalars), all cheap field writes; no FFT/alloc. Capture is a one-shot O(params) copy on the message thread. Honest scope: this is a sizable FEATURE (a new module + ~12 handlers + a stepper + the kernel), staged v1 (IDW + routes + message cursor) then v1.x (NN + signal cursor + edge topologies).

**Out of scope (v1)**

- **Natural-Neighbour (Sibson) kernel + the 2D Delaunay/Voronoi mesh** — deferred to v1.x behind `morph_interp 1`; needs node insert/remove + stolen-area coords (Sambridge circumtriangle), a substantial subproject. v1 ships IDW.
- **Convex-hull edge topologies** (reflect/cylinder/torus) — an NN-era concern; IDW covers the whole plane.
- **A GUI surface** — ligase~ is a Pd external driven by messages/CV; the surface is the message API + (optionally) a Pd patch GUI built externally. No C-side GUI.
- **Snapshot morphing of generator internal CHAOS state** (perlin/lorenz/nbody/sphere positions/velocities) — live state, excluded; only their TUNING params (epsilon/damping/G/elasticity/noise_frequency_scale) + output modes are snapshotted.
- **BPM-locked routes** (legs quantized to the cycle clock) — a natural v1.x extension reusing `cycle_total_sec`; v1 routes are seconds-per-leg.
- **Microtonal/per-step blending of scale LISTS** — scale lists are categorical; they step whole from the argmax snapshot, not element-wise blended.
- **Touching the audio inner loop, `sample_param_range`, the existing clamps, or any existing setter's behavior** — the morph only writes the same fields those setters write; it adds no new DSP and changes no existing math.