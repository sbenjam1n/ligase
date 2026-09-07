# The ligase~ control surface — every section, what it must do, and how it reaches the engine

_Written 2026-09-07 from the SVG silkscreen (`docs/ui/ligase_synthi_panel.svg`), the live
prototype (https://sbenjam1n.github.io/ligase/), the Pd wiring (`docs/ui/emit_pd.py`), and the
engine's message table (`src/ligase~.c` `ligase_tilde_setup`, 229 selectors). It is the spec
the shared panel brain (`web/ligase_panel_logic.js`) and the plugin (`plugin/`) implement;
`docs/ui/panel_bridge.md` is the wire contract between them._

## 0. How the surface reaches the engine (one model for browser and plugin)

```
                widgets (ligase_controls.js, GENERATED)         surface.set(id, v) / surface.bang(id)
                          │                                                 ▼
                          │                              panel brain (ligase_panel_logic.js)
                          │        bind kinds: inlet · msg · msgmap · toggle · bang · special
                          │                                                 ▼
                          │                                          bridge (per host)
                          │                     ┌───────────────────────────┴───────────────────────────┐
                          │             browser: lgR_<id> → [line~] → inlet          plugin: host parameter (automatable)
                          │                      engine messages → lg_engine                    engine messages → "cmd" state
                          ▼                                                                             │
      readbacks ◄──── status / out9 / vu / scope / print  ◄──── outlet 9, VU, scope taps, console ◄─────┘
```

* **Signal inlets 1-24** (audio L/R + 22 CVs). The panel "IS the hardware": every CV knob drives
  its inlet (engine runs `headless 0`, literal inlet values win). In the plugin each CV is a
  DAW parameter in engine units with a 20 ms glide (the panel's `[line~]`).
* **Messages**: everything else is a typed message on the main inlet (`grainsize 0.25`,
  `matrix_connect lorenz1 moog_cutoff 500`, `pattern pitch [ 0 4 7 ]`). Same text in both hosts.
* **Readbacks**: outlet 9 (`get_params` replies incl. the NEW `splice/reel/playing/recording/
  rec_mode` lines, `snapbuf …` replies, `morph_state`), outlets 10/11 (scope XY taps), the
  output peaks (VU), the console. The plugin adds a direct status struct
  (`src/ligase_status.h`) so LEDs and lamps are exact rather than polled text.
* **Precedence** (`docs/modulation_layers.md`): driving inlet → morph route/cursor → last message;
  `param_range` bands regenerate on top; the matrix sum is added last. A DAW automation lane
  therefore always beats a morph blend on the same parameter.

## 1. Header jacks — IN L / IN R / OUT L / OUT R
Silkscreen only. Browser: `[adc~]`/`[dac~]` (mic opt-in). Plugin: the stereo audio ports.
IN L doubles as the **clock input** (a bang on the main inlet = tempo tick); the plugin
synthesizes those bangs from the DAW transport (CLOCK SOURCE = HOST) at exact beat times.

## 2. A · GRANULAR ENGINE — GRAIN SIZE · START · SPEED · DENSITY · VOICES · LEVEL
| control | engine | units / range | plugin |
|---|---|---|---|
| grainsize | inlet 3 | 0.001–2 s | param (log-ish knob) |
| start | inlet 4 | 0–1 of the splice | param |
| speed | inlet 5 | −4…+4 | param |
| density | inlet 9 (IOT) | 0.001–2 s between onsets | param |
| voices | inlet 10 (maxgrains) | 1–200 | param (int) |
| level | inlet 21 (amplitude) | 0–2 | param; × MIDI velocity when VELOCITY→LEVEL > 0 |
Interaction: continuous knobs, double-click = default, wheel = fine. All six are the GATE A.3
"grain engine 6" DAW lanes.

## 3. B · TAPE REEL / RECORD — ORGANIZE · S.O.S. · REC MODE · RECORD · PLAY · LOOP · SELECT/EXPORT REEL
* organize → inlet 6 (splice select by 0–1); sos → inlet 8 (sound-on-sound mix / VCA).
* **REC MODE** (INPUT / SPLICE / OVRDUB) is panel-side state; **RECORD** ON sends
  `recinput` | `recsplice` | `record 1` by mode, OFF sends `record 0`. (`recinput`/`recsplice`
  START a take; the marker is planted at start so the playing splice keeps playing.)
* **PLAY** toggle → `play 1` / `play 0` (a bare `play` STOPS — never sent). **LOOP/1-SHOT** → `loop 0|1`.
* **SELECT REEL / EXPORT REEL** → `load <path>` / `save <path>` (browser: file picker → MEMFS →
  `load`; plugin: file dialog → path; the reel WAV carries the splice markers as cue points).
* Readback: RECORD/PLAY lamps from status.recording/playing.
* Plugin: recmode, record, play, loop are parameters (record/play automatable = DAW-driven takes).

## 4. C · PLAYHEAD + SPLICE SELECT — MODE · SCAN · QUANTIZE (GRID/AMOUNT) · CLK-ADV · splice LED/DATA/ENTER/◀/▶
* MODE → `playhead 1|2|3` (STATIC / SCAN / CLOCK); scan → inlet 7 (0–8); GRID → `quantize <1|2|4…128>`
  (the brain snaps the knob to the valid power-of-two set; <0.5 sends nothing); AMOUNT → `quant 0-1`;
  CLK-ADV QUANT → `clock_advance_quant 0|1`.
* ◀/▶ → `shift -1` / `shift 1`; **ENTER** jumps to splice DATA: `shift (N − current)` using the
  live splice index (previously a panel-side counter that drifted — the LED now shows the engine's
  real current splice from `get_params → splice <cur> <count>` / the status struct).
* Plugin: playhead, scan, quantize (enum OFF,1/1…1/128), quant, clkadv, RETRIG trigger (`trigger`).

## 5. D · GRAIN DELAY — MODE · TIME/REPS · REGEN/DECAY · TONE/SPACE · MIX · QUANTIZE · STUT! · GLIDE
* MODE → `delay_mode 0|1|2` (DD-4 / BENCINA / STUT). Inlets 11/12/13/14 carry time/feedback/tone/mix
  in DD-4 and Bencina; in STUT the same three inlets map to reps 1–16 (0–10 s linear), reduction
  (0–1) and spacing 1–5000 ms (exponential) — one knob per inlet, all modes.
* GRID/AMOUNT → `delay_quantize` (power-of-two snap) / `delay_quant`; **STUT!** → `stut`; GLIDE → `delay_glide <ms>`.
* Plugin: all as parameters + a STUT trigger parameter (DAW-clocked stutters).

## 6. E · LADDER FILTER + SMEAR / RESONATOR BANK
* CUTOFF/RESONANCE/MIX → inlets 16/17/18 (Moog ladder, 20 Hz–20 kHz log, 0–4, 0–1).
* SMEAR MIX → inlet 15; FREQ/RESON/STAGES/FDBK → `smear_frequency` (Hz) / `smear_resonance`
  (0–0.999) / `smear_stages` (0–48) / `smear_feedback` (±0.99); MODE → `smear_mode 0|1` (SNGL / BANK,
  the bank tuned by the SEQ/SCALE smear scale); BANK MIX (MONITOR strip) → `smear_bank_mix`.

## 7. F · GRAIN ENVELOPE + PITCH/MIDI
* TYPE → `envelope 0..4` (PARA / TRAP / COS / **GAUS / EXP — now real**: the engine accepted only 0–2
  although envelope.c has generated gaussian/exponential tables all along; the handler now takes 3/4).
* SKEW → inlet 20; SAW CYC/DEP → `saw_cycles` / `saw_depth`.
* MIDI NOTE → inlet 19 (CV pitch, 1–127; a `midi` message takes ownership of the grain destination).
* PITCH MODE → `pitch_mode 0|1|3|4|5` (OFF / SEMI / SCALE / MIDI / PATRN; engine mode 2 "range" has no panel slot).
* FINE → `pitch_fine ±50¢`; POLY ×8 → `poly 0|1`; CHORD → `chord 0 4 7` (demo triad).
* **MIDI input (plugin, NEW)**: note on/off → `midi <note> <vel> <ch>` (poly pool with vel-0 note-off;
  channel routing grain ch 1 / smear ch 2, both parameters); CC → parameters through a default
  map (1 dly mix · 7 level · 10 pan · 11 sos · 12 grainsize · 13 density · 14 speed · 15 start ·
  16/17 morph X/Y · 64 play · 71 resonance · 74 cutoff · 91 dly feed · 93 smear mix · 94 filter mix;
  editable via the `midimap` state); pitch bend → `pitch_fine` (±PITCH BEND RANGE cents);
  program change → `snapshot_recall n`; CC 120/123 → all notes off. VELOCITY→LEVEL scales the
  LEVEL inlet by velocity. The browser gets the same via the computer keyboard (a–k, z/x octave)
  and Web MIDI.

## 8. G · DISTORTION + OUTPUT/SPACE
* ON/OFF → `distortion_enable`; EMPHASIS → `dist_emphasis_mode 0|1`; **PRESET 1–8** → a message
  bundle per preset (`DIST_PRESETS`: intensity, waveshaper mode, pregain, blend/drives/poly coefficients).
* PAN → inlet 22; PAN MODE → `pan_mode 0|1|2` (MONO / STEREO / SPATIAL); WIDTH → `spatial_width`;
  SOURCE → `spatial sphere` / `spatial nbody`.

## 9. H · PRESETS / SNAPSHOTS (snapshots ARE the preset system)
32 slot buttons select a slot (silkscreen 1–32 = engine `snapshot 0–31`); STORE → `snapshot <slot>`;
RECALL → `snapshot_recall <slot>`. Slot lamps light from the engine's snapshot mask (plugin) /
the last store (browser). **Slot 63 is reserved** for the plugin's live-voice capture. Program
changes recall slots; the DAW project stores every snapshot body via the text schema (v5).

## 10. PRESTO-PATCH · MODULATION MATRIX (16 sources × 22 destinations)
Pins `mx_<src>_<dst>`: ON → `matrix_connect <src> <dst> <depth × (1 − 2·pol)>`, OFF → `matrix_disconnect`.
Depth/polarity are panel-side policy values (DEPTH 0–4 default 1, POL + / −). Sources: sine1 saw1
square1 perlin1 perlin2 lorenz1 nbody1 sphere1 rand1 pattern0-3 env_l env_r env_mono; destinations:
gdelay gdelay_feed gdelay_tone gdelay_mix moog_cutoff moog_resonance moog_mix smear_frequency
smear_resonance smear_stages smear_feedback scanrate organize sos iot env_skew speed grainsize
grain_start amplitude pan pitch_fine (the last six per-grain). Routings are never captured by
snapshots; the plugin persists them in the message journal.

## 11. SOURCE SHAPE — multi-engine edit (the matrix rows)
FAMILY (SIN SAW SQR PERL LRNZ NBDY SPHR RAND FOLW) × INST (1–4) re-mean RATE/A/B/C/D
(`SHAPE_MEANINGS`): RATE → `noise_freq_<inst>`; A → waveform phase / perlin freq / lorenz σ / nbody G /
sphere damping / env_follow_ms (FOLW); B → saw skew / square PW / lorenz ρ / nbody damping / sphere
elasticity; C → lorenz β / nbody ε / sphere spin; D → nbody pump. KICK (sphere) → `sphere_kick <inst> s s s`
(s = D×5); RESET → `<family>_reset <inst>`; MODE → `nbody_mode` / `sphere_mode`. **NEW**: with the
SCOPE TAP switch on FOLW, a FAMILY/INST change also retargets the scope (`scope_tap <family> <inst>`),
the behaviour `Plans/scope_taps.md` specified but nothing implemented.

## 12. MORPH METASURFACE — JOYSTICK · SNAP · ROUTE RUN · STOP · PAUSE · KERNEL · POWER
The bed is a live canvas: drag = cursor (`joy_x`/`joy_y` = inlets 23/24, `morph_cursor 1`);
SNAP = `snapshot <slot>` + `morph_point <slot> <x> <y>`; drag a point = `morph_point`; double-click =
`morph_unplace` + `snapshot_clear`; shift-click = route waypoints; ROUTE RUN = `morph_route x y rate curve`
legs + `morph_run 1`; STOP/PAUSE; KERNEL → `morph_interp 0|1`; POWER → `morph_power`; BASE rate strip →
`morph_rate`. Plugin: joy_x/joy_y are DAW lanes (the most valuable automation: two floats drive the
whole surface); points/route/power/cursor persist in the "voice" state (`morph_export` text).

## 13. SCOPE — TAP · VIEW · OUT 10/11
XY phosphor display of outlets 10/11 (`scope_x~`/`scope_y~`). TAP → `scope_tap folw` / `scope_tap grain`
(load default `scope_tap lorenz 1`). **VIEW (XY / SWP)** is display-only (**NEW**: SWP draws Y as a
time sweep). Browser: the engine patch windows the taps into `scope_x_arr`/`scope_y_arr` read by the
worklet; plugin: the DSP keeps a 1024-sample XY history read by the UI.

## 14. SEQ / SCALE sidecar — TONE CIRCLE · SLOTS/COMMIT · TIME CIRCLE · PATTERN GRID
* TONE CIRCLE: 12 ring pins compose the ascending pitch-class list; RING ORDER is a display projection;
  ROOT/MODE/AXIS/PRESET edit cold; **APPLY** → `pitch_scale <deg…>` + `scale_root` + `scale_rotate`
  (and `smear_*` twins) routed by DEST (GRAIN / SMEAR / BOTH); PRESET lights the ring per `SEQ_PRESET_PCS`.
* SLOTS A–P → `pitch_scale_slot <i>` (+ smear twin); AXIS→SLOTS → `pitch_scale_to 0 <deg…>` +
  `pattern scale_root [ shifts ]` (REV reverses; ALT>0 → `< … >`); READOUT shows the last send.
* TIME CIRCLE: K×N select a curated euclid preset (`1(k,n)`, default `1(3,8)`); TARGET → `pattern event grain`
  / `pattern moog_cutoff` / `pattern pitch` / `pattern smear_pitch` + token. ROT is visual-only
  (engine seam: the `(k,n)` token has no rotation).
* PATTERN GRID 8×16: each row → `pattern <field> <16 values>` (pin × VALUE), field = PAGE×PARAM
  (XPNDR addressing). Patterns persist in the plugin's journal.

## 15. SNAPSHOT EXPANDER (XPNDR) — SNAPSHOT | VALUE · ADDRESS | MOD BAND · COMMIT | AUDITION · MONITOR
* LOAD → `snapbuf_load <slot>`; FROM LIVE → `snapbuf_from_live`; PAGE×PARAM address a field
  (`XPNDR_FIELDS`) and query `snapbuf_get <field>`; VALUE → `snapbuf_set <field> <v>`; **VALUE LED shows the
  `snapbuf` reply** (was static art); MIN/MAX/SLEW/ENABLED/INVERT/INST → `snapbuf_set <band> <sub> <v>`
  (**NEW**: MIN/MAX are scaled into the field's natural range, `XPNDR_BAND_RANGES`, instead of raw 0–1);
  SOURCE → `snapbuf_set <band> rand_type <code>`; STORE → `snapbuf_store <slot>`; ASSIGN → `snapbuf_apply`;
  AUDITION → `snapbuf_audition 0|1`; A/B → `snapbuf_compare`.
* MONITOR: VU L/R from the output peaks; **MASTER** (was unwired: two `line~` writers on inlet 21 would
  sum) is now a plugin-side output gain parameter; BANK MIX → `smear_bank_mix`.

## 16. State the plugin persists (DAW project / preset)
| key | content |
|---|---|
| parameters | the 63 host parameters (every panel control with an engine binding + transport + plugin settings) |
| `voice` | `morph_export` text: every snapshot body incl. slot 63 = the live voice, points, route, power, cursor |
| `journal` | last value of each settable message not covered by snapshots (matrix, patterns, grids, shapes, timesigs…) |
| `reel` / `reel_path` | the reel as an embedded float WAV with cue-point splices (≤ 60 s) or the last load/save path |
| `panel` | panel-side state (recmode, selected slot, xpndr/seq/shape/matrix policy, morph waypoints) |
| `midimap` | the CC → parameter map |

## 17. Seams: what the surface promised, what was wired, what is wired now
| item | before (Pd panel / web prototype) | now |
|---|---|---|
| current splice LED | panel-side ◀/▶ counter (drifted vs engine) | engine value (`get_params splice`, status struct); ENTER jumps to N |
| MASTER knob | unwired | plugin output gain parameter |
| ENV TYPE GAUS/EXP | silkscreen-forward, no message | `envelope 3/4` accepted by the engine |
| SCOPE VIEW switch | no behaviour | XY / sweep display modes |
| XPNDR MIN/MAX/SLEW | raw 0–1 into Hz/ms fields | scaled into `XPNDR_BAND_RANGES` |
| XPNDR VALUE LED | static "0.42" art | live `snapbuf` reply |
| scope follows FAMILY×INST (FOLW) | specified, not implemented | implemented in the brain |
| morph surface (points/route/rate) | web only; Pd panel could RUN a route it could not build | shared brain; DAW-persistent via `voice` |
| quantize knobs sending invalid values | `quantize 37` = engine error | power-of-two snap |
| MIDI | note messages only, no CC/bend/velocity/program | full MIDI in the plugin; keyboard + Web MIDI in the browser |
| host clock | `[metro]` bangs by hand | DAW transport → beat bangs (exact BPM) |
| state | snapshots in RAM; `morph_save` files by hand | full DAW project state (§16) |
| two instances in one process | `perlin_perm` global + `last_organize` static corrupted each other | per-instance (src fix, regression-exact) |
| Euclid ROT · AXIS→SLOTS literal transposition | engine seams (no `(k,n,rot)` token; sequenced via `scale_root`) | unchanged — engine work, tracked in QUEUE |
