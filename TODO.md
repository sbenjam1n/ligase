# Ligase~ Improvement Notes

## To Implement

### Refactor ligase~.c perform routine
The perform routine is ~1400 lines handling inlet validation, parameter modulation,
recording modes, SOS crossfading, grain scheduling, delay processing, effects,
modulation outlets, and note change detection. Split into sub-routines
(e.g., `ligase_update_inlets()`, `ligase_process_grains()`, `ligase_process_effects()`)
to make the signal flow easier to follow and debug.

### Dedicated scratch buffer for process_fft_frame
The `process_fft_frame` function reuses `fft_imag_left` as scratch space. This works
because calls are sequential, but it's a coupling that's invisible at the call site.
A dedicated `scratch` buffer in the `grain_fog_t` struct would be clearer.

### Compute Hann window COLA sum at init
Rather than relying on the analytical value of 1.5, compute the actual COLA sum at
initialization and use it for normalization. This would make the code self-correcting
if the window type, overlap factor, or periodic/symmetric choice ever changes.

### Separable kernel for smear
The current O(bins * smear_width) loop recalculates the sum from scratch for each bin.
A sliding-window approach (add the new neighbor, subtract the one that fell off) would
be O(bins), which matters at higher smear widths.

### Compile-time configuration via ligase.conf
The FFT size (1024), overlap factor (4x), and various buffer sizes are hardcoded or
scattered as local constants. Add these as configuration options to `ligase.conf`
(which already handles grain pool size). This would make it easier to experiment with
2048-point FFT (better frequency resolution) or 8x overlap (smoother temporal evolution).

### Fog bypass/passthrough test
With mix=1.0 and both smear and specmagfilter disabled, fog should be a transparent
FFT->IFFT roundtrip. `test_fog.c` verifies unity gain and low THD to catch
normalization regressions. See `test_fog.c`.

## Design Decisions (Intentional, Do Not Change)

### Fog stereo filter state is shared between channels
The `mag_z1`, `mag_z2`, `phase_prev`, and `phase_delta_z1` arrays are used by both
L/R channel FFT processing in sequence. The left channel updates the filter state,
then the right channel reads and overwrites it. This means the temporal filter sees
interleaved L/R data rather than tracking each channel independently. This creates
a diffuse mono-ish spectral character that fits the dreamy intent of the effect.

### Magnitude filter tanh limiter placement is intentional
The soft clipping (`tanhf`) happens between the filter output and the state update,
meaning the feedback sees the clipped value. This is a nonlinear filter — the
frequency response no longer matches the cookbook coefficients. The warm nonlinear
character is preferred and fits the dreamy intent.

### Per-grain fog/distortion was tried and rejected
Applying fog per-grain (or per-splice) rather than to the mixed output has been
tested and is less musically interesting than expected. The current global spectral
processing is the preferred architecture.
