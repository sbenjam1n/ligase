# Plan B1: Sample-Rate-Agnostic Engine & Consistent Buffering

**Owner:** SLB
**Date:** 2026-06-16
**Status:** ✅ DONE (2026-06-16; archived 2026-06-22). Steps 1+2 implemented and verified headless (Tier-1) at 44.1/48/96 kHz — delay tap correct, distortion bounded, reel duration consistent + WAV header rate correct, 96 k round-trip intact (QUEUE §6 Seq 5/6/12). Only the optional user Tier-2 Focusrite ear-check remains — that is hardware sign-off, not code. (Header below predates completion; left as the original plan record.)
**Tracked in:** `QUEUE.md` §1 (AGENT lane, B1). Verification harness: `TEST_PLAN_MACOS.md`.
**Related:** Plan B2 (`reel_io_macos.md`) shares the WAV-header sample-rate fix (§Fix-4 below).

## Progress (2026-06-16)
**Environment found:** installed Pd is **0.51.1** (`/Applications/Pd-0.51.1.app`), not the `Pd-0.53-2` the Makefile's `PD_INCLUDE` points at — build currently works only via the vendored `src/m_pd.h` (quote-include). The committed `src/*.o` are stale/other-platform, so **`make clean` is required** before `make`. The Focusrite was **not enumerated by CoreAudio** at scan time (see `TEST_PLAN_MACOS.md` prerequisite) — Tier-2 hardware tests blocked until it appears. *(Follow-ups: fix Makefile `PD_INCLUDE` → installed Pd — needed for B2's `g_canvas.h`; stop committing `src/*.o`.)*

**Step 1 DONE (F1, F2, F3, F6 + dispatch):**
- F1 `grain_delay_set_sample_rate()` (`grain_delay.c`) — reallocates the 9.5 s line for the new rate, resets `write_pos`/feedback/lpf state.
- F2 `grain_delay_bencina_set_sample_rate()` (`grain_delay_bencina.c`) — recomputes `trigger_period_samples`, clears grains.
- F3 `grain_distortion_set_sample_rate()` (`grain_distortion.c`/`.h`) — re-runs all SR-dependent coeff updaters + resets IIR state.
- F6 — block-size bail (`ligase~.c`) now zeros the output buffers instead of leaving them untouched.
- Dispatch — `ligase_set_sample_rate()` helper + change-gated call in `ligase_dsp` (`ligase~.c`): subsystems re-init only when `sp[0]->s_sr` actually changes (not on every dsp re-add).
- **Verified:** `make clean && make` clean (no warnings); new setters exported; headless run at **96000 Hz** loads + records 3 s correctly, no crash.

**Tier-1 headless verification — PASS at 44.1 / 48 / 96 kHz (2026-06-16):**
- `test_delay.pd` (new): 6 s delay tap lands at [6.0–7.0 s] (RMS ≈ 0.085) at **every** rate; the pre-fix clamp zone [4.5–5.0 s] is silent (0.000) — i.e. the 96 kHz long-delay clamp (the headline "delay doesn't work" bug) is fixed.
- `test_dist.pd` (new): heavy distortion (0.8) + resonant Moog (res 3.8) → output bounded (max ≈ 0.02, RMS finite, no NaN/Inf) at every rate — no blow-up (the "clips/drops out" symptom).
- Both patches capture ligase~'s **live output** via `writesf~` (writes at the true `-r` rate, bypassing the hardcoded-48k reel save); `sos 0` isolates the granular bus from input monitoring.

**Remaining:**
- **Tier-2 (Focusrite, now connected — Scarlett 2i2):** audible/GUI tests per `TEST_PLAN_MACOS.md` — clean audio + working delay at 44.1/96 k, live interface-swap re-init. *(User-driven; needs listening.)*
- **Step 2 → GATE C:** F4 reel/WAV runtime-SR (blocked on the reel-sizing decision) + F5 fog_pool/scheduler scalar setters. Then update the manual to drop "48 kHz fixed".

## Problem
With an external interface (e.g. Focusrite at 44.1 kHz / 96 kHz, not 48 kHz): delay effects don't work, audio clips and drops out. ligase~ must run correctly and buffer consistently at **any** sample rate, with or without an interface. The manual currently claims "Sample rate: 48000 Hz (fixed)".

## Root cause (3-agent scan, 2026-06-16; file:line verified)
The earlier "SR frozen at construction" guess is **half right**. `ligase_dsp` *does* push the live rate (`sp[0]->s_sr`) into every subsystem's scalar `sample_rate` field (`ligase~.c:1627-1659`). The defect is that **derived/cached values computed once at create time (from a hardcoded `48000`) are never recomputed** — there are **no `*_set_sample_rate` reinit functions anywhere** in the codebase. Construction hardcodes 48000 at `ligase~.c:4375,4377-4381,4384,4509`.

Ranked causes:
1. **Delay buffer sized once at 48 kHz** — `grain_delay.c:25` `buffer_size = 9.5f * sample_rate` (created with the literal 48000). At 96 kHz the buffer holds only 4.75 s, so any delay time > 4.75 s is silently clamped (`grain_delay.c:82`) while `grain_delay_set_time` still accepts 9.5 s (`grain_delay.c:252`). → **the top half of the delay range is dead at high SR** = "delay doesn't work."
2. **Distortion IIR coefficients not recomputed on SR change** — `update_{lowpass,notch,emphasis}_coeffs` + the oversampling anti-alias LP use `omega = 2π·f/sample_rate` from the rate at last-set (`grain_distortion.c:26,42,71,107,156-157`); `ligase_dsp` writes `dist->sample_rate` (`:1659`) but never re-runs them. 48k-tuned poles at 44.1/96k detune and can destabilize resonant/notch stages → soft-clip blow-ups = "clips/drops out."
3. **Bencina `trigger_period_samples` frozen** at create (`grain_delay_bencina.c:31`; recomputed only via the spacing setter `:251`) → wrong grain spacing until a spacing message is re-sent.
4. **Reel buffer + WAV I/O hardwired to `SAMPLE_RATE`=48000** (`types.h:323`; `reel.c:15,48,68,130`; load rejects ≠48k `reel.c:270-274`; save stamps 48k `reel.c:385`) → inconsistent buffering across rates; non-48k WAVs rejected. (WAV-header half shared with Plan B2.)
5. **Hard block-size ceiling 8192** — scratch buffers `float[8192]` (`ligase~.c:274-279`); perform bails if `n > 8192` (`ligase~.c:1497`) returning **without writing outputs** → total dropout if an interface negotiates a larger block (latent; CoreAudio usually ≤2048).

NOT bugs (do not "fix"): the BPM/quant clock path is SR-correct — the `14112.0` constant (`ligase~.c:1842,2403`) is the genuine Pd time unit; moogladder and stut spacing recompute live and self-correct.

## Fix surface
The re-init hook already exists: **`ligase_dsp` (`ligase~.c:1614-1693`)**. Pd re-calls it on any SR/blocksize change. Make it detect a change (compare `sp[0]->s_sr` to previous `x->sample_rate`) and call new propagation functions instead of poking scalars:
- **F1** `grain_delay_set_sample_rate(d, sr)` — update field, **reallocate** `buffer_{left,right}` to `9.5f*sr`, reset `write_pos`, clear `lpf_state_*`, re-clamp `current_delay_samples`.
- **F2** `grain_delay_bencina_set_sample_rate(b, sr)` — update field, recompute `trigger_period_samples` from `grain_spacing_ms`, reset grain pool.
- **F3** `grain_distortion_set_sample_rate(dist, sr)` — update field, **re-run all coeff updaters** and **reset filter state** (avoid transient blow-up).
- **F4** Make the reel runtime-SR-aware: store `reel->sample_rate`; size by seconds×runtime-SR (reallocate on change) or allocate for a max supported SR; remove the 48k load gate (`reel.c:270-274`), write the true rate in the header (`reel.c:385`). *(Coordinate with Plan B2 — same functions.)*
- **F5** Scalar-only setters for scheduler / moogladder / fog / stut (consistency; recompute fog smoothing time-constants if exact).
- **F6** Block size: allocate scratch from `n` in `ligase_dsp`, or raise the ceiling; at minimum **zero the outputs** on the >8192 bail path (`ligase~.c:1497-1500`).

Secondary risks: reallocate only in `ligase_dsp` (main thread, dsp lock held) — never in `ligase_perform`; reset delay/feedback/grain-pool state on realloc; a 96k×600s stereo reel ≈ 0.9 GB if sized for worst case — consider lowering `MAX_REEL_SECONDS` or reallocating by runtime SR.

## Steps & gates
- **GATE A (approval)** — confirm scope + the reel-sizing strategy (reallocate-by-SR vs allocate-for-max-SR vs keep file I/O at 48k w/ resample). Needs owner.
- **Step 1 → GATE B** — add F1–F3 + F6 (delay realloc, bencina recompute, distortion coeff+state reset, blocksize safety); wire SR-change detection in `ligase_dsp`.
- **Step 2 → GATE C** — F4/F5 reel + remaining subsystem setters.
- **Step 3 → GATE D (verify)** — build; run the audio test at **44100, 48000, 96000** (`pd -r <sr> -nogui …`, extend `AUTOMATED_TEST_PROCEDURE.md`): (i) delay audible at a long delay time at every rate; (ii) no clipping/dropouts with distortion engaged; (iii) record→save→load round-trip consistent; (iv) live SR switch (48k→96k) doesn't glitch. Then update the manual to drop "48 kHz fixed".

## Acceptance criteria
Delay works across its full range at 44.1/48/96 kHz; no distortion-induced clipping at non-48k; record/playback buffer timing correct at any rate; switching interfaces (SR change) re-inits cleanly; no dropout from block size.
