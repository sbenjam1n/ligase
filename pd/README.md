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
- **`ligase_seq.pd`** — the **SEQ / SCALE** sidecar (tone circle, time circle,
  pattern grid) as a standalone patch, the same `lg_engine` / `lg_state9` idiom
  as the expander. Embedded in `ligase_panel.pd` as the `[pd seq]` subpatch
  window. See "The SEQ / SCALE sidecar" below.

## Installing in plugdata — the `.plugdata` bundle

`make bundle` builds **`dist/ligase.plugdata`** (deterministic; run it on each
platform whose external you want included — a Mac build adds
`ligase~.pd_darwin` next to `.pd_linux`). Drag the file into plugdata
**standalone** (≥ 0.9.2): it installs as `Patches/ligase/` in the library —
panel + expander + this README + `ligase.conf` + the external(s), which load
from the package's own directory (conf included: `max_grains` is read from
the external's directory). Standalone path only — the plugin (VST/AU) build
needs ligase compiled in (`Plans/vst_plugin.md`). macOS note: Gatekeeper
quarantines *downloaded* unsigned binaries; a locally built bundle is
unaffected, and plugdata itself warns on quarantined installs.

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

## The SEQ / SCALE sidecar (`[pd seq]` / `ligase_seq.pd`)

The harmonic + notation surface (`Plans/seq_scale_sidecar.md`). Coltrane's insight:
**a scale is a polygon on the pitch-class ring; a rhythm is the same polygon on the
cycle ring** — both point-sets you PIN. Layout data + `SEQ_*` bindings live in
`docs/ui/panel_layout.py`; every control keeps its `lgR_<id>` receive symbol. The
message composers use the `[list prepend]`/`[list trim]` idiom the matrix already ships.

- **TONE CIRCLE (cold):** the 12 ring `[tgl]` are pitch classes 0–11. A
  complementary-spigot **prepend cascade** composes the ascending degree list; **ROOT**
  → `scale_root`, **MODE** → `scale_rotate`; **POLYGON PRESET** (MAJ MIN PENT W-T OCTA
  AUG) lights the ring. The circle edits a **cold** buffer — **APPLY** is the only
  realtime touchpoint (the expander rule): it commits `pitch_scale`/`scale_root`/
  `scale_rotate`, routed by **DEST** (GRAIN → `pitch_scale…`, SMEAR → `smear_pitch_scale…`,
  BOTH). Verified: `lgR_seq_ring_* + APPLY` → `snapbuf_from_live` + `snapbuf_get
  pitch_scale` returns the composed scale.
- **SLOTS / AXIS→SLOTS:** A–P select the live slot (`pitch_scale_slot`). **AXIS→SLOTS**
  writes the composed shape into slot 0 (`pitch_scale_to`) and arms the Coltrane cycle
  by **sequencing `scale_root`**: AXIS 3 arms `pattern scale_root [ 0 4 8 ]` — the Giant
  Steps three-key tonic cycle, driven entirely from the panel (REV = retrograde, ALT =
  `<>` alternation form).
- **TIME CIRCLE (euclid):** K × N select a static **euclid preset token** `<v>(k,n)`
  (the DIST-preset idiom; `panel_layout.SEQ_EUCLID_PRESETS`), routed to the **TARGET**
  (EVNT `event grain` / MOD a param / PTCH `pitch` / SMR `smear_pitch`) as
  `pattern <target> <v>(k,n)`. The engine expands the Bjorklund rhythm.
- **PATTERN GRID (8×16):** a pin writes its step at the **VALUE** knob's level (the matrix
  DEPTH-at-pin rule) → `pattern <field> [ v v … ]`. The row target = the **XPNDR PAGE ×
  PARAM** field (one addressing scheme across the product).

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
- **SEQ RING ORDER** (CHRO/5THS/W-T) is a pure display projection: the 12 ring
  toggles are pitch classes regardless of order, so the same mask always sends the
  same scale (verified). Repositioning the pins per-order is a plugdata-visual nicety
  not expressed in vanilla Pd.
- **SEQ AXIS→SLOTS** sequences the axis rotations through `scale_root` (the engine's
  own "the polygon spins with scale_root" model = the Coltrane axis) rather than writing
  transposed degree lists into slots A/B/C. Element-wise list transposition needs
  `vexpr`, which is not compiled into the target vanilla Pd; `scale_root` sequencing is
  the identical musical cycle using registered messages (`pattern` + `scale_root`).
- **SEQ TIME ROT** knob is display-only: the euclid `(k,n)` suffix carries no rotation,
  so ROT rotates the ring visual and keeps its `lgR_` hook but does not alter the sent
  token. An engine euclid-rotation token `(k,n,rot)` would be its own B-item. The euclid
  arm sends only the curated `SEQ_EUCLID_PRESETS` (k,n) pairs (vanilla Pd cannot build a
  comma-bearing atom dynamically).
- **SEQ PATTERN GRID** rows share the one PAGE × PARAM target field, so rows editing the
  same field share its auto-allocated pattern slot (param patterns cap at
  `PATTERN_SLOTS − 2` = 6). The field must be a valid `param_range` name.

## Headless smoke test (what CI runs)

```bash
pkill -9 pd; timeout -s KILL 25 pd -nogui -nosound -stderr -path . \
    -send "; pd quit" pd/ligase_panel.pd    # must print ZERO error lines
```

Scripted control moves are verified through the engine's own readback
(`snapbuf_from_live` + `snapbuf_get`, `matrix_dump`, `morph_state`) — see
`Plans/pd_panel_prototype.md`.
