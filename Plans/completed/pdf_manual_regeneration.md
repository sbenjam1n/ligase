# Plan: PDF Manual Regeneration

**Owner:** SLB
**Date:** 2026-06-16
**Status:** ✅ DONE (2026-06-16). All gates cleared; single-source pipeline live, PDF regenerated & current.
**Tracked in:** `QUEUE.md` §1 (AGENT lane, item M1)

## Progress (2026-06-16)
- **GATE 0 ✓** — toolchain chosen + installed: `pandoc 3.10` + `weasyprint 69.0` (native deps already present via Homebrew). Smoke-tested end-to-end.
- **GATE 1 ✓** — `docs/ligase_manual.md` master built from `src/ligase_manual.txt` via `scripts/manual_txt_to_md.py` (strips zero-width/BOM, drops dotted TOC, promotes 17 ALL-CAPS sections to `#`, normalizes bullets). 0 stray control chars; FOG present; inlet 15 = Fog Mix.
- **GATE 2 ◐** — light spot-check done (FOG section + inlet table present and code-consistent). The *deep* manual-vs-`src/ligase~.c` accuracy pass is deferred to the ON-DECK manual-content edits.
- **GATE 3 ✓** — `make manual` target added (`pandoc … --toc --pdf-engine=weasyprint --css docs/manual.css`) + `docs/manual.css` (title page, auto TOC, page numbers, mono code). Regenerated `ligase_manual.pdf` (44 pp); visually verified title page + INLETS page. Old stale PDF archived at `docs/ligase_manual_OLD_prefog.pdf`.
- **GATE 4 ✓** — `src/ligase_manual.txt` **deleted** (user decision 2026-06-16); the Markdown master is now the sole hand-edited source. `make manual` confirmed working without it. README `Documentation` link still resolves (PDF regenerated in place). The `.txt` remains recoverable from git (commit `0580679`).

## Objective

Establish a **single, diffable source of truth** for the ligase~ manual and a
**one-command, reproducible build** that regenerates `ligase_manual.pdf` from it.
Then regenerate the shipped PDF so it matches the current code — and so that the
upcoming manual content edits have a clean place to land.

## Why now (current state)

The repo ships two manuals that have **drifted apart**:

| | `ligase_manual.pdf` (root, 44 pp) | `src/ligase_manual.txt` (3697 lines) |
|---|---|---|
| FOG (Spectral Effect) section | **absent** | present |
| `fog_position` / `fog_pool_size` / per-grain fog | absent | present |
| Sphere generator, `saw_cycles`, Gaussian envelope | absent | present |
| Inlet 15 | `Distortion - Drive intensity` | `Fog Mix - Spectral fog blend` |

Verdict (established 2026-06-16, via `pdftotext` + normalized diff): **the PDF is
the older revision; the `.txt` is current and matches the codebase** (`grain_fog.c`,
the fog keys in `ligase.conf`, the README signal flow, the all-`✓ DONE` fog items
in `TODO.md`). The normalized diff confirmed every PDF-only line was formatting
noise (page numbers, TOC dot-leaders, `●` bullets) — **no content exists only in
the PDF**, so `.txt` is a safe basis.

Blocking reality: **no PDF converter is installed** (`pandoc`, `wkhtmltopdf`,
`weasyprint`, all TeX engines absent as of 2026-06-16). The `.txt` also carries
Google-Docs export artifacts (zero-width spaces `U+200B`, dotted-leader TOC,
`●` bullets) that must be cleaned in the source master.

## Definition of done

1. One **Markdown master** (`docs/ligase_manual.md`) is the sole source the manual
   is authored in.
2. `make manual` regenerates `ligase_manual.pdf` **deterministically** from that master.
3. The regenerated PDF includes the FOG section, sphere/saw/Gaussian features, and
   shows **inlet 15 = Fog Mix** — i.e. it matches the current feature set.
4. README's `Documentation` link still resolves to the regenerated PDF.

## Approach

Author in Markdown → build to PDF via a CSS/HTML or LaTeX pipeline → commit both
master and PDF. Wire it into the existing `Makefile` so regeneration is `make manual`,
mirroring how `make` already drives the build.

## Steps (each ends at a GATE)

### Step 0 — Toolchain decision  ⟶ GATE 0 (DECISION, needs user)
No converter is installed. Pick one and install it:
- **Recommended:** `pandoc` + `weasyprint` (`--pdf-engine=weasyprint`). Pip-installable,
  no TeX, full CSS control — good for a manual with a title page + auto TOC.
- **Higher typographic fidelity:** `pandoc` + `basictex` (LaTeX). Heavier install.
- **Lightest HTML route:** `pandoc` + `wkhtmltopdf`.
GATE 0: user confirms the toolchain; it is installed and `--version` works.

### Step 1 — Build the Markdown master  ⟶ GATE 1
Convert `src/ligase_manual.txt` → `docs/ligase_manual.md`:
- strip zero-width / format chars (`U+200B`, `U+FEFF`, NBSP), normalize `●`/`*` bullets;
- rebuild the table of contents as Markdown headings (let pandoc auto-generate the TOC —
  drop the hand-typed dot-leader TOC);
- preserve the copyright / GPL-v2 header for the title page.
GATE 1: master renders cleanly to HTML; section/heading structure intact; no stray
control characters (`grep -nP '[\x{200B}\x{FEFF}]'` returns nothing).

### Step 2 — Content reconciliation check  ⟶ GATE 2
Confirm the master is complete and code-accurate:
- nothing content-bearing was lost from the old PDF (already established: PDF-only lines
  were formatting only — re-verify after the markdown conversion);
- spot-check load-bearing facts against `src/ligase~.c` (inlet table, message names,
  default values) — the manual should track the **code**, not just the old `.txt`.
GATE 2: master verified against source; discrepancies (if any) logged for the
manual-content edits (see ON DECK in QUEUE.md).

### Step 3 — Build target + styling  ⟶ GATE 3
- add a `manual` target to the `Makefile`: `pandoc docs/ligase_manual.md --toc
  --pdf-engine=<chosen> -o ligase_manual.pdf` (+ a small CSS/template for title page,
  page numbers, monospace for message syntax);
- add `manual` to `.PHONY`.
GATE 3: `make manual` produces `ligase_manual.pdf`; spot-check — TOC present, FOG
section present, inlet 15 = Fog Mix, page count sane (~40–50 pp).

### Step 4 — Retire the divergence  ⟶ GATE 4
- regenerate the shipped PDF (replaces the stale one);
- decide the fate of `src/ligase_manual.txt`: either delete it, or regenerate it from
  the master (`pandoc -t plain`) so there is exactly one hand-edited source;
- confirm README `Documentation` link still resolves.
GATE 4: single source of truth in place; PDF + (optional) txt are both generated
artifacts; committed.

## Workflow after this plan lands

Future "changes that require manual edits" edit **`docs/ligase_manual.md` only**, then
run `make manual` and commit both the master and the regenerated `ligase_manual.pdf`.
(Optional follow-up: a pre-commit / CI check that the PDF is regenerated whenever the
master changes.)

## Risks / open decisions

- **Toolchain install** (GATE 0) is the one real blocker — requires network +
  Homebrew/pip; it is a user decision, not a default.
- **Visual fidelity** vs the original Google-Docs look: acceptable to diverge.
  Priority is *accuracy + reproducibility*, not pixel-matching the old PDF.
- **Manual ≠ code is a separate risk:** this plan makes the manual *buildable and
  current vs the `.txt`*; verifying the manual vs `src/ligase~.c` is folded into the
  manual-content edits tracked separately in QUEUE.md ON DECK.
