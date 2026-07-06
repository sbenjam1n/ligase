# Plan: Scope Taps — mod-source & grain-state visualization outputs

**Owner:** SLB
**Date:** 2026-07-05
**Status:** IN PROGRESS — **GATE A cleared 2026-07-05** (owner: "take the recommendations"): signal outlets 10/11; the tap table as specified; constellation + grainsum both (constellation default); monitor-not-state; default tap `lorenz 1`. Implementation running per Steps 1–4.
**Tracked in:** `QUEUE.md` §4a (prototyping/UI/VST arc)
**Related:** `docs/ui/ligase_synthi_panel.svg` (the SCOPE card, drawn with a real
integrated Lorenz trace), `Plans/pd_panel_prototype.md` (the panel scope = a plugdata
`[oscilloscope~]` fed from these outlets), `Plans/source_shapes.md` (the shapes these
make visible).

> **PROVENANCE (2026-07-05).** The per-axis readouts ALREADY EXIST:
> `lorenz_get_normalized(state, axis)` takes axis 0/1/2; `sphere_get_normalized(sphere,
> mode)` and `nbody_get_normalized(state, mode)` are axis-mode readouts, plus the
> spatial `sphere_get_normalized_vec3`/`nbody_get_normalized_vec3` helpers. Outlets
> today: 2 signal (audio L/R) + 2 bang + 4 float (modout1-4) + 1 list (state) = 9.
> Appending signal outlets is the known class-construction change (`dsp_add` 28→30 and
> the four perform `return (w+N)` bumps — the exact trap caught and documented during
> the morph CV-inlet work). Grain state lives in the scheduler's pool (position,
> envelope phase, amplitude per active grain).

---

## Problem

The modulation sources and the grain cloud are ligase's most *visual* objects — a
Lorenz butterfly, orbiting spheres, n-body constellations, a cloud of enveloped grains
— and none of it can be seen. The owner's concept: route modulation signals, selected
by a switch beside SOURCE SHAPE, to a small oscilloscope display; and add **a new
output signal representing grain state** — something no parameter meter can show.

## Design

### Two new signal outlets: `scope_x~` / `scope_y~` (outlets 10/11, appended)

A per-family **tap table** drives what they carry, selected by message:

```
scope_tap <family> [inst]     sine|saw|square|perlin|rand|folw -> Y = readout, X = internal sweep ramp
                              lorenz [inst]  -> X = x-axis, Y = z-axis  (the butterfly)
                              nbody  [inst]  -> selected body X = x, Y = z  (orbits)
                              sphere [inst]  -> X = x, Y = z  (bounces, spin orbits, kicks)
scope_tap grain               the GRAIN CONSTELLATION (below)
```

The panel's SOURCE SHAPE FAMILY×INST selector doubles as the scope source in FOLW
mode (the panel sends `scope_tap` on selector change — pure UI wiring, no engine
coupling). Generator taps are sample-and-hold at audio rate (updated each generator
tick) — crisp on a scope, zero interpolation cost.

### The grain constellation (the new representation)

`scope_tap grain` scans the scheduler's **active grain pool round-robin, one grain per
sample** (a vector-display refresh, Vectrex-style):

```
X = the grain's current position within its splice, normalized to ±1
Y = its envelope value × amplitude (the grain's instantaneous loudness)
```

On an XY scope the whole granular engine becomes visible at once: a cloud of dots
whose horizontal spread IS the grain-position scatter (organize/grainstart/spray),
whose height IS the envelope shape, whose density IS voices/IOT — and it *breathes*
with the music. Chords (poly), position modulation, one-shot tails, and stolen voices
all have distinct signatures. No grains active → the beam parks at center (0,0).
Cost: one pool index increment + two field reads per sample.

### Panel (already drawn, Seq 72)

The SCOPE card sits directly under SOURCE SHAPE (the former messages slot): 202×148
phosphor display (the mockup shows a genuinely integrated Lorenz x/z trace), TAP
switch (FOLW / GRAIN), VIEW switch (XY / SWEEP), and the outlet badges. In the Pd
prototype the display is plugdata's `[oscilloscope~]` fed from outlets 10/11; the
same outlets serve any external scope (or hardware, via DC-coupled interface).

## GATE A (approval) — owner decisions ([R] = recommendation)

1. **Signal outlets vs modout floats.** [R] **two new signal outlets** (true
   audio-rate XY, works with `[oscilloscope~]`/hardware scopes; block-rate floats
   would cap the constellation at ~750 dots/s and can't draw it). Appended as outlets
   10/11 so every existing patch is untouched. Confirm.
2. **Tap table** as above (chaos/physics = x/z plane; waveforms = readout + sweep
   ramp). [R] confirm; axis pairs are one-line changes later if a different plane
   reads better on hardware.
3. **Grain representation.** [R] the **round-robin constellation** (position × env,
   one grain per sample). Alternative/additional: `scope_tap grainsum` — Y = the
   summed envelope profile as a sweep (the cloud's amplitude silhouette) — cheap
   enough to include as a second grain tap [R] include both, constellation default.
4. **Is the scope routing voice state?** [R] **no** — it is a monitor, like the
   matrix pins ("pins are physical"); not captured by snapshots, not morphed.
5. **Default tap.** [R] `lorenz 1` (the butterfly is the best power-on demo and
   matches the drawn panel).

## Steps

1. **Outlets + plumbing.** Two `outlet_new(&s_signal)` appended; `dsp_add` 28→30;
   ALL FOUR perform `return (w+29)` → `(w+31)` (the documented crash trap); scope
   buffers written in `ligase_perform`. **GATE:** build warning-free; regression gate
   at the exact baseline (existing outlets/indices untouched); DSP starts clean
   (the missed-return crash class explicitly re-checked).
2. **Tap table + `scope_tap` message.** Generator taps via the existing per-axis
   readouts (S/H per tick); sweep ramp for 1D families. **GATE:** headless captures
   of `scope_x~/scope_y~` via `writesf~`: lorenz XY trace is the attractor (x/z
   scatter matches integration), sine sweep is a sine, sphere-with-spin XY is an
   orbit (quadrature-ish x/z).
3. **Grain taps.** Constellation scan + grainsum sweep. **GATE:** with 3-voice poly
   + wide grainstart range, the constellation capture shows the expected position
   spread and env heights (histogram of X matches the configured range; Y ≤ amp);
   silence → (0,0); no pool locking added (single-reader perform-thread scan of
   perform-owned state).
4. **Docs + panel true-up.** Manual SCOPE section + quick-list; panel drops the
   "(GATE A)" mark. **GATE:** regression exact; `test_delay.pd` clean; warning-free.

## Acceptance criteria

1. Existing patches unaffected (outlets appended; exact-baseline regression).
2. Each tap's capture is the right shape (attractor / orbit / waveform / sweep).
3. The constellation visibly encodes grain state: position spread, envelope height,
   density; poly chords show multiple position clusters when splice-transposed reads
   differ; empty pool parks at center.
4. CPU delta negligible (S/H writes + one pool scan index per sample).
5. Owner: the scope, fed from outlets 10/11 into plugdata's `[oscilloscope~]`, is
   genuinely useful while dialing SOURCE SHAPE (the feel test).

## Out of scope

- Drawing the scope inside the external (no GUI code in ligase~ — the display is the
  host's; ligase provides honest signals).
- Spectrum/FFT views (a panel-side nicety later).
- Per-grain pitch/pan encodings on Z/brightness (no third channel; revisit if a
  blanking/Z outlet is ever wanted).
