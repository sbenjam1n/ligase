# Plan: One-shot playback (stop-at-splice-end)

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE); promote into §2 (ON DECK) when scheduled.
**Related:** the playhead-mode + playback sections — this plan composes a new orthogonal axis on top of the existing `playhead` modes (STATIC/SCANNING/CLOCK_ADVANCE) and the `play <0|1>` transport.

## Problem

The owner wants a one-shot playback behavior, in their words:

> "oneshot: triggering play in this mode should start playback at the BEGINNING of the current slice and AUTO-STOP when the playhead reaches the END of the splice. Grains that are already going should NOT be cut — they finish on their own. Hitting play again should restart from the slice beginning without silencing whatever grains are still ringing out."

Concretely, the four sub-requirements:

1. **Start at slice begin.** Every (re)trigger reinitializes the playhead to `splice_start`, regardless of where the previous pass stopped.
2. **Auto-stop at splice end.** When the advancing playhead reaches the splice boundary, transport stops instead of folding back and looping.
3. **Grains unaffected / complete on their own.** Grains already scheduled when the stop fires must render to their natural envelope end; the stop must never cut them.
4. **Repeated play-bang restarts without cutting grains.** Re-arming the one-shot restarts spawning from `splice_start` while any in-flight grains keep ringing out.

The owner also floated **"oneshot as a combination of modes"** — i.e. they want one-shot to compose with *how* the playhead moves (scan AND oneshot, clock-advance AND oneshot), not to be a fourth mutually-exclusive playhead mode. This plan honors that: one-shot is a composable boolean flag orthogonal to `playhead_mode`.

## Mechanics / target surface

The existing code this extends, verified by reading `src/ligase~.c`, `src/types.h`, and `src/grain.c`:

### The grain-completion decoupling already exists (the hard part is already solved)

`ligase_process_grains` (`src/ligase~.c:1042`, called per DSP vector by `ligase_perform` (`src/ligase~.c:1503`) at `src/ligase~.c:1626`) runs **two independent state machines** in sequence:

- **(A) The trigger/playhead block** (`src/ligase~.c:1068-1247`) is gated entirely on `if (x->is_triggering && x->reel && x->reel->length>0)` (`:1068`). It decides where/when to spawn new grains and how the playhead advances and wraps.
- **(B) The render block** (`src/ligase~.c:1255-1277` → `scheduler_process` at `src/grain.c:1051`) runs **UNCONDITIONALLY**, gated only by `if (x->reel && x->reel->length > 0)` (`src/ligase~.c:1255`) — **NOT** by `is_triggering`. The comment at `src/ligase~.c:1249` states the invariant explicitly: *"Process active grains (regardless of is_triggering state, let them finish)."*

A grain's life is bounded **solely** by `grain->envelope_phase >= grain->grain_length` (`src/grain.c:1128`, releasing via `scheduler_release_grain` at `:1131`, defined at `src/grain.c:707`). `scheduler_trigger_grain` (`src/grain.c:725`) snapshots position/speed/grain_length/splice-bounds/amp/pan into the grain (`src/grain.c:999-1009`) so it is fully independent of the playhead once allocated. `ligase_play` already exercises exactly this decoupling: the comment at `src/ligase~.c:1855` documents that `play 0` clears both flags but *"Active grains always play out regardless of is_playing state."*

**Consequence:** the entire "grains continue to completion" requirement (sub-req 3) is delivered **for free** by setting `is_triggering = 0` and touching nothing in block (B). This is the load-bearing fact of the whole plan — the spec's hard part is already implemented. (Verified: setting `is_triggering = 0` only re-gates block (A) on the *next* vector; it does not iterate `active_list` and does not affect `scheduler_process`, so active grains finish.)

### `play 1` already starts at `splice_start`

`ligase_play` (`src/ligase~.c:1852`) sets `is_playing` and `is_triggering` together (`:1853-1854`) and, on `play 1`, re-inits `x->playback_position = splice_start` on **every** call (`:1872` via `splice_get_bounds` at `:1870`). So sub-req 1 (start at slice begin) is **already** satisfied by the start path — no change to it is needed. The only new work is the wrap→stop conversion (sub-req 2) plus a clean re-arm path.

### The wrap point that must become a stop

Looping is **structural**, not a flag — there is no field today that says "stop at splice end." Three sites:

- **STATIC** (`src/ligase~.c:1079-1090`): no advancing playhead. `grain_start` slides a static read offset; its only "end" event is a `grain_start` 0↔1 parameter wrap (`|grain_start - prev_grain_start| > 0.5`, `:1084-1085`) that bangs the outlet (`:1088`). No `playback_position` to clamp.
- **SCANNING** (`src/ligase~.c:1116-1171`): inside the per-sample `for` loop (`:1118-1171`), `playback_position += scan_rate` (`:1143`; comment notes scan_rate "can be negative"). `wrapped` is computed at `:1147-1149` as `pos >= splice_end || pos < splice_start || !isfinite(pos)` — already covers forward overrun, backward/negative-scan underrun past `splice_start`, zero-length, and non-finite. `wrap_to_splice` folds (`:1150`); the `wrapped` branch bangs `x_splice_end_out` under `outlet3_mode == 0` (`:1153-1155`) and consumes `pending_splice` navigation (`:1158-1170`).
- **CLOCK_ADVANCE** (`src/ligase~.c:1172-1246`): advances `playback_position` by `advance_samples` only inside `if (x->clock_bang_received)` (`:1186-1189`); identical `wrapped` test (`:1192-1194`), `wrap_to_splice` fold (`:1195`), bang (`:1198-1200`), `pending_splice` nav (`:1203-1215`), then clears `clock_bang_received` (`:1218`). The grain-trigger loop is at `:1221-1245`.

`wrap_to_splice` (`src/ligase~.c:375`) is an O(1) hang-safe fold (returns `start` on `len<=0`/non-finite, NO while-loop). It exists precisely because subtractive while-loop wraps once hung the audio thread at 100% CPU on zero-length/runaway playheads. **It is shared** (also wraps per-grain positions) and must not be modified or have a loop added near it.

### The transport / message surface

- `bang` is **already the BPM clock** (`ligase_bang`, `src/ligase~.c:2581`, registered via `class_addbang` at `:5169`): it computes BPM from inter-bang intervals, recomputes the quant grids (`:2584-2626`), and in CLOCK_ADVANCE sets `clock_bang_received = 1` (`:2632-2633`). It **cannot** be the re-trigger.
- Selectors register in the `class_addmethod` cluster at `src/ligase~.c:5130-5136` (`play`/`record`/... adjacent; `recsplice`/`recinput` no-arg at `:5135-5136`). `ligase_splice_jump` (`src/ligase~.c:2161-2170`) is the canonical 0/1-validating setter template. The splice_behavior init block is at `src/ligase~.c:5018-5023`.
- `outlet3_mode` (struct field at `src/ligase~.c:300`) selects whether `x_splice_end_out` (outlet 3) bangs on wrap (mode 0, default) or is repurposed for note-change (mode 1). All three wrap-bang sites are guarded by `outlet3_mode == 0`.

**Net: the new work is `wrapped`→stop at exactly two sites, plus a struct field + init, a `loop` setter, and a `trigger` re-arm selector.** The render block (`src/ligase~.c:1255-1277`) is UNTOUCHED — that is the rule.

## Design

### The flag: a composable boolean, not a 4th playhead mode

Add `int loop_mode` to `splice_behavior_t` (`src/types.h:327-334`), semantics **`1 = loop forever (today's behavior, DEFAULT)`, `0 = oneshot/stop-at-splice-end`**:

```c
typedef struct {
    int create_position;
    int jump_to_new;
    int finish_before_nav;
    int split_current;
    int pending_splice;
    int send_splice_msg;
    int loop_mode;            // 1=loop forever (default), 0=oneshot stop-at-splice-end
} splice_behavior_t;
```

It lives in `splice_behavior_t` (not the main struct) because it is a per-instance transport/nav toggle exactly like its siblings (`create_position`, `jump_to_new`, `finish_before_nav`, `pending_splice`, `send_splice_msg`) — each already has a setter and this is the established home for "what happens at splice boundaries."

**Why a flag, not a mode** (grounded in the owner's intent + the code shape):
1. The owner explicitly wants "oneshot as a combination of modes." `playhead_mode` answers *how does the head move*; loop-vs-oneshot answers *what happens at the boundary*. These are independent axes. A 4th enum value would force a false "scan OR oneshot" choice.
2. The stop logic is literally the same `wrapped` boolean already computed identically at `src/ligase~.c:1147-1149` and `:1192-1194`. A flag reuses both with a one-line guard at each; a new mode would re-implement scan/clock advance + the wrap test.
3. STATIC (`:1079`) has no advancing playhead, so a "ONESHOT mode" would be undefined there; a flag cleanly no-ops.
4. Looping is the structural default and the basis of the splice-end bang + `pending_splice` contract. Defaulting `loop_mode = 1` and taking the exact existing fold/bang/nav path unchanged when the flag is set preserves the default by construction.

**INIT:** in the splice_behavior init block (`src/ligase~.c:5018-5023`), add `x->splice_behavior.loop_mode = 1;` immediately after the `send_splice_msg = 0` line (`:5023`). Default 1 preserves always-loop; a zero/uninitialized default would silently turn every existing patch into oneshot — an explicit regression to avoid.

**No new field for the stop itself:** `is_triggering` (`:217`) and `is_playing` (`:216`) already exist and are the only flags the stop writes; `clock_bang_received` (`:231`) already exists and is what CLOCK_ADVANCE additionally gates.

### Perform-site change — SCANNING (primary, `src/ligase~.c:1147-1170`)

The `wrapped` branch already bangs the splice-end outlet (`:1153-1155`) and handles `pending_splice` (`:1158-1170`). After the existing bang and the `pending_splice` handling, add the one-shot stop:

```c
// (existing) bang x_splice_end_out under outlet3_mode==0  (:1153-1155)
// (existing) pending_splice nav, if queued                 (:1158-1170)

if (x->splice_behavior.loop_mode == 0 && x->splice_behavior.pending_splice < 0) {
    x->is_triggering = 0;
    x->is_playing    = 0;   // keep UI/state mirror coherent (see Risks)
    break;                  // freeze the head: stop advancing remaining samples this vector
}
```

- We **still call `wrap_to_splice`** at `:1150` so `playback_position` is left finite/in-range (clamp-not-loop discipline; no while-loop introduced — hang-safety preserved). A later re-trigger forces it to `splice_start` anyway.
- The `break` exits the per-sample `for` loop (`:1118-1171`) so the remaining samples in the DSP vector do not keep advancing a stopped head and re-fire the bang / re-enter `pending_splice` in the same vector. (Any grain due on the current sample `i` has already been triggered before the wrap test at `:1147`, so it finishes naturally — no grain is cut.)
- **Negative scan / both boundaries:** because the `wrapped` test at `:1148` already includes `playback_position < splice_start`, a backward head that underruns `splice_start` triggers the **same** stop. We freeze at whichever boundary was crossed; `wrap_to_splice` folds back into `[start, start+len)` either way, leaving `playback_position` valid.

### Perform-site change — CLOCK_ADVANCE (secondary, `src/ligase~.c:1192-1218`)

Same one-line stop in the matching `wrapped` block (after the bang at `:1198-1200` and the `pending_splice` nav at `:1203-1215`, before `clock_bang_received` is cleared at `:1218`):

```c
if (x->splice_behavior.loop_mode == 0 && x->splice_behavior.pending_splice < 0) {
    x->is_triggering = 0;
    x->is_playing    = 0;
}
```

No `break` is needed here — the wrap test sits inside `if (x->clock_bang_received)` (`:1186`), not inside the per-sample loop, so there is no remaining-sample advance to suppress. **Note on same-vector behavior:** after this stop sets `is_triggering = 0`, control falls through to the grain-trigger loop at `:1221-1245`, which will still spawn grains at the (now-frozen) `playback_position` for the remainder of *this* DSP vector. That is benign and consistent with the architecture — those grains finish naturally, and on every subsequent vector the whole block `:1068-1247` is re-gated off by `is_triggering == 0`, so no further advance, bang, or grain-spawn occurs. The effective stop is at the next vector boundary, identical in spirit to the existing `play 0` behavior.

**CRITICAL extra — clock coherence.** A stopped one-shot in CLOCK_ADVANCE must stop honoring `clock_bang_received`, or the next BPM `bang` (which sets `clock_bang_received = 1` at `src/ligase~.c:2632-2633`) re-advances the frozen head and re-crosses the boundary, producing phantom splice-end bangs. **Recommended fix (a):** in `ligase_bang`, gate the CLOCK_ADVANCE assignment (`:2632-2633`) additionally on `&& x->is_triggering`:

```c
if (x->playhead_mode == PLAYHEAD_MODE_CLOCK_ADVANCE && x->is_triggering) {
    x->clock_bang_received = 1;
}
```

This keeps BPM detection and quant-grid recomputation (`:2584-2626`) alive — important, those grids feed grain_size/IOT/delay/stut — while suppressing only the playhead-advance of a stopped transport. `clock_bang_received` stays cleared at `:1218` as today.

### STATIC (`src/ligase~.c:1079-1090`): no-op for v1

STATIC has no advancing playhead and no transport boundary — its "end" (the `grain_start` 0↔1 wrap at `:1084-1085`) is a parameter event the user drives, not a position the engine reaches. For v1, `loop_mode` is a documented **no-op in STATIC** (oneshot is meaningful only in SCANNING/CLOCK_ADVANCE). A `grain_start`-wrap stop (on the wrap at `:1085`, when `loop_mode == 0`, bang once + `is_triggering = 0`) is an opt-in open decision below.

### Message interface — two new selectors

Registered in the `class_addmethod` cluster (`src/ligase~.c:5130-5136`), adjacent to `play`/`record`:

**1) `loop <0|1>` — the oneshot/loop toggle.** New handler `ligase_loop(ligase_t *x, t_floatarg mode)` modeled on `ligase_splice_jump` (`src/ligase~.c:2161-2170`): validate 0/1, set `x->splice_behavior.loop_mode`, `post()` confirmation, `pd_error` otherwise.

```c
static void ligase_loop(ligase_t *x, t_floatarg mode) {
    int m = (int)mode;
    if (m == 0 || m == 1) {
        x->splice_behavior.loop_mode = m;
        post("ligase~: loop set to %d (%s)", m, m ? "loop" : "oneshot");
    } else {
        pd_error(x, "ligase~: invalid loop %d (use 0 or 1)", m);
    }
}
```
Register: `class_addmethod(ligase_class, (t_method)ligase_loop, gensym("loop"), A_DEFFLOAT, 0);`. (Name `loop`, sense `1=loop`, so the default-preserving value is the natural `1`; an inverted `oneshot` selector would default to `0`, more error-prone to init.)

**2) `trigger` — the (re)trigger.** New no-arg handler `ligase_trigger(ligase_t *x)` factoring out the re-arm body of `ligase_play` (the no-audio guard `:1859-1864` + bounds re-init `:1869-1872`) MINUS the multi-line debug dump (`:1874-1896`):

```c
static void ligase_trigger(ligase_t *x) {
    if (x->reel->length == 0) {
        pd_error(x, "ligase~: cannot trigger - no audio loaded");
        return;
    }
    uint32_t s, e;
    splice_get_bounds(&x->reel->splices, x->reel->splices.current_splice,
                      x->reel->length, &s, &e);
    x->playback_position = (float)s;
    x->is_playing    = 1;
    x->is_triggering = 1;
}
```
(`splice_get_bounds` signature confirmed: `(splice_array_t *, int, uint32_t, uint32_t *, uint32_t *)` — `splice.c:71`; this call matches.) Register: `class_addmethod(ligase_class, (t_method)ligase_trigger, gensym("trigger"), 0);` (no typed args, like `recsplice`/`recinput` at `:5135-5136`).

**Why `trigger` cannot be a bare `bang`:** `bang` is already `ligase_bang` (`src/ligase~.c:2581`, registered via `class_addbang` at `:5169`), the BPM clock. Stealing it would destroy BPM detection (`:2584-2626`) and clock-advance transport (`:2632-2633`). The re-trigger MUST be its own selector.

**Re-trigger / start-at-slice-begin (sub-reqs 1 & 4):** both `play 1` (already re-inits `playback_position = splice_start` at `:1872`) and the new `trigger` set `playback_position = splice_start` unconditionally and set `is_triggering = 1`. Neither iterates `active_list` nor touches `scheduler_process`, so any grains scheduled by the prior pass keep rendering to completion via the unconditional render block — repeated triggers re-arm spawning from the start; **in-flight grains are never cut.** `trigger` is the canonical re-arm (no verbose `post()` dump on every re-arm); `play 1` works too but floods the console — docs steer users to `trigger`.

### `pending_splice` precedence (recommended, must be confirmed)

The `wrapped` branch already consumes `pending_splice` (switch current_splice, re-fetch bounds, reset `playback_position` to the new `splice_start`, send msg) at `:1158-1170` / `:1203-1215`. **Recommended precedence: NAV WINS.** If a `pending_splice` is queued at the boundary, honor the nav and **keep playing**; the one-shot stop fires only when `pending_splice < 0` (encoded in the `&& x->splice_behavior.pending_splice < 0` guard above). This makes oneshot compose with sequenced splice navigation: queue the next splice → it advances; queue nothing → it stops. The splice-end bang at `:1153` / `:1198` fires in **both** cases, so downstream sequencers still advance. This is a semantic decision and is an openDecision for sign-off.

### Outlet-3 bang on stop

The terminal stop **reuses** the existing `x_splice_end_out` bang under the existing `outlet3_mode == 0` guard (`:1153-1155` / `:1198-1200`) — it already fires *before* we clear `is_triggering`, so downstream patches listening for splice-end get exactly one bang at the stop, identical to a loop-wrap bang. `outlet3_mode == 1` (note-change repurpose) suppresses it, same as today. **No new outlet and no new bang call** — the contract is preserved.

## Steps & gates

### GATE A (approval) — APPROVED by owner 2026-06-24 (Seq 50)

All recommended options confirmed: **(1)** `loop_mode` on `splice_behavior_t`; **(2)** `pending_splice` precedence = **NAV WINS** (queued nav advances and keeps playing; stop only when none queued); **(3)** STATIC = **no-op + documented** (oneshot meaningful only in SCANNING/CLOCK_ADVANCE); **(4)** selectors **`loop <0|1>`** (1=loop, default) + no-arg **`trigger`**; **(5)** clear **both** `is_triggering` and `is_playing` on stop. Cleared to Step 1. Original sign-off items retained for the record:

1. **Field home.** `loop_mode` on `splice_behavior_t` (`src/types.h:327-334`) — recommended. **Serialization is a non-issue:** a search of `src/` found NO code path that memcpys/fwrites/binary-serializes `splice_behavior_t` — `ligase_get_state` (`:4890`) only `post()`s field values, and there is no `reel_io.c`/struct-blob save path. The struct is a single in-memory member of `ligase_t` (`:238`), so adding a field is layout-safe by construction. (Step 1 will spend one line confirming this, not a real audit.) Confirm home (splice_behavior_t vs main struct).
2. **`pending_splice` precedence at the boundary.** Recommended **NAV-WINS** (queued nav advances and keeps playing; stop only when no nav queued). Confirm vs. STOP-WINS (always stop, ignore queued nav). Both are defensible; must be pinned and documented.
3. **STATIC behavior.** Recommended **no-op-with-documentation** for v1 (oneshot meaningful only in SCANNING/CLOCK_ADVANCE). Confirm vs. opt-in `grain_start`-wrap stop (bang once + `is_triggering=0` on the `:1085` wrap).
4. **Selector names.** `loop <0|1>` (default-preserving sense `1=loop`) + no-arg `trigger`. Confirm names (vs. `oneshot`/`oneshot_play`).
5. **`is_playing` on stop.** Recommended clear **both** `is_triggering` and `is_playing` (coherent with the `:1853-1854` pairing). Confirm (clearing `is_triggering` alone is sufficient for the no-cut guarantee; `is_playing` is the UI/state mirror).

### Step 1 → GATE B (types + flag plumbing, no behavior)

Add `int loop_mode;` to `splice_behavior_t` (before the closing brace at `src/types.h:334`, after `send_splice_msg` at `:333`) and `x->splice_behavior.loop_mode = 1;` in the init block (`src/ligase~.c:5023`). Add `ligase_loop` (modeled on `ligase_splice_jump` `:2161-2170`) and `ligase_trigger` (re-arm body of `ligase_play` `:1859-1872` minus debug dump `:1874-1896`); register both in the cluster (`:5130-5136`). Confirm (one line) there is no `splice_behavior_t` memcpy/serialize anywhere (openDecision-1). GATE: `make clean && make` warning-free; `loop`/`trigger` reach the handlers; no perform-path change yet (default `loop_mode=1` ⇒ identical behavior).

### Step 2 → GATE C (perform-site stop, SCANNING + CLOCK_ADVANCE)

Add the `loop_mode == 0 && pending_splice < 0` stop branch to the SCANNING `wrapped` block (after `:1170`, with the `break`) and the CLOCK_ADVANCE `wrapped` block (after `:1215`, no break). Add the `&& x->is_triggering` clock-gate to `ligase_bang` (`:2632`). Keep the existing `wrap_to_splice` fold, the `outlet3_mode==0` bang, and the `pending_splice` handoff unchanged on the default (`loop_mode==1`) path. GATE: `make clean && make` warning-free; with `loop 1` (default) behavior is byte-for-byte the old loop; with `loop 0` the playhead stops at the boundary.

### Step 3 → GATE D (verify, headless)

Build; run the acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (the `AUTOMATED_TEST_PROCEDURE.md` convention; each patch loadbangs `\; pd dsp 1` so perform actually runs). Confirm all acceptance criteria. No regression in the loop default.

## Acceptance criteria

All headless via `pd -nogui -nosound -stderr -path . <patch>.pd` (record noise into the reel, then play), reading the splice-end-bang outlet and the recorded WAV. Every patch loadbangs `\; pd dsp 1` (without it no block advances and timing assertions pass by vacuity). Playhead selectors: `playhead 2` = SCANNING, `playhead 3` = CLOCK_ADVANCE (confirmed in `ligase_playhead_mode`, `:2369+`).

1. **Auto-stop at splice end (SCANNING).** Record ~2 s of noise; set `playhead 2` (SCANNING) + a `scanrate` that traverses the splice in a known time; `loop 0`; `play 1`. Assert the splice-end outlet bangs **once** at the splice boundary and that, after the boundary time, no further grains are triggered (the live granular output via `writesf~` goes silent after the in-flight grains finish; no second bang). With `loop 1` the same patch must keep banging once per pass indefinitely (loop unchanged).
2. **Long grain rings out after stop.** Configure a grain length long enough that a grain triggered just before the boundary outlasts the stop. Assert the recorded output is **non-silent for that grain's full envelope after** the stop bang (the tail of the last grain rings out past `is_triggering=0`) — verifying block (B) `scheduler_process` (`src/grain.c:1051`) is never gated on the stop.
3. **Re-bang restarts from `splice_start` without silencing active grains.** While a long grain from pass 1 is still ringing (post-stop), send `trigger`. Assert (a) new grains begin spawning from `splice_start` (output energy resumes from the slice start), and (b) the still-ringing pass-1 grain is **not** cut — its envelope tail is continuous across the re-trigger boundary in the recorded output.
4. **Loop mode unchanged when oneshot is off.** With `loop 1` (the default — verify a freshly constructed object reports/behaves as loop without any `loop` message), the SCANNING and CLOCK_ADVANCE wrap paths must fold-and-continue exactly as before: splice-end bang once per wrap, `pending_splice` nav still consumed, no early stop. (Byte-for-byte default preservation.)
5. **CLOCK_ADVANCE oneshot + clock coherence.** `playhead 3` (CLOCK_ADVANCE), `loop 0`, `play 1`, then drive `bang`s. Assert transport stops at the boundary and that **subsequent `bang`s do not** re-advance/re-bang the frozen head (the `&& x->is_triggering` gate at `:2632`), while BPM detection still runs (a later `trigger` + new bangs resumes cleanly). (Allow the stop to take effect at the next-vector boundary, not mid-vector — see CLOCK_ADVANCE same-vector note above.)
6. **Negative-scan stop (both boundaries).** SCANNING with a **negative** `scanrate` so the head underruns `splice_start`; `loop 0`; `play 1`. Assert the stop fires at the `splice_start` underrun (the `pos < splice_start` term of `wrapped` at `:1148`), not only at `splice_end`.

## Risks / out-of-scope

**Risks**
- **CLOCK_ADVANCE clock-gate omission.** If the `ligase_bang` gate (`:2632`) is not updated, a stopped oneshot is silently re-advanced and re-stopped on every BPM bang — phantom splice-end bangs. Easiest site to forget because the perform-side stop looks complete on its own.
- **SCANNING missing `break`.** If `is_triggering=0` is set but the per-sample loop (`:1118-1171`) is not broken, remaining samples keep running `playback_position += scan_rate` and re-fire the bang / re-enter `pending_splice` in the same vector. Must `break` after the stop.
- **`is_playing` coherence.** Clearing `is_triggering` alone gives the no-cut guarantee, but `is_playing` is the UI/state mirror (paired at `:1853-1854`, read in `ligase_play` at `:1857`/`:1861`). Clearing only `is_triggering` leaves a stale "playing" state. Recommend clearing both (openDecision 5).
- **`pending_splice` precedence is semantic, not mechanical.** If implemented opposite to the owner's expectation, sequenced patches either stop when they should advance or run away when they should stop. Must be confirmed and documented (openDecision 2).
- **STATIC expectation gap.** A user who sets `loop 0` in STATIC (the default playhead mode) may expect a stop, but STATIC has no transport boundary. A silent no-op can look like a bug — must document that oneshot is meaningful only in SCANNING/CLOCK_ADVANCE (openDecision 3).
- **Re-trigger debug-dump wart.** Using `play 1` as the re-arm re-runs the multi-line `post()` dump (`:1874-1896`) every time, flooding the Pd console in fast oneshot-retrigger sequences. The dedicated `trigger` selector avoids it; docs must steer users to `trigger`.
- **Zero-length splice on re-trigger.** A degenerate splice (`start==end`) makes `trigger` set `playback_position=splice_start` + `is_triggering=1`, but `scheduler_trigger_grain` rejects `splice_end<=splice_start` (`src/grain.c:729`) so no grains spawn and the `wrapped` test may immediately re-stop. Safe (no hang — `wrap_to_splice` returns `start` for `len<=0`) but can look like "trigger does nothing"; acceptable, matches existing degenerate-splice handling.
- **Struct layout (theoretical, downgraded).** Adding `int loop_mode` to `splice_behavior_t` changes its `sizeof`/offsets in principle, but a `src/`-wide search found NO memcpy/binary-serialize/save-load of the struct — `ligase_get_state` only `post()`s values and there is no struct-blob persistence. So this is NOT a real risk; the fallback (main struct near `is_triggering` `:217`) is unnecessary but available if a future serializer is added (openDecision 1).

**Out of scope**
- Modifying `wrap_to_splice` (`src/ligase~.c:375`) or adding any loop near it — strictly forbidden (hang-safety; it is also shared with per-grain wrapping).
- Touching the render block (`src/ligase~.c:1255-1277` / `scheduler_process` `src/grain.c:1051`) or iterating `active_list` to release grains — the no-cut invariant forbids it.
- STATIC `grain_start`-wrap stop (deferred to an opt-in follow-up unless GATE A elects it).
- Any new outlet or new bang call — the terminal stop reuses the existing `x_splice_end_out` under its `outlet3_mode==0` guard.
- `headless 0/1` (`ligase_headless`, handler `src/ligase~.c:2355`, registered `:5151`) interaction — it gates only the signal-inlet epsilon thresholds (confirmed `:2355-2367`), not play/trigger/loop; the oneshot/loop surface is headless-agnostic and reachable purely via the `loop`/`trigger` selectors (no GUI dependency), consistent with the headless-0 target.