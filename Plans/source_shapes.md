# Plan: Source Shapes — settable shape parameters for every modulation generator

**Owner:** SLB
**Date:** 2026-07-05
**Status:** ✅ **DONE + headless-verified (2026-07-05, commit fe90c79).** GATE A taken at
the [R] recommendations per owner. As-built corrections: **28** new scalars (7 params ×
4 instances — the summary said 24, the table was authoritative); square polarity = HIGH
for the final `pw` fraction (duty == pw AND the pw-0.5 default bit-identical — the
"high while phase < pw" wording contradicted the default constraint). See QUEUE Seq 70.
**Tracked in:** `QUEUE.md` §4a
**Related:** `docs/ui/ligase_synthi_panel.svg` (the SOURCE SHAPE multi-engine cluster,
drawn with the new params marked `*`), `docs/modulation_layers.md` (sources are voice
state since schema v3), `Plans/snapshot_expander.md` (the walker these fields join).

> **PROVENANCE.** Grounded 2026-07-05: waveform readouts in `mod_source_value`
> (`src/grain.c` — sine/saw/square all read the shared per-instance
> `waveform_phase[i]`, advanced by `iot_seconds * noise_frequency_scale[i]`);
> lorenz `sigma/rho/beta` fixed at 10 / 28 / 8⁄3 in `src/perlin.c` (struct fields
> exist, **no setters**); sphere sim (`src/sphere.c`) has damping/elasticity/mode/
> reset + the `sphere_kick <inst> <vx> <vy> <vz>` impulse but **no rotation term**;
> the shared 150-entry field walker (`morph_fields[]`, `since`-versioned) makes the
> capture-schema bump mechanical.

---

## Problem

The modulation sources' *shape* is mostly frozen. The nbody orbit params and sphere
damping/elasticity are settable (and snapshot state since schema v3), but:

- **Waveforms** (sine/saw/square ×4) have only rate — no phase offset, no square
  pulse-width, no saw skew. Nine matrix rows whose character can't be touched.
- **Lorenz** σ/ρ/β are struct fields locked at the textbook attractor (10, 28, 8⁄3).
  ρ especially is the musical knob — it moves the system from limit-cycle to full
  chaos — and it is unreachable.
- **Sphere** has no continuous energy/motion control: kick is an impulse event; there
  is no spin. "Orbiting" motion (which also feeds `pan_mode 2` spatial placement)
  can't be dialed in.

The SOURCE SHAPE panel cluster (Seq 69) draws the interface; this plan builds the
params it needs.

## New parameters (all message-set, per instance 1–4, all → capture schema v4)

| Message | Range (clamped) | Default (= current behavior) | Applies to |
|---|---|---|---|
| `waveform_phase <inst> <0-1>` | 0–1 | **0** | phase OFFSET added at readout for sine/saw/square of that instance (shared phase advance untouched) |
| `square_pw <inst> <pw>` | 0.05–0.95 | **0.5** | square readout: high while `phase < pw` |
| `saw_skew <inst> <0-1>` | 0–1 | **0** | saw readout: 0 = ramp up (current), 0.5 = triangle, 1 = ramp down (rise time = skew·period fold) |
| `lorenz_sigma <inst> <v>` | 1–20 | **10** | attractor σ |
| `lorenz_rho <inst> <v>` | 1–60 | **28** | attractor ρ (the chaos knob: ~<24 settles, 28 = classic, higher = wilder) |
| `lorenz_beta <inst> <v>` | 0.5–8 | **8/3** | attractor β |
| `sphere_spin <inst> <rate>` | −10–10 | **0** | rotates the VELOCITY vector about the y-axis by `spin·dt` per update — an energy-neutral curl (speed magnitude preserved → cannot blow up; composes with damping/elasticity/kick; gives circular orbit motion, audible in `pan_mode 2`) |
| `sphere_kick_rand <inst> <strength>` | 0–50 | (event) | convenience: fires `sphere_kick` with a random unit direction × strength — the panel KICK! button + D-knob gesture |

GATE-A decisions, taken at [R]:
1. **Waveform readout-side shaping** (phase/pw/skew modify the *readout*, never the
   shared phase advance) — sine1/saw1/square1 stay phase-locked siblings, and defaults
   are bit-identical to today.
2. **Lorenz = raw σ/ρ/β setters, clamped** (no CHAOS macro in v1 — the panel shows
   three labeled knobs; a macro is sugar that can ride later). Clamps keep the
   integrator bounded with the existing fixed dt; the existing NaN flush stays the
   backstop, and `lorenz_reset` recovers any degenerate orbit.
3. **Spin = velocity-vector rotation** (curl), not position rotation — it cannot fight
   the wall bounces, is energy-neutral by construction, and reads as orbit.

## Capture / persistence (schema v4)

- 28 new scalars (`waveform_phase_1..4`, `square_pw_1..4`, `saw_skew_1..4`,
  `lorenz_sigma/rho/beta_1..4`, `sphere_spin_1..4`) join `morph_snapshot_t`,
  capture/restore (through the setters' clamps), and the shared walker with
  `since = 4`. (`sphere_kick_rand` is an event, not state — not captured.)
- Text schema 3→4; v1–v3 files import with the new fields keeping current values
  (the established `since` mechanism). Binary version 2→3, explicit refusal on
  mismatch as before.
- All new fields belong to the **`sources`** selection-tree group —
  `morph_exclude sources` keeps global-weather behavior in one move, unchanged.

## Steps

1. **Waveform shaping.** Fields on `perlin_state_t` + the three readout tweaks in
   `mod_source_value` + setters. **GATE:** defaults bit-identical (regression gate at
   exact baseline); `waveform_phase 1 0.25` shifts sine1 by 90°; `square_pw 1 0.2`
   duty measurably 20%; `saw_skew 1 0.5` = triangle (measure via a matrix pin on a
   readable dest or modout).
2. **Lorenz setters.** Three setters with clamps + capture. **GATE:** ρ=15 settles
   toward the fixed point (readout variance collapses vs ρ=28); ρ=50 stays finite
   over a long run (clamp + flush hold); defaults unchanged.
3. **Sphere spin + kick_rand.** Curl term in the sphere update + the two selectors.
   **GATE:** spin>0 with damping 0 orbits (x/z readouts quadrature-ish, bounded);
   speed magnitude conserved by the rotation (measure |v| before/after updates with
   damping 1.0/elasticity 1.0); `sphere_kick_rand` moves a resting sphere; spin 0 =
   bit-identical sim.
4. **Schema v4 + docs + panel true-up.** Walker/capture/restore/import-compat; manual
   (SOURCE SHAPE section + quick-list); `docs/modulation_layers.md` sources row;
   panel legend drops the `*` marks. **GATE:** v4 export→import exact; a v3 file
   imports (new fields keep current); capture→recall round-trips a changed
   `saw_skew_2` and `lorenz_rho_1`; regression gate exact; warning-free.

## Acceptance criteria

1. Every new param measurably changes its generator's readout, and its default
   measurably doesn't (regression gate byte-exact with no messages sent).
2. Shape params are snapshot state: changed values travel with capture→recall and
   survive the v4 text round-trip; `morph_exclude sources` leaves them live.
3. The expander addresses them by name (`snapbuf_set saw_skew_2 0.5` etc.) with no
   additional work (the walker provides this — verify, don't assume).
4. Lorenz stays finite at every clamped extreme (soak run, no NaN, non-degenerate
   unless ρ chooses the fixed point — which `lorenz_reset` recovers).
5. Sphere spin composes with spatial: `pan_mode 2` + spin sweeps the stereo field
   periodically (windowed L/R ratio oscillates).

## Out of scope

- CHAOS macro over ρ (sugar; later if the three raw knobs prove unwieldy).
- Per-family (unshared) waveform phase advance — instance phase stays shared by
  design (sine/saw/square *n* are views of one oscillator).
- Matrix-modulatable shape params (source-rates-as-destinations remains parked in
  the matrix plan's domain).
