# Plan: Manual Content Edits (code-accuracy pass)

**Owner:** SLB
**Date:** 2026-06-16
**Status:** ON DECK (awaiting go-ahead).
**Source of truth:** `docs/ligase_manual.md` → `make manual` (see `Plans/pdf_manual_regeneration.md`).
**Tracked in:** `QUEUE.md` §2 / §4a.

## Scope

Two streams feed this plan:
1. **Incoming manual content changes** — the pending edits the user has flagged. _(TBD — fill in as specified.)_
2. **Code-accuracy pass** — bring `docs/ligase_manual.md` in line with `src/` (chiefly `ligase~.c`). The first concrete batch (Worklist A) comes from the modulation-coverage audit below; each item is a discrete manual fix.

---

## Worklist A — Modulation coverage audit (3-agent source scan, 2026-06-16)

**Source of truth:** `get_param_range_by_name` (`ligase~.c:3137`, the target dispatcher), the `RAND_TYPE_*` enum (`types.h:338-349`), and the `class_addmethod` block (`ligase~.c:4525-4681`). Manual side verified by direct grep of `docs/ligase_manual.md`.

**Sources (generators): no gaps** — all 9 families (`rand, perlin_1d, perlin_2d, lorenz, nbody, sphere, saw, sine, square`, 4 instances each) are documented. The gaps are all on the target/control side.

### A1. Add 5 modulatable targets the manual omits
Accepted by `get_param_range_by_name` but absent from the "Modulatable Parameters Include" list (manual ~line 2068):
- [ ] `organize` — `ligase~.c:3140`
- [ ] `sos` — `ligase~.c:3141`
- [ ] `env_skew` — `ligase~.c:3146`
- [ ] `gdelay_feed` — `ligase~.c:3148` (token is `gdelay_feed`, **not** `gdelay_feedback`)
- [ ] `gdelay_mix` — `ligase~.c:3150`
- [ ] Also list `modout1`–`modout4` explicitly as targets (`ligase~.c:3184-3187`) — currently shown only in examples.

### A2. Document the missing control message
- [ ] `param_invert <param> <0|1>` — `ligase~.c:4644` (handler `3292`); inverts a param's modulation output. **Zero mentions in the manual.** Add to the modulation Messages section beside `param_range` / `param_slew` / `param_lock` / `param_base_value`.

### A3. Fix the phantom `modout` messages
- [ ] Remove/correct `modout<N>_source` and `modout<N>_range` as standalone commands (manual lines 292-293, 2251, 2261). **These selectors do not exist** (`ligase~.c:4672` comment confirms removal). modout is configured via `param_range` / `rand_type` / `param_base_value` / `param_slew` / `param_invert` with param-name `modoutN` — which the manual's own examples (2299-2307) already show correctly. Reconcile the contradiction.

### A4. Fix the modulatable-parameter count
- [ ] "Supports **21** modulatable parameters" (manual line 2010) → the dispatcher accepts **37** (41 incl. `modout1-4`). Update the number, or drop the hard count.

### A5. Document (or fix) the broadcast-skip behavior — flag to owner
- [ ] `rand_type <type>` with **no** param name (broadcast) silently skips 6 params: `organize, sos, env_skew, gdelay_feed, gdelay_tone, gdelay_mix` (broadcast array `ligase~.c:3431-3467` is out of sync with the 41-token dispatcher). Either document the caveat **or** treat as a code bug and raise separately. Behavioral — decide with owner.

### A6. Broaden the Generator Outlet Sends type list
- [ ] "Generator Outlet Sends" lists type as `rand | perlin_1d | perlin_2d | lorenz | nbody` (manual ~2251); the same `rand_type` grammar also accepts `sphere/saw/sine/square`. Broaden, or note the restriction is editorial only.

### Confirmed NON-issues — do NOT "fix" these (verified 2026-06-16)
- `semitone` / `saw_cycles` / `saw_depth` are **not** modulation targets (no token in `get_param_range_by_name`; `saw_cycles`/`saw_depth` are grain-envelope shapers with their own messages, `ligase~.c:4561-4562`).
- `RAND_TYPE_NONE` has no input token — there is no `none`/`off` rand type.
- `midi` / `bpm` / `fog` appear only in `param_lock`'s `get_current_value` (`ligase~.c:3864-3897`), not as targets.

---

## Acceptance criteria
- Every target accepted by `get_param_range_by_name` is in the manual's modulatable list (or deliberately excluded with a note).
- `param_invert` documented; phantom `modout_*` messages reconciled; count corrected.
- After edits: `make manual`, regenerate the PDF, commit master + PDF.

## Provenance
Worklist A from a 3-agent source scan (sources / targets / message-plumbing) on 2026-06-16; manual side verified by direct grep of `docs/ligase_manual.md`. Logged in `QUEUE.md` §6.
