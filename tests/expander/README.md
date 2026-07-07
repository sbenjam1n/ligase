# snapshot expander — headless acceptance patches

Backfills the `expander(v1)` + `expander(v1.1)` verification (commits `2644a57`, `a38b3c5`):
the cold-edit snapshot buffer + `snapbuf_*` API.

## How they self-assert

Every patch is **fully self-asserting** — it prints `PASS: bang` or `FAIL: ...` on stderr and
quits. The mechanism:

- `snapbuf_get <field>` emits `snapbuf <field> <value>` on the **state outlet (outlet index 8)**.
- The patch wires outlet 8 → `[route snapbuf]` → `[route env_follow_ms]` → the float value →
  `[expr $f1 == <expected>]` → `[sel 1]` → `[print PASS]` (matched) / `[print FAIL]` (else).

`env_follow_ms` is the probe field (a captured `sources` scalar). Live values are read back with
`snapbuf_from_live` + `snapbuf_get` — NB `query <scalar>` returns 0 for an unmodulated scalar, so
the from_live→get path is the correct probe.

## Run one

```sh
pkill -9 pd 2>/dev/null; pd -nogui -nosound -stderr -path . tests/expander/snapbuf_audition_revert.pd 2>&1 | grep -E 'PASS|FAIL'
```

or all six via `sh tests/run_acceptance.sh`.

## The patches

| File | Asserts | PASS condition |
|------|---------|----------------|
| `snapbuf_from_live_get.pd` | `snapbuf_from_live` reads the **live** value | `snapbuf_get env_follow_ms` == 900 (the live value just set) |
| `snapbuf_set_get.pd` | `snapbuf_set` writes the **buffer** | after `snapbuf_set env_follow_ms 2000`, get == 2000 |
| `snapbuf_cold_edit.pd` | buffer edits are **COLD** (never touch live) | after `snapbuf_set 2000`, a fresh `snapbuf_from_live` + get == **900** (live unchanged) |
| `snapbuf_store_recall.pd` | store / recall **round-trips** through a morph slot | from_live(900) → `snapbuf_store 3` → live changed to 1500 → `snapshot_recall 3` → get == **900** |
| `snapbuf_audition_hear.pd` | `snapbuf_audition 1` makes the buffer the **live** voice | after audition-on, live-read == **2000** (the buffer's edited value) |
| `snapbuf_audition_revert.pd` | `snapbuf_audition 0` reverts **EXACTLY** | after audition 1 then 0, live-read == **900** (pre-audition value, exact) |

All six print `PASS: bang`. The `post()` lines (`audition ON/OFF`, `snapbuf captured`, …) are
informational and are filtered out by the `_PASS/_FAIL` grep in the runner.
