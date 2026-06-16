<!-- QUEUE.md — THE authoritative work-ordering for the ligase~ project. PLANNER-WRITE-ONLY.
================================ WRITE RULE (PERMANENT) ================================
- Only the PLANNER (SLB) may set, add, remove, reorder, or re-prioritize items in this
  queue. The ordering in §1 is the single source of truth for "what to work on next."
- The AGENT is READ-ONLY here: it executes the top UNBLOCKED item in its lane (§1), and
  NEVER edits this file. It reports progress, completions, and blockers via a commit/PR
  note and/or MESSAGE.md; the planner reconciles them into this queue. The agent MAY
  propose a reordering, but only in MESSAGE.md/a note — a proposal is not a change until
  the planner makes it here.
- Every change to §1 increments **Queue Seq** and adds a one-line §6 history entry signed
  "SLB". An unsigned or non-SLB edit to the ordering is invalid by convention.
- This file ORDERS; it does not DUPLICATE. Each item points to its plan + step and its
  tracking note. Truth lives in the pointed-to doc; if they disagree, the doc wins and the
  planner fixes the pointer.
======================================================================================== -->

# QUEUE.md — Authoritative Work Queue

**Queue Seq:** 10
**Set by:** SLB (planner)
**Date:** 2026-06-16
**Relationship to other coordination files:** `MESSAGE.md` = tactical, per-turn handoff (nuance, the current catch). **QUEUE.md = the standing ordering** (survives turns). When they differ on *what's next*, QUEUE.md wins; MESSAGE.md carries the *how/why* of the top item.
**What this queue draws from:** active execution plans (`Plans/*.md`) and the project's own indexes (`README.md`, `TODO.md`). Items are promoted from backlog → active by the planner only (see §4).

> **Planner-write-only.** The agent reads this (see §5) and is read-only here. Section numbers are stable references — **append, never renumber.** Log every change in §6.

---

## §0. STRATEGIC MAP — where the project is, where it's going

ligase~ is a Pure Data granular synthesizer/sampler/looper/delay external (C, GPL-v2). The
feature set is complete, but two **major runtime bugs** were reported 2026-06-16 and are now
the top priority: (B1) effects/buffering break at non-48 kHz sample rates (delay dead, audio
clips/drops out with external interfaces like a Focusrite); (B2) reel load/save is broken on
macOS. Both are scoped with verified root causes. Documentation work (manual single-source =
DONE; content edits = on deck) drops below the bug fixes.

| Objective | Status | Lane | Source |
|-----------|--------|------|--------|
| **B1 — Sample-rate-agnostic engine & consistent buffering** | IN-PROGRESS (Step 1 done; GATE B) | AGENT | `Plans/sample_rate_buffering.md` |
| **B2 — Reel load/save on macOS** | SCOPED (awaiting go-ahead) | AGENT | `Plans/reel_io_macos.md` |
| Manual content edits (code-accuracy pass) | ON-DECK | AGENT | `Plans/manual_content_edits.md` |
| Manual on a single source of truth + reproducible PDF build | DONE | AGENT | `Plans/pdf_manual_regeneration.md` |
| Stale-artifact / build-naming cleanup (`erosion` leftovers) | BACKLOG | AGENT | §4 |
| Core external (build, fog, modulation engine) | DONE | — | `README.md`, `TODO.md` |

## §0.5 ACTIVE INDEXES (fixed pointer — do not catalogue here)

- `README.md` — feature overview, build, install, signal flow.
- `TODO.md` — change log (all items `✓ DONE`); the "don't change" notes.
- `AUTOMATED_TEST_PROCEDURE.md` — audio-routing regression test (Linux).
- `TEST_PLAN_MACOS.md` — macOS + Focusrite test plan (SR sweep, buffering, reel I/O); verifies B1/B2.
- `Plans/` — execution plans.

## §1. ACTIVE QUEUE (ordered; the agent runs the top UNBLOCKED item in the AGENT lane)

### AGENT lane
_Top unblocked item: **B1** (then B2). Both scoped and sitting at GATE A (approval) — awaiting the user's go-ahead to start implementation._

| # | Item | Status | Gate to stop at | Plan |
|---|------|--------|-----------------|------|
| B1 | Sample-rate-agnostic engine & consistent buffering (delay dead + clicks/dropouts off-48k) | **IN-PROGRESS — Step 1 verified (Tier-1)** | Step 1 (F1/F2/F3/F6 + SR dispatch) DONE; headless Tier-1 PASS at 44.1/48/96 kHz (delay tap correct via `test_delay.pd`; distortion bounded via `test_dist.pd`). Remaining: Tier-2 Focusrite audible tests (user) + Step 2 reel/WAV SR (needs reel-sizing decision). | `Plans/sample_rate_buffering.md` |
| B2 | Reel load/save on macOS | **SCOPED — at GATE A** | GATE A (approval): path-fix-only vs full robust parser; file I/O 48k-canonical vs follow engine rate. Root cause verified: raw `s->s_name`→`fopen`, no canvas-relative resolution; macOS Finder CWD=`/`. | `Plans/reel_io_macos.md` |
| B3 | "Empty splices" report (OVERDUB silent at high SOS) | **CLOSED — NOT A BUG (reverted)** | OVERDUB `input×(1−sos)` is DESIGNED SOS attenuation (incl. initial pass — confirmed by owner 2026-06-16). My virgin-territory "fix" altered designed SOS behavior and was fully reverted (`reel.c`/`types.h` back to original). | (n/a) |
| B5 | Overdub Time Lag Accumulation | **DONE — hardware-confirmed (2026-06-16)** | Record head loops within current splice + records granular playback fed back (SOS-coeff capped 0.95 + ±1 feedback clamp + tanh ceiling). Owner confirmed "expected splice behavior" on the Focusrite. `TLA_FEEDBACK_MAX` tunable. | (inline; `test_tla.pd`) |
| B4 | `recsplice` (NEW_SPLICE) bypassed the SOS VCA | **FIXED (2026-06-16)** | NEW_SPLICE was lumped with INPUT_ONLY (forced full-replacement of raw input), so SOS didn't act as a VCA there — contradicting the design ("recinput is the only non-VCA mode") and the manual. Fix: the two `sos_mode==1` record sites in `ligase~.c` now copy `out_left/out_right` (the SOS-mixed monitor signal) for NEW_SPLICE; `recinput` raw + `sos_mode==0` untouched. Verified: recsplice sos 0/0.5/1 → 0.00/0.41/0.58 (constant-power VCA); recinput raw 0.58; overdub `1−sos` unchanged. | (inline; `test_rec.pd`) |
| M1 | PDF manual regeneration — single source + `make manual` | **DONE (2026-06-16)** | All gates cleared. Source of truth = `docs/ligase_manual.md`; `make manual` regenerates `ligase_manual.pdf`; `src/ligase_manual.txt` deleted. | `Plans/pdf_manual_regeneration.md` |

### SLB lane (planner; runs in parallel, agent does not wait on these)
- ✓ Manual-content-edits plan authored (`Plans/manual_content_edits.md`), seeded with Worklist A (modulation coverage audit). Awaiting the user's incoming content changes to fill stream 1.

### FRIEND lane (advisory)
- _(none)_

### AUDITOR lane (audit agent; runs the audit loop, idle-fallback only)
- _(none)_

## §2. ON DECK (planner-ordered; promote into §1 when the blocking item clears)
- **Manual content edits** — plan: `Plans/manual_content_edits.md`. M1's GATE 3 is cleared (master + `make manual` exist), so this is ready to promote on the user's go-ahead. **Worklist A is seeded** from the 2026-06-16 modulation-coverage scan (5 missing targets, `param_invert` undocumented, phantom `modout_*` messages, stale "21" count); stream 1 (the user's incoming content changes) is still to be specified.

## §3. BLOCKED (visible but skipped; ordered by what unblocks them)
- _(none.)_ M1's GATE 0 cleared (pandoc + weasyprint installed); it now rests at GATE 4, a decision checkpoint, not a block.

## §4. BACKLOG / IDLE (NOT active — pointers only; planner promotes to §2/§1)
- **Build-naming / stale-artifact cleanup:** `Makefile` still carries `erosion` naming (`@region:erosion_pd.utils`, `erosion_query_test`); `src/` has leftover `erosion~.o` and `.1` backup dupes (`kiss_fft.c.1`, `_kiss_fft_guts.h.1`). Cosmetic; promote only if a release is being cut.

## §4a. PLAN COVERAGE — queue-wide build-out (planner workstream)

| Item | Plan | Status |
|------|------|--------|
| B1 Sample-rate / buffering | `Plans/sample_rate_buffering.md` | ✓ |
| B2 Reel load/save on macOS | `Plans/reel_io_macos.md` | ✓ |
| M1 PDF manual regeneration | `Plans/pdf_manual_regeneration.md` | ✓ |
| Manual content edits | `Plans/manual_content_edits.md` | ◐ stub (Worklist A seeded; stream 1 TBD) |
| Build-naming cleanup | _(none; backlog stub)_ | — |

## §5. HOW THE AGENT USES THIS (read-only protocol)

1. Read §1. Take the **top item in the AGENT lane whose Status is ACTIVE and is not BLOCKED**.
2. Do it to its gate/checkpoint. Record the outcome in a commit/PR note (and a `MESSAGE.md` note if a handoff/nuance is needed). Apply the usual discipline (verify, don't rubber-stamp; be honest about what's done vs asserted).
3. At a GATE, **STOP and report** — do not roll past a gate without the planner.
4. If the top item is blocked or you believe the order is wrong, **PROPOSE in `MESSAGE.md`; do not edit this file.** The planner reorders.
5. When you finish an item, say so in the note; the planner marks it DONE here and advances the queue (increments Queue Seq).
6. `MESSAGE.md` (SLB direction) **overrides** the planned next step for the current turn.

## §6. Version History (planner-only; sign "SLB")

| Date | Seq | Change | By |
|------|-----|--------|----|
| 2026-06-16 | 0 | Template instantiated for ligase~. Added M1 (PDF manual regeneration, `Plans/pdf_manual_regeneration.md`); manual-content-edits on deck (§2); build-naming cleanup to backlog (§4). | SLB |
| 2026-06-16 | 1 | M1 advanced: GATE 0 cleared (pandoc+weasyprint installed), Steps 1 & 3 done (Markdown master `docs/ligase_manual.md` + `make manual` build), PDF regenerated & current. M1 now at GATE 4 (txt-fate decision). §3 unblocked. | SLB |
| 2026-06-16 | 2 | M1 DONE: GATE 4 cleared — `src/ligase_manual.txt` deleted per user; single-source pipeline live. Manual-content edits (§2) now unblocked, awaiting go-ahead. | SLB |
| 2026-06-16 | 3 | Modulation-coverage scan (3 agents) recorded. Authored `Plans/manual_content_edits.md` with Worklist A (5 missing modulatable targets: organize/sos/env_skew/gdelay_feed/gdelay_mix; `param_invert` undocumented; phantom `modout_*` messages; stale "21" count). §2, §4a, and the SLB lane updated to point at it. | SLB |
| 2026-06-16 | 4 | Two major runtime bugs scoped (2-agent root-cause scan) and placed at top of §1 AGENT lane: B1 sample-rate/buffering (`Plans/sample_rate_buffering.md`) and B2 reel load/save on macOS (`Plans/reel_io_macos.md`). Both at GATE A (approval). §0 map re-prioritized: bugs above doc work. | SLB |
| 2026-06-16 | 5 | `TEST_PLAN_MACOS.md` authored (Focusrite + SR-sweep). B1 Step 1 implemented (F1 delay realloc, F2 bencina recompute, F3 distortion coeff+state reset, F6 blocksize-bail silence, + change-gated `ligase_set_sample_rate` dispatch in `ligase_dsp`); `make clean && make` clean; headless smoke at 96 kHz passes. B1 → IN-PROGRESS at GATE B. (Env notes: installed Pd 0.51.1 vs Makefile 0.53 path; stale `src/*.o` need `make clean`; Focusrite not yet enumerated by CoreAudio.) | SLB |
| 2026-06-16 | 6 | B1 Step 1 functionally verified headless (Tier-1) at 44.1/48/96 kHz: `test_delay.pd` (6 s tap correct, 96 k clamp gone) + `test_dist.pd` (resonant distortion bounded, no NaN). Focusrite (Scarlett 2i2) now enumerated. Remaining: Tier-2 audible tests (user) + Step 2 reel/WAV SR (reel-sizing decision still open). | SLB |
| 2026-06-16 | 7 | B3 added + FIXED: empty-reel OVERDUB recorded silence when SOS up (recorded `input×(1−sos)`; pre-existing, NOT a B1 regression — recording code untouched by B1). Fix in `reel.c`: per-record `initial_length`; virgin samples capture full input regardless of SOS, SOS-crossfade only for genuine overdub onto existing content. Verified headless (sos 1.0 silent→full). Tests `test_rec.pd`, `test_input.pd` added. Awaiting user hardware confirmation (live input via Focusrite, `record 1` with SOS up). | SLB |
| 2026-06-16 | 8 | B3 CLOSED — NOT A BUG. Owner confirmed SOS is designed to attenuate the initial recording too; OVERDUB `input×(1−sos)` is intended. Seq-7 "fix" reverted in full (`reel.c`/`types.h` restored). Build clean; B1 changes remain intact and untouched. | SLB |
| 2026-06-16 | 10 | B5 IMPLEMENTED (owner-directed): overdub Time Lag Accumulation. (1) Record head now loops WITHIN the current splice (`reel.c` — was running off splice_end and extending the reel); cross-splice is via `shift`. (2) Overdub records the granular playback fed back (so pitch/grain settings compound), with a sub-unity SOS-scaled feedback coefficient (`TLA_FEEDBACK_MAX=0.95`), the granular feedback clamped to ±1 before the coefficient (grain engine has no gain-comp), and a tanh output ceiling — stable, no runaway. Verified headless: loops in splice (reel stays 0.3 s), bounded (max 0.66, RMS 0.28, no rail); recinput/recsplice unaffected. Grounded in Morphagene TLA research. Ear test on hardware pending; `TLA_FEEDBACK_MAX` tunable. | SLB |
| 2026-06-16 | 9 | B4 FIXED: `recsplice` (NEW_SPLICE) now honors SOS-as-VCA. It had been merged with INPUT_ONLY (recorded raw input, SOS bypassed), contradicting design + manual. Both `sos_mode==1` record sites in `ligase~.c` now record the SOS-mixed monitor output (`out_*`) for NEW_SPLICE; `recinput` (raw) and `sos_mode==0` left untouched; OVERDUB unchanged. Verified headless (recsplice sos-VCA constant-power; recinput raw; overdub `1−sos`). Owner-confirmed intended behavior. | SLB |
