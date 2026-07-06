# Plan: VST/AU Plugin — ligase~ in a DAW

**Owner:** SLB
**Date:** 2026-07-05
**Status:** PLANNED (GATE A — owner decisions below; recommendations flagged [R]).
**GATE A.2 (license) DEFERRED by owner 2026-07-06** ("I can set the license later, no one
is using it but us right now") — personal-use builds are unaffected; the decision must land
before any *distribution* of a combined plugdata/VST3 build. Not a blocker for v1 build work.
**Tracked in:** `QUEUE.md` §4a (prototyping/UI/VST arc, Seq 71)
**Related:** `Plans/pd_panel_prototype.md` (the panel patch = this plan's v1 GUI),
`docs/modulation_layers.md` (the state model that becomes the preset system),
`Plans/snapshot_expander.md` (the walker/schema that becomes the preset format).

> **PROVENANCE / RESEARCH (2026-07-05).** (1) **Neither plugdata-as-plugin nor
> Camomile loads externals at runtime** — a custom external must be **compiled into
> the plugin binary**. plugdata documents the exact mechanism: add the external's
> sources to the `externals` target in `Libraries/CMakeLists.txt` (or drop `.c` files
> into `Libraries/ELSE/Source`, auto-compiled) and register the setup function in
> `Source/Pd/Setup.cpp` (near `libpd_init_pdlua`). plugdata standalone (the owner's
> current environment) loads `ligase~.pd_darwin` dynamically — that convenience does
> NOT carry into the DAW build. (2) **License:** ligase's `LICENSE` is **plain
> GPL-2** with no "or later" grant in the file or source headers; plugdata and the
> VST3 SDK's open-source option are **GPLv3** — GPL-2-only and GPL-3 are famously
> incompatible for combined distribution. The owner is the sole copyright holder and
> can relicense at will; personal (non-distributed) builds are unaffected either way.
> (3) The engine's non-Pd core is already modular (`grain.c`, `reel.c`, `splice.c`,
> `sphere.c`, `perlin.c`, `morph.c`, `grain_*.c`); the Pd coupling concentrates in
> `ligase~.c` (inlet pipeline, matrix apply, morph capture, message dispatch).

---

## Problem

ligase~ lives only inside a Pd host. The owner wants it in a DAW as a plugin. Two
genuinely different destinations hide in "VST": (v1) *the existing external, hosted* —
fastest path to ligase-in-a-DAW; (v2) *a native plugin* — the engine extracted from Pd
entirely, the long-term product. This plan stages both and makes v1 cheap.

## Staging

### v1 — plugdata build with ligase compiled in (the fast path)

Fork/vendor plugdata; add the 15 ligase sources to the externals target per the
documented mechanism; register `ligase_tilde_setup()`; build VST3 + AU (macOS first —
the owner's DAW machine). Ship the `Plans/pd_panel_prototype.md` panel patch inside as
the working GUI. DAW automation: expose the key continuous controls through plugdata's
parameter objects ([param]-style, GATE A.3) so the DAW can automate the granular
engine (grain size/start/speed/density, delay, filter, mix) without touching the
panel.

What v1 buys: ligase in Ableton/Logic/Reaper with the full panel, presets via
snapshots/surfaces (`morph_save`/`.txt` export), state saved with the DAW project
(plugdata persists the patch). What it costs: a plugdata fork to maintain (rebase per
plugdata release; the diff is ~2 files + sources).

### v2 — native engine extraction (the product; own plan when greenlit)

Extract an engine facade: `ligase_engine.{c,h}` wrapping the existing modules +
the per-block pipeline lifted out of `ligase~.c`, with a message-string dispatch
(the `class_addmethod` table IS the parameter API — keep the selectors as the
canonical control vocabulary so patches, panel, and plugin all speak one language).
Pd shim (`post`/`pd_error`/`gensym` → callbacks) for the few coupled utilities. JUCE
wrapper: parameters bound to the dispatch, GUI = a JUCE rendering of the panel layout
(same `panel_layout.py` source, third emitter), presets = **the schema-v4 text format
verbatim** (the walker gives a complete, versioned, human-readable preset chunk for
free — this is the payoff of the export-schema discipline). v2 is scoped as its own
plan (`Plans/native_plugin.md`) once v1 proves the DAW workflow.

## GATE A (approval) — owner decisions ([R] = recommendation)

1. **v1 path.** [R] **plugdata-with-ligase-compiled-in** (documented mechanism, the
   owner's familiar environment, panel patch = GUI). Confirm vs Camomile (same
   compiled-in requirement, leaner single-purpose binary, but a second codebase to
   learn and the panel would need Camomile's GUI subset) or skipping straight to v2
   (months, not days).
2. **License.** GPL-2-only cannot ship combined with GPLv3 plugdata / VST3-SDK-GPL.
   [R] relicense ligase to **"GPL-2 or later"** (one LICENSE note + header line; sole
   author) — preserves the v2 spirit while unlocking GPLv3 combination. Confirm vs
   dual-license GPL-2/GPL-3 or keep GPL-2-only (then v1 builds are personal-use only,
   which may be fine for now — decide with distribution intent in mind).
3. **DAW-automatable parameter set (v1).** plugdata exposes a limited number of host
   parameters. [R] the ~16 core continuous controls (grain engine 6, delay 4, filter
   3, smear mix, pan, SOS) as host params; everything else via the panel/messages.
   Confirm the list (it's a taste decision — which knobs deserve DAW lanes).
4. **Platform order.** [R] macOS AU + VST3 first (the owner's machine), Linux VST3
   second (CI-testable headless), Windows when someone asks.
5. **Fork maintenance posture.** [R] vendor a pinned plugdata release tag (rebase
   deliberately, not tracking HEAD). Confirm.
6. **v2 greenlight criteria.** [R] decide after living with v1 in the DAW: if the
   plugdata wrapper feels sufficient, v2 may never be needed; if the GUI/CPU/workflow
   chafes, `Plans/native_plugin.md` gets authored with the engine-facade scope above.

## Steps (after GATE A; v1 only — v2 is its own plan)

1. **License housekeeping** (per GATE A.2 decision) + vendor plugdata at a pinned tag;
   build stock plugdata VST3/AU on the Mac toolchain (baseline: unmodified build works).
   **GATE:** stock build loads in a DAW.
2. **Compile ligase in**: sources into the externals target, setup registration,
   `-fvisibility` interplay checked (the B7 symbol-collision fix must not fight
   plugdata's static linking — the LIGASE_PUBLIC macro may need a static-build path).
   **GATE:** plugdata-standalone (built) instantiates `ligase~`; the automated test
   procedure's patches run inside it with the exact baseline numbers.
3. **Plugin smoke**: the VST3/AU in a DAW hosts the panel patch; audio in/out; reel
   load/save; snapshots persist with the DAW session. **GATE:** owner records/plays a
   splice in the DAW; project reopen restores state.
4. **Host parameters** (GATE A.3 list) wired via plugdata's param mechanism;
   automation lane test. **GATE:** DAW automation of grain size audibly tracks.
5. **Docs**: build recipe (`docs/plugin_build.md`), manual note, QUEUE close-out.

## Acceptance criteria

1. A DAW on the owner's Mac runs ligase~ as VST3/AU with the panel GUI; record →
   splice → granulate → morph works end-to-end inside the DAW.
2. The automated test procedure passes at the exact baseline *inside the built
   plugdata* (the engine is provably the same engine).
3. DAW project save/reopen restores the full voice (patch state + reel path).
4. The fork diff against the pinned plugdata tag stays ≤ a few files (maintainability).

## Risks / notes

- **plugdata internals drift** — pin the tag (GATE A.5); the integration diff is small
  by design.
- **Symbol visibility** (B7) — ligase hides everything but its setup; static linking
  into plugdata changes the linking model; verify no collision with plugdata's vendored
  ELSE/cyclone externals (kiss_fft is the known hazard — ligase vendors it, other
  externals may too; may need prefixing in the static build).
- **CPU**: DAW block sizes/sample rates vary more than plugdata standalone; B1's
  SR-agnostic work covers rates, but soak-test at 96k/small blocks.
- **License** is the only true blocker for *distribution* — decided at GATE A.2, not
  discovered at release time.

## Out of scope

- v2 native extraction (own plan, greenlit at GATE A.6 time).
- Windows builds, AAX, standalone-app packaging.
- Panel visual parity beyond the Pd prototype (the JUCE-rendered panel is v2).
