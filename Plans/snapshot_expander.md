# Plan: Snapshot Expander — an edit-buffer "sidecar module" for stored snapshots

**Owner:** SLB
**Date:** 2026-07-05
**Status:** PLANNED (GATE A — owner decisions below; recommendations flagged [R])
**Tracked in:** `QUEUE.md` §4a
**Related:** `docs/modulation_layers.md` (the layer contract this slots into),
`Plans/morph_metasurface.md` (the snapshot/surface system being exposed),
`docs/ui/ligase_synthi_panel.svg` (the sidecar panel is drawn beside the main surface).

> **PROVENANCE.** Grounded in the morph system as shipped (Seq 59–60) and re-read on
> 2026-07-05: `morph_snapshot_t` (`src/morph.h` — 45 `morph_range_slot_t` bands, the
> scalar array incl. 11 FX-shadow bases, 30 discretes, 2 pitch-scale lists),
> `morph_capture`/`morph_restore`/`morph_collect_scalars` (`src/ligase~.c`), the
> text export/import schema (version 2, **logical field names** — layout-independent),
> and the `morph_state` re-sendable-dump idiom on outlet 9. Line numbers are
> deliberately omitted for `ligase~.c` (matrix v1.5 is landing concurrently);
> anchor by function name.

---

## Problem

Snapshots are currently **write-only**. You can capture, recall, blend, place, and
export them — but you cannot *look inside* one, and you cannot *adjust* one without
recalling it into the live engine, disturbing the sound that is playing, tweaking, and
re-capturing. For live use this is hostile: preparing the next scene means wrecking the
current one.

The owner's concept (2026-07-05): a modular-synth style **expander/sidecar module** that
exposes the parameters and modulation bands captured in snapshots, condensed behind mode
switches and analog selectors (Synthi idiom — a parameter *cursor* plus one reusable edit
cluster, not 100+ knobs). It gives insight into stored snapshots, lets a snapshot be set
up, captured, or adjusted **offline**, and **does not affect realtime modulation until
the snapshot is explicitly assigned and loaded**.

## Design

### The edit buffer (the architectural core)

One additional `morph_snapshot_t` on the ligase object — the **edit buffer** — that is
**never read by `ligase_perform`**. It is not a fifth modulation layer; it is patch
memory's classic *edit buffer*, sitting outside the per-block pipeline entirely:

```
snapshot slots  ──snapbuf_load──▶  EDIT BUFFER  ◀──snapbuf_from_live── live engine
     ▲                                │  ▲
     └────────snapbuf_store───────────┘  │ snapbuf_set / snapbuf_get (field edits)
                                         │
                     live engine ◀──snapbuf_apply── (the ONLY path into realtime)
```

Isolation is structural, not conventional: expander knob turns mutate the buffer only.
The live engine changes on exactly two deliberate acts —

- **`snapbuf_apply`** (panel: ASSIGN) — buffer → live, via the existing `morph_restore`
  path. Precedence-wise identical to a snapshot recall per the layer contract: writes
  bases + bands + discretes, honors the morph selection tree, never touches matrix pins.
- **`snapbuf_store <id>`** (panel: STORE) — buffer → slot. If that slot is placed on the
  surface and is part of an active blend, the morph field changes shape on the next
  block. This is *desired* (reshape a corner of the surface mid-set) and is safe because
  it only ever happens on the explicit STORE, never as a knob side-effect.

### Field addressing — reuse the export schema

The text export/import feature (Seq 60) already solved the hard problem: a complete,
**stable, logical-name enumeration of every capturable field** (bands with their
min/max/enabled/rand_type/rand_instance/base/slew/invert subfields, scalars, discretes,
scale lists), built layout-independent so it survives struct changes. The expander's
get/set API reuses that walker/table verbatim:

- `snapbuf_get <field> [subfield]` → one `outlet_anything` report on outlet 9
  (state out), e.g. `snapbuf amplitude_range min 0.2`.
- `snapbuf_set <field> [subfield] <value…>` → writes the buffer. Accepts the same
  line grammar as the text import, so an exported file's lines can be replayed at the
  buffer directly.
- `snapbuf_dump` → the whole buffer as **re-sendable `snapbuf_set` messages** on
  outlet 9 (the `morph_state` idiom) — this is the "insight" half: a Pd panel can
  populate every display/knob from one dump.

### Message surface (all new selectors; nothing existing changes)

```
snapbuf_load <id>          copy stored snapshot -> buffer
snapbuf_from_live          capture the CURRENT voice -> buffer (morph_capture path —
                           inherits matrix-v1.5 capture transparency: bases, not wobble)
snapbuf_set <field> ...    edit one field/subfield in the buffer
snapbuf_get <field> ...    report one field/subfield on outlet 9
snapbuf_dump               whole buffer as re-sendable snapbuf_set lines on outlet 9
snapbuf_store <id>         buffer -> slot (surface reshapes next block if slot is placed)
snapbuf_apply              buffer -> live engine (the ONLY realtime touchpoint)
snapbuf_clear              re-init buffer to defaults
```

Implementation shape: the handlers live beside the morph cluster in `ligase~.c`; the
field walker is shared with `morph_export`/`morph_import` (factor the existing
per-field table into one iterator both call — **no schema duplication**). The buffer is
plain memory owned by the message thread; `snapbuf_apply`/`snapbuf_store` are the only
points that need the same care the existing recall/capture paths already take.

### Panel (drawn as a sidecar beside the main SVG)

Synthi condensation — a parameter cursor plus one fixed edit cluster reused for
everything:

- **SNAP** LED display + DATA knob + **LOAD** / **FROM LIVE** buttons (slot addressing;
  same amber-LED idiom as the splice selector).
- **PAGE** selector (8-pos): GRAIN / TAPE / DELAY / FILTER / SMEAR / ENV / PITCH / SPACE
  — mirrors the main panel strips. **PARAM** selector (8-pos) within the page.
  PAGE×PARAM = 64 addresses, covering the scalar/discrete space with sensible grouping;
  band-carrying params expose their band through the edit cluster below.
- **VALUE** LED display — the addressed field's stored value (the read-only "insight"
  path works even before any editing).
- **Band edit cluster** (reused for all 45 bands): MIN / MAX / SLEW knobs, SOURCE
  selector (OFF/PERLIN/LORENZ/NBODY/SPHERE/RAND/PATTERN) + INST (1–4), ENABLED and
  INVERT toggles.
- **VALUE knob** — scalars; detented behavior for discretes (env type, delay mode,
  pan mode — the "mode switches and analog selectors" of the concept).
- **STORE** and **ASSIGN** buttons + the cold-edit legend ("edits are cold — live
  engine untouched until ASSIGN").

Pd prototype: the expander is its own canvas speaking only `snapbuf_*` messages and
parsing outlet 9 — it validates the API with zero engine coupling beyond the messages.

## GATE A (approval) — owner decisions ([R] = recommendation)

1. **Audition semantics.** **[R] cold-only v1**: edits are audible only after ASSIGN
   (`snapbuf_apply`). A momentary AUDITION (apply-and-revert on release) is useful but
   violates the "no realtime effect" spec's purist reading and needs a revert snapshot;
   defer to v1.1 as an explicit opt-in. Confirm cold-only vs shipping audition now.
2. **Scale-list editing.** Variable-length pitch scales don't map to a knob. **[R]
   message-edited in v1** (`snapbuf_set pitch_scale 0 4 7 …` — whole-list set, matching
   the import grammar); the DATA-knob + ENTER append pattern (like the splice selector)
   is a v1.1 panel nicety. Confirm.
3. **Compare.** A/B COMPARE (buffer vs live) requires a temporary apply+revert — the
   same machinery as audition. **[R] defer with audition** (they ship together or not
   at all). Confirm.
4. **get/set granularity.** **[R] per-subfield** (`snapbuf_set moog_cutoff_range min
   200`) *plus* the whole-line import-grammar form; per-subfield is what a knob-per-edit
   panel needs. Confirm vs whole-band-only (simpler, panel then rewrites 8 subfields per
   knob turn — noisy).
5. **Does STORE re-place the slot?** If the stored-to slot is on the surface, STORE
   keeps its placement and the blend reshapes next block **[R]**. Confirm vs requiring
   an explicit re-place (safer, but breaks the "reshape the field mid-set" workflow).
6. **Buffer count.** **[R] one buffer.** N buffers add addressing for marginal value
   (the slots themselves are the storage; the buffer is a workbench). Confirm.
7. **Generator params in snapshots (schema v3)?** The modulation *sources* have their own
   message-only params — `noise_freq_1..4` (the per-instance rate scales; note the rate is
   `IOT × scale`, so sources breathe with grain density, and instance *n* drives
   SIN/SAW/SQR/PERL *n* together), `env_follow_ms`, and the physics params
   (`sphere_damping`/`_elasticity`, `nbody_G`/`_damping`/`_epsilon`/`_pump`). **None of
   these are captured by snapshots today** (verified: zero generator fields in
   `morph_snapshot_t`), so a snapshot's motion *character* — how fast perlin_1 wanders —
   is global "weather," not part of the voice: retune a rate for scene B and scene A
   changes retroactively. **[R] YES, capture them** — motion character travels with the
   voice, the expander gains a SOURCES page (the 8-pos PAGE selector grows to 9 or SPACE
   shares), and it rides the export schema as a **version-3 bump**, which is cheapest
   folded into Step 1's walker unification (deciding *after* Step 1 means touching the
   walker twice). The counter-argument (global weather as a feature — one rate knob
   sweeping every scene at once) stays available either way via the morph exclusion tree:
   capture them but `morph_exclude` the sources group to get global behavior back. Confirm.

## Steps (after GATE A)

1. **Field walker unification.** Factor the export/import per-field table into a shared
   iterator (`morph_field_iter` or equivalent); `morph_export`/`morph_import` re-route
   through it. **GATE:** `make clean && make` warning-free; export→import round-trip
   byte-identical to pre-refactor on a populated surface (regression fixture); text
   schema version unchanged.
2. **Buffer + load/from_live/store/clear/apply.** Add the buffer + the five whole-buffer
   verbs. `snapbuf_apply` routes through `morph_restore` (selection tree honored).
   **GATE:** load→apply ≡ `snapshot_recall` (identical engine state, verified via the
   state/query outs); from_live→store→recall round-trips the live voice; zero-touch
   guarantee — a loaded+edited buffer with no apply produces a byte-identical automated
   test procedure run.
3. **get/set/dump.** Per-subfield get/set via the shared walker + `snapbuf_dump` as
   re-sendable lines. **GATE:** set→get echoes exactly; dump→(clear)→replay-dump
   reconstructs the buffer (field-count-complete vs the export schema); invalid
   field/subfield names → `pd_error`, buffer untouched.
4. **Docs + panel.** Manual section (expander concept, cold-edit contract, message
   quick-list, the STORE-reshapes-surface note); `docs/modulation_layers.md` gains the
   edit buffer as the explicitly-offline fourth state-holder; the sidecar panel SVG
   (already drafted alongside this plan) trues up to any GATE-A changes.
   **GATE:** acceptance criteria below all pass; automated test procedure at exact
   baseline; `test_delay.pd` clean.

## Acceptance criteria (headless except where marked)

1. **Insight:** `snapbuf_load 2` + `snapbuf_get moog_cutoff_range min` reports slot 2's
   stored value on outlet 9 while the live engine plays something else, unchanged.
2. **Cold edit:** a full edit session (load, 20+ `snapbuf_set`s across bands/scalars/
   discretes, dump) leaves the automated-test-procedure output byte-identical — the
   live engine never noticed.
3. **Assign:** `snapbuf_apply` lands the edited voice exactly as if it had been
   recalled from a slot (state-out comparison), honoring `morph_exclude`.
4. **Store-reshape:** with slot 1 placed and an active 50/50 blend, `snapbuf_store 1`
   moves the blended parameter to the new midpoint on the next block (measured via the
   query out), while `snapbuf_store` to an *unplaced* slot changes nothing live.
5. **Round-trip:** from_live → store → export; the exported text equals one produced by
   the pre-existing capture path for the same voice (schema untouched).
6. **Workflow (owner, not headless):** prepare a next-scene snapshot on the expander
   panel while performing on the current one — the deliverable feel of the module.

## Risks / notes

- **Schema drift.** The walker unification (Step 1) is what prevents the expander and
  the text schema diverging; do it first, not last.
- **Concurrent land with matrix v1.5.** `snapbuf_from_live` calls `morph_capture`, which
  v1.5 is making modulation-transparent — this plan *depends on* that behavior for its
  "capture the base, not the wobble" claim. Sequence: v1.5 merges first.
- **Not a modulation layer.** The buffer must never be read in `ligase_perform`; any
  future "live buffer preview" feature is the audition decision (GATE A.1), not a
  pipeline change.

## Out of scope (deliberate)

- Audition / A/B compare (GATE A.1/A.3 — v1.1 pair).
- Multiple edit buffers (GATE A.6).
- Editing matrix pins from the expander (pins are global, not snapshot state — layer
  contract R4; a "morphable depth" would be its own plan).
- **Source rates as matrix destinations** (matrix-on-matrix: e.g. envelope follower →
  perlin rate). Mechanically cheap (ordinary per-block dests) and musically potent, but it
  belongs to the modulation-matrix plan's domain, not the expander — captured here only so
  the idea isn't lost.
- Hardware/MIDI surface binding for the expander (the Pd panel is the v1 surface).
