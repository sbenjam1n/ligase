# Plan: Spatial granulation — physics-driven grain placement

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — new directions)
**Related:** the **granular scheduler** (`scheduler_trigger_grain` snapshot + the `scheduler_process` per-sample render loop, `src/grain.c`), the **physics generators** (`sphere_state_t` / `nbody_state_t` 3D state in `src/sphere.c` / `src/perlin.c`, today flattened to one scalar at the pan boundary), the **modulation system** (`param_range_t` + `sample_param_range`, `src/grain.c:201`), the existing **pan** path (`grain_t.pan` + `pan_mode`), and the **stereo output bus** (`x_out_left`/`x_out_right`, `src/ligase~.c:179-180`). Conceptually a *fourth* destination-of-physics alongside the existing modulation/pitch/delay consumers — but the first one that consumes the FULL 3D vector instead of one flattened axis.

> **PROVENANCE (2026-06-24).** Every file:line reference below was verified by reading `src/types.h`, `src/grain.c`, `src/sphere.c`, `src/sphere.h`, `src/perlin.c`, and `src/ligase~.c` on 2026-06-24. The grain has exactly one spatial field today (`grain_t.pan`, `types.h:294`); the sims hold full 3D state (`sphere_state_t.position {x,y,z}`, `sphere.h:46`; `nbody_state_t.pos[NBODY_COUNT][3]`, `types.h:527`) and expose it raw (`sphere_get_x/y/z`, `sphere.c:241-251`) but flatten it to one `[0,1]` scalar at the pan boundary (`sphere_get_normalized`, `sphere.c:265`; `nbody_get_normalized`, `perlin.c:433`). The output bus is hard stereo (two `outlet_new(&s_signal)` at `ligase~.c:5270-5271`, bound as `sp[22]/sp[23]` → `out_left/out_right` in the `dsp_add(ligase_perform, 26, …)` at `ligase~.c:1841`). No `spatial`/`pos_x`/`azimuth` symbol exists in the tree yet — this is a forward-direction plan, no code written.
>
> **ADVERSARIAL RE-VERIFICATION (2026-06-24).** Every file:line + symbol re-read against source. **All references confirmed accurate** with three small corrections folded in: (a) the sphere `output_mode` range is **0-6** (3-6 = velocities/magnitude), not "3-9" — an earlier draft overstated it (`sphere.c:276-301`, `default`→0.5 at `:298-300`); (b) the render-site mode-1 branch is a **bare `else`** (`grain.c:1240`) that today captures every `pan_mode != 0`, so adding mode 2 requires converting it to `else if (pan_mode == 1)` — the one place spatial is *not* a pure append (see render-branch §, flagged inline); (c) `nbody_get_normalized` takes a **non-const** `nbody_state_t *` in the codebase, while the new vec3 helper is `const`-correct (cleaner, binds fine). Additionally **confirmed**: `grain.c` reaches `sphere.h` transitively via `types.h:7` (it already calls `sphere_get_normalized`/`sphere_tick`/`sphere_init`), so adding `sphere_get_normalized_vec3` to `sphere.h` needs **no new `#include`**; `scheduler_trigger_grain` is on the **audio thread** (`ligase_perform`→`ligase_process_grains`→trigger, `ligase~.c:1704/1162/1191/1312`), validating the snapshot thread-safety claim; all 4 sphere + 4 nbody instances are stepped **unconditionally** each trigger (`update_perlin_coords`, called `grain.c:788`, loops `i<4` at `:361/375`), so the snapshot always reads a live, evolving sim; `RAND_TYPE_NONE == 0` (`types.h:370`), confirming the zero-init footgun note; `#include <math.h>` present (`grain.c:8`) with `powf`/`floorf`/`cosf`/`sinf` already in use, so `atan2f`/`fmaxf` are available.

---

## Problem

The synth already runs two genuine 3D physics simulations per modulation instance — an n-body gravitational system (`nbody_state_t`, three bodies with `pos[3]`/`vel[3]`, `types.h:525-529`) and an STK-style bouncing sphere (`sphere_state_t`, a `position {x,y,z}` inside a boundary box, `sphere.h:46`). But **every consumer of those sims sees only one number.** The pan boundary (`sphere_get_normalized` / `nbody_get_normalized`) collapses the whole `(x,y,z)` state to a single `[0,1]` scalar chosen by an `output_mode` (sphere modes 0/1/2 = PosX/PosY/PosZ, 3-6 = velocities/magnitude, default → 0.5; `sphere.c:276-301`). Two of the three axes are thrown away at that boundary. The grain that the sim drives carries exactly one spatial number, `grain_t.pan` (`types.h:294`), and the render loop turns that one number into an L/R gain pair (`grain.c:1233`). So the bodies orbit in 3D inside the engine, and the listener hears a mono point smeared left-to-right on one axis.

**OWNER INTENT.** A distinct **SPATIAL mode** where each grain's stereo position is driven by a physics source's **FULL 3D trajectory** — unflattening the sims. The grain cloud becomes *orbiting bodies in space*: a grain fired while body 1 is front-left-high lands front-left (with a tonal/level hint of "high" and "near"); a grain fired a moment later, after the body has swung to back-right, lands back-right. The 3D coordinate is sampled per grain at trigger and frozen, exactly as `pan` is today, so the cloud is a constellation of fixed points each placed by where its driving body *was* at birth — the motion of the cloud is the motion of the sim, scattered across the grain population.

Concretely, decomposed:

1. **Snapshot a 3D vector, not a scalar.** At grain trigger, read the driving sim's raw `(x,y,z)` (bypassing the lossy `*_get_normalized` API), normalize each axis to `[-1,1]` via the sim's own bounds, and freeze it onto the grain alongside `pan`.
2. **Map 3D → L/R in the render loop.** Add a new `pan_mode == 2` ("spatial 3D") branch beside the existing two. Derive an **azimuth** from the horizontal plane `(x, z)` and pan with the *same constant-power law already in the file* (`grain.c:1233`); optionally fold **distance** into overall level and **elevation** into a gentle tone tilt for a binaural feel.
3. **v1 target = stereo/binaural, no new outlets.** The owner's hardware is a stereo Focusrite. The 3D→spatial map collapses to the existing two outlets (`out_left`/`out_right`) — **no class-construction change.** Multichannel (quad/Ambisonics, more outlets) is explicitly a later/optional path (GATE A).
4. **CV/signal-first, headless-0.** *Which* generator drives position is message-configured (a `spatial <source> <instance>` selector, the same way every other modulatable source is chosen). The position *itself* is signal-rate — it tracks the live sim via the per-grain snapshot, like every other modulatable param. The spatial source must not be a static message-only value.
5. **Backward compat is load-bearing.** `pan_mode` 0 (mono point) and 1 (stereo balance) and the entire scalar `pan` / `pan_range` path behave bit-for-bit as today. Spatial is purely additive: a new `pan_mode` value plus new POD fields on the grain.

This plan is the **spatial slice**: add a small frozen 3D vector to the grain, a snapshot read at trigger time, a `pan_mode == 2` binaural render branch, a `sphere_get_normalized_vec3` / `nbody_get_normalized_vec3` pair so the flatten logic lives in one place, and a `spatial` selector message. v1 ships stereo/binaural to the two existing outlets; multichannel is staged out (GATE A + a v2 note).

## Mechanics / target surface — the EXISTING code this extends

### The grain's one spatial field (the thing we widen)

- **`grain_t.pan`** — `src/types.h:294`. `float pan; // Stereo pan (0=left, 0.5=center, 1=right)`. The ONLY spatial state on a grain today. POD; snapshotted at trigger; read every sample. The grain struct (`types.h:290-303`) is a flat POD with a `next` pool pointer — new `float pos_x/pos_y/pos_z` (and optionally precomputed `float spatial_left_gain/spatial_right_gain`) go right next to `pan`. No allocation, no init cost beyond the existing zeroing.

### The snapshot point (trigger / audio thread)

- **`scheduler_trigger_grain`** — `src/grain.c:751` (the function builds a grain). The pan snapshot lives at `grain.c:986-998`: `if (sched->pan_range.enabled) grain_pan = sample_param_range(&sched->pan_range, &sched->perlin_state, pan);` (`:988-990`) else `grain_pan = pan` (`:993`), then clamp `[0,1]` (`:997-998`).
- **`grain->pan` assignment** — `src/grain.c:1040`, `grain->pan = grain_pan;` — inside the field-store block (`:1035-1045`, where `position`/`increment`/`amplitude`/`saw_*`/`splice_*` are all frozen). **This is the SNAPSHOT line.** New `grain->pos_x/pos_y/pos_z = …` go right here, sampled the same way pan is — from the live sim, once, frozen. (The grounding's "snapshot, not in the audio loop" invariant: the constellation must be fixed points; re-reading the moving sim per-sample would smear each grain across the trajectory.)

### The render loop (audio thread — where 3D → L/R happens)

- **`scheduler_process`** — `src/grain.c:1087` (walks the active grain list); the per-sample loop is `src/grain.c:1163`. Per sample it reads the reel (`:1207-1208`), applies envelope + amplitude, then **branches on `pan_mode`** at `src/grain.c:1223`:
  - **`pan_mode == 0`** (`:1223-1239`): mono point source — sum L+R to mono (`:1226`), apply `env_val*amplitude`, then constant-power pan: `pan_angle = grain->pan * 1.5707963267948966f` (= `pan*(pi/2)`, `:1233`), `left_gain = cosf(pan_angle)` (`:1234`), `right_gain = sinf(pan_angle)` (`:1235`), accumulate `target_left[i] += mono_sample*left_gain` / `target_right[i] += …` (`:1238-1239`).
  - **`pan_mode == 1`** (the bare `else` at `:1240`, body `:1241-1254`): stereo balance — keep the stereo source, apply the **same** `cos/sin` law (`:1248-1250`) as an L/R balance (`:1253-1254`).
- **The constant-power pan math** — `src/grain.c:1233` (and the identical `:1248`). `cos²+sin² = 1` ⇒ equal power at every pan position. **This is exactly where a 3D azimuth slots in**: replace `grain->pan` with an azimuth derived from `grain->pos_x/pos_z`. The new `pan_mode == 2` branch is a sibling of these two; it reuses this gain law verbatim, just fed by a different angle (and optionally scaled by a distance gain).

### The physics generators (full 3D state, currently flattened)

- **`sphere_get_x/y/z`** — `src/sphere.c:241-251`. Return `sphere->position.x/y/z` raw. The building blocks for a full-3D snapshot.
- **`sphere_get_normalized`** — `src/sphere.c:265`. FLATTENS to ONE scalar in `[0,1]` by `mode` (0 = PosX `(x − boundary_min_x)/pos_range_x`, `:277-278`; 1 = PosY; 2 = PosZ; 3-6 = velocities/magnitude; `default` → 0.5, `:298-300`). The boundary box is `boundary_min_x/max_x` … (`sphere.h:53-58`), default **±10** (`sphere.c:81-86`). **This is the lossy collapse** the plan unflattens: a 3-axis snapshot normalizes each axis the same way mode 0/1/2 do here, but keeps all three.
- **`nbody_get_normalized`** — `src/perlin.c:433`. Same flattening: `mode` selects Body0 X / Body1 Y / Body2 X / distances / velocities / energy. Raw `state->pos[body][x/y/z]` (`types.h:527`) normalizes by `range = pos_max − pos_min` (`perlin.c:435`); `pos_min/pos_max` default **±10** (`perlin.c:258-259`). The raw axes (`pos[0][0]`, `pos[1][1]`, `pos[2][0]`) are read directly here — a 3-axis snapshot reads `pos[body][0..2]` for one chosen body.
- **`sample_param_range`** — `src/grain.c:201`. The generic single-scalar modulation sampler. Its `RAND_TYPE_NBODY` / `RAND_TYPE_SPHERE` cases (`:261-275`) call the `*_get_normalized` flatteners (`:265` / `:273`), selecting the per-instance `output_mode` (`perlin_state->nbody_output_mode[instance]` / `sphere_output_mode[instance]`). **The 3D path BYPASSES this single-scalar API** — it reads the generator's raw axes directly at snapshot time, because `param_range` carries exactly one scalar by construction.

### Scheduler config fields (where the new mode + source selector live)

- **`scheduler.pan_range`** — `src/types.h:641` (`param_range_t pan_range; // Grain pan range`). The one-source scalar that feeds `grain->pan` via `sample_param_range`. Untouched by spatial; the spatial source bypasses it.
- **`scheduler.pan_mode`** — `src/types.h:687` (`int pan_mode; // 0 = constant-power mono panning, 1 = stereo balance`). A new value **2 = spatial 3D** lives here. The setter `ligase_pan_mode` (`ligase~.c:2608-2616`) currently clamps `m` to `[0,1]` (`:2611`) — widening to `[0,2]` is the one-line change that admits the mode.

### The stereo output bus (the v1 constraint boundary)

- **`x_out_left` / `x_out_right`** — `src/ligase~.c:179-180`, the TWO signal outlets, created in `ligase_new` at `src/ligase~.c:5270-5271` via `outlet_new(&x->x_obj, &s_signal)` (followed only by bang/float/list outlets — so signal outlets are exactly two). Fixed at construction. Binaural fits these two; >2 channels requires adding `outlet_new(&s_signal)` calls here = a class-construction change.
- **`ligase_dsp` `dsp_add` wiring** — `src/ligase~.c:1841`, `dsp_add(ligase_perform, 26, x, sp[0]->s_vec, …)`; `sp[22]` = `out_left` (`:1864`), `sp[23]` = `out_right` (`:1865`) — i.e. `w[24]`/`w[25]` inside `ligase_perform`. To add channels you grow the outlet count, the `dsp_add` arg count (`26`), and every `w[]` index — non-trivial, confirming **multichannel is a v2 concern.**
- **`ligase_process_effects`** — `src/ligase~.c:1529`, the final stereo stage (smear → distortion → moog → clamp), operating in-place on `out_left/out_right`, called at `src/ligase~.c:1719` just before perform returns. The entire chain downstream of grain panning is hard-wired stereo, so a **binaural panner upstream needs no changes here.**

### Spatial-source selector surface (how spatial picks its generator)

- **`ligase_pan_mode`** — `src/ligase~.c:2608`, registered at `src/ligase~.c:5494` (`class_addmethod(ligase_class, (t_method)ligase_pan_mode, gensym("pan_mode"), A_DEFFLOAT, 0)`). Widen its clamp to admit mode 2.
- **`get_param_range_by_name`** — `src/ligase~.c:3962`; the per-name param dispatch (`"pan"` → `&pan_range` at `:3978`). The spatial source selector is NOT a `param_range` (it picks a *generator instance*, not a min/max source), so it gets its own tiny `spatial`/`spatial_source` message rather than riding this table — mirroring how `sphere_mode`/`nbody_mode` (registered at `ligase~.c:5613/5607`) are their own selectors, not `param_range` entries.

## Design

### Overview

Spatial granulation is a **new `pan_mode` (value 2)** plus the machinery to feed it a frozen 3D vector instead of a scalar. Three pieces, all additive except for one required guard-edit at the render site (see render-branch §):

1. **Grain widening + snapshot** — `grain_t` gains a frozen `(pos_x, pos_y, pos_z)` (each in `[-1,1]`) plus two precomputed per-grain gains; `scheduler_trigger_grain` fills them from the selected sim's raw axes when spatial is active.
2. **Render branch** — `scheduler_process` gains a `pan_mode == 2` case that turns the snapshot into an azimuth (and optional distance gain / elevation tilt) and reuses the constant-power `cos/sin` law to accumulate into the two existing channels.
3. **Selector + vec3 helpers** — a `spatial <source> <instance>` message picks which generator (sphere/nbody, instance 0-3) drives position; `sphere_get_normalized_vec3` / `nbody_get_normalized_vec3` return all three axes normalized in one call so the flatten-to-`[-1,1]` logic lives in one place.

### Data structures (`src/types.h`)

**Grain spatial fields** — added next to `grain_t.pan` (`types.h:294`), inside the existing POD struct (`types.h:290-303`). All `float`, zero-init covered by the same path that already zeroes the pool / sets `pan`:

```c
typedef struct grain {
    float position;
    float increment;
    float amplitude;
    float pan;                // EXISTING: scalar pan (0=L, 0.5=C, 1=R) — pan_mode 0/1
    // --- spatial (pan_mode 2): frozen 3D position, snapshotted at trigger, read every sample ---
    float pos_x;              // normalized to [-1,1] via the sim's bounds (L .. R on the listener's axis)
    float pos_y;              // normalized to [-1,1]  (down .. up — elevation)
    float pos_z;              // normalized to [-1,1]  (back .. front — depth)
    float spatial_left_gain;  // precomputed at trigger (constant for the grain's life)
    float spatial_right_gain; // precomputed at trigger  — keeps atan2/sqrt OFF the per-sample loop
    int   envelope_phase;     // EXISTING …
    /* … unchanged: grain_length, active, saw_cycles, saw_depth, splice_start/end, *next … */
} grain_t;
```

`spatial_left_gain`/`spatial_right_gain` are the key CPU decision: the azimuth → constant-power gains are **snapshot-constant** (the grain's frozen position never moves), so we compute them ONCE at trigger and the per-sample loop is two multiplies, identical in cost to the existing scalar branches. (See "CPU honesty" below.) If a per-grain distance gain or elevation tilt is added, it folds into these two gains too (still computed once).

**Spatial source selector on the scheduler** — a small config block next to `pan_mode` (`types.h:687`):

```c
    int pan_mode;            // EXISTING: 0 = mono point, 1 = stereo balance, 2 = spatial 3D (NEW)
    // Spatial source: which physics generator drives the 3D position when pan_mode == 2.
    int spatial_source;      // RAND_TYPE_SPHERE or RAND_TYPE_NBODY (reuse the existing enum values)
    int spatial_instance;    // which of the 4 sim instances (0-3)
    int spatial_nbody_body;  // for nbody: which of the 3 bodies (0-2) supplies the position
    float spatial_width;     // azimuth scaling 0..1 (0 = collapse to center, 1 = full L/R) — GATE A knob
    float spatial_depth_amt; // 0..1 how much pos_z(distance) attenuates level — GATE A knob (default 0 = off in lean v1)
    float spatial_tilt_amt;  // 0..1 how much pos_y(elevation) tilts L/R or tone — GATE A knob (default 0 = off in lean v1)
```

Reusing `RAND_TYPE_SPHERE`/`RAND_TYPE_NBODY` (already in the `rand_type_t` enum, used at `grain.c:261/269`) for `spatial_source` means the selector speaks the same vocabulary as every other modulation source — consistent with the "configured the same way other modulation is" intent. `spatial_width`/`depth_amt`/`tilt_amt` default to a lean **azimuth-only** v1 (width 1, depth 0, tilt 0) so v1 is "pure horizontal placement" and the binaural extras are opt-in knobs (GATE A).

### Initialization (`src/grain.c`, `scheduler_create`)

The scheduler struct is zeroed at create (`memset(sched, 0, sizeof(scheduler_t))`, `grain.c:487`), so `pan_mode = 0`, `spatial_source = 0`, etc. come free — but **0 is a valid instance and `RAND_TYPE_NONE` is a valid (== 0) source value** (`types.h:370`), so set explicit musical defaults right where the other scheduler defaults are set (next to `sched->pan_mode = 0;` at `grain.c:592`):

```c
    sched->pan_mode          = 0;                 // backward compat: mono point source (existing, grain.c:592)
    sched->spatial_source    = RAND_TYPE_SPHERE;  // default driver = bouncing sphere instance 0
    sched->spatial_instance  = 0;
    sched->spatial_nbody_body = 1;                // body 1 = the orbiting body (most musical motion)
    sched->spatial_width     = 1.0f;              // full L/R spread
    sched->spatial_depth_amt = 0.0f;              // lean v1: distance->level OFF (GATE A to enable)
    sched->spatial_tilt_amt  = 0.0f;              // lean v1: elevation tilt OFF (GATE A to enable)
```

The grain pool's `pos_x/pos_y/pos_z`/`spatial_*_gain` are covered by whatever zeroes a fresh grain; they are only *read* when `pan_mode == 2` AND have been *written* at trigger, so an un-snapshotted grain is never spatialized (the mode is set at config time, the snapshot at trigger time — both precede any render read).

### The vec3 helpers (`src/sphere.c` / `src/perlin.c`) — flatten logic in one place

Pure additions; the existing scalar `*_get_normalized` API is untouched (backward compat for every other consumer). Prototypes go in `sphere.h` (already transitively included by `grain.c` via `types.h:7`, so no new `#include` is needed — `grain.c` already calls `sphere_get_normalized`/`sphere_tick`/`sphere_init`) and the nbody header (`perlin.h`, already included by `grain.c:4`).

```c
// sphere.c — all three axes normalized to [-1,1] via the boundary box, in one call.
void sphere_get_normalized_vec3(const sphere_state_t *s, float out[3]) {
    float rx = s->boundary_max_x - s->boundary_min_x;   // default 20 (±10)
    float ry = s->boundary_max_y - s->boundary_min_y;
    float rz = s->boundary_max_z - s->boundary_min_z;
    out[0] = (rx > 1e-6f) ? 2.0f*(s->position.x - s->boundary_min_x)/rx - 1.0f : 0.0f;
    out[1] = (ry > 1e-6f) ? 2.0f*(s->position.y - s->boundary_min_y)/ry - 1.0f : 0.0f;
    out[2] = (rz > 1e-6f) ? 2.0f*(s->position.z - s->boundary_min_z)/rz - 1.0f : 0.0f;
}
```

```c
// perlin.c — one chosen body's position, normalized to [-1,1] via pos_min/pos_max.
// NOTE: the existing nbody_get_normalized takes a NON-const nbody_state_t* (perlin.c:433);
// this read-only helper is const-correct (cleaner) and binds fine to a non-const lvalue.
void nbody_get_normalized_vec3(const nbody_state_t *st, int body, float out[3]) {
    if (body < 0 || body >= NBODY_COUNT) body = 0;       // NBODY_COUNT == 3 (types.h:523)
    float range = st->pos_max - st->pos_min;             // default 20 (±10)
    if (range < 1e-6f) range = 1.0f;
    for (int a = 0; a < 3; a++)
        out[a] = 2.0f*(st->pos[body][a] - st->pos_min)/range - 1.0f;
}
```

These are the **single point of truth** for "sim 3D → normalized `[-1,1]`." They mirror exactly how `sphere_get_normalized` mode 0/1/2 (`sphere.c:277-285`) and `nbody_get_normalized` (`perlin.c:441-451`) normalize a single axis, but output `[-1,1]` (centered, so 0 = center of the box = center of the stereo field) and keep all three axes. They are `const`-correct reads, no state mutation — safe to call from the trigger path.

### The snapshot (`src/grain.c`, `scheduler_trigger_grain` near `grain.c:1040`)

Right where `grain->pan = grain_pan` is set (`:1040`), add the spatial snapshot, gated on the mode so the scalar path is untouched when spatial is off:

```c
    grain->pan = grain_pan;                         // EXISTING (:1040) — scalar path, always set

    // --- SPATIAL snapshot (pan_mode 2): freeze the driving sim's 3D position onto the grain ---
    if (sched->pan_mode == 2) {
        float v[3] = {0.0f, 0.0f, 0.0f};
        if (sched->spatial_source == RAND_TYPE_SPHERE) {
            sphere_get_normalized_vec3(&sched->perlin_state.sphere[sched->spatial_instance], v);
        } else if (sched->spatial_source == RAND_TYPE_NBODY) {
            nbody_get_normalized_vec3(&sched->perlin_state.nbody[sched->spatial_instance],
                                      sched->spatial_nbody_body, v);
        }
        grain->pos_x = v[0];                          // [-1,1]  L .. R
        grain->pos_y = v[1];                          // [-1,1]  down .. up (elevation)
        grain->pos_z = v[2];                          // [-1,1]  back .. front (depth)

        // Precompute the constant-power L/R gains ONCE (azimuth from the horizontal plane x,z).
        // atan2/cos/sin live HERE (trigger thread / once per grain), NOT in the per-sample loop.
        float azimuth = atan2f(grain->pos_x, fmaxf(grain->pos_z + 1.0f, 1e-6f)); // front-biased; see GATE A
        azimuth *= sched->spatial_width;                                          // 0=center, 1=full
        // Map azimuth (roughly [-pi/2, pi/2]) to the pan angle [0, pi/2] the cos/sin law expects:
        float pan01 = 0.5f + 0.5f * (azimuth / 1.5707963267948966f);              // -> [0,1]
        if (pan01 < 0.0f) pan01 = 0.0f; if (pan01 > 1.0f) pan01 = 1.0f;
        float pan_angle = pan01 * 1.5707963267948966f;                            // [0, pi/2]
        float lg = cosf(pan_angle);                                               // SAME law as :1234
        float rg = sinf(pan_angle);                                               // SAME law as :1235

        // Optional distance->level and elevation tilt (lean v1 leaves both at 0 == off):
        if (sched->spatial_depth_amt > 0.0f) {
            // pos_z in [-1,1]: front(+1)=near=louder, back(-1)=far=quieter
            float dist_gain = 1.0f - sched->spatial_depth_amt * (1.0f - (grain->pos_z*0.5f + 0.5f));
            lg *= dist_gain; rg *= dist_gain;
        }
        // (elevation tilt: a small +/- skew or a tone hint — GATE A; folds into lg/rg or a per-grain coef)

        grain->spatial_left_gain  = lg;
        grain->spatial_right_gain = rg;
    }
```

This keeps the **snapshot-at-trigger invariant** (the grain's position and its gains are frozen; the per-sample loop never re-reads the sim) and keeps `atan2f`/`cosf`/`sinf` **off the per-sample path** (computed once per grain, not once per sample). `sched->perlin_state.sphere[…]` / `.nbody[…]` are the existing 4-instance arrays (`types.h:585/577`) the modulation system already drives (`grain.c:265/273`); they are stepped **unconditionally every trigger** by `update_perlin_coords` (called `grain.c:788`, loops `i<4` over all sphere/nbody instances at `:375/:361`), so spatial just reads their freshly-advanced state at grain birth.

> **Thread note (verified).** `scheduler_trigger_grain` is invoked from the audio thread: `ligase_perform` (`ligase~.c:1581`) → `ligase_process_grains` (`:1704`) → `scheduler_trigger_grain` (`:1162/1191/1312`). The snapshot does **no malloc / no lock / no message dispatch** — it reads existing sim state and writes POD fields. `atan2f`/`cosf`/`sinf`/`fmaxf` are pure libm (`<math.h>` already included at `grain.c:8`; `powf`/`floorf`/`cosf`/`sinf` already used per-grain), fine on the audio thread at grain rate (a handful per block, same order as the existing per-grain pitch/saw math). This matches the existing audio-thread-safety rules.

### The render branch (`src/grain.c`, `scheduler_process`, beside the `pan_mode` branches at `grain.c:1223`)

Add `pan_mode == 2` as a sibling of the existing two branches. It is **cheaper than mode 0/1** in the inner loop (no per-sample `cosf`/`sinf` — those moved to the snapshot).

> **One required structural edit (not purely additive at this site).** The code today is `if (sched->pan_mode == 0) { … } else { /* mode 1 */ }` — the mode-1 branch is reached via a **bare `else`** (`grain.c:1240`), so it currently captures *every* `pan_mode != 0`. To carve out mode 2 we must change that bare `else` into `else if (sched->pan_mode == 1)` and add a new trailing `else` for mode 2. The mode-1 **body** (`:1241-1254`) is unchanged; only its guard condition changes. (If the bare `else` is left as-is, `pan_mode 2` silently renders as mode-1 stereo balance — the snapshot gains would be ignored. This is the single place spatial is not a clean append.)

```c
            if (sched->pan_mode == 0) {
                /* EXISTING mono point source (:1223-1239) — body unchanged */
            } else if (sched->pan_mode == 1) {   // was a bare `else` at :1240 — guard ADDED here
                /* EXISTING stereo balance (:1241-1254) — body unchanged */
            } else {  // pan_mode == 2: SPATIAL 3D (binaural placement)
                // Mono point source, placed by the frozen 3D azimuth (gains precomputed at trigger).
                float mono_sample = (sample_left + sample_right) * 0.5f;
                mono_sample *= env_val * grain->amplitude;
                target_left[i]  += mono_sample * grain->spatial_left_gain;
                target_right[i] += mono_sample * grain->spatial_right_gain;
            }
```

`target_left`/`target_right` are the local aliases of `out_left`/`out_right` set at `grain.c:1160-1161`; `env_val`, `sample_left`, `sample_right` are the same in-loop locals the existing branches use. Output is strictly `target_left[i]`/`target_right[i] +=` — **two channels, the Focusrite path unchanged.** Constant-power is preserved by construction (`spatial_left_gain² + spatial_right_gain² = cos² + sin² = 1` before any distance attenuation; distance attenuation, when enabled, is an intentional level cue, not a power leak). The existing mono-sum choice (`(L+R)*0.5`) matches `pan_mode 0` — spatial treats each grain as a point source, which is the right model for "a body at a location." (A stereo-preserving spatial variant — like `pan_mode 1` but with the spatial azimuth as the balance — is a GATE A option.)

### Control surface (`src/ligase~.c`)

**Widen `ligase_pan_mode`** (`ligase~.c:2608-2616`) to admit mode 2:

```c
static void ligase_pan_mode(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m < 0) m = 0;
    if (m > 2) m = 2;                      // was: > 1 (:2611)
    x->scheduler->pan_mode = m;
    post("ligase~: pan mode set to %d (%s)", m,
         m == 0 ? "constant-power mono panning" :
         m == 1 ? "stereo balance" : "spatial 3D (physics-driven)");
}
```

**New `spatial` selector** — picks which generator drives position, the same way `sphere_mode`/`nbody_mode` are their own selectors (registered at `ligase~.c:5613/5607`). Message-config of *which* sim; the position itself stays signal-rate via the snapshot:

```c
// spatial <source> [instance] [body]   e.g.  "spatial sphere 0"  |  "spatial nbody 2 1"
static void ligase_spatial(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc < 1 || argv[0].a_type != A_SYMBOL) { pd_error(x, "ligase~: spatial <sphere|nbody> [instance 0-3] [body 0-2]"); return; }
    const char *src = argv[0].a_w.w_symbol->s_name;
    if      (strcmp(src, "sphere") == 0) x->scheduler->spatial_source = RAND_TYPE_SPHERE;
    else if (strcmp(src, "nbody")  == 0) x->scheduler->spatial_source = RAND_TYPE_NBODY;
    else { pd_error(x, "ligase~: spatial source must be 'sphere' or 'nbody'"); return; }
    if (argc >= 2 && argv[1].a_type == A_FLOAT) { int i = (int)argv[1].a_w.w_float; x->scheduler->spatial_instance  = (i<0)?0:(i>3)?3:i; }
    if (argc >= 3 && argv[2].a_type == A_FLOAT) { int b = (int)argv[2].a_w.w_float; x->scheduler->spatial_nbody_body = (b<0)?0:(b>2)?2:b; }
    post("ligase~: spatial source = %s instance %d (nbody body %d); send 'pan_mode 2' to engage",
         (x->scheduler->spatial_source==RAND_TYPE_SPHERE)?"sphere":"nbody",
         x->scheduler->spatial_instance, x->scheduler->spatial_nbody_body);
}
```

Plus three thin knob setters (`spatial_width`, `spatial_depth`, `spatial_tilt`), each a clamp-and-store on the scheduler field (GATE A decides which ship in v1). Registration goes next to the pan cluster (`ligase~.c:5493-5494`, where `pan`/`pan_mode` are registered):

```c
    class_addmethod(ligase_class, (t_method)ligase_spatial,       gensym("spatial"),       A_GIMME,    0);
    class_addmethod(ligase_class, (t_method)ligase_spatial_width, gensym("spatial_width"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_spatial_depth, gensym("spatial_depth"), A_DEFFLOAT, 0);
    class_addmethod(ligase_class, (t_method)ligase_spatial_tilt,  gensym("spatial_tilt"),  A_DEFFLOAT, 0);
```

### Composition with the existing engine

- **Modulation system:** spatial reads `perlin_state.sphere[]`/`.nbody[]` — the *same* sim instances the modulation system already steps each trigger. Two consumers of one sim is fine (the modulation read is a separate `*_get_normalized` scalar call; spatial's vec3 read is independent). A patch can run `pan_mode 2` driven by `sphere[0]` while *also* pointing `iot` or `pitch` at a different sphere/nbody instance — they don't interfere.
- **Pan / `pan_range`:** untouched. `pan_mode 2` ignores `grain->pan` and `pan_range` entirely (it uses the snapshot gains). Switching back to `pan_mode 0/1` resumes the scalar path with zero residue (and now correctly reaches mode 1 via the explicit `else if` guard rather than the old catch-all `else`).
- **Downstream FX:** the binaural placement happens upstream of `grain_delay`/`constant_power_mix`/`ligase_process_effects` (`ligase~.c:1719`), all of which stay hard stereo — no changes.
- **Multichannel (v2):** see Risks / out-of-scope. The `target_left/right +=` accumulation generalizes to `target_ch[c][i] +=` only after the outlet/`dsp_add`/`w[]` plumbing grows — explicitly out of v1.

### CPU honesty

- **Per-sample cost of `pan_mode 2`: two multiplies + two adds** — strictly *less* than mode 0/1 (which do a `cosf`+`sinf` per sample as written at `:1234-1235`/`:1249-1250`). The trig moved to the snapshot.
- **Per-grain (trigger) cost:** one vec3 read (3 normalizes), one `atan2f`, one `cosf`+`sinf`, a few mults — same order as the existing per-grain `powf` (pitch) and `floorf` (saw). At typical grain densities this is negligible; at extreme densities (hundreds of grains/block) it is bounded by the existing grain-count cap, identical to every other per-grain computation.
- **Net:** v1 spatial is *cheaper* in the hot loop than scalar pan and adds a bounded per-grain trig cost. No new allocation, no new buffers. This is an honestly cheap feature *because* the snapshot invariant lets all the expensive math be per-grain, not per-sample.

## Steps & gates

### GATE A (approval) — owner sign-off (substantial; this is a direction plan)

1. **v1 scope: azimuth-only vs full binaural.** Recommend **lean v1 = azimuth-only** (`spatial_width` live; `spatial_depth_amt`/`spatial_tilt_amt` default 0/off, knobs present but inert), shipping the cleanest "orbiting placement" first, then enabling distance-as-level and elevation-tilt as a fast follow once the placement is ear-confirmed. Confirm vs **full binaural in v1** (distance gain + elevation tilt enabled by default).
2. **Azimuth derivation / front-bias.** The horizontal azimuth is `atan2(x, z)`. A true `atan2(x, z)` maps back-center (z<0, x≈0) to the *rear*, which a 2-speaker stereo field can't render (front/back collapse). Recommend the **front-biased map** in the snapshot (`atan2(x, z+1)`, so the whole orbit stays in the frontal arc and only L/R varies) for honest stereo. Confirm vs **plain `atan2(x, z)`** (treats z as a second L/R-ish axis) or **`asin(x)` pure-x azimuth** (z ignored, simplest). This decides how "3D" the stereo actually feels.
3. **Which axes map to what.** Recommend **x → L/R azimuth, z → depth (level), y → elevation (tilt/tone)**. Confirm vs a different axis convention (e.g. sphere's most-active axis → L/R). Note the sphere/nbody axes have no intrinsic "listener orientation," so this is a taste choice; a `spatial_axes` remap message could be a later nicety.
4. **Point source vs stereo-preserving.** Recommend **point source** (mono-sum then place, like `pan_mode 0`) — it's the physically honest "a body at a location" model. Confirm vs a **stereo-preserving spatial** (place the existing stereo image's balance by azimuth, like `pan_mode 1`).
5. **nbody driver: which body, or all-three?** Recommend **one selectable body** (`spatial_nbody_body`, default 1 = the orbiting body) so the cloud follows one clear trajectory. Confirm vs **per-grain round-robin across the 3 bodies** (richer constellation, but the listener can't "hear" which body) or **center-of-mass**.
6. **Multichannel (>2 outlets): in or out of v1?** Strongly recommend **OUT of v1** (it is a class-construction change: new `outlet_new(&s_signal)` at `ligase~.c:5270`, grow `dsp_add`'s arg count `26` + every `w[]` index at `:1841/1864-1865`, add per-channel accumulation in `scheduler_process`). Confirm v1 = stereo/binaural only; capture quad/Ambisonics as a separate **v2 plan**.
7. **Selector vocabulary.** Recommend a dedicated `spatial <sphere|nbody> [inst] [body]` message + `pan_mode 2` to engage. Confirm vs folding the source choice into `pan_mode` args, or auto-engaging `pan_mode 2` on the first `spatial` message (a convenience, like `smear_pitch_semitones` auto-selecting its mode).
8. **`spatial_width` semantics.** Recommend `spatial_width ∈ [0,1]` scaling the azimuth (0 = collapse to center, 1 = full L/R). Confirm vs allowing >1 (over-spread / hard-pan exaggeration).

### Step 1 → GATE B (types + init, no behavior change)

Add `pos_x/pos_y/pos_z/spatial_left_gain/spatial_right_gain` to `grain_t` (`types.h:294`); add `spatial_source/spatial_instance/spatial_nbody_body/spatial_width/spatial_depth_amt/spatial_tilt_amt` to `scheduler_t` (`types.h:687`); add the vec3-helper prototypes to `sphere.h` and `perlin.h`. Set explicit defaults in `scheduler_create` (next to `grain.c:592`). **GATE:** `make clean && make` warning-free; a fresh object has `pan_mode == 0` and the render loop is unchanged ⇒ byte-identical behavior; no new field is read by any existing path.

### Step 2 → GATE C (vec3 helpers, pure additions)

Implement `sphere_get_normalized_vec3` (`sphere.c`) and `nbody_get_normalized_vec3` (`perlin.c`); leave the scalar `*_get_normalized` untouched. **GATE:** `make clean && make` warning-free; a unit-style headless check (or a temporary debug `post`) confirms, for a known sim state, the vec3 axes match the scalar `*_get_normalized` modes 0/1/2 rescaled to `[-1,1]` (cross-validation against the existing flatten math).

### Step 3 → GATE D (snapshot at trigger)

Add the `pan_mode == 2` snapshot block in `scheduler_trigger_grain` next to `grain->pan = grain_pan` (`grain.c:1040`): read the selected sim's vec3, freeze `pos_x/y/z`, precompute `spatial_left_gain/right_gain` (azimuth → constant-power, per GATE A.2). **GATE:** `make clean && make` warning-free; with `pan_mode != 2` the block is skipped (scalar path bit-identical); a debug `post` of the frozen gains for a few grains shows them tracking the live sim's position (different grains, different placements) and *constant within a grain's life*.

### Step 4 → GATE E (render branch)

Add the `pan_mode == 2` branch in `scheduler_process` (`grain.c:1223`), accumulating `mono_sample * spatial_left_gain / right_gain` into `target_left/right`. **Convert the bare `else` at `grain.c:1240` to `else if (sched->pan_mode == 1)`** and append the new `else` for mode 2 (the mode-1 *body* `:1241-1254` is untouched — only its guard is added; see the render-branch § callout). **GATE:** `make clean && make` warning-free; with `pan_mode 0/1` the existing branches are untouched (mode 1 still reached now via the explicit guard, *not* the old catch-all `else`); with `pan_mode 2` headless capture shows the grain energy splitting L/R per the snapshot (a sim parked hard-x produces hard-L or hard-R; centered produces equal L/R).

### Step 5 → GATE F (control surface)

Widen `ligase_pan_mode` to `[0,2]` (`ligase~.c:2611`); add `ligase_spatial` + the `spatial_width/depth/tilt` setters; register them by the pan cluster (`ligase~.c:5494`). **GATE:** `make clean && make` warning-free; `spatial sphere 0` then `pan_mode 2` engages spatial driven by sphere instance 0; `pan_mode 0` cleanly reverts; out-of-range args clamp/error.

### Step 6 → GATE G (verify, headless + ear-test)

Build; run the acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1`), recording L/R via `writesf~` and measuring per-channel energy / L-R correlation. Confirm the headless criteria; **flag the binaural "does it actually feel like it's orbiting in space" criterion as subjective — it requires an ear-test on the Focusrite + headphones, not a headless metric.** Update the manual's pan section to document `pan_mode 2` / `spatial …` and the snapshot/tempo behavior.

## Acceptance criteria

Headless where possible (`pd -nogui -nosound -stderr -path . <patch>.pd`, record L/R via `writesf~`, measure per-channel RMS and L/R correlation); subjective where noted.

1. **Backward compat (headless).** A patch that never sends `pan_mode 2` / `spatial …` is byte-for-byte identical to today: `pan_mode 0` (mono point) and `pan_mode 1` (stereo balance) produce the exact same L/R as before; the scalar `pan` / `pan_range` path is unchanged. `make clean && make` warning-free; existing pan tests / `test_delay.pd` clean. (Note: this exercises the `else`→`else if` guard conversion — mode 1 must still produce identical output now that it is reached via an explicit guard.)
2. **Hard-axis placement (headless).** Park the sphere so `position.x` sits at the box max (hard-right) — e.g. by configuring/holding the sim — send `spatial sphere 0` + `pan_mode 2`. Captured output has right-channel RMS ≫ left (near hard-pan-right). Mirror for box-min → hard-left. Centered (x≈0) → equal L/R. Verifies the snapshot axis mapping and the render branch.
3. **Constant-power preservation (headless).** With `spatial_depth_amt == 0` (lean v1), sweep the driving body across the box; total power (`left_rms² + right_rms²`) stays ~constant as the placement moves L↔R (within tolerance) — confirming the `cos²+sin²=1` law carries over to spatial.
4. **Snapshot-not-smear (headless).** With a moving sim and a moderate grain rate, individual grains land at *distinct* fixed placements (a spread of L/R ratios across the grain population), and a single grain's placement does not drift over its lifetime — verifiable by driving very long grains and checking each grain's L/R ratio is constant across its envelope. Confirms the freeze-at-trigger invariant (the constellation, not a per-sample re-read).
5. **Source selection + revert (headless).** `spatial nbody 2 1` retargets to nbody instance 2, body 1; `spatial sphere 0` retargets to sphere; `pan_mode 0` fully reverts to scalar pan with no residue. Out-of-range instance/body clamp; bad source name errors without crashing.
6. **Distance / elevation knobs (headless, if shipped in v1 per GATE A.1).** `spatial_depth 1` makes a far (z = back) body audibly quieter than a near (z = front) one (measurable RMS drop); `spatial_tilt …` produces the intended L/R or tonal skew. If GATE A defers these, this AC moves to the fast-follow.
7. **The cloud orbits in space (SUBJECTIVE — ear-test required).** With a live sphere/nbody sim and `pan_mode 2`, the grain cloud is heard as bodies moving through the stereo (binaural) field — placements track the sim's motion, the cloud feels spatial rather than a flat L↔R smear. **This is audio-quality/subjective: it must be confirmed by ear on the owner's Focusrite + headphones, not by a headless metric.** The headless ACs (2-5) establish *correctness*; this AC establishes *the feature actually delivers the intent.*

## Risks / out-of-scope

**Risks**

- **Stereo can't render true 3D (front/back/elevation collapse).** Two speakers/headphone channels carry azimuth (L/R) well, depth as level, elevation barely (a tilt/tone hint at best — not real HRTF). The "FULL 3D trajectory" is honestly **projected** to what stereo can express; AC7 (ear-test) is where this is judged. GATE A.2 (front-bias) exists precisely so the projection doesn't put bodies "behind" a stereo field that can't render behind. True binaural would need an HRTF convolution stage — explicitly out of v1 (a possible v2).
- **Snapshot vs motion granularity.** Position is frozen per grain. Sparse grains (low density) sample the trajectory coarsely — the cloud is a *scatter* of the orbit, not a smooth sweep. This is the intended TidalCycles-like "constellation" model (consistent with how pitch/modulation already snapshot per grain), but a user expecting a continuous pan sweep of a single voice may be surprised. Document the per-grain-snapshot semantics.
- **The render-site guard edit (not a clean append).** Mode 2 requires converting the bare `else` at `grain.c:1240` to `else if (sched->pan_mode == 1)`. If this is missed, `pan_mode 2` silently renders as mode-1 stereo balance (snapshot gains ignored) — caught by AC1 (mode-1 byte-identical) + AC2 (hard-axis placement). The mode-1 body is otherwise untouched.
- **Two consumers of one sim instance.** If the same `sphere[0]` drives both spatial *and* a modulation target, they read the same evolving state — intended and harmless (independent reads), but worth documenting so a user isn't surprised that "the pan and the cutoff move together" when they share a sim.
- **Zero-init footguns.** `spatial_instance == 0` / `spatial_source == 0` are valid values; an un-set `spatial_source` of 0 maps to `RAND_TYPE_NONE` (== 0, `types.h:370`; no sim) → centered placement, not a crash. Mitigated by the explicit `scheduler_create` defaults (`spatial_source = RAND_TYPE_SPHERE`, etc.). The grain's spatial fields are only read under `pan_mode == 2` AND only after a trigger writes them, so a stale grain never spatializes.
- **atan2/trig on the audio thread.** Confined to the per-grain snapshot (bounded by the grain-count cap), never per-sample. Same class as the existing per-grain `powf`/`floorf`. If profiling ever shows it hot at extreme densities, a small azimuth→gain lookup table is a drop-in optimization (not needed for v1).
- **Width >1 / clamp edges.** `spatial_width` and the `pan01` clamp guard against gains outside `[0,1]`; verify the clamps in AC5 so no NaN/over-unity gain reaches the bus. (The `fmaxf(pos_z+1, 1e-6)` guard also keeps `atan2f`'s second arg away from the (0,0) singularity.)

**Out of scope (v1)**

- **Multichannel output (quad / 5.1 / Ambisonics, >2 outlets).** A class-construction change: new `outlet_new(&s_signal)` calls (`ligase~.c:5270`), grow `dsp_add`'s arg count (`26`) and every `w[]` index (`:1841`, `:1864-1865`), per-channel accumulation in `scheduler_process`. Captured as a **v2 plan**; v1 stays within the two existing outlets (GATE A.6).
- **True HRTF binaural** (per-ear impulse-response convolution for real elevation/front-back). v1 is a constant-power azimuth panner with optional level/tone hints — a far cheaper, "binaural-feel" approximation. HRTF is a possible v2.
- **Doppler / velocity-driven pitch from the sim's `vel[]`.** The sims carry velocity (`nbody_state_t.vel`, `types.h:528`; `sphere` velocity getters, `sphere.c:253-262`) — a Doppler shift from radial velocity is a natural extension but not part of spatial *placement*; separate plan.
- **Per-grain axis remap / listener-orientation message** (`spatial_axes …`). A taste-knob nicety once the default mapping is ear-confirmed; not v1.
- **Touching the scalar `pan` / `pan_range` path, the `pan_mode 0/1` bodies, or the downstream stereo FX chain.** Spatial is additive (a new `pan_mode` value + new POD fields + new helpers + a new selector); the only non-additive edit is the render-site `else`→`else if` guard, and the entire `ligase_process_effects` stereo stage is untouched by construction.