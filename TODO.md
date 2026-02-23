# Ligase~ TODO

## What I'd Change

### Fog stereo filter state — add independent per-channel mode
`mag_z1`, `mag_z2`, `phase_prev`, and `phase_delta_z1` are single arrays shared between
the left and right channel FFT processing. The left channel updates the filter state, then
the right channel reads and overwrites it — so the temporal filter sees interleaved L/R data
rather than tracking each channel independently. For the current dreamy/diffuse character
this is acceptable, but it is architecturally incorrect.

Add an optional mode that gives each channel its own per-bin filter state arrays:
- Add `float *mag_z1_right`, `*mag_z2_right`, `*phase_prev_right`, `*phase_delta_z1_right`
  to `grain_fog_t`
- Add `int stereo_filter_independent` flag to the struct
- In `process_fft_frame` (or a new per-channel variant), branch on the flag to use the
  correct state arrays
- Expose a setter: `grain_fog_set_stereo_filter_mode(fog, int independent)`
- **File:** `src/types.h` (struct), `src/grain_fog.c` (alloc/free/processing)
- **Effort:** Medium

### process_fft_frame scratch buffer — use dedicated struct member
`process_fft_frame` uses `fft_imag_left` as scratch space for the windowed input and IFFT
output. This works because calls are sequential, but the coupling is invisible at the call
site and would silently break if processing order ever changed.

Add a dedicated `float *scratch` to `grain_fog_t`, allocated in `grain_fog_create` and
freed in `grain_fog_destroy`. Replace the `fft_imag_left` alias in `process_fft_frame`.
- **File:** `src/types.h` (struct), `src/grain_fog.c`
- **Effort:** Low

### Split ligase~.c perform routine (~1400 lines)
The DSP perform routine handles inlet validation, parameter modulation, recording modes,
SOS crossfading, grain scheduling, delay processing, effects, modulation outlets, and note
change detection all in one function. Even with region markers, it's hard to reason about.
Suggested split:
- `ligase_update_inlets()` — read and validate all inlet values
- `ligase_process_grains()` — grain scheduling and rendering
- `ligase_process_effects()` — fog, distortion, and delay chain
- **File:** `src/ligase~.c`
- **Effort:** Medium — pure refactor, no behavior change

## What I'd Add

### Per-grain fog mode (experimental)
The current fog applies to the mixed output. If fog could optionally operate per-grain,
each grain would have independent spectral evolution — different grains fogging at different
rates. This would be dramatically more interesting than global spectral processing.
Per-grain distortion is already an experimental mode; per-grain fog is a natural extension.
- **Status:** Idea / experimental candidate
- **Effort:** High — requires major architecture change

### Compile-time configuration in ligase.conf
FFT size (1024), overlap factor (4x), and various buffer sizes are hardcoded or scattered
as local constants. Add these to `ligase.conf` alongside `max_grains` so they can be tuned
at startup without recompiling. Format follows the existing key=value style:
```
# Fog FFT configuration
# fft_size: 512, 1024 (default), or 2048
#   512  = faster, less frequency resolution
#   2048 = slower, more spectral detail for smear
fft_size = 1024

# overlap_factor: 2, 4 (default), or 8
#   4x = standard (75% overlap, ~1024 sample latency)
#   8x = smoother temporal evolution, 2x CPU cost
overlap_factor = 4
```
- **File:** `ligase.conf`, `src/grain_fog.c` (read config at create time)
- **Effort:** Medium

## What I'd Improve

### Compute Hann window COLA sum at initialization, don't assume 1.5
`grain_fog.c` hardcodes the normalization factor as `2/(3*N)`, relying on the analytical
COLA sum of 1.5 for periodic Hann with 4x overlap. Instead, compute the actual COLA sum
at initialization by summing `w[n]^2` across all 4 overlapping hop positions. This makes
the code self-correcting if the window type, overlap factor, or periodic/symmetric choice
ever changes.
- **File:** `src/grain_fog.c` — `grain_fog_create()`, `process_fft_frame()`
- **Effort:** Low

### O(bins) sliding-window smear (currently O(bins × smear_width))
`smear_magnitudes()` recalculates the neighbor sum from scratch for each bin:
O(bins × smear_width) per frame. A sliding-window approach (add the new neighbor, subtract
the one that fell off) reduces this to O(bins), which matters at higher smear_width values.
- **File:** `src/grain_fog.c` — `smear_magnitudes()`
- **Effort:** Low

## Notes (Don't Change, Just Document)

### Magnitude filter tanh limiter and biquad feedback interaction
The tanh soft-limiter in `filter_magnitudes()` is applied between the filter output and the
state update, so the feedback path sees the clipped value. This makes the filter's frequency
response differ from the standard biquad cookbook coefficients — it's effectively a nonlinear
filter. This is musically interesting (warm, saturated spectral persistence) and fits the
dreamy intent of the fog effect. If precise spectral matching is needed in a future mode,
move the limiter after the state update. For now: **leave as-is, this is intentional**.
- **File:** `src/grain_fog.c` — `filter_magnitudes()`
