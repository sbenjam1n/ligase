# Plan: Web Build — ligase in the browser (GitHub Pages + CI), with optional primase clock

**Owner:** SLB
**Date:** 2026-07-06
**Status:** IN PROGRESS — **GATE A cleared at ALL recommendations (owner 2026-07-06: "take the
recommendations, begin arc A").** **Arc A Steps 1–2 core PROVEN headless (Seq 92):** Emscripten
6.0.3 + libpd (BSD) toolchain stood up; stock libpd→WASM verified in node (`test_sine.pd` → RMS
0.141421 / MAX 0.200000); **all 15 ligase sources compile under emcc and `[ligase~]` instantiates
+ processes audio in WASM** (no "couldn't create"; no kiss_fft hazard — fog/FFT was removed). A7
license audit confirmed: libpd/pure-data BSD + Emscripten MIT/NCSA → the GPL-2 web build is
distributable with NO relicense. **Arc A COMPLETE + verified (Seq 93):** engine-identity in WASM reproduces the exact native
baseline (RMS 0.372309 / frames 132288 / buffer 0.330109; ≤1-ULP cross-target float delta on the
saved reel, gate metrics exact — characterized, no engine change); single-threaded AudioWorklet +
HTML player (no SharedArrayBuffer/COOP/COEP — plain-Pages-safe); `docs/ui/emit_web.py` = the FOURTH
emitter (289 web controls driving the engine over the `lgR_*` bus); getUserMedia audio-in (opt-in),
reel import (file→MEMFS→load) + export (save→MEMFS→Blob); `.github/workflows/web-deploy.yml` builds
emsdk+libpd+ligase, runs the identity gate as a required check, and deploys to Pages. Orchestrator
independently re-verified (identity RMS reproduced; headless-chromium boot injects all 289 controls;
YAML valid; src/ untouched). First Pages deploy is owner-triggered (Settings → Pages → GitHub
Actions). **Arc B DONE + verified (Seq 94):** primase~ (separate repo, not vendored) drives ligase as a
rhythm brain — `pd/ligase_primase.pd` (outlet-0 bang → stut + ligase's cycle clock; position →
grainstart; velocity → amplitude); optional in the desktop bundle + compiled into the WASM build
(`PRIMASE_DIR`); `tests/primase/` self-asserting pairing (8→8 stut, clock-locked). Verified: WASM
identity gate STILL 0.372309 with primase compiled in; no ligase/primase engine change. **Both arcs
DONE.** Owner gates remain: enable Pages (first deploy); primase LICENSE (B6, distribution blocker);
deferred B4 panel CLOCK-SRC switch + primase.c `(t_method)` cast cleanup (upstream) are B-items.
**Tracked in:** `QUEUE.md` §4a (prototyping/UI/VST/web arc).
**Related:** `Plans/vst_plugin.md` (the SAME compiled-in constraint — a third deploy target),
`Plans/pd_panel_prototype.md` (`panel_layout.py` becomes a fourth emitter here),
`docs/ui/emit_bundle.py` (the desktop bundle primase joins in Arc B).

> **PROVENANCE / RESEARCH (2026-07-06, web-verified).**
> (1) **WebPd cannot run ligase.** WebPd is a *compiler for a vanilla-Pd subset* — it
> reimplements built-in objects and does NOT execute compiled C externals. `[ligase~]`
> (and `[primase]`) are compiled externals, so the WebPd path is out.
> (2) **The path is libpd → WebAssembly** (Emscripten Wasm Audio Worklets,
> `-sAUDIO_WORKLET`) with ligase's C sources **statically compiled in** and
> `ligase_tilde_setup()` registered — the identical "no runtime external loading"
> mechanism as the VST plan. This is the THIRD instance of that constraint
> (plugdata-VST, and now the browser).
> (3) **GitHub Pages** serves the static WASM/JS/HTML over HTTPS and a **GitHub Actions**
> job with the Emscripten SDK builds it — no Mac needed (unlike the VST's AU build).
> (4) **License asymmetry — the web path is the CLEAN one.** libpd / Pd core are **BSD**
> and Emscripten is MIT (both GPL-compatible), so a web build of GPL-2 ligase is
> distributable WITHOUT the relicense that the GPLv3 plugdata/VST3-SDK path needs
> (`Plans/vst_plugin.md` GATE A.2). The deferred license gate does not block the web build.
> (5) **`sbenjam1n/primase`** (added to the session 2026-07-06) is `primase~`, a compiled-C
> Pd pattern-replicator/cyclic-mutation external (`primase.c` → `primase.pd_{linux,darwin}`).
> Interface confirmed from source: `[primase 120]`; **outlet 1 = bang per playback event**,
> outlet 2 = position 0–1, outlet 3 = velocity, outlet 4 = event count, outlet 5 = status;
> inlet 2 = external-clock bang (`clockfollow`/`sync` lock cycle phase); Euclid + a live
> transform chain (palindrome/rotate/reverse/fast/slow/euclid/jitter/skip/degrade/ratchet).
> **primase has NO license file** — a distribution prerequisite (see B GATE A.6).

---

## Problem

ligase lives on the desktop (plugdata bundle) and, planned, in a DAW (VST). The owner wants
it **playable in a browser from a URL** — no install — served off GitHub Pages with CI. And,
separately but naturally, wants **primase~** (the sibling rhythm external) usable as an
optional trigger/clock brain driving ligase's `play`/`stut` and cycle. Both reduce to the
same engineering spine: a compiled Pd external running client-side in WASM, and honest
answers for audio-in and reel file I/O without a native filesystem.

---

## ARC A — Web deployment

### Design

- **Runtime:** libpd compiled to WASM, hosted in an **AudioWorkletProcessor** (the DSP runs
  on the audio thread); ligase's ~15 C sources compiled in + `ligase_tilde_setup()`
  registered. The panel `.pd` patches run as the libpd patch; ligase is baked into the binary.
- **GUI = a fourth emitter.** libpd has **no GUI** — the `.pd`'s GUI objects don't render. So
  the settled panel drives from an **HTML/JS front-end** generated from `panel_layout.py`
  (`emit_web.py`), talking to the engine over libpd's message API using the **existing
  `lgR_<id>` receive-symbol bus** — the same scripting interface the headless tests use.
  - **The panel IS the SVG (as-built).** The web UI renders the rendered silkscreen
    (`ligase_synthi_panel.svg`) as its backdrop and overlays live, exactly-registered widgets
    on top of — and hiding — each control's static twin (knobs turn, switches/toggles/buttons/
    ring+grid pins respond), placed at the SAME `panel_layout` coordinates the SVG drew them,
    so the overlay tracks the art at any scale. This is pixel parity with the silkscreen, not
    just layout parity. The panel renders on page load (the engine buffers control input until
    Start arms audio). **Every interactive surface is live (as-built, Seq 96–98):** all
    knobs/switches/toggles/buttons, the tone-ring + pattern-grid pins, the 352-pin **mod
    matrix** (`lgR_mx_i_j` → matrix_connect), the **morph joystick** (`joy_x`/`joy_y` → IN
    23/24), the **VU meters** (per-channel output peaks from the worklet), and the **scope XY**
    (ligase~ outlets 10/11 windowed into arrays the worklet reads via `libpd_read_array`, drawn
    on a phosphor canvas). Only the reel-waveform idea was dropped (out of the panel spec).
- **Audio in:** `getUserMedia` → a Web Audio source → the AudioWorklet feeding `[adc~]`.
  Opt-in (mic permission; HTTPS satisfied by Pages). Default OFF (many sessions are reel-only).
- **Reel I/O without a filesystem** (`[openpanel]→load` has nothing to open):
  - *Import:* `<input type=file>` / drag-drop → WAV bytes → write into Emscripten's in-memory
    FS (MEMFS) at a virtual path → `load <path>` (ligase's reel loader reads the WAV bytes).
  - *Export:* `save <virtual-path>` → read the bytes back from MEMFS → `Blob` + `<a download>`.
  - *Persistence:* MEMFS is per-session; optional IndexedDB stash for reels/snapshots across
    reloads (else the user re-downloads).
- **Hosting/threads gotcha:** threaded WASM / `SharedArrayBuffer` needs `COOP`/`COEP` headers,
  which **plain GitHub Pages cannot set**. Mitigation order: (a) build **single-threaded**
  (Pd is single-DSP-thread anyway — natural fit); (b) a `coi-serviceworker` shim; (c) host on
  Cloudflare/Netlify Pages where headers are settable.

### GATE A — owner decisions ([R] = recommendation)

- **A1. Runtime.** [R] **libpd→WASM AudioWorklet + ligase compiled in**. Confirm vs a
  from-scratch WASM port of just the engine (months) — WebPd is already ruled out.
- **A2. Threading/hosting.** [R] **single-threaded, plain GitHub Pages** (no COOP/COEP). Fall
  back to the service-worker shim or Cloudflare Pages only if a thread is ever needed. Confirm.
- **A3. GUI.** [R] `emit_web.py` **fourth emitter** from `panel_layout.py`, driving `lgR_*`.
  Confirm vs a bespoke hand-written web UI (drifts from the layout source).
- **A4. Audio-in.** [R] `getUserMedia`→`[adc~]`, **opt-in, default off**. Confirm.
- **A5. Reel I/O.** [R] file-input/drag → MEMFS → `load`; `save` → MEMFS → Blob download;
  IndexedDB persistence optional-later. Confirm.
- **A6. CI + hosting.** [R] **GitHub Actions (Emscripten SDK) → GitHub Pages**; the automated
  test procedure runs in CI inside the WASM build (via node) = the engine-identity gate. Confirm.
- **A7. License.** [R] confirm libpd BSD / Pd BSD-3 / Emscripten MIT → the **web build is
  distributable as-is** (GPL-2 ligase absorbs BSD); no relicense needed. Verify exact libpd
  license text at Step 1. Confirm.

---

## ARC B — primase~ as an optional trigger/clock source (desktop + web)

### Design

primase's **outlet 1 bangs on every pattern event**, and it has an external-clock inlet.
ligase's transport is entirely bang/message-driven (the **main-inlet bang IS ligase's cycle
clock**; `stut` is bang-triggerable; `play 1` starts transport). So the pairing is pure patch
wiring — **primase is the rhythm brain, ligase the granular voice it triggers**:

- **primase out-1 → `stut`** — each mutating Euclid/transformed event fires a granular
  stutter. The headline pairing.
- **primase out-1 (or out-4 cycle) → ligase main-inlet bang** — primase becomes ligase's
  **master cycle clock**; ligase's own patterns, quantization, and CLOCK-mode playhead lock
  to primase's mutating cycle.
- **primase out-1 → `play 1`** — in one-shot/`loop 0` mode, rhythmic re-triggering of
  transport / grain bursts.
- **primase out-2 (position 0–1) → playhead/scan or a matrix source**; **out-3 (velocity) →
  amplitude or a matrix source** — the pattern's shape as CV.
- **Bidirectional clock option:** a shared `[metro]`/external clock into both (primase inlet 2
  `sync`/`clockfollow` + ligase inlet bang), so they lock to a common grid.

**Deployment symmetry (the reason B is coupled to A):** primase is ALSO a compiled external,
so — desktop: drop `primase.pd_{linux,darwin}` next to ligase (add to the `.plugdata` bundle
as an optional 2nd external); web: compile primase's C sources into the **same libpd-WASM
build** alongside ligase. Both baked in.

### GATE A — owner decisions ([R] = recommendation)

- **B1. Canonical wiring.** [R] default demo = **primase out-1 → `stut`** + **primase clock →
  ligase cycle**, with position→playhead and velocity→amplitude as optional routes; one-shot
  `play 1` documented. Confirm the default.
- **B2. Clock topology.** [R] **primase = master, ligase follows** (simplest, most musical).
  Confirm vs shared-external-clock-into-both.
- **B3. Packaging.** [R] desktop — primase as an **optional** entry in `emit_bundle.py`
  (present-if-built, like the darwin external); web — primase compiled into the WASM build.
  Confirm.
- **B4. Where the wiring lives.** [R] a hand-authored **`pd/ligase_primase.pd`** demo patch
  (primase is external, outside the `panel_layout.py` model) + an optional panel **"CLOCK SRC:
  INT / PRIMASE"** switch routing the main-inlet bang. Confirm.
- **B5. Engine changes.** [R] **NONE** to ligase (already takes external-clock bang, `stut`,
  `play 1`) and **NONE** to primase. Any gap → a B-item, not a hack. Confirm.
- **B6. primase license (BLOCKER for distribution).** primase has **no license file**. [R]
  owner adds one — **GPL-2 to match ligase** (or "GPL-2 or later" to also unblock the VST
  path). Required before primase ships in any bundle. Owner decision.

---

## Steps (after GATE A)

**Arc A**
1. **Toolchain spike + license audit.** Emscripten SDK in CI; build a **stock libpd→WASM
   AudioWorklet** baseline (a sine patch) deployed to a Pages branch; prove single-threaded
   audio works with NO COOP/COEP. Confirm libpd/Pd/Emscripten license texts (A7). **GATE:**
   audible sine from a github.io URL, plain Pages.
2. **Compile ligase in.** ligase sources into the libpd-WASM build; the `-fvisibility`/
   `LIGASE_PUBLIC` static-link path (B7, shared with the VST plan). Run the **automated test
   procedure inside the WASM build (node)** — exact baseline RMS 0.372309 / buffer 0.330109 =
   the engine-identity gate. **GATE:** `[ligase~]` instantiates in WASM; baselines match.
3. **`emit_web.py` GUI.** `panel_layout.py` → HTML/JS control surface driving `lgR_*` over the
   libpd message send; audio out → Web Audio. **GATE:** a knob move in the browser measurably
   drives its param (query/state readback in-page); layout parity with the SVG.
4. **Audio-in + reel I/O.** `getUserMedia`→`[adc~]` (opt-in); file/drag→MEMFS→`load`;
   `save`→MEMFS→Blob download; (IndexedDB optional). **GATE:** record from mic, import a
   dragged WAV reel, granulate, export a WAV download — end to end in the browser.
5. **CI + deploy.** GitHub Actions builds + deploys to Pages; the test gate runs in CI on
   every push. **GATE:** green CI publishes a working page; the engine-identity test is a
   required check.

**Arc B**
6. **Vendor + package primase.** primase compiled into the WASM build (web) + added optional
   to `emit_bundle.py` (desktop, license-gated on B6); author `pd/ligase_primase.pd`; the
   optional CLOCK-SRC panel switch. **GATE:** both builds instantiate `[primase]` next to
   `[ligase~]`.
7. **Verify the pairing (headless).** primase out-1 → `stut` fires a stutter (captured);
   primase-as-clock → ligase cycle locks (quantized events land on primase's grid);
   position/velocity routes measurable. **GATE:** the demo patch drives ligase from primase
   in both desktop and WASM builds, verified in captured audio/state.

## Acceptance criteria

1. A **github.io URL** loads ligase, makes sound, imports a reel (drag WAV) and exports one
   (download), driven by the panel-derived web UI — no install.
2. The **automated test procedure passes inside the WASM build** at the exact baseline (the
   engine is provably the same engine), enforced in CI.
3. **primase drives ligase** `stut`/clock (and optional position/velocity) in BOTH the desktop
   `.plugdata` bundle and the web build; wiring verified in captured audio/state.
4. **Distributable:** the web build ships under GPL-2 with no relicense (BSD libpd); primase
   carries a license (B6).
5. No ligase or primase engine source changes (gaps → B-items).

## Risks / notes

- **CPU.** ligase is heavy (up to 2000 grains); WASM audio in an AudioWorklet must sustain
  real-time at small block sizes — **soak-test**; may need a web-default grain cap
  (`ligase.conf max_grains` is already the knob).
- **COOP/COEP** if any threading creeps in (A2) — plain Pages can't set headers.
- **libpd GUI absence** — mitigated by the `emit_web.py` emitter (A3); nothing renders the
  `.pd` GUI in WASM.
- **primase license** (B6) is the only true distribution blocker for Arc B — decided up front.
- **MEMFS is ephemeral** — reels/snapshots don't survive reload without IndexedDB or download.

## Out of scope

- The VST/AU path (`Plans/vst_plugin.md`) and the existing desktop `.plugdata` bundle (built).
- A full mini-notation web editor; Web MIDI in (a later nicety).
- Collaboration/multiplayer; server-side rendering or account/state sync.
- Changes to primase's own feature set (upstream; this plan only consumes + packages it).
