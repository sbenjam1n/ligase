# Plan P2: Channel-aware MIDI message + dual-destination routing (same channel = unison, different = separate)

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (PLAN COVERAGE — pitch-destination build-out). (NOT §1 — §1 is the COMPLETE-work changelog.)
**Related:** Plan P1 `Plans/smear_pitch.md` (the SMEAR pitch DESTINATION + its `smear_pitch_control_t` controller and note→Hz math — **P2 depends on it being merged first**); the existing pitch/MIDI surface (`pitch_*` messages, the inlet-19 signal-MIDI path); the pattern subsystem (`Plans/pattern_notation.md` / `pattern_modulation.md` / `pattern_pitch.md`, all ✅ DONE) which supplies the per-block smear-pitch and per-grain grain-pitch PATTERN sources that the "MIDI-on-one / pattern-on-other" acceptance criterion exercises.

> **VERIFY NOTE (2026-06-24, adversarial pass against the actual tree):** All file:line refs and symbols below were re-checked by reading `src/ligase~.c`, `src/types.h`, `src/grain.c`, `src/grain_smear.c`. Verdict **revised** — the plan is sound; two small code-accuracy corrections were applied in place: (1) `scheduler_trigger_grain` STARTS at `grain.c:725` (the pattern-slot read is at `:816`); (2) the `midi_msg_active` FIELD DECLARATION goes in `struct _ligase` (`ligase~.c:144`, fields ~`:300-322`), NOT inside `ligase_new` at `:4969` — only its *initialization* lives at `:4969`/`:5006`. MIDI-channel soundness, smear clamp/precedence, backward-compat, and threading all PASS. One **P1-scope** hazard surfaced (not a P2 defect, deferred): P1's recommended smear pattern slot `PATTERN_SLOTS-2`=6 collides with the param-pattern auto-allocator range (slots 0-6); the P1 author must pick a non-colliding slot.

---

## Problem

The owner wants a single incoming MIDI note stream (from a Pd `[notein]`, which emits note / velocity / channel) to drive the synth's **two pitch DESTINATIONS** — the GRAIN pitch (already exists) and the SMEAR/resonator pitch (added in P1) — and to **route by MIDI channel**. In their words:

> "I want to play the granular pitch and the smear/resonator pitch from one MIDI keyboard. If I send both destinations the **same channel** they should play in **unison** — both follow the one note. If I put them on **different channels** they're **separate** — the grain follows its channel, the smear follows its channel, and each ignores the other's channel. And I should be able to run a **pattern on one destination while MIDI drives the other** (e.g. a scale-pattern smear bed while I play the grains live, or vice-versa)."

Concretely, four sub-requirements:

1. **A real channel-carrying MIDI ingress.** The existing MIDI inlet (signal inlet 19, `midi_in[0]`) is one bare float per DSP block with NO channel, NO velocity. Channel-aware routing is structurally impossible from it. A NEW control-rate message `midi <note> [vel] [channel]`, fed from `[notein]`, is required.
2. **Channel → destination routing.** A note on the grain's channel sets grain pitch; a note on the smear's channel sets smear pitch.
3. **Same channel = unison, different channels = separate.** If both destinations are configured to the same channel, one note writes to BOTH (unison). If configured to different channels, each destination accepts only its own channel (independent).
4. **MIDI-on-one / pattern-on-other.** Because the two destinations have independently selectable sources, one can be on `source=MIDI` while the other runs a `source=PATTERN` (P1/P3) sequence — and it "just works."

Hard constraint throughout: **backward compatibility**. An object that uses none of the new features (i.e. only the inlet-19 signal MIDI for grain pitch, no `midi` message) must behave **bit-for-bit as today**.

This plan is the **channel-routing slice**: P1 builds the SMEAR pitch destination and its `smear_pitch_control_t` (with a `source=MIDI` field that exists but is left un-fed); P2 adds the `midi <note> [vel] [channel]` message, the channel→destination map, and the routing/coexistence logic that feeds notes into **both** destinations' MIDI fields by channel.

## Mechanics / target surface — the EXISTING code this extends

**Provenance note:** every reference into the **current tree** below was verified by reading `src/ligase~.c`, `src/types.h`, `src/grain.c`, `src/grain_smear.c` on 2026-06-24 (re-verified in the adversarial pass). References to `smear_pitch_control_t` / `SMEAR_PITCH_MIDI` / the smear-pitch slot are **forward-dependencies on P1** (`Plans/smear_pitch.md`), which is **not yet in the tree** — grep finds no `smear_pitch_control`, `SMEAR_PITCH_*`, or `attach_smear_pitch` as of 2026-06-24 (confirmed: `grep -rn 'smear_pitch\|SMEAR_PITCH' src/` returns empty). P2 does not compile without P1 (see GATE A).

### GRAIN-pitch MIDI ingress (the inlet-19 signal path — to be coexisted-with, NOT rewritten)

- **The signal-inlet MIDI read** — `src/ligase~.c:495-503`, inside `ligase_perform`. Gated on `pitch_control.mode == PITCH_MODE_MIDI` (`:495`); reads `int midi_note = (int)midi_in[0]` (`:497`); gates `if (midi_note >= 1 && midi_note <= 127)` (`:499`); writes `pitch_control.midi_note` + `pitch_control.midi_enabled = 1` (`:500-501`). This is the entire MIDI note ingress today — a single float per block, NO channel, NO velocity. **This is the gate-text we mirror (1..127) and the write we conditionally suppress.**
- **`midi_current` cache** — `src/ligase~.c:763`, `x->midi_current = midi_in[0];` every block, for the query/state outlet. Untouched.
- **The MIDI signal inlet itself** — `x->x_midi` created as a pure `&s_signal` inlet at `:4938` (it can never carry a channel float-list); wired `sp[18]->s_vec` (`:1782`), unpacked as `w[20]` / `midi_in` (`:1546`). It is documented as **"Inlet 19: midi"** by `get_inlets` (`:4604`). *(Aside: a stale source warning at `:4352` mis-says "inlet 17"; that is a pre-existing source typo, NOT referenced by this plan.)* P2 does not touch the inlet; it adds a parallel control-rate message.
- **Outlet-3 note-change detector** — `src/ligase~.c:1668-1693`, inside `ligase_perform`, runs once per perform call when `outlet3_mode == 1`. The MIDI branch (`:1673-1678`) bangs outlet 3 when `pitch_control.midi_note != x->prev_midi_note` (`:1675`) and then stores `x->prev_midi_note = pitch_control.midi_note` (`:1677`). **Any new path that mutates `midi_note` must keep `prev_midi_note` consistent or this outlet mis-fires.** (`prev_midi_note` is seeded to 60 at `src/ligase~.c:5006`.)

### The `pitch_*` message surface (siblings of the new `midi` handler — registration neighbors, untouched)

- **Handlers** — `ligase_pitch_mode` (`src/ligase~.c:4337`), `ligase_pitch_semitones` (`:4357`), `ligase_pitch_range` (`:4363`), `ligase_pitch_rand_type` (`:4371`), `ligase_pitch_scale` (`:4430`). All run on the **message thread** and do plain field stores. **The new `ligase_midi` handler is added immediately after `ligase_pitch_scale` (function closes at `:4452`; insert after `:4452`).**
- **Registrations** — `src/ligase~.c:5275-5279`, one `class_addmethod(...)` per `pitch_*` selector, the last being `pitch_scale` … `A_GIMME` at `:5279`. **The new `midi` / `midi_channel` / `pitch_channel` / `smear_pitch_channel` registrations go immediately after `:5279`.** (Verified: no inbound `midi` method is registered today — the `gensym("midi")` at `:4705` is an OUTPUT selector on the state outlet, so there is no collision.)

### The SMEAR pitch destination (built by P1 — P2 writes its MIDI field)

- **The smear param-sampling stanza** — `src/ligase~.c:908-925`, inside `ligase_update_inlets`, runs once per DSP block on the block thread. Today it samples `smear_frequency_range` (`:909-911`), `smear_resonance_range` (`:913-915`), `smear_stages_range` (`:917-919`), `smear_feedback_range` (`:921-923`), each via `grain_smear_set_*`. **P1 adds the `smear_pitch_control` resolution branch here (placed AFTER the `smear_frequency_range` branch, so smear-pitch OVERRIDES per GATE A);** P2 only ensures the MIDI source feeds `smear_pitch_control.note` so that branch picks it up next block.
- **`grain_smear_set_frequency`** — `src/grain_smear.c:89`. Sets `freq_hz` (`:91`) then calls `smear_update_coeffs` (defined `:47`, called `:92`), which **clamps `f` to `[20, 0.45*sr]`** (`:50-51`). The clamp lives ONLY here — note→Hz feeds raw Hz and lets this clamp bound it. P2 introduces no new Hz math (that is P1); it only routes a note INTO `smear_pitch_control`.
- **`ligase_smear_frequency`** — `src/ligase~.c:3436`, manual `smear_frequency <hz>` message → `grain_smear_set_frequency`. Untouched.
- **`x->smear` creation** — `src/ligase~.c:4969`, `grain_smear_create(48000)`. The *initialization* of new `ligase_t` routing fields happens here (and `prev_midi_note` is seeded a few lines down at `:5006`). NOTE: the *field declarations* themselves live in `struct _ligase` (`:144`, fields ~`:300-322`), not here — see Step 1.

### `pitch_control_t` and `scheduler_t` (where the GRAIN MIDI field and the routing map live)

- **`pitch_control_t`** — `src/types.h:416-426`: `mode, semitones, semitone_range, scale, midi_note, midi_enabled, last_semitone, pitch_pattern_slot`. `midi_note`/`midi_enabled` are the GRAIN destination's MIDI fields — the SAME fields the `midi` message writes when a note routes to the grain channel.
- **`scheduler_t`** — closes at `src/types.h:659`; `pitch_control` is a member at `:650`. **P1 adds `smear_pitch_control_t smear_pitch_control;` near `:650`.** P2 adds the two routing channels (`grain_midi_channel`, `smear_midi_channel`) — see Design for the home decision (GATE A).
- **`scheduler_create`** — `src/grain.c:472`; the `memset(sched, 0, …)` at `:486` zeroes everything, then explicit defaults are set (e.g. `pitch_control.pitch_pattern_slot = -1` at `grain.c:563`). New routing fields needing a non-zero default are set here (or in `ligase_new` if they live on `ligase_t`).

### The pattern PATTERN sources (the "MIDI-on-one / pattern-on-other" criterion only READS these — no change)

- **Grain pitch PATTERN** — slot `PATTERN_SLOTS-1` (=7), `PITCH_MODE_PATTERN`, read per-grain inside `scheduler_trigger_grain` (function starts `grain.c:725`; the slot read `int slot = sched->pitch_control.pitch_pattern_slot;` is at `grain.c:816`, in the `PITCH_MODE_PATTERN` case `:811-833`). The slot is *assigned* `PATTERN_SLOTS-1` at `ligase~.c:2889`. Already DONE (P3).
- **Smear pitch PATTERN** — P1 reserves a dedicated slot and reads it per block in the smear stanza. **P1-scope caveat surfaced by this verify:** P1's recommended slot `PATTERN_SLOTS-2`=6 would **collide** with the param-pattern auto-allocator, which hands out slots `0..PATTERN_SLOTS-2` (i.e. 0-6, loop bound `i < PATTERN_SLOTS - 1` at `ligase~.c:2686`) — only slot 7 is reserved (grain pitch). P1 must pick a slot the param allocator never auto-assigns (or shrink the allocator range). P2 does not touch either; it only proves that a destination on `source=PATTERN` coexists with the other on `source=MIDI`.

## Design

### Threading model (unchanged from the existing design)

`ligase_midi` (and `midi_channel` / `pitch_channel` / `smear_pitch_channel`) run on the **Pd message thread**, exactly like every `pitch_*` handler. They do **only single-word field stores** — no allocation, no `gensym`, no DSP, no `grain_smear_*` calls. The **perform thread** reads those fields (grain MIDI at `:495-503` / the P1 smear stanza at `:908-925`). Writes are single-word stores and reads are in perform; the existing code tolerates this race without locks, and P2 matches that — **no new locks.**

### New message: `midi <note> [vel] [channel]` (A_GIMME)

New handler, added immediately after `ligase_pitch_scale` (after `src/ligase~.c:4452`):

```c
// Channel-aware MIDI note ingress, fed from Pd [notein] (note / vel / channel).
// Message thread: simple field stores only. Routes by channel to the two pitch destinations.
static void ligase_midi(ligase_t *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;
    if (argc < 1) {
        pd_error(x, "ligase~: midi requires <note> [vel] [channel]");
        return;
    }
    int note    = (int)atom_getfloatarg(0, argc, argv);
    /* int vel  = (argc >= 2) ? (int)atom_getfloatarg(1, argc, argv) : 64; */  // accepted, unused (GATE A)
    int channel = (argc >= 3) ? (int)atom_getfloatarg(2, argc, argv) : 1;       // [notein] right outlet; default ch 1

    // Validate. Mirror the perform gate at ligase~.c:499 (note 1..127); channel 1..16.
    if (note < 1 || note > 127) {
        pd_error(x, "ligase~: midi note %d out of range (1-127)", note);
        return;
    }
    if (channel < 1 || channel > 16) {
        pd_error(x, "ligase~: midi channel %d out of range (1-16)", channel);
        return;
    }

    scheduler_t *sch = x->scheduler;

    // --- Route to GRAIN destination ---
    if (channel == sch->grain_midi_channel) {
        sch->pitch_control.midi_note    = note;
        sch->pitch_control.midi_enabled = 1;
        x->midi_msg_active = 1;                 // message owns the grain destination (coexistence flag)
        // NOTE: do NOT touch prev_midi_note here; the outlet-3 detector at :1668-1693 owns it and
        // will bang + update prev_midi_note on the next perform call exactly as for the inlet path.
    }

    // --- Route to SMEAR destination ---
    if (channel == sch->smear_midi_channel) {
        sch->smear_pitch_control.note         = note;     // P1 field
        sch->smear_pitch_control.midi_enabled = 1;        // P1 field
        sch->smear_pitch_control.source       = SMEAR_PITCH_MIDI;  // P1 enum value
        sch->smear_pitch_control.enabled      = 1;        // gate the P1 smear-pitch override on
    }
    // SAME channel for both => both branches fire => note written to BOTH => UNISON.
    // DIFFERENT channels => exactly one branch fires => SEPARATE/independent.
}
```

> **API note (verify):** `atom_getfloatarg` is a real Pd function (`m_pd.h:336`). The existing GIMME handler `ligase_pitch_scale` instead unpacks via `argv[i].a_type == A_FLOAT` / `argv[i].a_w.w_float`; either is valid. If you prefer to match the in-tree idiom, type-check the atoms before reading. Both compile.

Registration, immediately after `src/ligase~.c:5279`:

```c
class_addmethod(ligase_class, (t_method)ligase_midi, gensym("midi"), A_GIMME, 0);
```

`A_GIMME` because the arg count varies (`note` / `note vel` / `note vel channel`). The arg order `note, vel, channel` matches `[notein]`'s three outlets packed left-to-right (note, velocity, channel) via a `[pack]`.

**Velocity:** accepted syntactically for `[notein]` compatibility but **not acted on** (monophonic last-in-range-note-wins, mirroring the channel-less signal inlet). Note-off-as-`vel==0` is a documented later refinement (GATE A).

### The channel → destination map

Two configurable target channels. **Home:** on `scheduler_t` next to `pitch_control` (`src/types.h:650`), so both the perform-thread reads and the message-thread writes touch one struct (and `scheduler_create`'s `memset` at `grain.c:486` + explicit-default discipline already lives there). Alternative home (`ligase_t`, near where `x->smear` is created at `:4969`) is a GATE A item; `scheduler_t` is recommended for locality with `pitch_control`/`smear_pitch_control`.

```c
// In scheduler_t (src/types.h, near :650):
int grain_midi_channel;   // which MIDI channel routes to the GRAIN pitch destination
int smear_midi_channel;   // which MIDI channel routes to the SMEAR pitch destination
```

**Defaults** (GATE A — recommended): grain stays on the inlet-19 signal path as today and the smear MIDI source is **OFF by default** (P1: `smear_pitch_control.enabled = 0`), so nothing changes until the user opts in. When the user starts sending `midi` messages, the channel defaults are `grain_midi_channel = 1`, `smear_midi_channel = 2` (**independent by default**); set both equal for unison. Set explicitly in `scheduler_create` (after the `memset` at `grain.c:486`, beside `grain.c:563`):

```c
sched->grain_midi_channel = 1;
sched->smear_midi_channel = 2;
```

**Setter messages** (message thread; modeled on the validating `pitch_*` setters):

```c
// midi_channel <grain_ch> <smear_ch>  — sets both at once; equal values = unison config
static void ligase_midi_channel(ligase_t *x, t_floatarg g, t_floatarg sm) {
    int gi = (int)g, si = (int)sm;
    if (gi < 1 || gi > 16 || si < 1 || si > 16) {
        pd_error(x, "ligase~: midi_channel needs two channels 1-16");
        return;
    }
    x->scheduler->grain_midi_channel = gi;
    x->scheduler->smear_midi_channel = si;
    post("ligase~: MIDI routing grain<-ch%d, smear<-ch%d (%s)",
         gi, si, (gi == si) ? "UNISON" : "separate");
}
// pitch_channel <n>        — per-destination: grain only
// smear_pitch_channel <n>  — per-destination: smear only
```

Registrations after `:5279` (the two-arg `A_DEFFLOAT, A_DEFFLOAT` form is an established in-tree pattern, e.g. `:5261-5262`):

```c
class_addmethod(ligase_class, (t_method)ligase_midi_channel,       gensym("midi_channel"),       A_DEFFLOAT, A_DEFFLOAT, 0);
class_addmethod(ligase_class, (t_method)ligase_pitch_channel,      gensym("pitch_channel"),      A_DEFFLOAT, 0);
class_addmethod(ligase_class, (t_method)ligase_smear_pitch_channel, gensym("smear_pitch_channel"), A_DEFFLOAT, 0);
```

### Routing semantics (the unison/separate behavior)

The `if (channel == grain_midi_channel)` and `if (channel == smear_midi_channel)` tests in `ligase_midi` are **independent, both evaluated** for every note:

| Config | Incoming note on… | Grain branch | Smear branch | Result |
|---|---|---|---|---|
| `grain=1, smear=1` (equal) | ch 1 | fires | fires | **UNISON** — both destinations get the note |
| `grain=1, smear=2` | ch 1 | fires | — | grain only (smear ignores ch 1) |
| `grain=1, smear=2` | ch 2 | — | fires | smear only (grain ignores ch 2) |
| `grain=1, smear=2` | ch 3 | — | — | dropped (no destination listens) |

This is exactly the owner's spec: **same channel ⇒ unison, different ⇒ separate.** No "unison" special-case code path exists — unison is the *emergent* result of two equal channel comparators both matching. This keeps the routing branch-local and obviously-correct.

### Backward-compat coexistence with the inlet-19 signal path (GATE A option b)

The hazard: both the signal inlet (`:495-503`, every block when `mode==PITCH_MODE_MIDI`) and the `midi` message write `pitch_control.midi_note`. If both write, the inlet overwrites the message every block whenever something feeds the inlet.

**Policy (GATE A option b — recommended):** the `midi` message is the channel-aware source; **once it is in use, it OWNS the grain destination and the inlet-19 write is suppressed**; with no `midi` message in use, the inlet is **bit-identical to today**. A one-bit message-active flag drives this:

- Add `int midi_msg_active;` **to `struct _ligase` (the field declaration goes with the other state ints, ~`:300-322`, beside `outlet3_mode`/`prev_midi_note`)** — NOT inside `ligase_new`. Initialize `x->midi_msg_active = 0;` in `ligase_new` (near `:4969`/the init block at `:5004-5006`).
- `ligase_midi` sets `x->midi_msg_active = 1` whenever it routes a note to the grain channel (see handler above).
- Gate the inlet write at `:495-503`:

```c
    // src/ligase~.c:495 — suppress the signal-inlet write once a 'midi' message owns the grain dest.
    if (x->scheduler->pitch_control.mode == PITCH_MODE_MIDI && !x->midi_msg_active) {
        int midi_note = (int)midi_in[0];
        if (midi_note >= 1 && midi_note <= 127) {
            x->scheduler->pitch_control.midi_note    = midi_note;
            x->scheduler->pitch_control.midi_enabled = 1;
        }
    }
```

That is the **only** change to the inlet path: a single `&& !x->midi_msg_active` guard. When `midi_msg_active == 0` (no `midi` message ever routed to grain), the block is identical to today (the gate text, the 1..127 validation, the `midi_note`/`midi_enabled` writes are untouched). The `midi_current` cache at `:763` is left unconditional, so the query/state outlet still mirrors the raw inlet.

**Staleness window (optional, GATE A):** `midi_msg_active` could auto-clear after N blocks of no `midi` message so a patch can hand grain control *back* to the inlet. Recommendation: **latch and do not auto-clear** in v1 (simplest, single source-of-truth once chosen); expose an explicit `midi_release` / `pitch_channel`-driven reset later if a use-case appears. Documented, not silent.

**The smear destination has no signal-inlet writer at all** (the only writers of `smear_pitch_control` are the P1 message/stanza paths), so there is **no clobber hazard on the smear side** — the `midi` message is its sole MIDI source, no flag needed there.

### `prev_midi_note` / outlet-3 consistency

The `midi` message deliberately does **not** touch `prev_midi_note`. The outlet-3 detector at `:1668-1693` runs on the perform thread and is the **sole** owner of `prev_midi_note`: when `pitch_control.midi_note` (which the message just set) differs from `prev_midi_note` (`:1675`), it bangs and updates `prev_midi_note` (`:1677`). Because the message writes `midi_note` exactly the way the inlet did, the detector fires identically whether the note came from the inlet or the message — outlet 3 works for free, with no special-casing. (The smear destination has no equivalent note-change outlet today; out of scope.)

### MIDI-on-one / pattern-on-other (sub-req 4 — emergent, no new code)

Because the GRAIN destination's source is `pitch_control.mode` (OFF/SEMITONES/RANGE/SCALE/MIDI/PATTERN, enum `PITCH_MODE_*` at `types.h:401-406`) and the SMEAR destination's source is `smear_pitch_control.source` (OFF/SEMITONE/MIDI/PATTERN per P1), and the two controllers are fully independent, the following compose with **zero P2 code**:

- **Grain = MIDI, smear = PATTERN:** user sends `pitch_mode 4` + plays `midi … <grain_ch>`; smear runs `pattern smear_pitch <tokens>` (P1) on its dedicated slot. Grain pitch tracks live notes; smear pitch steps the pattern per block.
- **Grain = PATTERN, smear = MIDI:** grain runs `pattern pitch <tokens>` (P3, slot `PATTERN_SLOTS-1`=7); smear takes `midi … <smear_ch>` (`smear_pitch_control.source = SMEAR_PITCH_MIDI`).

The only requirement P2 must satisfy is that routing a note to one destination does not perturb the other's source/state — which the independent-`if` routing already guarantees (a note on the grain channel never writes any `smear_pitch_control` field, and vice-versa).

### Precedence note (inherited from P1, restated)

On the SMEAR side, when a `midi` message sets `smear_pitch_control.source = SMEAR_PITCH_MIDI` + `enabled = 1`, P1's smear stanza (placed AFTER the `smear_frequency_range` branch at `:909-911`) makes the note-derived Hz the **last writer** of `freq_hz` for the block — so it OVERRIDES manual `smear_frequency` / `smear_frequency_range`. The downstream clamp `[20, 0.45*sr]` in `smear_update_coeffs` (`grain_smear.c:50-51`) bounds whatever Hz lands. P2 introduces no new precedence; it only flips the source to MIDI and supplies the note. This is the documented two-writer resolution, not a silent race.

## Steps & gates

### GATE A (approval) — owner decisions 2026-06-24 (Seq 51)

**Owner-confirmed:** **(3)** channel defaults = **INDEPENDENT** — smear MIDI OFF by default, and when MIDI routing is enabled `grain_midi_channel=1` / `smear_midi_channel=2` (set equal for unison); fields on `scheduler_t`. **Defaulted (not separately raised, recommended options stand):** **(1)** P1-merged hard blocker remains (build-order, not a design choice); **(2)** coexistence = option (b) (`midi_msg_active` latch suppresses the inlet write once a `midi` message owns grain); **(4)** latch, no auto-clear in v1; **(5)** ignore velocity (accept syntactically); **(6)** ship `midi_channel` + `pitch_channel` + `smear_pitch_channel`. Original sign-off items retained for the record:

1. **HARD BLOCKER — P1 merged.** `Plans/smear_pitch.md` (P1) must be **written AND merged** first. As of 2026-06-24 it is NOT: `grep -rn 'smear_pitch\|SMEAR_PITCH' src/` returns empty (no `smear_pitch_control`, `SMEAR_PITCH_MIDI`, `smear_pitch_control.note/.midi_enabled/.source/.enabled`, or an `attach_smear_pitch` slot). P2's smear-routing branch in `ligase_midi` references those P1 symbols and **does not compile without them.** Do not start P2 until P1 lands. (Note: P3 grain-pitch PATTERN and the inlet-19 grain-MIDI path already exist and are stable — only the smear half is the P1 dependency.) **Verify add-on:** flag to the P1 author that P1's recommended smear slot `PATTERN_SLOTS-2`=6 collides with the param-pattern auto-allocator (slots 0-6, `ligase~.c:2686`); P1 must choose a non-auto-assigned slot.
2. **Coexistence policy for the GRAIN destination.** Recommended **option (b)**: the `midi` message owns the grain destination once used (`midi_msg_active` flag suppresses the `:495-503` inlet write); inlet is the exact default when no `midi` message is sent. Confirm vs (a) staleness-window arbitration or (c) message feeds a separate field. **(b)** gives the cleanest single-source-of-truth-per-destination and bit-identical backward compat.
3. **Routing-channel home + defaults.** Recommended: fields on **`scheduler_t`** (locality with `pitch_control`/`smear_pitch_control`); smear MIDI **OFF by default** (`smear_pitch_control.enabled = 0` from P1, nothing changes); when MIDI routing is enabled, default `grain_midi_channel = 1`, `smear_midi_channel = 2` (**independent by default**, equal = unison). Confirm vs both-default-to-1 (unison out of the box) or fields-on-`ligase_t`.
4. **`midi_msg_active` latch vs staleness.** Recommended: **latch, no auto-clear** in v1. Confirm vs an N-block staleness window that returns grain control to the inlet.
5. **Velocity / note-off semantics.** Recommended: **ignore velocity** for pitch (last in-range note wins, mirroring the channel-less inlet); accept `vel` syntactically. Confirm vs `vel==0`-as-note-off (clear `midi_enabled` for the routed destination).
6. **Setter surface.** Recommended: ship **`midi_channel <grain_ch> <smear_ch>`** (both at once) PLUS per-destination **`pitch_channel <n>`** / **`smear_pitch_channel <n>`**. Confirm the trio (vs `midi_channel` only).

### Step 1 → GATE B (types + routing-map init, no behavior change)

Add `grain_midi_channel` / `smear_midi_channel` to `scheduler_t` (`src/types.h`, near `:650`). Add `int midi_msg_active;` **to `struct _ligase` (`ligase~.c:144`, with the other state ints ~`:300-322` beside `outlet3_mode`/`prev_midi_note`) — NOT inside `ligase_new`.** Set defaults in `scheduler_create` (after the `memset` at `grain.c:486`, beside `grain.c:563`: `grain_midi_channel=1`, `smear_midi_channel=2`) and initialize `midi_msg_active=0` in `ligase_new` (near `:4969`/the init block at `:5004-5006`). **GATE:** `make clean && make` warning-free; no perform-path change yet; defaults present; an object with no `midi` message behaves exactly as today (regression check passes). *(Verify baseline: current tree builds warning-free, confirmed 2026-06-24.)*

### Step 2 → GATE C (the `midi` message + routing + setters)

Add `ligase_midi` (after `ligase_pitch_scale`, function closes `:4452`) with note 1..127 + channel 1..16 validation and the two independent routing `if`s (grain → `pitch_control.midi_note`+`midi_enabled`+`midi_msg_active=1`; smear → `smear_pitch_control.note`+`midi_enabled`+`source=SMEAR_PITCH_MIDI`+`enabled=1`). Add `ligase_midi_channel` / `ligase_pitch_channel` / `ligase_smear_pitch_channel` setters. Register all four after `:5279`. **GATE:** `make clean && make` warning-free; `midi`, `midi_channel`, `pitch_channel`, `smear_pitch_channel` reach their handlers; out-of-range note/channel `pd_error` and write nothing.

### Step 3 → GATE D (inlet-19 coexistence guard)

Add the single `&& !x->midi_msg_active` guard to the inlet-MIDI read at `src/ligase~.c:495`. Confirm: (a) with `midi_msg_active==0` the block is byte-identical to today (gate text + 1..127 + writes unchanged), and (b) once a `midi` message routes to the grain channel, the inlet stops writing `midi_note`. Leave `midi_current` (`:763`) unconditional. Confirm the outlet-3 detector (`:1668-1693`) still owns/updates `prev_midi_note` and bangs on message-driven note changes. **GATE:** `make clean && make` warning-free; inlet-only object unchanged (regression); message-driven grain pitch tracks the `midi` message without inlet clobber.

### Step 4 → GATE E (verify, headless)

Build; run the acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1` so perform actually runs; record noise into the reel + `play 1` to granulate; capture grain output and the resolved smear `freq_hz` via the state/query outlet and/or `writesf~`). Confirm every acceptance criterion and no regression in the inlet path, `pitch_*`, or `smear_frequency`. Update the manual's MIDI/pitch section to document `midi <note> [vel] [channel]`, `midi_channel`, the unison-vs-separate rule, and the inlet-coexistence (`midi_msg_active`) behavior.

## Acceptance criteria (headless-testable with `pd -nogui -nosound`)

All patches drive ligase~ via the `midi`/`midi_channel` selectors and a `[notein]→[pack note vel channel]` analogue (in a headless test, send the `midi` list directly). Each loadbangs `\; pd dsp 1`.

1. **Same-channel UNISON.** `midi_channel 1 1`, `pitch_mode 4` (grain MIDI), smear MIDI enabled. Send `midi 69 64 1` (A4). Assert (a) grain pitch reflects note 69 (grain playback speed = `base_speed * 2^((69-60)/12)` per the grain MIDI semantics — confirmed `semitones_to_speed = powf(2, s/12)` at `grain.c:86-88`, `current_semitone = midi_note - 60` at `grain.c:838`; `midi_note==69`), AND (b) the smear destination's resolved `freq_hz` is the P1 note→Hz for 69 (≈440 Hz with default `ref_note=69/ref_hz=440`). Both destinations followed **one** note on **one** channel.
2. **Different-channel SEPARATION.** `midi_channel 1 2`. Send `midi 60 64 1` (grain channel) then `midi 81 64 2` (smear channel). Assert grain pitch tracks note 60 only (unchanged by the ch-2 note) and smear `freq_hz` tracks note 81 only (unchanged by the ch-1 note). Then send `midi 72 64 3` (neither channel) and assert **neither** destination changes (note dropped).
3. **Inlet-19-only object unchanged (regression).** A patch that sends NO `midi` message, uses `pitch_mode 4`, and feeds the signal inlet a note (e.g. a `[sig~ 67]` into the MIDI inlet) must produce **byte-for-byte** the same grain pitch as the current build (grain tracks note 67 via `:495-503`). `midi_msg_active` stays 0; `midi_current` still mirrors the inlet at `:763`.
4. **`midi` message owns grain, suppresses inlet.** `pitch_mode 4`, `midi_channel 1 2`, feed the signal inlet a constant note 67 AND send `midi 50 64 1`. Assert grain pitch tracks **50** (the message), NOT 67 (the inlet is suppressed because `midi_msg_active==1`). Confirms the coexistence guard.
5. **MIDI-on-one + pattern-on-other.** `midi_channel 1 2`. Grain = MIDI (`pitch_mode 4`, send `midi 64 64 1`); smear = PATTERN (`pattern smear_pitch <0 4 7>` from P1, with a `smear_pitch_scale`/`pitch_scale` loaded). Assert grain pitch holds note 64 while smear `freq_hz` **steps** the pattern degrees per cycle (one degree per cycle at the default cycle), independently. Then swap: grain = PATTERN (`pattern pitch [0 4 7]`, P3 slot 7), smear = MIDI (send `midi 81 64 2`); assert grain steps while smear holds 81.
6. **Channel / note range rejection.** `midi 0 64 1` and `midi 128 64 1` each `pd_error` and write nothing (no destination changes). `midi 60 64 0` and `midi 60 64 17` each `pd_error` and write nothing. A valid `midi 60 64 1` after each rejection works normally (state was not corrupted).
7. **Outlet-3 note-change via the message.** `outlet3_mode 1`, `pitch_mode 4`, `midi_channel 1 2`. Send `midi 60 64 1` then `midi 64 64 1`. Assert outlet 3 bangs exactly once per distinct note (the existing `:1668-1693` detector fires on the message-written `midi_note` change at `:1675` and updates `prev_midi_note` at `:1677`), and does NOT bang on a repeated identical note.
8. **No regression in `smear_frequency` / `pitch_*`.** With smear MIDI OFF and no `midi` message, `smear_frequency <hz>` (`:3436`) and `smear_frequency_range` (`:909-911`) behave exactly as today; all `pitch_*` messages (`pitch_mode`/`pitch_semitones`/`pitch_range`/`pitch_rand_type`/`pitch_scale`, `:4337-4452`) behave identically. Build warning-free; `test_delay.pd` clean.

## Risks / out-of-scope

**Risks**

- **P1 not merged (hard dependency).** The top risk: P2's smear-routing branch references `smear_pitch_control_t` / `SMEAR_PITCH_MIDI`, which do not exist until P1 lands (verified absent in `src/`). P2 is unbuildable until then. GATE A.1 blocks on this. (The grain half — inlet-19 path + `pitch_control.midi_note`/`midi_enabled` — already exists, so the grain-only subset could in principle build, but ship P2 whole.)
- **Smear pattern-slot collision (P1-scope, flagged by verify).** P1's recommended smear slot `PATTERN_SLOTS-2`=6 collides with the param-pattern auto-allocator, which assigns slots 0..6 (`ligase~.c:2686`). Only slot 7 is reserved (grain pitch). P1 must reserve a slot the auto-allocator never returns. Out of P2 scope but a hard P1 correctness item.
- **`midi_msg_active` latch is one-way (v1).** Once a `midi` message routes to grain, the inlet is suppressed for the object's lifetime (no auto-clear). A user who wires both the inlet and a `[notein]` and expects to "switch back" to the inlet live will find the inlet dead. Documented; a `midi_release`/staleness path is the GATE A.4 alternative if a use-case appears. Mitigation: the inlet-only object (no `midi` message) is wholly unaffected (criterion 3).
- **Race on single-word fields (existing design).** `ligase_midi` (message thread) writes `midi_note`/`midi_enabled`/`smear_pitch_control.*` while perform reads them. This matches the existing lock-free `pitch_*`/inlet design exactly (single-word stores, reads in perform) — acceptable by precedent, not a new hazard. Do not introduce locks.
- **`prev_midi_note` ownership.** If a future change makes `ligase_midi` write `prev_midi_note` directly (it must NOT), it would race the outlet-3 detector and double-update, causing missed bangs. The detector at `:1668-1693` is the sole owner; keep it so.
- **Channel-default surprise.** `grain=1, smear=2` (independent default) means a user expecting unison must explicitly `midi_channel 1 1`. The `midi_channel` handler `post`s "UNISON"/"separate" to make the active config visible; documented in the manual.
- **`[notein]` arg-order / `[pack]` wiring.** `[notein]` emits note/velocity/channel on three outlets; the user must `[pack f f f]` (or message-box) in note,vel,channel order to match `midi <note> [vel] [channel]`. A mis-wired pack (e.g. channel in slot 2) routes to the wrong destination silently. Documented with an example patch; out of scope to auto-detect.

**Out of scope**

- The SMEAR pitch destination, `smear_pitch_control_t`, note→Hz math, the smear-pitch PATTERN slot, and the smear stanza override placement — all **P1** (`Plans/smear_pitch.md`). P2 only *feeds* the MIDI source field P1 defines.
- Velocity-as-amplitude/gate and note-off (`vel==0`) handling — accepted syntactically, not acted on (GATE A.5); a later refinement.
- Polyphony / chords — the routing is monophonic last-note-wins per destination (matching the channel-less inlet); multi-voice MIDI is not in scope.
- A smear note-change outlet — the grain destination's outlet-3 detector is reused as-is; the smear destination gets no analogous change-bang.
- Modifying the MIDI signal inlet, `wrap_to_splice`, the render block, or any `pitch_*` / `smear_frequency` handler — P2 is strictly additive plus the single `&& !x->midi_msg_active` guard at `:495`.
- MIDI-out / echoing the routed notes — only the existing speed-transpose (grain), Hz (smear), and outlet-3 bang are wired.