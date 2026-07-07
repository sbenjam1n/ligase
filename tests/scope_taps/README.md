# scope taps — headless acceptance patches

Backfills the `scope(v1)` verification (commit `b9629f7`): `scope_x~`/`scope_y~` signal outlets
+ per-family taps + the grain constellation.

## Wiring

`scope_x~` and `scope_y~` are appended as signal **outlets 10 and 11** — i.e. `[ligase~]`
**object outlet indices 9 and 10** (indices 0-8 are L, R, splice-bang, grain-bang, modout1-4,
state). Every capture patch wires `... 9 <writesf> 0` and `... 10 <writesf> 1` so channel 1 of
the WAV is X and channel 2 is Y. **`play` with no argument STOPS playback — these patches always
use `play 1`.**

## 1. `scope_grain_idle.pd` — SELF-ASSERTING

`scope_tap grain` with **no grains playing** must park the beam at exactly **(0,0)**. The patch
samples `scope_x~` and `scope_y~` with `[snapshot~]` and asserts both are 0 in-patch.

```sh
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . tests/scope_taps/scope_grain_idle.pd 2>&1 | grep -E 'IDLE_PASS|IDLE_FAIL'
```

Expect `GRAINX_IDLE_PASS: bang` and `GRAINY_IDLE_PASS: bang`.

## 2. Audio-shape metrics (WAV + external check)

Capture to a float WAV in `/tmp`; measure with sox. `X`=channel 1 (`remix 1`), `Y`=channel 2.

| File | Tap | Expected metric |
|------|-----|-----------------|
| `scope_lorenz_butterfly.pd` | `scope_tap lorenz 1` + `play 1` | X = the attractor: **both lobes** (min < 0 < max, spans ≈ `[-0.73, 1.0]`), X sd ≈ **0.39**; Y sd ≈ 0.61 |
| `scope_sine.pd` | `scope_tap sine 1` + `play 1` | Y = pure sine, RMS ≈ **0.69** (→ 1/√2), ±1; X = 2 Hz ramp, RMS ≈ **0.57** (→ 1/√3), ±1 |
| `scope_grain_active.pd` | `scope_tap grain`, wide `grainstart`, `play 1` | X spans the splice range ≈ `[-1, 1]` (sd ≈ 0.60); Y = env×amp with **zero negatives** (min = 0.000, max ≈ 1.0, mean ≈ 0.49) |
| `scope_grainsum.pd` | `scope_tap grainsum` + `play 1` | Y = tanh(Σ env×amp)/... : a **positive, tanh-bounded** silhouette (mean ≈ 0.23, max < 1); magnitude grows with concurrent grain count |

Example check (butterfly — both lobes + X spread):

```sh
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . tests/scope_taps/scope_lorenz_butterfly.pd
sox /tmp/scope_lorenz.wav -n remix 1 stat 2>&1 | grep -E 'Minimum|Maximum|RMS'
# PASS: Minimum amplitude < 0 AND Maximum amplitude > 0 (both wings), RMS amplitude ~0.39
```

Example check (sine — Y sinusoid, X ramp):

```sh
sox /tmp/scope_sine.wav -n remix 2 stat 2>&1 | grep RMS   # Y ~0.69 (1/sqrt2)
sox /tmp/scope_sine.wav -n remix 1 stat 2>&1 | grep RMS   # X ~0.57 (1/sqrt3, the ramp)
```

Example check (constellation — Y never negative):

```sh
sox /tmp/scope_grain.wav -n remix 2 stat 2>&1 | grep -E 'Minimum|Maximum'
# PASS: Minimum amplitude = 0.000000 (env*amp is one-sided), Maximum ~1.0
```
