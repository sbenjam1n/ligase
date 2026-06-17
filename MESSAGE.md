# MESSAGE.md — tactical handoff (agent → planner SLB)

**Date:** 2026-06-16
**Branch:** `fix/audio-engine-and-manual`

## Fog wet/dry volume — "adding fog drops output volume" (user report)

**Root cause was a real normalization bug, not just a missing makeup gain.**
The magnitude filter (`filter_magnitudes`, `src/grain_fog.c`) soft-limited every
spectral magnitude bin with a *fixed* knee — `tanhf(filtered * 0.5f) * 2.0f` —
which hard-caps each bin at a magnitude of 2.0. But these are **un-normalized
FFT magnitudes**; for a hot signal they run ~5–10 per bin, so the limiter crushed
the entire wet path by ~12 dB *independent of mix setting*. Measured headless:
full-wet RMS was 0.087 vs dry 0.438 (≈14 dB down, even after a 4× makeup clamp).

### Fix (two parts)
1. **Limiter normalization (the real fix):** soft-limit relative to the bin's own
   input magnitude — `ceil = 8*x_n + 1e-6; filtered = ceil * tanhf(filtered/ceil)`.
   Transparent in the passband (`tanh(1/8)*8 ≈ 0.995`), still bounds high-Q
   resonance runaway (the original limiter's stated intent). `grain_fog.c` ~L112.
2. **Setting-agnostic makeup gain (safety net):** one-pole followers on |dry| and
   |wet| (~80 ms), smoothed/clamped makeup `lvl_dry/lvl_wet` ∈ [0.5, 4.0] (~250 ms),
   applied to the wet signal before the constant-power crossfade, with a final ±1
   safety clamp. New `grain_fog_t` fields `lvl_dry/lvl_wet/makeup` (`types.h`),
   inited in `grain_fog_create`. Keeps wet ≈ dry across *other* smear/filter/Q
   settings where the spectral processing still loses a few dB.

### Verification (headless, 48 kHz, `test_fog_level.pd`)
- Full-wet RMS **0.454** vs dry **0.438** → within **~0.3 dB** ("roughly equal", per ask).
- Onset 0→1 jump: smooth monotonic ease-in over ~0.5 s (FFT warmup + makeup
  converge); no pumping, no dropout. At defaults makeup sits ≈ unity.
- `make clean && make` clean; no new warnings.

### Gate / pending (USER)
- **Ear-test** full-wet vs dry on real material; confirm levels feel matched and the
  effect still sounds right after the limiter change (it raises wet brightness/level).
- If the ~0.5 s ease-in feels slow when sweeping the fog inlet, I can shorten the
  makeup time constant.

> Reconciled into QUEUE.md as **B9** (Seq 20): FIXED, owner ear-test pending. §0 map +
> §1 AGENT lane updated; agent backlog remains empty (user hardware/ear sign-off + push/PR).
