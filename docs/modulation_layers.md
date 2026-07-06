# Modulation Layers — snapshots, metasurface, and the modulation matrix

**Status:** SHIPPED — modulation-matrix v1.5 (per-grain tier + capture transparency) is
implemented; rules R1–R5 below are the as-built contract. Every claim about the build was
verified against the source (perform ordering, capture reads, matrix apply sites) on
2026-07-05. The manual's MODULATION MATRIX section carries the user-facing version.

---

## 1. The three layers and what each owns

The three systems are not rivals — they hold different kinds of state:

| Layer | Owns | Granularity | Persisted where |
|---|---|---|---|
| **Snapshots / metasurface** (`snapshot`, `morph_*`) | the **voice**: scalar bases, the param_range **bands** (min/max/enabled/generator — the band itself, never its momentary output), discretes, both pitch scales, and — since export schema v3 — the **generator ("sources") params**: `noise_freq_1..4`, `env_follow_ms`, the sphere damping/elasticity and N-body G/damping/epsilon/pump params + both output-mode sets; since schema v4 also the **SOURCE SHAPE params**: `waveform_phase/square_pw/saw_skew_1..4`, `lorenz_sigma/rho/beta_1..4`, `sphere_spin_1..4` (`sphere_kick_rand` is an event, never captured) | recalled/blended per block by the route/joystick | the surface (`.morph` binary / `.txt` export / `morph_state` dump) |
| **param_range** (`param_range`, `rand_type`) | **per-grain texture**: each triggered grain samples its ranges individually | per grain | captured *inside* snapshots as bands |
| **Modulation matrix** (`matrix_connect`) | **per-block motion + input listening**: signed offsets summed on top of whatever base exists, incl. the envelope follower | per block (v1) · per grain at trigger (v1.5) | global — **not** captured by snapshots |

Since schema v3 motion *character* is voice state, not global weather: how fast perlin_2 wanders
travels with the snapshot. The old global behavior — one rate knob sweeping every scene at once —
is one message away: `morph_exclude sources` (the selection tree gates every restore path:
the cursor blend, `snapshot_recall`, and `snapbuf_apply`).

**Pins are physical.** A snapshot deliberately does not capture matrix connections
(`morph_snapshot_t` carries ranges/scalars/discretes/scales — and, since schema v3, the generator
params — but **never pins**). Recalling or morphing a snapshot changes the *sound*; the patch cords
stay in — exactly like the pin board on the hardware the panel mockup is modelled on. Blending
connection topology between snapshots (argmax on pins) would be musically incoherent, so it is
ruled out by design, not omission. (Note the one nuance v3 adds: `env_follow_ms` — the follower's
release *time* — is now voice state, while the follower's *pins* remain physical.)

### The fourth state-holder — the Snapshot Expander's edit buffer (explicitly OFFLINE)

The expander (`snapbuf_*`, see the manual's SNAPSHOT EXPANDER section) adds one more place state
lives: a single `morph_snapshot_t` **edit buffer** on the object. It is *not* a modulation layer —
it sits outside the per-block pipeline entirely and is **never read by `ligase_perform`**. It holds
a voice being inspected or prepared; the live engine changes on exactly two deliberate acts:
`snapbuf_apply` (buffer → live, through the same masked-restore path as a recall) and
`snapbuf_store <id>` (buffer → slot — if that slot is placed on the surface, an active blend
reshapes on the next block; that is the STORE contract, never a knob side-effect). Everything else
(`snapbuf_load`, `snapbuf_from_live`, `snapbuf_set/get/dump`, `snapbuf_clear`) is cold by
construction: the regression gate proves a full edit session leaves the audio byte-identical.

Grain parameters never needed the v1 matrix because they already have per-grain modulation
via param_range, which is the richer mechanism for grain-cloud texture. The v1.5 matrix tier
adds *offsets* to those parameters without replacing the per-grain system.

---

## 2. Precedence — the per-block pipeline

The order inside `ligase_perform` defines precedence. `morph_step` runs **before**
`ligase_update_inlets` (where both the inlets and the matrix apply):

```
morph route / joystick writes the BASE
  → a driving signal inlet overwrites the base    (CV wins over morph — Seq 58 decision)
    → param_range band (if enabled) generates the value for its param
      → matrix sum is ADDED on top                (mod_track_base adopts morph/inlet writes)
        → per-destination clamp → engine setter
```

Consequences, all verified in the source:

1. **Morph and matrix compose instead of fighting.** `mod_track_base()` re-adopts the base
   whenever the field changes underneath it, so a morph route sweeping cutoff 440→880 with an
   LFO pinned to cutoff produces *the sweep with the wobble riding on it*.
2. **Precedence is layered, not override.** Every layer adds to (or regenerates) the base; no
   layer silently disables another. The one pre-existing exception is pitch: an active pitch
   **source** bypasses the speed/frequency ranges (the P1 override rule). v1.5 keeps this: a
   matrix pin on `speed` applies in **all** pitch modes as a fine offset *around* the
   pitch-derived speed — it composes with the override rather than defeating it.
3. **Base precedence** (who sets the number the modulation rides on):
   **driving inlet → morph route/cursor → last message**, in that order per block.

---

## 3. Capture semantics — what SNAP records while everything is moving

| State | Capture reads | Mid-modulation capture is… |
|---|---|---|
| param_range modulation | the **band** (min/max/generator/enabled), never the generator's momentary output | **clean** — recall reproduces the motion, not a frozen phase |
| the 11 opaque-FX params (moog/smear/gdelay) | the **fx_shadow** — written only by user-facing setters, never by the matrix apply | **clean** — a wobbling cutoff snapshots as its knob value |
| self-read transport params (scanrate, organize, sos, iot, env_skew) | the matrix's **tracked base** (`mod_base[dest]`) when a connection actively drives the captured field (scanrate); organize/sos/iot capture message-stored bases the matrix never writes, and env_skew's live field is not a captured scalar | **clean** (was dirty in v1 for scanrate — SNAP baked `base + this block's LFO value`) |
| generator ("sources") params (schema v3; schema v4 adds the SOURCE SHAPE params — waveform phase/pw/skew, lorenz sigma/rho/beta, sphere spin) | the message-set params themselves (`noise_frequency_scale[]`, `env_follow_ms`, the sphere/nbody physics params + output modes, the shape params) — never a simulation's momentary position or the shared LFO phase accumulator | **clean** — restore re-applies through the setters' own clamps, and `env_follow_ms` recomputes the follower coeff |

**v1.5 closed the gap** (rules below): capture reads the matrix's tracked base
(`mod_base[dest]`) whenever a connection is active on that destination, making SNAP
**modulation-transparent everywhere**. The per-grain tier is clean *by construction* because
it never writes the shared fields at all.

---

## 4. Who stops whom

Nobody, implicitly — the layers are independent and only their own controls stop them:

- `morph_stop` / `morph_pause` halt the route. The matrix and ranges keep running.
- `matrix_clear` / `matrix_disconnect` pull pins. The morph and ranges keep running.
- `param_lock <name>` freezes one range. Everything else keeps running.

One deliberate asymmetry: **snapshot recall can flip param_range bands on/off** (band
`enabled` is snapshot state — the snapshot owns the texture), but it never touches pins.
To hand a parameter over to the matrix so that morphing cannot disturb its base, exclude it
from the morph: `morph_exclude moog_cutoff`. The selection tree is the ownership boundary.

---

## 5. The workflow

1. **Design voices.** Knobs and messages set bases; param_range bands set grain texture.
   `snapshot <id>` each voice; place them on the surface (`morph_point`).
2. **Patch motion.** Put pins in: `matrix_connect env_mono moog_cutoff 3000`,
   `matrix_connect perlin1 gdelay 0.8`, `matrix_connect pattern0 sos 0.4`. Pins persist
   across every recall, morph, and route.
3. **Perform.** The joystick/route sweeps between voices *underneath* the live modulation;
   the envelope follower keeps listening to what you play; grain texture morphs because the
   bands themselves interpolate.
4. **Capture in flight.** SNAP during a performance records the base voice — never a
   transient LFO phase, never the route's current blend masquerading as a setting (v1.5).

---

## 6. v1.5 design rules (the implementation contract)

R1. **Per-grain destinations** — `speed`, `grainsize`, `grain_start`, `amplitude`, `pan`,
    `pitch_fine` become matrix destinations. The sum for each is computed once per block
    (sources are per-block) and applied **functionally at grain trigger**: the offset is
    added to the grain's sampled value *after* param_range sampling and *before* the
    existing in-place clamps (`grain_size` [0.01, 2.0], `amplitude` [0, 2.0], `pan` [0, 1],
    speed floor −4.0). Matrix per-grain offsets **never write back to the shared fields**
    (`x->grain_size`, `x->speed`, …) — capture stays clean by construction and the inlets
    keep sole ownership of those fields.

R2. **Speed composes with the pitch override.** The `speed` offset applies in all pitch
    modes, added to the final pitch-derived speed (the plan's verified constraint), then the
    ±4.0 clamp. A pin on `speed` is a detune/drift around the note, not a pitch-source
    bypass.

R3. **Capture transparency.** `morph_capture` (and therefore `snapshot`, blends, and all
    three persistence routes) reads `mod_base[dest]` instead of the live field for any
    destination with an active matrix connection. FX params already capture from the
    fx_shadow; param_range bands are already captured as bands. After R1+R3 there is no
    state anywhere whose capture bakes a modulation transient.

R4. **No topology capture.** Snapshots continue to exclude matrix connections/depths.
    (A future "morphable depth" would be a new, explicit feature — not a side effect.)

R5. **Backward compat.** Zero matrix connections ⇒ every path byte-identical to v1
    (regression gate: automated test procedure at exact baseline, plus per-grain trigger
    path unchanged when no per-grain dest is active).

Panel consequence: the Presto-Patch mockup gains the six grain destination columns once the
engine accepts them (`docs/ui/ligase_synthi_panel.svg`).

---

## 7. Message quick-reference (current + v1.5)

```
matrix_connect <src> <dest> <depth>   depth signed, in the destination's own units
matrix_disconnect <src> <dest>        remove one pin
matrix_clear                          pull all pins
matrix_dump                           list connections
env_follow_ms <ms>                    follower release time (peak detector; snapshot
                                      state since schema v3 — morph_exclude sources
                                      to keep it global)

v1 destinations  (per block): gdelay gdelay_feed gdelay_tone gdelay_mix moog_cutoff
                 moog_resonance moog_mix smear_frequency smear_resonance smear_stages
                 smear_feedback scanrate organize sos iot env_skew modout1-4
v1.5 additions   (per grain): speed grainsize grain_start amplitude pan pitch_fine

Snapshot Expander (all cold except the two marked):
snapbuf_load <id> · snapbuf_from_live · snapbuf_set/get/dump · snapbuf_clear
snapbuf_apply  (buffer -> live; honors the selection tree)      [touches realtime]
snapbuf_store <id>  (buffer -> slot; placed slot reshapes blend) [touches realtime]
```
