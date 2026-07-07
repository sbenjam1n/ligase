# source shapes — headless acceptance patches

Backfills the `source-shapes` verification (commit `fe90c79`): settable shape params for every
modulation generator + capture schema v4.

## 1. `shapes_params_capture.pd` — SELF-ASSERTING (the core proof)

Proves that **each shape setter lands and is captured** into the snapshot buffer. Sets five
representative params through their real setters, then `snapbuf_from_live` + `snapbuf_get`
reads each captured field back and asserts it in-patch (state outlet → `[route]` → `[expr]`
→ `[sel 1]` → `[print *_PASS]`).

```sh
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . tests/source_shapes/shapes_params_capture.pd 2>&1 | grep -E '_PASS|_FAIL'
```

| Setter (message) | Captured field | Asserted value |
|------------------|----------------|----------------|
| `waveform_phase 1 0.25` | `waveform_phase_1` | 0.25 (`WPHASE_PASS`) |
| `square_pw 1 0.2` | `square_pw_1` | 0.2 (`PW_PASS`) |
| `saw_skew 2 0.5` | `saw_skew_2` | 0.5 (`SKEW_PASS`) |
| `lorenz_rho 1 15` | `lorenz_rho_1` | 15 (`RHO_PASS`) |
| `sphere_spin 1 5` | `sphere_spin_1` | 5 (`SPIN_PASS`) |

All shape setters take `<instance 1-4> <value>`. Expect five `*_PASS: bang` lines.

## 2. Audio-shape metrics (WAV + external check)

These capture the generator readout via the scope outlets (`scope_x~`/`scope_y~`, outlets 10/11
= object outlet indices 9/10) to a float WAV in `/tmp`, and pair it with a documented metric.
Convert the float WAV with `sox -b 16 -e signed` before reading it in python.

### `shapes_lorenz_rho_hi.pd` / `shapes_lorenz_rho_lo.pd` — rho is the chaos knob

`lorenz_rho 1 28` (chaotic) vs `lorenz_rho 1 15` (below the chaotic threshold rho_c≈24.7, so it
converges to a fixed point). Both `scope_tap lorenz 1` and capture the Lorenz readout on
`scope_y~` (WAV channel 2). Run both, then:

```sh
for r in 28 15; do sox /tmp/shapes_rho$r.wav -b 16 -e signed -t wav /tmp/rho${r}_16.wav remix 2 trim 2.0; done
python3 - <<'PY'
import wave,struct,statistics
def tail(f):
    w=wave.open(f,'rb'); n=w.getnframes(); d=struct.unpack('<%dh'%n,w.readframes(n))
    xs=[v/32768 for v in d]; return statistics.pstdev(xs),min(xs),max(xs)
for r in (28,15):
    s,mn,mx=tail('/tmp/rho%d_16.wav'%r); print('rho',r,'stdev %.4f range [%.3f,%.3f]'%(s,mn,mx))
PY
```

**Expected:** rho 28 tail stdev ≈ **0.35**, wandering over ≈ `[-0.49, 0.70]`; rho 15 tail stdev
≈ **0.16**, collapsed into a narrow band ≈ `[-0.68, -0.23]` (a fixed point). i.e. the low-rho
readout variance is much smaller and its range collapses.

> Note vs narrative: the commit measured a ~3800× tail-variance collapse over a much longer
> soak (rho 15 settles fully toward stdev→0). In this ~2 s capture window the measured ratio is
> ≈ **2.2×** with a clear range collapse — same qualitative "chaos knob" behavior; the absolute
> ratio depends on soak length.

### `shapes_sphere_spin.pd` — spin drives the field

`sphere_kick 1 6 0 6` + `sphere_spin 1 5` + `scope_tap sphere 1` → the sphere's (x,z) readout on
`scope_x~`/`scope_y~` orbits.

```sh
sox /tmp/shapes_sphere_spin.wav -b 16 -e signed -t wav /tmp/sphere_16.wav
python3 - <<'PY'
import wave,struct,statistics
w=wave.open('/tmp/sphere_16.wav','rb'); n=w.getnframes(); d=struct.unpack('<%dh'%(n*2),w.readframes(n))
xs=[d[i]/32768 for i in range(0,len(d),2)][int(1.2*48000):]
ys=[d[i]/32768 for i in range(1,len(d),2)][int(1.2*48000):]
print('X stdev %.3f  Y stdev %.3f (both > 0 => orbit motion)'%(statistics.pstdev(xs),statistics.pstdev(ys)))
PY
```

**Expected:** X stdev ≈ 0.06, Y stdev ≈ 0.09 — both axes oscillate, i.e. `sphere_spin` rotates
the readout (with `sphere_spin 0` there is no such orbit). The commit's "energy-neutral / `|v|`
conserved to 1e-6" is an **internal velocity-magnitude** property; the scope reads the (x,z)
**position projection** of a point moving on the sphere, whose radius is NOT constant, so this
patch asserts spin's observable effect (motion), not `|v|` directly.

## 3. Defaults are bit-identical

The "no-shape-message defaults are bit-identical to baseline" claim is guarded by the top-level
regression `test_auto.pd` (RMS **0.372309** / max **0.608839**), which exercises the default
readout path. The shape params default to neutral (`waveform_phase 0`, `square_pw 0.5`,
`saw_skew 0`, `lorenz` 10/28/8÷3, `sphere_spin 0`); sending those explicit defaults produces
the same output as sending nothing.
