# RESUME.md — ligase~ session handoff

_Snapshot for picking work back up. Authoritative changelog lives in `QUEUE.md`; this is the
"where we are / how to continue" digest._

## Where we are (2026-06-24)
- **All work merged into `main`.** `main` == `fix/audio-engine-and-manual` == `origin/main` ==
  `origin/fix/...` == **`9495b5e`**; 0 commits unmerged; working tree clean.
- Every reported bug/feature (B1–B35, M1) is **implemented and verified headless**.
- **NEW — TidalCycles pattern subsystem (P1+P2+P3) is feature-complete + headless-verified**
  (QUEUE Seq 47–49; plans `Plans/pattern_*.md`). Mini-notation step-sequencing of any param, a BPM
  quantization cycle, and scale-degree pitch — all built on the existing modulation/BPM/scale engine.
  See the pattern control-surface section below. The only remaining pattern work is the **owner
  ear-test** (musical feel) — not code.
- Remaining overall is **user hardware/ear sign-off** (not code) + a few stubs (below).

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
  - Pattern tests live in `tests/pattern/` (`P1*`/`P2*`/`P3*`). They drive the clock with two `bang`s
    500 ms apart (→ 120 BPM), enable `pattern_debug 1`, and read step/semitone changes from stderr;
    the pitch tests must `record` + `play 1` so grains actually trigger (pitch is applied per grain).

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

## Pattern subsystem (TidalCycles mini-notation) — NEW (Seq 47–49), feature-complete + verified
- **Canonical Tidal mini-notation, space-separated (NO commas — comma=stack is unsupported, and Pd's
  binbuf eats `,` anyway).** `< >` alternation (one member per cycle), `[ ]` subdivision (nestable),
  `@N` weight, `*N`/`!N`, `~` rest.
- `pattern <param> <tokens…>` — step-sequence any `get_param_range_by_name` target (`moog_cutoff`,
  `smear_frequency`, `amplitude`, `modout1`–`4`, …; **NOT** bare `smear`). Auto-allocates a slot +
  attaches via the new `RAND_TYPE_PATTERN` source, reusing the existing invert/map/slew tail. Values
  are 0..1, mapped to the param's `param_range` min/max. `pattern <N>` (numeric slot) loads WITHOUT
  attaching (testing / two-step with `rand_type pattern_N <param>`).
- `pattern pitch <tokens…>` — tokens are **scale degrees** (index into the loaded `pitch_scale`), with
  octave wrap (degree == count → +12 semitones). Auto-sets `PITCH_MODE_PATTERN` (pitch mode 5). Load a
  `pitch_scale` first or it plays unison.
- `pattern_cycle <N/D> <N/D> …` — the quantization cycle as musical durations at the detected BPM
  (`pattern_cycle 4/4 3/8` = 2.75 s @120). No `pattern_cycle` → default 1-bar cycle. Built from the SAME
  `(60000/bpm)*4` grid math as the quant grids; it is a FIFTH free-running clock, independent of them.
- `pattern_clear <param|pitch|N>` — restore the param's prior source / pitch → OFF / free a numeric slot.
- `pattern_debug 1` — log step + applied-semitone changes to stderr (verification aid; off by default).
- Internals: `PATTERN_SLOTS=8` (slot 7 reserved for pitch). Parse is message-thread only into a flat
  weighted step table (validate-then-commit, `step_count` published LAST); the audio thread only READS
  the per-block cache (`pattern_eval_slot` is its sole writer). BPM unset (≤1) → clock frozen at phase 0
  (no NaN). Single-level alternation (ALT-inside-ALT is rejected); Euclid `(k,n)` + group-glued suffixes
  not yet implemented (grammar reserves them).

## Open / next
- **Stubs (the only non-complete items):**
  1. Build-naming / stale-artifact cleanup — `erosion` leftovers in the Makefile + `src/*.1` backups.
     Cosmetic; do only when cutting a release.
  2. `Plans/manual_content_edits.md` "stream 1" — TBD, awaiting the owner's incoming content edits.
  3. Empty advisory lanes (FRIEND / AUDITOR) in QUEUE.
- **Standing:** regenerate `ligase_manual.pdf` (`make manual`) when asked — many `.md` edits have
  accumulated since the last PDF.
- **Pending sign-off:** the "owner verify / ear-test pending" items across B6–B35 **and the new pattern
  subsystem** — needs a hardware/ear pass in plugdata on the Focusrite. If something misbehaves there,
  capture `get_inlets` / `get_state` from the bad state.

## Pointers
- `QUEUE.md` — full changelog (§1 completed B/M table; §4a plan coverage; §6 history to Seq 49).
- `Plans/pattern_notation.md` / `pattern_modulation.md` / `pattern_pitch.md` — the pattern subsystem
  plans (P1/P2/P3), each with a Progress + headless-verification section (all DONE).
- `tests/pattern/` — the headless pattern acceptance patches (`P1*`/`P2*`/`P3*`) + `README.md`.
- `Plans/completed/` — archived B1/B2/M1 plans. `Plans/manual_content_edits.md` — partial, still active.
- `docs/ligase_manual.md` — manual source (PDF held). Now documents the pattern subsystem AND the
  pitch-destination arc + one-shot: SMEAR > Smear Pitch (resonator note->Hz, sources, override, fine),
  PITCH & SPEED > Fine Tune + Channel-Aware MIDI Routing, PLAYBACK CONTROL > One-Shot (loop/trigger),
  plus the MESSAGES quick-list + MODULATION targets (pitch_fine/smear_pitch_fine). **PDF is stale vs the
  `.md`** — run `make manual` only when the owner asks (much has accumulated).
