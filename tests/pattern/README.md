# Pattern subsystem — headless acceptance patches

These Pd patches verify the TidalCycles pattern subsystem (plans
`Plans/pattern_notation.md` (P1), `pattern_modulation.md` (P2), `pattern_pitch.md` (P3)).
They are **headless** — they drive themselves via `loadbang` + `delay`, print to stderr,
and quit via `\; pd quit`.

## Run one

```sh
make                      # build ligase~.pd_darwin first
/Applications/Pd-0.51.1.app/Contents/Resources/bin/pd -nogui -nosound -r 48000 \
    -lib ./ligase~ tests/pattern/P1a_even.pd
```

## How they work

- **BPM** is set by two `bang`s 500 ms apart → 120 BPM (the pattern cycle clock only runs once a
  tempo is known; before that it holds at phase 0).
- **`pattern_debug 1`** logs each step change to stderr as
  `ligase~ pat t=<ms> slot <s>: step <i> value <v> rest <r> cycle <k>`, and (in pitch mode) the
  applied semitone as `ligase~ pitch: degree <d> -> semitone <st> (cycle <k>)`.
- **P2** patches read the mapped value off a `modout` outlet through `[change]` → `[print]`.
- **P3** patches `record` noise + `play 1` first, because pitch is applied **per grain** — the
  granulator must actually be running.

## The patches

| File | Verifies |
|------|----------|
| `P1a_even.pd` | even step timing (`pattern_cycle 4/4` + 4 values → 0.5 s steps) |
| `P1b_cycle.pd` | `pattern_cycle 4/4 3/8` → 2.75 s cycle |
| `P1c_nested.pd` | nesting `[ 0.2 0.4 ] 0.8` → ¼,¼,½ |
| `P1d_weight.pd` | `@` weight `0.2@3 0.8` → ¾,¼ |
| `P1e_alt.pd` | `< >` alternation, one value per (default) cycle |
| `P1f_pitch.pd` | `pattern pitch < 0 4 7 >` loads slot 7 with raw degrees |
| `P1g_robust.pd` | comma/bracket robustness, unbalanced/bad-token errors, pre-BPM guard |
| `P2a_attach.pd` | `pattern <param>` attaches + maps to `param_range` min/max |
| `P2b_clear.pd` | `pattern_clear` restores the prior source (+ `get_rand_type_name` labels) |
| `P2c_slot.pd` | slot ≥4 via `rand_type pattern_N` (bypasses the 0..3 clamp) |
| `P2d_guards.pd` | `min==max` reset + `unknown parameter` rejection + slot allocation |
| `P2e_nested_invert.pd` | nested + `param_invert` on a real param target |
| `P3a_pitch.pd` | scale-degree pitch with **wrap+octave** (degree 7 → +12 semitones) |
| `P3b_alt_clear.pd` | pitch alternation (one degree/cycle) + `pattern_clear pitch` → mode OFF |
| `P3c_nested.pd` | nested pitch `[ 0 [ 4 7 ] ]` |
| `P3d_mode.pd` | `pitch_mode` 0–5 bounds (5 ok, 6 rejected) |
