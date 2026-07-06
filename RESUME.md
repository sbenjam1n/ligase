# RESUME.md — ligase~ session handoff

_Snapshot for picking work back up. Authoritative changelog lives in `QUEUE.md` (§6);
this is the "where we are / how to continue" digest._

## Where we are (2026-07-06, Queue Seq 79)

- **Branch state:** everything through Seq 70 is **merged to `main`** (PR #14 = Seq 61-68,
  PR #15 = Seq 69-70). Seq 71-78 (plans, panel restyle/integration, scope taps, the panel
  prototype + `.plugdata` bundle) is on `claude/queue-execution-plans-8snsz1`, pushed, unmerged.
- **The panel is a WORKING INSTRUMENT** (`Plans/pd_panel_prototype.md` steps 1-5 DONE,
  Seq 76/78). One layout source, three artifacts:
  - `docs/ui/panel_layout.py` — the control surface AS DATA (113 records: `lgR_<id>`
    receive symbols, bindings, engine-unit ranges per the update_inlets validation windows;
    matrix vocabulary; per-family SOURCE SHAPE meanings; XPNDR 8×8 field map; DIST presets).
  - `emit_svg.py` → `docs/ui/ligase_synthi_panel.svg` (the silkscreen; `gen_panel.py` is a
    thin wrapper; render via headless chromium). Byte-identical output through the refactor.
  - `emit_pd.py` → `pd/ligase_panel.pd` (3328 objects) + `pd/ligase_xpndr.pd` (standalone
    expander over the `lg_engine`/`lg_state9` buses). All gates measured headless:
    zero-error loads, knob→snapbuf readback, matrix pin ↔ `matrix_dump` round-trip,
    joystick→morph cursor, XPNDR round-trip, SOURCE SHAPE routing, 32-slot preset bank
    (silkscreen 1-32 → engine snapshot slots 0-31). `pd/README.md` = setup, scope hookup,
    scripting/MIDI hook (`; lgR_<id> <v>` drives GUI + engine), honest seams
    (`splice_finish_nav` is the 0/1 flag not a jump — B-item candidate; MASTER knob
    deliberately unwired; GAUS/EXP silkscreen-forward).
  - **`make bundle` → `dist/ligase.plugdata`** (Seq 78): drag-drop install for plugdata
    STANDALONE ≥ 0.9.2, matched against the actual loader (`installPackage()`: ZIP →
    top-level dir into `Patches/`; `meta.json` PatchInfo keys; `FolderName: ligase`).
    Deterministic (rebuild sha256-identical), `--verify` structural check, install
    simulated (extract + load: zero errors; external AND `ligase.conf` load from the
    package). Build on the Mac so `ligase~.pd_darwin` rides along.
  - Panel geometry (owner-directed, Seq 76): SCOPE display = exact 216×216 twin of the
    joystick pad, top-aligned with the metasurface grid; PRESETS = 4×8 = 32 slots.
- **Scope taps are DONE** (Seq 74, `Plans/scope_taps.md`): `scope_x~`/`scope_y~` signal
  outlets 10/11, 11-family `scope_tap` table (default `lorenz 1` — the butterfly), the
  grain CONSTELLATION (X = splice pos, Y = env×amp, one grain/sample; idle beam parks at
  0,0) + `grainsum`; every tap's shape measured in captured outlet data. Monitor, not
  voice state (never captured/morphed). Gotcha: bare `play` STOPS — drive tests with
  `play 1`.
- **Feature set shipped this session (all merged or on-branch, each regression-exact):**
  poly (8-voice pool, needs `pitch_mode 4`), spatial (`pan_mode 2` + sphere/nbody),
  matrix v1 + v1.5 (44 src × 26 dest incl. six per-grain dests with capture transparency),
  pattern events (Euclid + `rev`), resonator bank (`smear_mode 1`), snapshot expander
  (`snapbuf_*` cold-edit buffer + audition/compare, schema v3), source shapes (waveform
  phase/PW/skew, lorenz σρβ, sphere spin/kick, schema v4), scope taps. Contract doc:
  **`docs/modulation_layers.md`** — read it before touching modulation/capture/morph.
- **VST plan** (`Plans/vst_plugin.md`) at GATE A: v1 = plugdata fork with ligase COMPILED
  IN (no runtime external loading in plugin hosts), panel = GUI, ~16 host params.
  **License decision DEFERRED by owner (Seq 77)** — "no one is using it but us";
  personal-use builds unaffected; re-raise only at distribution time.
- Remaining overall: owner hands-on/ear gates (below) + §4 build-naming backlog stub +
  morph FX scalar bases for distortion-enhancement/stut/bencina (minor).

## What ligase~ is
- Pure Data granular synth / sampler / looper / delay external. C, GPL-v2 (plain, no
  or-later — deferral noted above). Repo `sbenjam1n/ligase`.
- **Hardware-synth PROTOTYPE** → design every parameter to be **signal/CV-driven via its
  inlet** where an inlet exists; message-only params should be matrix-reachable.
- Owner runs **plugdata 0.9.2 on an Intel Mac** + a Focusrite. Cloud sessions run Linux.

## Working conventions (carry these — they bit us when ignored)
- **This branch's commits** end with the `Co-Authored-By: Claude Fable 5` + session trailer
  (cloud-session convention). Older `main` history used the owner-identity convention.
- **Do NOT regenerate the PDF.** `docs/ligase_manual.md` is the source of truth; the PDF is
  intentionally stale. `make manual` ONLY when the owner explicitly asks.
- **Panel/patches are GENERATED — hand-tweaks go in `panel_layout.py`**, then re-run
  `gen_panel.py` + `emit_pd.py` (+ `make bundle` if shipping). Never edit the artifacts.
- **No "fog" in new comments** — replaced by the allpass **smear**.
- **plugdata caches the external**: a new build needs a full plugdata quit+relaunch.
- **After pulling, `make clean && make` once** (header deps tracked via `-MMD -MP`, B26).
- **Read `docs/modulation_layers.md` before changing modulation/capture/morph code** — it is
  the precedence + capture-transparency contract; violating it reintroduces the v1 SNAP bug.
- **Class-construction trap:** appending signal inlets/outlets requires the dsp_add arg
  growth + ALL FOUR perform `return (w+N)` bumps (current: dsp_add 30, returns `(w+31)`,
  outlets 0-11).
- **QUEUE discipline:** bump Queue Seq + one-line §6 entry on §1/§4a changes; plans live in
  `Plans/`, GATE-A style with [R] recommendations; the owner approves gates (sometimes
  wholesale: "take the recommendations").

## Build & headless-test recipe (Linux cloud session)
- `sudo apt-get install -y puredata sox` (pd 0.54.1 works). Build: `make` → `ligase~.pd_linux`.
- **Regression gate = `AUTOMATED_TEST_PROCEDURE.md`**, exact baselines:
  `test_auto.pd` → RMS **0.372309** / max 0.608839; `test_playback.pd` → buffer check
  **L=R=0.330109**; `test_delay.pd` clean. Any deviation from these exact numbers means a
  default behavior changed — investigate before committing.
- **pd hygiene (cost real time):** `pkill -9 pd` before runs; wrap in `timeout -s KILL <n>s`;
  every test patch must self-quit (`[delay]` → `\; pd quit`) or pd hangs forever; loadbang
  `\; pd dsp 1` (required under `-nosound`); **bare `play` STOPS — use `play 1`**; msg
  boxes don't chain to delays (use parallel loadbang→delay branches).
- Capture audio with `writesf~ 2` (wire `stop` → writesf~, or the WAV header never
  finalizes); float-extensible WAVs need `sox -b 16 -e signed` before python's `wave`.
- To HEAR delay wet: `sos 0` + `gdelay_mix 1`. Headless 0 honors an unconnected inlet's
  literal 0 — use headless 1 for message-only tests. `query` returns 0 for unmodulated
  scalars — probe via `snapbuf_from_live` + `snapbuf_get <field>` instead.
- **Panel patches are the scripting harness too:** `; lgR_<id> <val>` (ids in
  `panel_layout.py`) drives any control headless — used for all the panel gates.
- Owner-side (Mac): `ligase~.pd_darwin`, plugdata; test plans in `TEST_PLAN_MACOS.md`.

## Control-surface quick map (what shipped this session)
- Poly: `poly 1`, `chord 60 64 67`, `midi <n> <vel> <ch>` (vel 0 = off), needs `pitch_mode 4`.
- Spatial: `spatial sphere 0` / `spatial nbody 2 1`, `pan_mode 2`, `spatial_width <0-1>`.
- Matrix: `matrix_connect env_mono moog_cutoff 3000`; per-grain: `matrix_connect rand1
  grainsize 0.5`; `matrix_dump`, `env_follow_ms 300`. Full lists in the manual.
- Events: `pattern event grain [ 1(3,8) ]`; actions grain/splice/retrig/gate/bang; `rev`.
- Bank: `smear_pitch_scale 0 4 7` + `smear_mode 1` + `smear_bank_mix 0.5`.
- Expander: `snapbuf_load 2` → `snapbuf_get moog_cutoff` → `snapbuf_set moog_cutoff 620` →
  `snapbuf_store 2` / `snapbuf_apply`; `snapbuf_audition 1/0` (exact revert),
  `snapbuf_compare` (A/B). Edits are COLD; get/dump report on the state outlet (9).
- Source shapes: `waveform_phase/square_pw/saw_skew <inst> <v>`, `lorenz_sigma/rho/beta
  <inst> <v>`, `sphere_spin <inst> <v>`, `sphere_kick_rand <inst>`.
- Scope: `scope_tap lorenz 1` (default) / `scope_tap grain` / `scope_tap grainsum` →
  outlets 10/11 → `[oscilloscope~]` in XY mode.
- Snapshots ARE the presets: panel slots 1-32 → `snapshot 0-31` / `snapshot_recall 0-31`;
  place any slot on the morph surface.

## Immediate next steps
1. **Owner hands-on gate** (panel plan Step 6): on the Mac, `make && make bundle` (adds
   `ligase~.pd_darwin`), drag `dist/ligase.plugdata` into plugdata standalone, feel test
   (knob ranges, matrix workflow, scope hookup per the package README); findings become
   `panel_layout.py` data edits.
2. Owner ear-test round on the feature set (chord balance vs `maxgrains`, spatial orbit
   feel, matrix musicality, resonator-bank timbre = the GATE A.7 KS input, scope
   usefulness while dialing SOURCE SHAPE) — file findings as new B-items.
3. `Plans/vst_plugin.md` v1 build (plugdata fork, ligase compiled in) whenever the owner
   greenlights — license deferral does not block personal-use builds.
4. Parked: source-rates-as-matrix-destinations (matrix-on-matrix); morph FX bases
   completeness; §4 build-naming cleanup (only when cutting a release); `make manual` when
   the owner asks (much accumulated); splice jump-to-N message (panel B-item candidate).

## Pointers
- `QUEUE.md` — Seq 79; §4a plan coverage; §6 history is the changelog.
- `docs/modulation_layers.md` — the modulation-layer contract (precedence, capture, ownership).
- `docs/ui/panel_layout.py` → `emit_svg.py` (silkscreen) + `emit_pd.py` (`pd/` patches) +
  `emit_bundle.py` (`dist/ligase.plugdata`) — the control surface, single-sourced.
- `pd/README.md` — how to install/open/script/MIDI-map the panel.
- `Plans/` — execution plans (`pd_panel_prototype.md` steps 1-5 done; `vst_plugin.md` at
  GATE A; completed plans archived in `Plans/completed/`).
- `tests/` — per-feature headless acceptance patches (`polyphony/ spatial/ modmatrix/
  pattern_events/ resonator/ pattern/ morph/`).
- `docs/ligase_manual.md` — manual source (PDF intentionally stale).
