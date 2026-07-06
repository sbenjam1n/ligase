# RESUME.md — ligase~ session handoff

_Snapshot for picking work back up. Authoritative changelog lives in `QUEUE.md` (§6);
this is the "where we are / how to continue" digest._

## Where we are (2026-07-06, Queue Seq 76)

- **Branch state:** everything through Seq 70 is **merged to `main`** (PR #14 = Seq 61-68,
  PR #15 = Seq 69-70). Seq 71-73 work (plans + panel restyle/integration + the in-flight
  scope-taps build) is on `claude/queue-execution-plans-8snsz1`, pushed, unmerged.
- **All five remaining direction plans are DONE + headless-verified** (Seq 61), each built on
  its plan's recommended GATE-A options, one commit per feature, all gated on the automated
  test procedure at the exact baseline:
  - **Polyphony** (`7238cb9`) — 8-voice pool, `poly <0|1>` / `chord …` / vel-0 note-off via
    channel-aware `midi`; steal-oldest; per-voice transposition through
    `scheduler_trigger_grain(…, voice_note, voice_active)`. Needs `pitch_mode 4` to sound.
  - **Spatial granulation** (`2b8df2c`) — `pan_mode 2` + `spatial <sphere|nbody> [inst] [body]`;
    per-grain frozen 3D snapshot → front-biased azimuth → constant-power gains precomputed at
    trigger. `spatial_width/depth/tilt` (lean v1: depth/tilt inert).
  - **Modulation matrix v1** (`bc45503`) — `matrix_connect <src> <dest> <depth>` (+ disconnect/
    clear/dump), 44 sources (gens ×4, pattern0-7, env_l/r/mono peak follower `env_follow_ms`),
    20 per-block dests incl. modout1-4 (compound gate); bipolar depth in dest units;
    apply-site clamps; `mod_track_base()` prevents offset integration on self-read dests.
  - **Pattern events** (`fdc13b1`) — `pattern event|trigger grain/splice/retrig/gate/bang`,
    Euclid `1(3,8)` (Bjorklund), `rev`; kind tag in a parallel per-slot array written before
    the step_count publish barrier; free-scan `pattern_alloc_event_slot`.
  - **Resonator bank** (`a737b30`) — `smear_mode 1`: new `src/grain_smear_bank.{c,h}`, up to 16
    whole `grain_smear` voices verbatim (Shape A, 1/N pre-scale), tuned by `smear_pitch_scale`
    through the P1 note→Hz per block; `smear_bank_mix/feedback/resonance/stages`.
- **Modulation matrix v1.5 is DONE** (`30235fb`, Seq 62) — six per-grain dests
  (`speed grainsize grain_start amplitude pan pitch_fine`, alias `grainstart`) applied
  FUNCTIONALLY at grain trigger (never write the shared fields; bitmask-guarded), speed
  composes with the pitch override (a pin = detune around the note), and **capture
  transparency** (`morph_capture` reads `mod_base` under an active connection — SNAP records
  the base voice, never a wobble sample). Contract + as-built notes: **`docs/modulation_layers.md`**
  (the four-layer precedence/capture/ownership model — read it before touching modulation).
- **Snapshot expander (edit buffer) is DONE** (`2644a57`, Seq 63–67; plan
  `Plans/snapshot_expander.md`) — the cold-edit sidecar: `snapbuf_load/from_live/set/get/
  dump/store/apply/clear` over ONE shared 150-entry field walker (also drives export/import;
  `since`-versioned). **Schema v3**: the generator ("sources") params joined snapshots —
  "params are weather control" — with v1/v2 file compat and `morph_exclude sources` for
  global weather. `snapbuf_apply` is the only realtime touchpoint; STORE to a placed slot
  reshapes the blend next block. Deliberate alignment: `snapshot_recall` now honors the
  selection tree (shared `morph_mask_excluded()`; no-op when nothing is excluded). Cold-edit
  byte-identity proven (25 edits during recording, WAV md5 unchanged). **v1.1 audition/compare
  shipped** (Seq 68): `snapbuf_audition <0|1>` (capture-to-revert, masked apply, EXACT revert;
  apply mid-audition commits) + `snapbuf_compare` A/B toggle.
- **Source shapes are DONE** (`fe90c79`, Seq 69–70; plan `Plans/source_shapes.md`) — every
  generator's shape is settable: `waveform_phase/square_pw/saw_skew <inst> <v>` (readout-side,
  defaults bit-identical), `lorenz_sigma/rho/beta` (ρ = the chaos knob), `sphere_spin`
  (energy-neutral velocity curl; feeds pan_mode 2 beautifully) + `sphere_kick_rand`. All 28
  scalars are **capture schema v4** (v1–v3 files import; old exclude indices remapped;
  `sources` group covers them; the expander addresses them by name). Panel: the SOURCE SHAPE
  multi-engine cluster (FAMILY×INST cursor → RATE + A–D knobs + printed legend).
- **Panel = layout-as-data, TWO emitters (Seq 76)** — `docs/ui/panel_layout.py` is the single
  source of truth (113 control records with bindings + engine-unit ranges, matrix vocabulary,
  per-family SOURCE SHAPE meanings, XPNDR field map, distortion presets); `emit_svg.py` renders
  the silkscreen `docs/ui/ligase_synthi_panel.svg` (`gen_panel.py` is a thin wrapper; edit →
  run → screenshot via headless chromium) and `emit_pd.py` emits the WORKING patches
  `pd/ligase_panel.pd` + `pd/ligase_xpndr.pd` (see `pd/README.md`; every control has a
  `lgR_<id>` receive symbol = the scripting/MIDI hook). Style: minimalist-luxury chromecore
  Buchla, ONE chassis (expander = third column). Geometry (owner-directed): SCOPE display is
  the exact 216×216 twin of the joystick pad, top-aligned; PRESETS = 4×8 = 32 slots
  (silkscreen 1-32 → engine snapshot slots 0-31). Every signal inlet badged `IN n`, message
  params `MSG`.
- **Prototyping/UI/VST arc (Seq 71-76)**: `Plans/scope_taps.md` **DONE** (scope_x~/scope_y~
  outlets 10/11, 11-family `scope_tap` table default lorenz 1, grain CONSTELLATION
  X = splice pos / Y = env×amp + grainsum; every tap's shape measured; gotcha: bare `play`
  STOPS — drive tests with `play 1`). `Plans/pd_panel_prototype.md` **steps 1-4 DONE**
  (Seq 76): the panel is now a WORKING patch — all gates measured headless (zero-error
  loads, knob→snapbuf readback, matrix pin ↔ matrix_dump, joystick→morph cursor, XPNDR
  round-trip, SOURCE SHAPE routing). Remaining there: Step 5 `.plugdata` bundle
  (owner-approved: panel patches + README + conf + both platform externals, drag-drop
  install into plugdata STANDALONE) + Step 6 owner hands-on. `Plans/vst_plugin.md` still at
  GATE A (v1 = plugdata with ligase COMPILED IN; LICENSE gate: plain GPL-2 vs GPLv3 hosts).
- Remaining overall: **owner hardware/ear sign-off** on the new features (chord balance vs
  `maxgrains`, spatial orbit feel, matrix musicality, burst character, resonator-bank timbre —
  the GATE A.7 input for the v2 Karplus-Strong decision) + the §4 build-naming backlog stub +
  morph FX scalar bases for distortion-enhancement/stut/bencina (minor).

## What ligase~ is
- Pure Data granular synth / sampler / looper / delay external. C, GPL-v2. Repo `sbenjam1n/ligase`.
- **Hardware-synth PROTOTYPE** → design every parameter to be **signal/CV-driven via its inlet**
  where an inlet exists; message-only params should be modulation-matrix reachable.
- Owner runs **plugdata 0.9.2 on an Intel Mac** + a Focusrite. Cloud sessions run Linux.

## Working conventions (carry these — they bit us when ignored)
- **This branch's commits** end with the `Co-Authored-By: Claude Fable 5` + session trailer
  (cloud-session convention). Older `main` history used the owner-identity convention.
- **Do NOT regenerate the PDF.** `docs/ligase_manual.md` is the source of truth; the PDF is
  intentionally stale. `make manual` ONLY when the owner explicitly asks.
- **No "fog" in new comments** — replaced by the allpass **smear**.
- **plugdata caches the external**: a new build needs a full plugdata quit+relaunch.
- **After pulling, `make clean && make` once** (header deps tracked via `-MMD -MP`, B26).
- **Read `docs/modulation_layers.md` before changing modulation/capture/morph code** — it is
  the precedence + capture-transparency contract; violating it reintroduces the v1 SNAP bug.
- **QUEUE discipline:** bump Queue Seq + one-line §6 entry on any §1/§4a change; plans live in
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
  `\; pd dsp 1` (required under `-nosound`).
- Capture audio with `writesf~ 2` (wire `stop` → writesf~, or the WAV header never finalizes);
  float-extensible WAVs need `sox -b 16 -e signed` conversion before python's `wave` reads them.
- To HEAR delay wet: `sos 0` + `gdelay_mix 1` (default sos masks it). Headless 0 honors an
  unconnected inlet's literal 0 (it overwrites params) — use headless 1 for message-only tests.
- Grain debug: the first 5 triggers + first 8 finals print to stderr
  (`grain triggered #` / `grain final #`) — the finals show per-grain matrix offsets landing.
- Owner-side (Mac): `ligase~.pd_darwin`, plugdata; test plans in `TEST_PLAN_MACOS.md`.

## Control-surface quick map (what shipped this session)
- Poly: `poly 1`, `chord 60 64 67`, `midi <n> <vel> <ch>` (vel 0 = off), needs `pitch_mode 4`.
- Spatial: `spatial sphere 0` / `spatial nbody 2 1`, `pan_mode 2`, `spatial_width <0-1>`.
- Matrix: `matrix_connect env_mono moog_cutoff 3000`; per-grain: `matrix_connect rand1 grainsize 0.5`;
  `matrix_dump`, `env_follow_ms 300`. Full lists in the manual's MODULATION MATRIX section.
- Events: `pattern event grain [ 1(3,8) ]`; actions grain/splice/retrig/gate/bang; `rev`.
- Bank: `smear_pitch_scale 0 4 7` + `smear_mode 1` + `smear_bank_mix 0.5` (+ resonance ~0.998
  for tight tuning).
- Expander: `snapbuf_load 2` → `snapbuf_get moog_cutoff` → `snapbuf_set moog_cutoff 620` →
  `snapbuf_store 2` / `snapbuf_apply`; edits are COLD; get/dump report on the state outlet.
- (Earlier arcs — patterns P1-P3, smear pitch, MIDI routing, fine tune, one-shot, morph — are
  documented in the manual and QUEUE §6; the pattern mini-notation digest lives in the manual's
  PATTERNS section. Pattern tests: `tests/pattern/`; morph: `tests/morph/`.)

## Immediate next steps
1. **`.plugdata` bundle** (`Plans/pd_panel_prototype.md` Step 5, owner-approved): emit_bundle /
   `make bundle` → `ligase.plugdata` (panel patches + README + conf + both platform
   externals); container layout must be checked against plugdata's loader; drag-drop install
   is the owner-machine test.
2. Owner hands-on gate: open `pd/ligase_panel.pd` in plugdata on the Mac (feel test — knob
   ranges, matrix workflow, scope hookup per `pd/README.md`); findings become layout-data edits.
3. Owner ear-test round on the new features (list above) + the expander workflow feel (AC6)
   — file findings as new B-items.
4. `Plans/vst_plugin.md` GATE A: the LICENSE decision (GPL-2-only vs "or later") is the owner's.
5. Parked ideas: source-rates-as-matrix-destinations (matrix-on-matrix; matrix plan's domain);
   expander v1.1 = audition + A/B compare pair; morph FX bases completeness; §4 build-naming
   cleanup (only when cutting a release); `make manual` when the owner asks (much accumulated).

## Pointers
- `QUEUE.md` — full changelog (§4a plan coverage; §6 history to Seq 66).
- `docs/modulation_layers.md` — the modulation-layer contract (precedence, capture, ownership).
- `Plans/snapshot_expander.md` — the expander's spec + measured acceptance criteria (DONE).
- `docs/ui/panel_layout.py` → `emit_svg.py` (silkscreen SVG) + `emit_pd.py` (`pd/` patches) —
  the control surface, single-sourced; `pd/README.md` = how to open/script/MIDI-map it.
- `tests/` — per-feature headless acceptance patches (`polyphony/ spatial/ modmatrix/
  pattern_events/ resonator/ pattern/ morph/ param_lock/`).
- `docs/ligase_manual.md` — manual source (PDF intentionally stale).
