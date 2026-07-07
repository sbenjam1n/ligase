# Plan: Harmonic Layer — scale slots, root/rotate modulation, scale morphing, scale/pattern scope taps

**Owner:** SLB
**Date:** 2026-07-06
**Status:** ✅ **DONE + headless-verified (2026-07-06, Seq 85).** All five steps' gates
measured: regression exact after every step; slot-0 defaults **bit-identical** (md5
`a04e437c…` HEAD vs new vs re-run); in-key root wander under a lorenz matrix pin (24/24
integer-semitone changes, 9 roots); `pattern pitch_scale_slot` steps at exact cycle
thirds; D4 blend yields ONLY member pitches (grain + smear, disjoint-scale test);
`scope_tap scale` = the polygon exactly (79 360/79 360 samples on-circle at the root-spun
classes); v1/v4 fixtures import with slot-0/root-0 equivalence; v5 text re-export
byte-stable; binary v3 refused/v4 saves-loads (+1.06 MB ≈ the estimate); Giant Steps demo
= per-slot histograms ⊆ the three transposed triads. As-built notes: pattern_debug also
traces SCALE mode (measurement aid); the 6 new bands are not snapshot state; D4 pick also
covers PATTERN modes; resonator bank follows root but not rotate, and blends keep the
bank on the argmax scale (a per-block re-tuned bank body would flicker); harmonic ranges
default to [0,1] span so pattern attach passes indices 1:1. Owner ear tests remain.
**Tracked in:** `QUEUE.md` §4a (harmonic/notation arc, Seq 80)
**Related:** `Plans/seq_scale_sidecar.md` (the control surface that drives this layer — it
depends on Steps 1–2 here), `docs/modulation_layers.md` (the contract Step 3 extends),
`Plans/pattern_notation.md` (the cycle clock the slot sequencer rides).

> **PROVENANCE (2026-07-06).** Scale today: two `pitch_scale_t` (grain + smear), flat
> semitone lists (≤128), set whole-list by `pitch_scale`/`smear_pitch_scale`; grain samples
> degrees stochastically (`sample_scale_semitones`, PITCH_MODE_SCALE) or steps them by
> pattern (PITCH_MODE_PATTERN, wrap+octave); smear's scale also sizes/tunes the resonator
> bank. Both captured (walker `MF_SCALE` entries, schema v4). NO root/transpose, NO slots,
> NO presets; the scale itself is not a modulation destination — you can modulate which
> degree is picked, never what the scale is. Pattern modulation reaches anything in
> `get_param_range_by_name`; matrix destinations are a separate table. No new outlets
> anywhere in this plan (the class-construction trap does not apply).

---

## Problem

Harmony in ligase~ is static: one scale per destination, changed only by retyping the whole
list. There is no way to move through keys, modes, or chord changes — the thing Coltrane's
circle is *about* — and nothing modulates the scale. Meanwhile the cycle clock, pattern
system, matrix, and capture walker are all sitting there able to drive exactly that, if the
scale becomes addressable state.

## Design

### D1. Scale slots (×16 per destination)

Sixteen scale slots A–P per destination (grain, smear) — owner revision 2026-07-06
("add 12 scale slots"): A–D = the primary row (the axis generator writes here), E–P =
two banks of six for progressions and set-lists. New state: `pitch_scale_t slots[16]` +
active index per `*_pitch_control_t`.

```
pitch_scale_slot <0-15>           select the grain-side active slot
smear_pitch_scale_slot <0-15>     select the smear-side active slot
pitch_scale_to <slot> <semis...>  write a specific slot without selecting it
smear_pitch_scale_to <slot> <semis...>
pitch_scale / smear_pitch_scale   (unchanged) write the ACTIVE slot — full back-compat
```

Memory: 16 slots × 129 floats × 2 destinations ≈ 16.5 KB per snapshot capture
(~1.05 MB across all 64 morph snapshots — small next to the reel; acceptable). The text
export skips empty slots (count 0), so files stay compact.

Slot selection applies at the existing per-block/degree-lookup sites — inherently
click-free (a lookup-table swap, no audio-path discontinuity). Slot 0 initialized from the
legacy scale ⇒ defaults bit-identical.

**`scale_slot` becomes a modulation target** (grain + smear entries in
`get_param_range_by_name` + matrix destination, stepped: value rounds to 0–15). That makes
it pattern-drivable for free (`pattern pitch_scale_slot [ 0 1 2 ]` — chord progressions on
the cycle clock), matrix-drivable (an LFO sweeping slots), and morph-capturable.

### D2. `scale_root` — transposition as a destination

Per-destination semitone offset applied AFTER degree lookup (so the scale shape is
untouched — the polygon rotates):

```
scale_root <semitones>         grain side; float, clamp ±24 [approved R]
smear_scale_root <semitones>   smear side
scale_root_quant <0|1>         1 = round the APPLIED offset to integer semitones
                               (default 1 [approved R]; 0 = free/continuous drift)
```

Both are param_range targets + matrix destinations (`scale_root`, `smear_scale_root`).
Apply-site per `docs/modulation_layers.md`: offset joins at the note→speed / note→Hz
computation, base tracked by `mod_track_base()`, captured as base under active connections.
The Coltrane payoff: `matrix_connect lorenz1 scale_root 4` = the key wanders with the
weather; quantized, it wanders in-key.

### D3. `scale_rotate` — modal interchange as a destination

Integer degree-offset with wrap into the active slot's degree list:

```
scale_rotate <n>               grain side (smear_scale_rotate <n> for smear)
```

Rotating a major scale's degree indexing yields its modes without changing the pitch set.
Stepped modulation target + matrix destination (depth in degrees). Cheap: an index offset
at the two lookup sites (stochastic pick + pattern stepper).

### D4. Scale morphing rule — stochastic source-pick [approved R]

Scales already ride in snapshots; the blend rule becomes DEFINED behavior: during a morph
blend, semitone values are never interpolated (in-between values are out-of-key). Instead,
**each grain picks its degree from one contributing snapshot's scale, with probability
proportional to the kernel weights** (per-grain pick at trigger; smear side picks
per-block). At any cursor position every sounding pitch belongs to one of the placed
scales; the blend is a harmonic crossfade DENSITY — matching SCALE mode's stochastic
character. Slots + root + rotate participate in capture normally (root interpolates as a
scalar — that one IS musical to glide when quantized).
`docs/modulation_layers.md` gains a "scale fields" subsection recording this rule.

### D5. Scope taps: `scale` + `pattern` families

Two new families in the existing tap table (outlets 10/11 REUSED — no class-construction
change):

```
scope_tap scale [grain|smear]   beam steps through the active slot's pitch classes:
                                X = cos(2π·pc/12), Y = sin(2π·pc/12), one degree per
                                generator tick — the scale polygon drawn on the scope
scope_tap pattern <slot>        X = cycle-phase ramp, Y = slot's current value (S/H)
                                — the running pattern as a waveform, steps as plateaus
```

Root/rotate/slot changes are VISIBLE live (the polygon spins / reshapes). Sibling of the
grain constellation; same S/H write path.

### D6. Capture schema v5

Walker grows: 32 scale-slot fields (16×2 dests, `MF_SCALE`; empty slots skipped in text
export), active-slot indices ×2,
root ×2, quant ×2, rotate ×2 (`since = 5`). Text schema v4→v5 with v1–v4 import compat
(the `since` discipline); binary version bump with explicit refusal; v4 files load with
slot-0 semantics = the legacy scale (proven by import test). Exclude-group: all new fields
join the existing pitch-side groups (grain → the grain/pitch group, smear → smear), NOT
`sources` (they are voice, not weather).

## GATE A — owner decisions (ALL cleared at [R], 2026-07-06)

1. ✅ Slot count **16 per destination** (A–P; owner revision 2026-07-06 up from 4 —
   A–D primary + two six-slot banks; the axis generator still writes A/B/C).
2. ✅ `scale_root` clamp **±24 st**, quantize **default ON**.
3. ✅ `scale_rotate` wraps (no octave carry — octave lives in the degree lists).
4. ✅ Morph blend = **stochastic source-pick** (never interpolate semitones).
5. ✅ Scope taps **reuse outlets 10/11** (no new outlets).
6. ✅ Schema **v5** with full back-compat; new fields excluded from `sources` group.

## Steps

1. **Slots + selectors.** State, messages, slot-0 back-compat aliasing, block-rate select
   at both lookup sites. **GATE:** regression at the exact baseline (RMS 0.372309 / buffer
   0.330109); defaults bit-identical (HEAD-vs-new md5 WAV with SCALE mode active on slot 0);
   slot switch mid-note click-free (captured audio, no discontinuity beyond grain
   boundaries).
2. **Root + rotate + modulation plumbing.** Apply sites per the modulation contract;
   param_range names + matrix destinations (stepped for slot/rotate); `mod_track_base`
   audit; capture transparency (SNAP under an active scale_root connection records base).
   **GATE:** `matrix_connect lorenz1 scale_root 4` produces measurable in-key wander
   (captured pitch histogram = scale degrees transposed); `pattern pitch_scale_slot`
   steps slots on the cycle clock (pattern_debug + audible capture).
3. **Blend rule (D4) + contract doc.** Implement the stochastic pick in the blend path;
   update `docs/modulation_layers.md`. **GATE:** a blend between snapshots with disjoint
   scales yields ONLY pitches from the two scales (captured-pitch set membership test);
   cold-edit byte-identity re-proof.
4. **Scope taps (D5).** **GATE:** `scope_tap scale` capture = the polygon (XY points on
   the unit circle at active pitch classes, nothing else); `scope_tap pattern` capture =
   the step silhouette of a known pattern; regression exact; DSP restart clean.
5. **Schema v5 (D6) + docs.** Walker entries, import compat tests (v1–v4 fixture files),
   manual sections (SCALE SLOTS, MODULATING THE SCALE, quick-list), QUEUE close-out.
   **GATE:** v4 export → import on new build → slot-0/root-0 equivalence; new export →
   re-import byte-stable; warning-free build.

## Acceptance criteria

1. Existing patches and captures unaffected (exact regression; v1–v4 files import; slot 0
   IS the old scale).
2. **The Giant Steps demo:** three slots a major third apart + `pattern pitch_scale_slot
   [ 0 1 2 ]` renders periodic key motion, verifiable in captured pitch data (three
   transposed degree-histograms alternating at the cycle rate).
3. Scale root under matrix control moves the whole pitch set, in-key when quantized.
4. A morph blend between different-scale snapshots never sounds an out-of-key pitch.
5. The scale polygon and running patterns are visible on outlets 10/11.

## Out of scope

- The tone-circle/time-circle control surface (`Plans/seq_scale_sidecar.md`).
- Microtonal ratio scales (the semitone-list model stands; a ratio mode is its own plan).
- Named scale presets in the ENGINE (panel-side message bundles, sidecar plan).
- New outlets of any kind.
