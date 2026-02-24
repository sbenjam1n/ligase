# Ligase~ TODO

## What I'd Change

### ~~Fog stereo filter state — add independent per-channel mode~~ ✓ DONE
Implemented: `stereo_filter_independent` flag, per-channel state arrays (`mag_z1_right`,
`mag_z2_right`, `phase_prev_right`, `phase_delta_z1_right`), setter
`grain_fog_set_stereo_filter_mode()`, branching in `process_fft_frame()`.
Wired as `fog_stereo_filter_mode` Pd message.

### ~~process_fft_frame scratch buffer — use dedicated struct member~~ ✓ DONE
Implemented: dedicated `float *scratch` in `grain_fog_t`, allocated in `grain_fog_create()`
and freed in `grain_fog_destroy()`.

### ~~Split ligase~.c perform routine (~1400 lines)~~ ✓ DONE
Implemented: `ligase_update_inlets()`, `ligase_process_grains()`, `ligase_process_effects()`.
Perform function is now a thin coordinator calling these three.

## What I'd Add

### ~~Per-grain fog mode (experimental)~~ ✓ DONE
Implemented: `fog_pool_t` with configurable N slots (1-8, default 4 via `ligase.conf`).
Round-robin slot assignment at grain trigger. Grains write to per-slot accumulation buffers,
each slot runs its own FFT pipeline, results summed. Mode switch via `fog_position` message
(0=per-grain, 1=post-mix default). ~76KB per slot.

### ~~Compile-time configuration in ligase.conf~~ ✓ DONE
Implemented: `fft_size` (512/1024/2048) and `overlap_factor` (2/4/8) in `ligase.conf`.
Read by `read_fog_config()` in `grain_fog.c`. `fog_pool_size` (1-8) also added for
per-grain fog mode.

## What I'd Improve

### ~~Compute Hann window COLA sum at initialization, don't assume 1.5~~ ✓ DONE
Implemented: `cola_norm_factor` computed at init from actual window by summing `w[n]^2`
across all overlapping hop positions. Stored in `grain_fog_t` and used in `process_fft_frame()`.

### ~~O(bins) sliding-window smear (currently O(bins × smear_width))~~ ✓ DONE
Implemented: `smear_magnitudes()` uses sliding-window approach with running sum and count.
O(bins) regardless of smear_bins width.

## Notes (Don't Change, Just Document)

### Magnitude filter tanh limiter and biquad feedback interaction
The tanh soft-limiter in `filter_magnitudes()` is applied between the filter output and the
state update, so the feedback path sees the clipped value. This makes the filter's frequency
response differ from the standard biquad cookbook coefficients — it's effectively a nonlinear
filter. This is musically interesting (warm, saturated spectral persistence) and fits the
dreamy intent of the fog effect. If precise spectral matching is needed in a future mode,
move the limiter after the state update. For now: **leave as-is, this is intentional**.
- **File:** `src/grain_fog.c` — `filter_magnitudes()`
