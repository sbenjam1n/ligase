# RESUME.md — ligase~ session handoff

_Snapshot for picking work back up. Authoritative changelog lives in `QUEUE.md`; this is the
"where we are / how to continue" digest._

## Where we are (2026-06-24)
- **All work merged into `main`.** `main` == `fix/audio-engine-and-manual` == `origin/main` ==
  `origin/fix/...` == **`da34fff`**; 0 commits unmerged; working tree clean.
- Every reported bug/feature (B1–B35, M1) is **implemented and verified headless**. The QUEUE
  active list is **empty** (`QUEUE.md` §1 is now the changelog).
- Remaining is **user hardware/ear sign-off** (not code) + a few stubs (below).

## What ligase~ is
- Pure Data granular synth / sampler / looper / delay external. C, GPL-v2. Repo `sbenjam1n/ligase`.
- **Hardware-synth PROTOTYPE** → design every parameter to be **signal/CV-driven via its inlet**,
  not message-only. Run the hardware in **headless 0** (perfect-signal: honors a control sitting at 0).
- Owner runs **plugdata 0.9.2 on an Intel Mac** + a Focusrite.

## Working conventions (carry these — they bit us when ignored)
- **Commit + push as the owner:** `git -c user.name="sl" -c user.email="sbenja88@gmail.com" commit …`;
  end commit messages with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Push to `main`
  (now primary; keep `fix/audio-engine-and-manual` and `main` in sync — they're identical now).
- **Do NOT regenerate the PDF.** `docs/ligase_manual.md` is the source of truth; the PDF is
  intentionally stale. Run `make manual` ONLY when the owner explicitly asks.
- **No "fog" in new comments** — that effect was removed and replaced by the allpass **smear**.
- **plugdata caches the external**: a new build needs a full plugdata quit+relaunch to load.
- **After pulling, `make clean && make` once** — the Makefile now tracks header deps (`-MMD -MP`,
  B26); without the clean baseline a header change could leave mismatched objects.

## Build & headless-test recipe
- Build: `make` (`gcc -Wall -O2 -fvisibility=hidden -MMD -MP`) → `ligase~.pd_darwin`.
- Headless: `/Applications/Pd-0.51.1.app/Contents/Resources/bin/pd -nogui -nosound -r <SR> -lib ./ligase~ <patch.pd>`,
  capture with `writesf~ 2`, analyze with `sox`/python.
- Test-patch gotchas that cost time before:
  - Wire `stop` → **writesf~** (not ligase~), or the WAV header is never finalized (sox reads 0 frames).
  - To actually HEAR the delay wet: **`sos 0` + `gdelay_mix 1`** (default `sos 0.5` masks it with dry input).
  - Headless 0 honors an unconnected inlet's literal 0 → it overwrites params; use headless 1 for
    message-only tests, or drive the inlets.

## Control surface added/changed in the 2026-06-19→22 arc
- **DD-4:** `delay_glide <ms>` (0–5000, default 20) — de-zippers delay-time changes (msg + CV on inlet 11).
- **Stut:** signal-driven — inlets 11/12/13 MAP their delay-native range → reps 1-16 (lin) / reduction
  0-1 (direct) / spacing 1-5000 ms (exp); messages set native units as the fallback. Layering voice
  pool (up to 64). `stut_length` / `stut_length_mode` / `stut_length_quantize` / `stut_length_quant`.
- **Bencina** (granular cloud, NO pitch — pitch is deliberately smear + morphagene tape speed):
  - `bencina_spread <0-1>` — position scatter / graininess (default **1.0** = the grainy character).
  - `bencina_edge <0-1>` — grain edge-round / de-click (default **0 = OFF**; preserves the skew-edge
    clickiness, which the owner uses creatively).
  - `bencina_level <gain>` — wet makeup driven into the tanh soft-limit (default **6.0**; tanh keeps
    output ≤ ±1 so it can't clip).
  - `bencina_pan` — per-grain random pan as a `param_range` modulation target (the range = stereo
    width+skew; base = pan inlet 22). `bencina_iot` / `bencina_grainsize` / `bencina_wrap` / `bencina_clear`.

## Open / next
- **Stubs (the only non-complete items):**
  1. Build-naming / stale-artifact cleanup — `erosion` leftovers in the Makefile + `src/*.1` backups.
     Cosmetic; do only when cutting a release.
  2. `Plans/manual_content_edits.md` "stream 1" — TBD, awaiting the owner's incoming content edits.
  3. Empty advisory lanes (FRIEND / AUDITOR) in QUEUE.
- **Standing:** regenerate `ligase_manual.pdf` (`make manual`) when asked — many `.md` edits have
  accumulated since the last PDF.
- **Pending sign-off:** the "owner verify / ear-test pending" items across B6–B35 — needs a hardware/ear
  pass in plugdata on the Focusrite. If something misbehaves there, capture `get_inlets` / `get_state`
  from the bad state.

## Pointers
- `QUEUE.md` — full changelog (§1 completed B/M table; §6 history to Seq 45).
- `Plans/completed/` — archived B1/B2/M1 plans. `Plans/manual_content_edits.md` — partial, still active.
- `docs/ligase_manual.md` — manual source (PDF held).
