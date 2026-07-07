# harmonic layer — headless acceptance patches

Backfills the harmonic-layer verification (commit `9a90eec`): 16 scale slots per destination,
root/rotate modulation, scale/pattern scope taps, capture schema v5.

## 1. `harmonic_capture.pd` — SELF-ASSERTING

Proves the scale-slot state travels into the snapshot buffer. Sets slot / root / rotate through
the real setters, then `snapbuf_from_live` + `snapbuf_get` reads each field back and asserts it
in-patch (state outlet → `[route]` → `[expr]` → `[sel 1]` → `[print *_PASS]`).

```sh
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . tests/harmonic/harmonic_capture.pd 2>&1 | grep -E '_PASS|_FAIL'
```

| Sequence | Captured field | Asserted | Label |
|----------|----------------|----------|-------|
| `pitch_scale_to 1 0 3 5 7` + `pitch_scale_slot 1` | `pitch_scale_slot` | 1 (that slot is selected) | `SLOT_PASS` |
| `scale_root 3` | `scale_root` | 3 (quantize-by-default root) | `ROOT_PASS` |
| `scale_rotate 2` | `scale_rotate` | 2 (modal degree-index offset) | `ROTATE_PASS` |
| `pitch_scale 5 7 9` (writes the active slot, default slot 0) | `pitch_scale_a` (slot 0) | first degree 5 → **slot 0 == the legacy `pitch_scale`** | `LEGACY_PASS` |

Expect four `*_PASS: bang` lines.

## 2. `harmonic_schema_v5.pd` — schema v5 round-trip (md5)

`morph_save → morph_load → morph_save` must be **byte-stable**. `morph_load` resolves via the Pd
search path (`canvas_open`), not absolute paths, so run with an extra `-path /tmp` and load the
file by bare name:

```sh
rm -f /tmp/harm_v5_a.morph /tmp/harm_v5_b.morph
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . -path /tmp tests/harmonic/harmonic_schema_v5.pd
md5sum /tmp/harm_v5_a.morph /tmp/harm_v5_b.morph   # PASS: the two md5s are identical
```

The patch saves absolute to `/tmp/harm_v5_a.morph`, `morph_load harm_v5_a` (found on `-path /tmp`,
must log `morph loaded from ...`), then saves `/tmp/harm_v5_b.morph`. Measured stable md5:
`3f3ea2d23fe2ba8902f4e0a9d9ed807b` (binary schema-v5 / file version 4). This is asserted by
`tests/run_acceptance.sh`.

## 3. `harmonic_scale_polygon.pd` — the scale polygon (WAV + external check)

`scope_tap scale grain` draws the active slot's pitch classes on the **unit circle**: the beam
steps one degree per sample, `X = cos`, `Y = sin` of `pitchclass/12·2π`. Sets a C-major scale
`pitch_scale 0 2 4 5 7 9 11` and captures `scope_x~`/`scope_y~`.

```sh
sox /tmp/harm_polygon.wav -b 16 -e signed -t wav /tmp/poly_16.wav
python3 - <<'PY'
import wave,struct,math,statistics
w=wave.open('/tmp/poly_16.wav','rb'); n=w.getnframes(); d=struct.unpack('<%dh'%(n*2),w.readframes(n))
xs=[d[i]/32768 for i in range(0,len(d),2)]; ys=[d[i]/32768 for i in range(1,len(d),2)]
r=[math.hypot(x,y) for x,y in zip(xs,ys) if math.hypot(x,y)>0.5]
pcs=sorted(set(round(((math.degrees(math.atan2(y,x))%360)/30))%12 for x,y in zip(xs,ys) if math.hypot(x,y)>0.5))
print('radius mean %.5f stdev %.6f'%(statistics.mean(r),statistics.pstdev(r)))
print('pitch classes:',pcs)
PY
```

**Expected:** radius mean **1.00000** (stdev ≈ 1.7e-5) — every sample exactly on the unit circle
— and pitch classes **[0, 2, 4, 5, 7, 9, 11]** = the C-major scale (the polygon vertices).

## Notes on properties not re-run here

- **Giant Steps / pattern-driven key cycling** (`pattern pitch_scale_slot`, `scale_root`
  sequencing on the cycle clock) is exercised by the existing `tests/pattern/` suite plus the
  slot-select assertion above; the harmonic-layer commit measured per-slot pitch histograms
  landing within the transposed triads.
- **v4 fixture import**: the schema-v5 walker imports v1-v4 files (no exclude-index remap); this
  is covered by `tests/morph/` (MT2_import / MC8) against the pre-v5 layout.
