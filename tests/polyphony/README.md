# polyphony — headless acceptance patches

Backfills the `poly(v1)` verification (commit `7238cb9`): a voice pool on the ONE shared
playhead, one grain spawned per active voice at that voice's MIDI transposition.

## The observable

There is **no outlet or query that exposes voice count**, so these patches assert the
per-grain **increment** the engine logs to stderr for the first 8 grains it ever spawns:

```
ligase~: grain final #N: pos=... inc=<x> len=... amp=... pan=...
```

`inc` is the playback-speed multiplier `2^(semitones/12)` where `semitones = MIDI note - 60`.
A chord seats one grain per voice, so the SET of increments in the first grains IS the active
voice set. All four patches need `pitch_mode 4` (MIDI) to sound and raise `maxgrains 16` so the
whole pool spawns within the soft cap.

`chord <n...>` takes **absolute MIDI notes** (60/64/67 = the 0/4/7 major triad; notes < 1 are
skipped, so a literal `chord 0 4 7` would not seat the root).

## Run one

```sh
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . tests/polyphony/poly_chord_voices.pd 2>&1 \
  | grep 'grain final' | grep -oE 'inc=[0-9.]+' | head -8 | sort -u
```

or run all four (asserted) via `sh tests/run_acceptance.sh`.

## The patches

| File | Asserts | PASS condition (first-8 grain increments) |
|------|---------|-------------------------------------------|
| `poly_chord_voices.pd` | `poly 1` + `pitch_mode 4` + `chord 60 64 67` seats **three simultaneous transposed voices** | `inc=1.0000` AND `inc=1.2599` AND `inc=1.4983` all present (root / M3 / P5) |
| `poly_mono_scalar.pd` | `poly 0` + `midi 64 100 1` = **single scalar voice** (mono path unchanged) | only `inc=1.2599`; `1.0000` and `1.4983` absent |
| `poly_steal.pd` | `chord 60 61 62 63 64 65 66 67 68` (9 notes) **caps at MAX_VOICES=8, steals the oldest** (note 60) | exactly **8** distinct increments; `inc=1.0595` (note 61 survived) present; `inc=1.0000` (note 60 stolen) absent |
| `poly_release.pd` | `midi 65 0 1` (velocity 0) **releases** note 65 from a 62/65/69 pool | `inc=1.1225` (62) and `inc=1.6818` (69) present; `inc=1.3348` (65) absent |

## Behavior note (matches the narrative, not silence)

When the pool empties (all voices released) the trigger sites fall through to the **mono
fallback** grain (`voice_active=0`), so an emptied pool is **not silence** — it plays one
untransposed grain. The commit only claimed "vel 0 removes a voice", which is what
`poly_release.pd` asserts (the released note's transposition disappears), not silence.
