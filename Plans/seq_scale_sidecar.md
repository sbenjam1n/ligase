# Plan: SEQ/SCALE Sidecar — tone circle, time circle, pattern grid (the harmonic/notation surface)

**Owner:** SLB
**Date:** 2026-07-06
**Status:** APPROVED — GATE A cleared at ALL recommendations (owner 2026-07-06: "create the
execution plans with all recommendations"; layout symmetry directive applied — see mockup).
Build not started. **Depends on `Plans/harmonic_layer.md` Steps 1–2** (slots, root/rotate);
the circle input itself works against the existing `pitch_scale`/`smear_pitch_scale`
messages, but AXIS→SLOTS and SEQ need the slot engine.
**Mockup:** `docs/ui/ligase_seq_panel.svg` (generator `docs/ui/gen_seq_mockup.py` — a
plan-stage artifact; at Step 1 the layout folds into `panel_layout.py` and the standalone
generator is RETIRED, same discipline as the main panel).
**Tracked in:** `QUEUE.md` §4a (harmonic/notation arc, Seq 80)
**Related:** `Plans/pd_panel_prototype.md` (the emitters this extends),
`Plans/snapshot_expander.md` (the XPNDR sidecar idiom + cold-edit/commit philosophy this
reuses), `Plans/scope_taps.md` (the display the circles echo).

> **PROVENANCE (2026-07-06).** Mini-notation today: full parser (nesting, `<>`
> alternation, Euclid `(k,n)`, `rev`), 8 slots, BPM-locked cycle clock, targets =
> any param_range name, pitch degrees, event kinds, smear pitch. Panel exposure ≈ zero
> (PAT rows in the matrix, the PATRN pitch-mode position, `pitch_pattern_slot` in the
> XPNDR). The layout/emitter pipeline (`panel_layout.py` → emit_svg/emit_pd) already
> generates multi-canvas patches; the XPNDR proved the sidecar pattern (own canvas,
> speaks only `lg_engine`/`lg_state9`). Every control gets an `lgR_<id>` receive symbol =
> the headless test + MIDI-map hook, for free.

---

## Problem

The notation layer and (once `harmonic_layer` lands) the scale layer are the two most
musical systems in ligase~ with no physical surface. Text is the wrong panel idiom. The
unifying insight (Coltrane's tone circle): **a scale is a polygon on a pitch-class ring; a
rhythm is the same polygon on the cycle ring** — both are point-sets on circles, both are
things you PIN. The panel already pins things.

## Design (the mockup is the spec)

One new generated canvas — **`pd/ligase_seq.pd`** — laid out as three mirrored columns
over a full-width grid (all geometry echoes the main panel: the TONE and TIME displays are
**216×216 beds, the joystick/scope module**, top-aligned; matrix-style pins/holes; strips,
knobs, LED bezels verbatim from the shared drawing idiom):

### Column 1 — TONE CIRCLE (scale input)
- **12 pins on a ring** (matrix pin idiom: WHITE pin = pitch class in scale, GREEN pin =
  axis tonic, hole = out). Toggling composes and sends the whole-list scale message.
- **RING ORDER (CHRO / 5THS / W-T)** — a panel-side remap of the 12 positions: chromatic
  (symmetric scales = regular polygons), fifths (diatonic scales = contiguous 7-arcs),
  whole-tone (Coltrane's lens: the two whole-tone families = the ring's halves). Pure
  projection; the mask is the same.
- **ROOT knob** = rotate the polygon = `scale_root` (harmonic_layer D2); **MODE knob** =
  rotate the home degree = `scale_rotate` (D3); **AXIS knob** (1·2·3·4·6) = the symmetry
  divisor — with POLYGON PRESET it generates the limited-transposition (Messiaen) family:
  hexagon = whole-tone, alternating octagon = octatonic, triangle = augmented.
- **POLYGON PRESET (MAJ MIN PENT W-T OCTA AUG)** — named-scale message bundles
  (panel-side, like DIST presets) that also LIGHT the ring.

### Column 3 — TIME CIRCLE (pattern input, the twin)
- The selected slot's cycle drawn on the twin bed: step positions on the ring, onsets as
  pins, the Euclid polygon in phosphor, an amber playhead sweeping once per
  `pattern_cycle`.
- **K / N / ROT knobs** (Euclid pulses/steps/rotation), **TARGET switch
  (EVNT / MOD / PTCH / SMR)**, **SLOT radio (1–8)**.

### Column 2 — SLOTS / COMMIT (the mirror line)
- **SCALE SLOTS A–P (16)** — primary row A–D + two six-slot bank rows E–J / K–P (owner
  revision 2026-07-06) — + **AXIS→SLOTS**: takes the current shape, writes its rotations
  by the AXIS interval into slots A/B/C (`pitch_scale_to …`, harmonic_layer D1) — **the
  Coltrane-changes generator** (AXIS 3 = Giant Steps tonic cycle; AXIS 4 = the
  minor-third/diminished cycle). The banks hold progressions/set-lists; any slot is a
  `pattern pitch_scale_slot` step and a morph-surface citizen.
- **SEQ readout + REV toggle + ALT knob**: arms `pattern pitch_scale_slot [ 0 1 2 ]`;
  REV = retrograde progression, ALT = `<>` alternation depth of the slot string.
- **DEST (GRAIN / SMEAR / BOTH)** routes which destination the circle writes.
- **APPLY** — the circles edit **COLD**; APPLY commits [approved R]. Same philosophy as
  the expander: nothing touches the live voice until the explicit commit. (The readouts
  update live while cold — you always see what WILL be sent.)
- **SENDS readouts** — LED strips showing the exact generated message strings
  ("the knobs write the code"): truth-in-labeling, and the notation tutor.

### Bottom — PATTERN GRID (8 slots × 16 steps, matrix idiom)
- A pin grid writing PLAIN sequences (`pattern <target> [ v v … ]`). **Pin-at-VALUE
  policy** [approved R]: placing a pin writes the step at the VALUE knob's current level —
  the exact DEPTH-at-pin rule the Presto-Patch matrix already taught. Nesting/alternation
  stay in notation (or the ALT knob); the grid is the bread-and-butter step sequencer.
- **SLOT TARGET addressing reuses the XPNDR PAGE×PARAM idiom** [approved R]: a row's
  target = any snapbuf-addressable field, or an event kind via the TARGET switch. One
  addressing scheme across the whole product.
- Quarters marked with dashed rules (matrix per-grain-divider idiom).

### Emission & wiring
- Layout lives in `panel_layout.py` as `SEQ_*` records + a `SEQ_CANVAS` section table;
  `emit_svg.py` gains the sidecar page (emitted alongside the main SVG); `emit_pd.py`
  emits `pd/ligase_seq.pd` — standalone over the `lg_engine`/`lg_state9` buses like the
  XPNDR [approved R: sidecar canvas, no main-panel real estate; the main panel is full].
- **String building in Pd**: the message composers (scale list, Euclid string, slot
  progression) are generated `[list]`-family subpatches (`makefilename`/`list prepend`/
  `list trim`) — the same technique as the matrix depth-appender that already ships.
- The scope-side: the TONE bed's polygon is silkscreen; the LIVE polygon is
  `scope_tap scale` on the real scope (harmonic_layer D5) — display honesty, same as the
  main panel's drawn-Lorenz vs live-scope split.

## GATE A — owner decisions (ALL cleared at [R], 2026-07-06)

1. ✅ **Standalone sidecar canvas** (`pd/ligase_seq.pd`), not main-panel real estate;
   main panel unchanged in v1.
2. ✅ **Cold edit + explicit APPLY** (expander rule); readouts live while cold.
3. ✅ **Pin-at-VALUE** grid policy (the matrix DEPTH-at-pin rule).
4. ✅ **Ring orders CHRO/5THS/W-T** as a pure panel-side remap.
5. ✅ **XPNDR PAGE×PARAM addressing** reused for grid-row targets.
6. ✅ **Readouts show the literal sent strings** (truth in labeling).
7. ✅ Layout symmetry: twin 216×216 beds, mirrored control rows, commit column on the
   mirror line (mockup `docs/ui/ligase_seq_panel.svg` is the geometry spec). Owner
   revision 2026-07-06 applied: chassis condensed 1040→880 (tight gutters) and the slot
   bank grown to 16 (A–D + two rows of six).

## Steps

1. **Layout data + SVG page.** `SEQ_*` records into `panel_layout.py`; `emit_svg.py`
   emits the sidecar page; retire `gen_seq_mockup.py`. **GATE:** emitted sidecar SVG is
   render-equivalent to the approved mockup (image diff); MAIN panel SVG byte-identical
   (untouched); both emitters run clean from one invocation.
2. **`emit_pd.py` sidecar canvas — circles.** Ring toggles (order remap), knobs, presets,
   DEST/APPLY, message composers. **GATE:** patch loads headless zero-error; scripted ring
   edit + APPLY → `snapbuf_get`-verified scale on the engine; ring-order remap verified
   (same mask, three orders, same sent message); readout string equals the sent string for
   a test matrix of states.
3. **Slots / SEQ / axis generator.** A–D, AXIS→SLOTS (needs harmonic_layer Steps 1–2),
   SEQ arm with REV/ALT. **GATE:** the Giant Steps drive: one shape + AXIS 3 + AXIS→SLOTS
   + SEQ → `pattern_debug` shows the slot progression; captured pitch data shows the
   three-key cycle (harmonic_layer AC2 driven ENTIRELY from the panel wiring).
4. **Time circle + pattern grid.** Euclid composer, playhead (poll-driven from
   `pattern_cycle` phase), grid pins → plain sequences, XPNDR-idiom targeting.
   **GATE:** scripted K/N/ROT → generated string matches expected Euclid notation;
   grid round-trip via `pattern_debug`; a grid row drives a param (readback via snapbuf).
5. **Bundle + docs.** `ligase_seq.pd` joins the `.plugdata` bundle; `pd/README.md`
   sidecar section; manual TONE/TIME CIRCLE section; QUEUE close-out. **GATE:** bundle
   deterministic + structural verify; regression exact; owner drag-drop test queued.

## Acceptance criteria

1. A scale can be entered, transposed, moded, and committed entirely from the circle —
   headless-scriptable via `lgR_<id>` symbols, audible in SCALE mode.
2. AXIS→SLOTS + SEQ produces the Giant Steps cycle from three knob gestures.
3. The grid sequences any addressable param without typing notation; the readouts teach
   the notation that WOULD have produced every panel gesture.
4. Main panel and engine regression untouched (exact baseline; main SVG byte-identical
   through Step 1).
5. Owner feel test: the circles are a faster, more musical way to set harmony/rhythm than
   typing — and the sidecar reads as the same instrument (symmetry directive holds).

## Out of scope

- Engine changes (ALL in `Plans/harmonic_layer.md`).
- Full mini-notation text editing on the panel (message boxes in the patch remain the
  power path; the panel generates the canonical subset).
- Microtonal rings (>12 points).
- Main-panel layout changes (a future compact PATTERN strip is a separate data edit).
