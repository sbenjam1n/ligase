# pd/ — the ligase~ control surface as a WORKING patch

**Generated — do not hand-edit.** Both patches are emitted by
`docs/ui/emit_pd.py` from `docs/ui/panel_layout.py`, the same layout data that
renders the SVG silkscreen (`docs/ui/emit_svg.py` → `docs/ui/ligase_synthi_panel.svg`).
Hand-tweaks go in `panel_layout.py`; regenerate with:

```bash
cd docs/ui && python3 emit_pd.py     # patches
cd docs/ui && python3 gen_panel.py   # silkscreen (byte-identical wrapper)
```

## Files

- **`ligase_panel.pd`** — the instrument. One `[ligase~]`, every panel control
  live: 22 CV knobs → `[pack f 20]`→`[line~]` → their signal inlets (the panel
  runs `headless 0`: the panel IS the hardware and drives *every* inlet), the
  16×22 Presto-Patch pin matrix, SOURCE SHAPE routing (FAMILY×INST → per-family
  selectors), joystick (inlets 23/24, `morph_cursor 1`), splice select, reel
  I/O, snapshot row, and a 10 Hz `[metro 100]` → `get_params` display poll.
  The Snapshot Expander is embedded as the `[pd xpndr]` subpatch window
  (open it for the second-canvas sidecar).
- **`ligase_xpndr.pd`** — the same expander canvas as a standalone patch. It
  speaks only the `lg_engine` / `lg_state9` send–receive buses, so it works
  next to *any* patch that publishes them (`ligase_panel.pd` does: `[r
  lg_engine]` → ligase~ left inlet, outlet 9 → `[s lg_state9]`). Open it
  alongside the panel; don't open it at the same time as the embedded `[pd
  xpndr]` window expecting independent state — they share receive names.

## Opening in plugdata

Open `pd/ligase_panel.pd` in plugdata (or vanilla Pd ≥ 0.54) with the compiled
external on the search path (repo root — `ligase~.pd_linux` / `.pd_darwin`).
All GUI objects are vanilla (`hsl`/`vsl`/`tgl`/`bng`/`nbx`/`hradio`/`cnv`), so
the patch renders natively in plugdata and loads headless in vanilla pd for CI.
DSP starts automatically (`; pd dsp 1` on loadbang). Audio: `[adc~]` → inlets
1/2, outs → `[dac~]` + `[env~]` VU readouts.

**Scope hookup (plugdata):** ligase~ outlets 10/11 are the `scope_x~`/`scope_y~`
taps (default tap: `scope_tap lorenz 1`, sent on load; the TAP radio switches
to `folw`/`grain`). They are left unconnected for headless CI — open `[pd
engine]` and connect an `[oscilloscope~]` (XY mode) to the ligase~ outlets
10/11 for the SCOPE display.

## Scripting / MIDI mapping hook

Every control has a **receive symbol `lgR_<id>`** (ids in
`docs/ui/panel_layout.py`). A message like

```
[; lgR_grainsize 0.5, ; lgR_mx_5_16 1(
```

sets the GUI *and* drives the engine exactly like a hand on the panel — this
is both the headless-test interface and the MIDI-map hook
(`[ctlin 1]` → scale → `[s lgR_cutoff]`). Matrix pins are `lgR_mx_<row>_<col>`
(row = source index, col = destination index, 0-based, in the layout's
`MATRIX_SRCS`/`MATRIX_DSTS` order).

## Presets

Snapshots ARE the preset system (GATE A.7): the 1–8 row selects a slot,
STORE = `snapshot <slot>`, RECALL = `snapshot_recall <slot>`; the morph
surface interpolates between them (joystick = cursor CV; SNAP captures to the
selected slot). The XPNDR edits any snapshot cold (`snapbuf_*`) and commits
with ASSIGN (`snapbuf_apply`). The DISTORTION "PRESET" knob is a separate
8-position message-bundle set (curated in `panel_layout.DIST_PRESETS`).

## Matrix depth policy (GATE A.4)

`PIN_DEPTH` (nbx) × `POL` toggle define the signed depth applied when a pin is
placed: pin ON → `matrix_connect <src> <dest> <±depth>`, OFF →
`matrix_disconnect <src> <dest>`. Re-pin to change a pin's depth.

## Known prototype seams (honest list)

- **Splice display** is a panel-side counter driven by ◀/▶ (`shift ∓1`): the
  engine reports the current splice on the *console* only, not outlet 9 — a
  splice-number state report would be its own B-item. ENTER sends
  `splice_finish_nav <DATA>`; the engine treats that message as the 0/1
  finish-before-nav *flag* (there is no jump-to-splice-N message).
- **MASTER** knob is unwired — it duplicates LEVEL (inlet 21) and two `line~`
  writers on one signal inlet would sum. Use LEVEL.
- **ENV TYPE** positions GAUS/EXP and the SCOPE **VIEW** switch are
  silkscreen-forward: no engine message behind them yet.
- **XPNDR band MIN/MAX/SLEW** sliders are normalized 0–1 (raw field units for
  0–1 params). For wide-range bands (e.g. `moog_cutoff_range`) type into the
  VALUE box paradigm instead, or retune the ranges in `panel_layout.py`.
- **PITCH MODE** panel positions map to engine modes 0/1/3/4/5 (mode 2
  "range" has no panel position).

## Headless smoke test (what CI runs)

```bash
pkill -9 pd; timeout -s KILL 25 pd -nogui -nosound -stderr -path . \
    -send "; pd quit" pd/ligase_panel.pd    # must print ZERO error lines
```

Scripted control moves are verified through the engine's own readback
(`snapbuf_from_live` + `snapbuf_get`, `matrix_dump`, `morph_state`) — see
`Plans/pd_panel_prototype.md`.
