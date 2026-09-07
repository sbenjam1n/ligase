# RESUME.md — ligase~ session handoff

_Snapshot for picking work back up. Authoritative changelog lives in `QUEUE.md` (§6);
this is the "where we are / how to continue" digest._

## Where we are (2026-09-07, Queue Seq 102) — QUEUE RESET; the plugin arc is delivered

- **Branch state:** everything up to Seq 101 is on `main` (PR #24). This session's work is on
  **`claude/ligase-vst-audio-midi-ebh9sf`**, pushed, **unmerged** (owner PR = queue item P3).
  Merging triggers `web-deploy` (the unified panel brain goes live on Pages) and the new
  `plugin-build` workflow (Linux + macOS plugin artifacts).
- **The queue was reset** (`QUEUE.md` §1): P1 owner DAW hands-on · P2 owner ear-test of the
  DIST-ON panel default · P3 merge PR · P4 the two SEQ engine seams (Euclid ROT token, per-slot
  transposition) · P5 plugin polish backlog. Every earlier B/M item is closed history.

### What shipped this session
1. **ligase~ as a native DAW plugin** — `plugin/` (see `docs/plugin_build.md`):
   VST3 / VST2 / CLAP / LV2 / JACK standalone (+ AU on macOS) via the **DPF** submodule
   (ISC + its own VST3 ABI ⇒ the combined work stays GPL-2.0-only; the plugdata-fork route of
   `Plans/vst_plugin.md` v1 was not taken and its licence gate is moot).
   - `plugin/pdshim.c` = the m_pd.h subset (class/method table, inlets/outlets, dsp_add,
     post/pd_error, gensym, canvas path helpers, logical clock): **`src/ligase~.c` compiles
     UNMODIFIED**; its 229 `class_addmethod` selectors are the plugin's control vocabulary.
   - `plugin/ligase_engine.c` = the host facade (messages typed/text, CV inlets with the panel's
     20 ms glide, 64-frame inner blocks, status readback).
   - **Identity gate** `make -C plugin/tests`: reproduces `AUTOMATED_TEST_PROCEDURE.md` with Pd's
     own `noise~` sequence and scheduler timing — the saved reel is **byte-identical** to the
     native Pd reel (RMS 0.372309 / 132288 frames / playback buffer 0.330109).
   - Audio in (recording / SOS monitor), MIDI in (note on/off → chordal poly, CC map, pitch bend
     → `pitch_fine`, program change → `snapshot_recall`), DAW transport → exact-time clock bangs,
     **63 host parameters** generated from `panel_layout.py` (`docs/ui/emit_plugin.py`, the fifth
     emitter; engine units; log cutoff; enumerated grids), full project state (voice = morph_export
     text incl. reserved live slot **63** · message journal · embedded reel WAV ≤ 60 s / reel path ·
     panel state · CC map), 0 latency at 64-multiple host blocks (64 otherwise), web-view GUI.
   - **CLAP host test** `make -C plugin/tests clap`: loads the built binary, records noise through
     the audio input, plays back, MIDI transposes grains, CC74 → cutoff, LEVEL automation, state
     save/load into a second instance, latency reporting — PASS; valgrind clean; builds warning-free.
2. **One control surface, two hosts** — `docs/ui/panel_bridge.md` (the contract) and
   `docs/ui/ui_sections.md` (every section → controls → interaction → engine interface → seams).
   `web/ligase_panel_logic.js` is the hand-written **panel brain** (every `bind` kind and every
   `special` ported from `emit_pd.py`, node-tested by `web/test_panel_logic.mjs`); it runs over
   `webBridge` in the browser (lgR_/lg_engine + outlet-9 message/list hooks, keyboard + Web MIDI)
   and over `plugin/ui/bridge.js` in the plugin (parameters + the transient `cmd` state + base64
   live channels). Seams closed: real splice LED/ENTER, MASTER, ENV GAUS/EXP, SCOPE VIEW, XPNDR
   band scaling + VALUE LED, scope-follows-FAMILY, quantize snap. Verified: 32/32 node tests;
   browser prototype headless 32/32 with the real WASM engine (661 widgets, 0 errors); plugin page
   headless 16/16 with a mocked DPF bridge (0 errors); SVG identity gate empty; regenerated Pd panel
   loads with 0 errors.
3. **Engine fixes in `src/`** (regression-exact — reel byte-identical, 14/14 acceptance):
   per-instance Perlin table (was a process global, corrupting a second instance), per-object
   organize jitter filter (was a function static), `envelope 3/4` accepted, `get_params` now
   reports `splice/reel/playing/recording/rec_mode`, `src/ligase_status.h` host status API.

## What ligase~ is
- Pure Data granular synth / sampler / looper / delay external. C, **GPL-2-only**. Repo `sbenjam1n/ligase`.
- **Hardware-synth PROTOTYPE** → every parameter signal/CV-driven via its inlet where one exists;
  message-only params matrix-reachable. Now also a DAW plugin with the same engine.
- Owner runs **plugdata 0.9.2 on an Intel Mac** + a Focusrite. Cloud sessions run Linux.

## Working conventions (carry these — they bit us when ignored)
- **`panel_layout.py` is the single source** for FIVE emitters: `emit_svg.py` (silkscreen — SVG
  identity is a hard gate), `emit_pd.py` (Pd panel), `emit_bundle.py`, `emit_web.py` (widgets),
  `emit_plugin.py` (host parameters → `plugin/ligase_params.h` + `plugin/ui/params.json`, both
  committed). Change the data, re-run the emitters; never hand-edit generated artifacts.
- **Two identity gates now**: `AUTOMATED_TEST_PROCEDURE.md` for the Pd external AND
  `make -C plugin/tests` for the hosted engine (byte-identical reel). Run both after any `src/` change.
- **Panel brain edits** go in `web/ligase_panel_logic.js` (browser + plugin share it); run
  `node web/test_panel_logic.mjs`; the plugin page is rebuilt by `make -C plugin ui`
  (`plugin/ui/build_page.py` inlines SVG + widgets + brain + bridge into one `index.html`).
- **DPF is a git submodule** (`plugin/dpf`, pinned; `git submodule update --init --recursive`).
  The DGL stub must see `USE_WEB_VIEW=true` (exported by `plugin/Makefile`).
- **Snapshot slot 63 is reserved** for the plugin's live-voice capture — never expose it on the panel.
- Verify agent work independently; re-run the actual behaviour, not the report.
- Commits end with the `Co-Authored-By: Claude Fable 5.1` + session trailers.
- **Do NOT regenerate the PDF.** `docs/ligase_manual.md` is source of truth.
- plugdata caches the external: a new build needs a full plugdata quit+relaunch.
- After pulling, `make clean && make` once (header deps via `-MMD -MP`).
- Read `docs/modulation_layers.md` before modulation/capture/morph work.
- Class-construction trap: appending signal inlets/outlets needs the dsp_add arg growth + ALL FOUR
  perform `return (w+N)` bumps (current: dsp_add 30, returns `(w+31)`, outlets 0-11) — and the
  facade's `NSIG` in `plugin/ligase_engine.c`.
- QUEUE discipline: bump Queue Seq + one-line §6 entry on any §1/§4a change.

## Build & headless-test recipe (Linux cloud session)
- Pd external: `sudo apt-get install -y puredata sox`; `make`; regression = `AUTOMATED_TEST_PROCEDURE.md`
  (test_auto.pd → RMS **0.372309** / max 0.608839; test_playback.pd → **0.330109**); suites:
  `bash tests/run_acceptance.sh` (14 checks; primase skipped without the external).
- Plugin: `git submodule update --init --recursive`; deps `libx11-dev libxext-dev libxrandr-dev
  libxcursor-dev libdbus-1-dev libgl-dev`; `make -C plugin/tests` (gate); `make -C plugin -j8`
  (→ `plugin/bin/`); `make -C plugin/tests clap` (host test). GUI on Linux needs webkit2gtk at runtime.
- Web: `python3 docs/ui/emit_web.py` → `web/ligase_controls.js`; `node web/test_panel_logic.mjs`;
  headless render with Playwright (`NODE_PATH=/opt/node22/lib/node_modules`, Chromium at
  `/opt/pw-browsers/chromium-*/chrome-linux/chrome`) — serve `web/` + the SVG + `pd/ligase_panel.pd`
  (+ the deployed `ligase_wasm.js/.wasm` for the real engine; emsdk is not installed locally).
- pd hygiene: `pkill -9 pd` before runs; `timeout -s KILL`; patches self-quit; **bare `play` STOPS —
  use `play 1`**; `query` returns 0 for unmodulated scalars — probe via `snapbuf_from_live`/`snapbuf_get`.

## Control-surface quick map
- Plugin parameters = the panel: grain 6 (grainsize/start/speed/density/voices/level), tape
  (organize/sos/recmode/record/play/loop), playhead (mode/scan/grid/amount/clkadv), delay
  (mode/time/feed/tone/mix/grid/amount/glide), filter + smear (cutoff/resonance/mix, smr mix/freq/
  res/stages/fdbk/mode), envelope + pitch (type/skew/saw cyc/dep/midi note/pitch mode/fine/poly),
  distortion + space (on/emphasis/preset/pan/pan mode/width/source), morph (joy_x/joy_y/kernel/power),
  scope tap, master, bank mix, STUT + RETRIG triggers, clock source, MIDI channels, velocity→level,
  bend range. Everything else = engine messages from the surface (`cmd` state).
- Engine messages unchanged (`docs/ligase_manual.md`): `midi <n> <vel> <ch>`, `poly 1` (+ `pitch_mode 4`),
  `matrix_connect …`, `pattern …`, `snapshot 0-31` / `snapshot_recall`, `snapbuf_*`, `morph_*`, `scope_tap …`.

## Immediate next steps
1. **P1 owner DAW hands-on** on the Mac (build with `make -C plugin` or download the CI artifacts):
   AU/VST3/CLAP load, GUI in WKWebView, record → splice → granulate → morph, MIDI, project reopen.
2. **P2** decide the shipped panel default for DIST ON (currently ~7× broadband attenuation at load).
3. **P3** merge the branch (Pages + plugin CI run on merge).
4. Optional agent work: P4 (Euclid `(k,n,rot)` token + literal AXIS→SLOTS), P5 (reel > 60 s
   persistence, per-instance `rand()`, Windows build), the pd panel Step 6 bundle, manual stream 1.

## Pointers
- `QUEUE.md` — Seq 102; §1 ACTIVE QUEUE P1-P5; §4a plan coverage; §6 changelog.
- `docs/plugin_build.md` · `plugin/README.md` — the plugin. `docs/ui/panel_bridge.md` ·
  `docs/ui/ui_sections.md` — the surface. `docs/modulation_layers.md` — modulation/capture contract.
- `docs/ui/panel_layout.py` → `emit_svg.py`/`emit_pd.py`/`emit_bundle.py`/`emit_web.py`/`emit_plugin.py`.
- `plugin/tests/` (identity gate + CLAP host test) · `tests/` (per-feature Pd acceptance) ·
  `web/test_panel_logic.mjs` (brain unit tests).
- `Plans/` — `vst_plugin.md` (DELIVERED, provenance kept) · `pd_panel_prototype.md` (Step 6 owner) ·
  `seq_scale_sidecar.md` (P4 seams) · completed plans in `Plans/completed/`.
