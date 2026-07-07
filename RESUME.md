# RESUME.md — ligase~ session handoff

_Snapshot for picking work back up. Authoritative changelog lives in `QUEUE.md` (§6);
this is the "where we are / how to continue" digest._

## Where we are (2026-07-06, Queue Seq 89)

- **Branch state:** Seq 61-70 are **merged to `main`** (PRs #14/#15). Everything since —
  Seq 71-89 (scope taps, harmonic layer, the panel prototype, `.plugdata` bundle, all the
  panel geometry, the SEQ/SCALE wiring) — is on `claude/queue-execution-plans-8snsz1`,
  pushed, **unmerged** (owner PR owed). NOTE: the local `main` ref is stale (behind
  `origin/main`); diff against `origin/main`, not local `main`.
- **The whole 2026-07 feature arc is code-complete + headless-verified.** The momentum has
  shifted from CODE to owner GATES — almost every open item is an ear/feel-test, a merge,
  or a greenlight, not engineering.

### The control surface is a WORKING INSTRUMENT (one layout source, four emitters)
- `docs/ui/panel_layout.py` — the surface AS DATA (control records with `lgR_<id>` receive
  symbols, bindings, engine-unit ranges; now includes the SEQ_* records).
- `emit_svg.py` → `docs/ui/ligase_synthi_panel.svg` (silkscreen; `gen_panel.py` wraps it;
  render via headless chromium `--window-size=2456,1096`).
- `emit_pd.py` → `pd/ligase_panel.pd` (embeds `[pd xpndr]` + `[pd seq]`), `pd/ligase_xpndr.pd`,
  `pd/ligase_seq.pd` (standalone canvases over the `lg_engine`/`lg_state9` buses).
- `emit_bundle.py` (`make bundle`) → `dist/ligase.plugdata` (deterministic drag-drop install;
  `dist/` gitignored).
- **Panel geometry is settled** (owner iterated it to a final wide chassis, W 2456 × H 1096):
  left = the inlet strips; middle = Presto-Patch matrix + SOURCE SHAPE + joystick + SCOPE;
  right region = SEQ/SCALE block on top (tone circle, slots/commit, time circle, pattern
  grid) with the paired expander below (SNAPSHOT|VALUE, ADDRESS|MOD BAND, COMMIT|MONITOR —
  MONITOR in the bottom-right corner). `pd/README.md` documents scope hookup, the
  `; lgR_<id> <v>` scripting/MIDI hook, and the honest prototype seams.

### Engine feature set shipped this session (each regression-exact, commit noted)
- **Poly** (`7238cb9`, on main) — 8-voice pool; needs `pitch_mode 4`.
- **Spatial** (`2b8df2c`, on main) — `pan_mode 2` + `spatial sphere|nbody`.
- **Matrix v1** (`bc45503`, on main) + **v1.5 per-grain** (`30235fb`, on main) — 44 src × 26
  dest; capture transparency. Contract: **`docs/modulation_layers.md`** (read before touching
  modulation/capture/morph).
- **Pattern events** (`fdc13b1`, on main) — Euclid `(k,n)` + `rev`.
- **Resonator bank** (`a737b30`, on main) — `smear_mode 1`, 16 tuned voices.
- **Snapshot expander** (`2644a57` + `a38b3c5`) — `snapbuf_*` cold-edit buffer + audition/compare.
- **Source shapes** (`fe90c79`) — waveform/lorenz/sphere params, schema v4.
- **Scope taps** (`b9629f7`, branch) — `scope_x~/y~` outlets 10/11, 11-family `scope_tap`
  (default `lorenz 1`), grain constellation. Monitor, not voice state.
- **Harmonic layer** (`9a90eec`, branch) — 16 scale slots per destination (slot 0 = legacy,
  bit-identical), `scale_root`/`scale_rotate` as param_range/matrix destinations, stochastic
  scale-blend rule, `scope_tap scale`/`pattern`, capture schema v5 (v1-v4 import compat).
  Giant Steps demo passing.
- **SEQ/SCALE sidecar wiring** (`2b004b8`, branch) — the tone/time circles + pattern grid are
  now live pd against the harmonic engine (ring→scale, ROOT/MODE→root/rotate, AXIS→SLOTS
  Coltrane cycle, Euclid, grid). Fixed a cascade gate-init bug caught in review.

## What ligase~ is
- Pure Data granular synth / sampler / looper / delay external. C, **GPL-2-only** (no
  or-later; distribution-license decision deferred — personal-use fine). Repo `sbenjam1n/ligase`.
- **Hardware-synth PROTOTYPE** → every parameter signal/CV-driven via its inlet where one
  exists; message-only params matrix-reachable.
- Owner runs **plugdata 0.9.2 on an Intel Mac** + a Focusrite. Cloud sessions run Linux.

## Working conventions (carry these — they bit us when ignored)
- **Panel/patches are GENERATED — hand-tweaks go in `panel_layout.py`**, then re-run
  `gen_panel.py` + `emit_pd.py` (+ `make bundle` if shipping). Never edit the artifacts.
- **SVG identity is a hard gate** on any layout refactor — regenerate and diff; the geometry
  is owner-settled and must not shift.
- **Verify agent work independently** — the SEQ/SCALE agent reported a gate PASS that wasn't
  reproducible (the tone circle composed an empty scale under realistic drive); caught only
  by re-running the realistic case. Re-run the actual behavior, not the report.
- **This branch's commits** end with the `Co-Authored-By: Claude Fable 5` + session trailer.
- **Do NOT regenerate the PDF.** `docs/ligase_manual.md` is source of truth; `make manual`
  only when the owner asks (much accumulated).
- **No "fog" in new comments** — it's the allpass **smear**.
- **plugdata caches the external**: a new build needs a full plugdata quit+relaunch.
- **After pulling, `make clean && make` once** (header deps via `-MMD -MP`, B26).
- **Read `docs/modulation_layers.md` before modulation/capture/morph work** — precedence +
  capture-transparency contract (now includes the scale-fields blend rule).
- **Class-construction trap:** appending signal inlets/outlets needs the dsp_add arg growth +
  ALL FOUR perform `return (w+N)` bumps (current: dsp_add 30, returns `(w+31)`, outlets 0-11).
- **QUEUE discipline:** bump Queue Seq + one-line §6 entry on any §1/§4a change; owner
  approves gates (often wholesale: "take the recommendations").

## Build & headless-test recipe (Linux cloud session)
- `sudo apt-get install -y puredata sox`. Build: `make` → `ligase~.pd_linux`.
- **Regression gate = `AUTOMATED_TEST_PROCEDURE.md`**, exact baselines: `test_auto.pd` → RMS
  **0.372309** / max 0.608839; `test_playback.pd` → buffer **L=R=0.330109**; `test_delay.pd`
  clean. Any deviation = a default changed — investigate before committing.
- **pd hygiene:** `pkill -9 pd` before runs; `timeout -s KILL <n>s`; every test patch
  self-quits (`loadbang`→`[delay]`→`\; pd quit`; msg boxes don't chain to delays — parallel
  loadbang branches); loadbang `\; pd dsp 1`; **bare `play` STOPS — use `play 1`**; capture
  with `writesf~ 2` + wire `stop` before quit; float WAVs need `sox -b 16 -e signed` first;
  `query` returns 0 for unmodulated scalars — probe via `snapbuf_from_live` + `snapbuf_get`.
- **Driving the panel headless:** `; lgR_<id> <v>` sets a GUI control and fires its wiring
  (a `[tgl]` DOES output via its receive symbol). The `[pd seq]`/`[pd xpndr]` canvases route
  to `lg_engine`; read state on `r lg_state9`. Tapping `r lg_engine → print` shows exactly
  what a section emits — how the SEQ wiring was verified.
- Owner-side (Mac): `ligase~.pd_darwin`, plugdata; `TEST_PLAN_MACOS.md`.

## Control-surface quick map (what shipped this session)
- Poly: `poly 1`, `chord 60 64 67`, `midi <n> <vel> <ch>`, needs `pitch_mode 4`.
- Spatial: `spatial sphere 0` / `spatial nbody 2 1`, `pan_mode 2`.
- Matrix: `matrix_connect env_mono moog_cutoff 3000`; per-grain `matrix_connect rand1 grainsize 0.5`;
  `matrix_dump`.
- Events: `pattern event grain [ 1(3,8) ]`; grain/splice/retrig/gate/bang; `rev`.
- Bank: `smear_pitch_scale 0 4 7` + `smear_mode 1` + `smear_bank_mix 0.5`.
- Expander: `snapbuf_load 2` → `snapbuf_get moog_cutoff` → `snapbuf_set …` → `snapbuf_store`/
  `snapbuf_apply`; `snapbuf_audition 1/0`, `snapbuf_compare`.
- Harmonic: `pitch_scale 0 2 4 5 7 9 11` (writes active slot) / `pitch_scale_slot 0-15` /
  `pitch_scale_to <slot> …` / `scale_root <st>` / `scale_root_quant 1` / `scale_rotate <n>`
  (+ `smear_*` mirrors); `pattern pitch_scale_slot [ 0 1 2 ]`; `scope_tap scale|pattern`.
- Snapshots ARE presets: panel slots 1-32 → `snapshot 0-31` / `snapshot_recall 0-31`.

## Immediate next steps (from the Seq-88 multi-agent queue review, all owner-side or optional)
1. **Owner PR** bringing Seq 71-89 (scope/harmonic/panel/bundle/sidecar) to `main` (like #14/#15).
2. **Owner feel-tests** on the Mac/Focusrite — one checklist across features; priority on the
   **resonator-bank timbre** test (the GATE A.7 input deciding Karplus-Strong v2) and the new
   SEQ/SCALE tone-circle feel. Findings → panel_layout data edits or B-items.
3. **Owner panel Step 6** — drag `dist/ligase.plugdata` into plugdata (`make && make bundle`
   on the Mac to include `ligase~.pd_darwin`); the one step keeping the prototype plan open.
4. **VST v1** (`Plans/vst_plugin.md`) — blocked only on an owner GATE-A greenlight (license
   deferred = personal-use unblocked; the kiss_fft static-link hazard is moot — no longer
   compiled). Agent work once greenlit.
5. **Optional agent work:** backfill committed acceptance patches for the 5 features with
   narrative-only verification (poly, expander, source shapes, scope taps, harmonic); author
   the microtonal/ratio-scale paired plan on greenlight; the two SEQ/SCALE B-item seams
   (Euclid ROT token, per-slot transpose) if `vexpr`/an engine message lands.
6. **Parked/backlog:** `make manual` + `Plans/manual_content_edits.md` (owner-triggered);
   §4 build-naming cleanup; source-rates-as-matrix-destinations; splice jump-to-N message.

## Pointers
- `QUEUE.md` — Seq 89; §4a plan coverage; §6 is the changelog.
- `docs/modulation_layers.md` — the modulation-layer + scale-blend contract.
- `docs/ui/panel_layout.py` → `emit_svg.py`/`emit_pd.py`/`emit_bundle.py` — the single-sourced
  control surface; `pd/README.md` = open/script/MIDI-map/install it.
- `Plans/` — `pd_panel_prototype.md` (Step 6 owner) · `harmonic_layer.md` (DONE) ·
  `seq_scale_sidecar.md` (DONE) · `vst_plugin.md` (GATE A) · completed in `Plans/completed/`.
- `tests/` — per-feature headless acceptance patches; run all new suites with
  `bash tests/run_acceptance.sh` (14 self-asserting checks). Committed suites now cover
  spatial/ modmatrix/ pattern_events/ resonator/ + polyphony/ expander/ source_shapes/
  scope_taps/ harmonic/ (Seq 90 backfill closed the narrative-only gap); plus the older
  morph/ pattern/ oneshot/ param_lock/ suites.
- `docs/ligase_manual.md` — manual source (PDF intentionally stale).
