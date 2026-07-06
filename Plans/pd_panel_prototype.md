# Plan: Pd Panel Prototype — the Synthi control surface as a WORKING patch

**Owner:** SLB
**Date:** 2026-07-05
**Status:** IN PROGRESS — **GATE A cleared 2026-07-06** (owner: "continue", following the take-the-recommendations pattern): plugdata dialect; two canvases; generated via emit_pd; DEPTH-knob-at-pin-time matrix policy; headless-0 all-inlets-driven contract; 10 Hz display poll; snapshots ARE the preset system. Note: the layout source of truth is the CURRENT docs/ui layout data (single chassis, SCOPE card, integrated expander column) — it has evolved past this plan's prose.
**Steps 1–4 DONE + headless-verified 2026-07-06 (Seq 76):** `panel_layout.py` (layout AS DATA) + `emit_svg.py` (SVG **byte-identical** to the committed silkscreen) + `emit_pd.py` → `pd/ligase_panel.pd` (3328 objects) + `pd/ligase_xpndr.pd` + `pd/README.md`. All step gates measured: zero-error headless loads, knob→snapbuf readback, matrix pin ↔ matrix_dump round-trip, joystick→morph cursor, XPNDR canvas round-trip, SOURCE SHAPE routing sends, regression at the exact baseline. As-built seams recorded in `pd/README.md` (splice_finish_nav = flag not jump → B-item candidate; MASTER knob unwired by design; GAUS/EXP silkscreen-forward). **Step 5 DONE 2026-07-06 (Seq 78):** `make bundle` → `docs/ui/emit_bundle.py` →
`dist/ligase.plugdata`. Container matched against plugdata's actual loader
(`PluginEditor.cpp installPackage()`, develop): ZIP → top-level package dir(s) moved into
`Patches/`; `meta.json` PatchInfo keys incl. `FolderName: ligase` (overrides the hashed
install slug). GATES: deterministic (rebuild sha256-identical), structural verify
(single `ligase/` top-level, meta.json valid, patches byte-identical to `pd/`, external
present), and an install simulation — extract + load `ligase_panel.pd` from the package
dir: zero errors, external loads from the package, `ligase.conf` honored ("Loaded
max_grains = 200"). Darwin external rides along when built on the Mac (documented in
`pd/README.md`). Remaining: **Step 6** owner hands-on (drag-drop + feel test).
**Tracked in:** `QUEUE.md` §4a (prototyping/UI/VST arc, Seq 71)
**Related:** `docs/ui/gen_panel.py` → `docs/ui/ligase_synthi_panel.svg` (the design this
makes real), `Plans/snapshot_expander.md` (the XPNDR API the sidecar canvas speaks),
`Plans/vst_plugin.md` (the panel becomes the plugin GUI in that plan's v1).

> **PROVENANCE.** The SVG mockup already encodes the complete control surface as
> executable layout code (`gen_panel.py`: strips, knobs with inlet/message bindings,
> switches, the 22×16 matrix, joystick, LED displays, XPNDR cluster). The engine side
> needs nothing new: all 24 signal inlets exist; every panel message
> (`matrix_connect`, `snapbuf_*`, `morph_*`, `splice_finish_nav`, `load`/`save`, the
> SOURCE SHAPE selectors) shipped Seq 61–70; feedback exists on outlet 9 (state/query
> reports, `snapbuf` lines) + audio outs for metering.

---

## Problem

The control surface exists only as a picture. Every knob, pin, and button on
`ligase_synthi_panel.svg` maps to a real inlet or message, but nothing can be *played*.
The prototype closes the loop: a `ligase_panel.pd` (+ `ligase_xpndr.pd`) that IS the
instrument — and, downstream, the GUI shipped inside the plugin (`Plans/vst_plugin.md`).

## Design

### One layout source, two emitters

`gen_panel.py` already contains the panel as data-plus-drawing. Refactor it into:

```
panel_layout.py     the layout AS DATA: strips, controls, bindings
                    (inlet index | message selector | matrix coords), ranges, defaults
emit_svg.py         the current SVG renderer, consuming the data   (output unchanged)
emit_pd.py          NEW: emits ligase_panel.pd + ligase_xpndr.pd from the same data
```

The SVG stays the panel's "silkscreen print"; the `.pd` is the working instrument. They
can never drift because neither hand-maintains the layout. (This is the same
schema-single-source discipline as the morph field walker.)

### The generated patch, per section

- **Knobs → signal inlets**: each `IN n` control emits a GUI object whose value feeds
  `[line~ 20]`-smoothed signal into inlet *n* (the hardware/CV contract; headless 0 —
  see GATE A.5). `MSG` controls emit `[msg <selector> $1]`.
- **Presto-Patch matrix**: a generated 22×16 `[tgl]` grid; pin ON sends
  `matrix_connect <src> <dest> <depth>`, OFF sends `matrix_disconnect`. Depth comes
  from the panel's DEPTH controls at pin time (GATE A.4).
- **Joystick**: plugdata `[slider]` pair (or XY object where available) → inlets 23/24;
  SNAP/ROUTE/KERNEL/POWER as msg/bng.
- **Splice select**: `[nbx]` display + DATA knob + ENTER `[bng]` →
  `splice_finish_nav $1`; ◀/▶ → `shift -1/1`; display updated from the state out.
- **Reel I/O**: SELECT REEL → `[openpanel]` → `load $1`; EXPORT → `[savepanel]` →
  `save $1`.
- **SOURCE SHAPE cluster**: FAMILY×INST radio pair routes the RATE/A–D knobs to the
  right selector per the printed legend (a generated `[route]`/message-mapping layer —
  the per-family meaning table lives in `panel_layout.py`, same source as the legend).
- **XPNDR canvas**: speaks only `snapbuf_*`; populates its displays by parsing outlet 9
  (`snapbuf …` lines from `snapbuf_get`/`snapbuf_dump`). Zero engine coupling beyond
  messages — this canvas doubles as the API's acceptance test.
- **Feedback**: `[env~]` on the outs for the VU; a `[metro]` poll (GATE A.6) refreshes
  the splice display and any value readouts via the existing query/state messages.

## GATE A (approval) — owner decisions ([R] = recommendation)

1. **Dialect.** [R] **plugdata** (`[knb]`, its GUI theming; the owner's environment and
   the VST plan's host). Vanilla-Pd fallback (`[hsl]`-based) only if plugdata objects
   block headless CI — the acceptance tests run the *logic* (message wiring) headless
   with vanilla objects where needed.
2. **Two canvases** (main + XPNDR) [R] vs one giant canvas. Two matches the hardware
   sidecar metaphor and keeps the XPNDR reusable standalone.
3. **Generated vs hand-drawn.** [R] **generated** (`emit_pd.py`). 350+ matrix toggles
   and the SOURCE SHAPE routing layer are only tractable generated; hand-tweaks go in
   `panel_layout.py`, not the artifact.
4. **Matrix pin depth policy.** A `[tgl]` carries no depth. [R] panel **DEPTH knob +
   ± polarity switch** define the depth applied when a pin is placed (re-pin to change
   it); pins remember their depth in the patch state. Confirm vs fixed ±1 depths
   (purest Synthi: white/green pins only) or a right-click-per-pin editor (plugdata
   nicety, later).
5. **Headless contract.** [R] the panel runs **headless 0** with *every* inlet driven
   (the perfect-signal contract the engine was designed for) — the panel is the
   hardware. Confirm vs headless 1 + sparse wiring.
6. **Poll rate for displays.** [R] 10 Hz `[metro 100]` (readable, negligible load).
7. **Patch state persistence.** The panel's own knob positions: [R] rely on plugdata's
   patch save + a PANEL→`snapbuf_from_live`/snapshot workflow rather than inventing a
   parallel panel-preset system (snapshots ARE the preset system).

## Steps

1. **Layout refactor.** Split `gen_panel.py` → `panel_layout.py` + `emit_svg.py`
   (thin wrapper kept at `gen_panel.py` for compatibility). **GATE:** regenerated SVG
   pixel-identical (render + image diff) to the current committed one.
2. **`emit_pd.py` core.** Strips, knobs→inlets (line~ smoothing), MSG controls,
   switches, buttons; main canvas loads with ligase~ instantiated. **GATE:** patch
   loads headless with zero errors; a scripted knob move measurably drives its param
   (query/state readback); automated test procedure untouched (no engine change).
3. **Matrix + joystick + displays + reel I/O.** **GATE:** scripted pin place/remove
   round-trips `matrix_dump`; joystick sweep moves the morph cursor (state readback);
   splice display tracks `shift`; open/save panels wired (headless: message-level test).
4. **XPNDR canvas.** **GATE:** load→edit→get→store→apply round-trip driven entirely
   through the canvas's message wiring, verified against outlet-9 echoes.
5. **`.plugdata` bundle packaging (owner-added 2026-07-06).** A `make bundle` /
   `emit_bundle.py` step producing **`ligase.plugdata`**: the generated panel patches +
   README + `ligase.conf` + the compiled externals (`ligase~.pd_darwin` AND
   `.pd_linux` — Pd loads the platform match; the patch's own directory is on the
   search path). Drag-and-drop installs the whole instrument into plugdata
   **standalone** (0.9.2's bundle/LIBRARY mechanism). Boundary: this does NOT change
   the plugin story — plugdata-as-VST still requires the compiled-in build
   (`Plans/vst_plugin.md` v1). Format note: the bundle's exact container layout must
   be matched against plugdata's loader source / a sample bundle (structural check in
   CI; the drag-drop install is an owner-machine test). Distribution caveat: macOS
   Gatekeeper quarantines unsigned downloaded binaries — personal use unaffected;
   public distribution = signing + the GPL license gate. **GATE:** the bundle builds
   deterministically, its structure matches plugdata's loader expectations, and the
   contained patch set is byte-identical to the emitted ones.
6. **Owner hands-on gate.** plugdata on the Mac: drag-drop the bundle, then the feel
   test (knob ranges, layout ergonomics, matrix workflow). Findings become
   panel-layout tweaks (data edits).

## Acceptance criteria

1. `ligase_panel.pd` + `ligase_xpndr.pd` generate deterministically from
   `panel_layout.py`, load in plugdata with no console errors, and every control is
   live (spot-verified headless per section, in full by the owner).
2. The SVG and the patch are provably the same surface (both emitted from one data
   source; CI check = both emitters run clean from one invocation).
3. No engine changes required (any gap found becomes its own B-item, not a hack in
   the panel).

## Out of scope

- Custom plugdata theming/graphics beyond stock GUI objects (the SVG is the visual
  spec; pixel-matching it in Pd is not the goal — layout parity is).
- The plugin packaging of this panel (`Plans/vst_plugin.md` v1 consumes it).
- New engine features (feedback gaps → B-items).
