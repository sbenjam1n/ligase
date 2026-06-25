# Plan: Polyphony (chordal granular) — a voice pool + N-transposition grain spawning over the shared playhead

**Owner:** SLB
**Date:** 2026-06-24
**Status:** PLANNED (not started)
**Tracked in:** `QUEUE.md` §4a (new directions)
**Related:** the **grain trigger** section (`ligase_process_grains` at `ligase~.c:1103` + its three `scheduler_trigger_grain` call sites at `ligase~.c:1162/1191/1312`), the **grain pitch** section (`scheduler_trigger_grain` at `grain.c:751`, its `PITCH_MODE_MIDI` case at `grain.c:861` with the `midi_note - 60` assignment at `grain.c:864`, and `semitones_to_speed` at `grain.c:86`), and the **channel-MIDI** section (`ligase_midi` channel routing at `ligase~.c:4713`, the P2 routing this plan extends from last-note-wins to a voice pool).

> **PROVENANCE.** Every file:line below was verified by reading `src/grain.c`, `src/ligase~.c`, and `src/types.h` on 2026-06-24, with three corrections to the original grounding folded in (flagged ⟦V⟧): **(1)** the soft live cap default is `max_grains = 4` (`grain.c:494`), NOT 200 — 200 (`DEFAULT_MAX_GRAINS`) / 2000 (`MAX_POOL_SIZE`) are the **`pool_size`** constants at `types.h:607-608`, a different number; the budget math below uses 4. **(2)** `ligase_midi` (`ligase~.c:4713`) already routes by channel to BOTH the grain dest (`:4731`) and the smear dest (`:4738`) — P2 has landed — so the voice-pool write extends an existing channel branch, it is not a greenfield handler. **(3)** the increment snapshot is `grain->increment = final_speed;` at `grain.c:1036` and the fine-tune sample + ±4.0 clamp are at `grain.c:871-879` / `grain.c:887-901` (the original grounding's 874/888 were off by a few lines). MIDI-case logic, the per-trigger single-call structure, and the audio-thread-safety constraints all verified sound.

---

## Problem

The owner wants the external to play **chords**, but has made a specific, narrow decision about what "polyphony" means here:

> **OWNER DECISION (Reading A).** Polyphony = a distinct **POLY MODE** where a chord is **N simultaneous PITCH transpositions of the SAME grain stream from the ONE shared playhead** — NOT independent playheads. When POLY is on, each grain trigger spawns **one grain per active voice**, each at that voice's pitch (playback speed). The playhead, splice, SOS, and recording paths are **untouched**. A voice **POOL** holds up to `maxvoices` active notes; note allocation is **hold last-N notes, then steal the oldest** when full. Notes arrive from the channel-aware `midi` message (P2) and/or a chord/note message.

Concretely: a `[ midi 60 64 67 ]`-style chord (root/third/fifth of a C-major triad) played into POLY mode should make every grain-trigger tick spawn **three** grains from the same reel position/splice — one at the speed of note 60, one at note 64, one at note 67 — so the granular cloud rings as a chord while the playhead scans exactly as it does today. Turn POLY off (the default) and the external is **byte-for-byte identical** to today's monophonic last-note-wins behavior.

> **Reading B is explicitly OUT OF SCOPE.** A "voice = its own splice / its own material / its own playhead" is a different feature. The owner achieves Reading B by instantiating **parallel `ligase~` objects** in the patch — so `ligase~` itself stays **single-playhead** by construction. This plan does not add per-voice splices, per-voice playheads, or per-voice SOS. (See Risks / out-of-scope.)

The whole design reuses the **existing** note→speed machinery: `semitones_to_speed` (`grain.c:86`, `powf(2, n/12)`) + the per-grain fine-tune (`grain.c:871-879`). Per-voice pitch is just the voice's MIDI note transposed against middle C (60), exactly as the current single-note `PITCH_MODE_MIDI` path already does (`grain.c:861`, `current_semitone = midi_note - 60`). The only genuinely new things are: a small voice array, a POLY gate, and turning each of the three single trigger calls into a per-voice loop.

## Mechanics / target surface — the EXISTING code this extends

### Single grain pitch today (last-note-wins, one scalar)

- **`scheduler->pitch_control.midi_note`** — one `int` scalar. Two writers, both last-note-wins:
  - **`ligase_midi`** (control/message thread — a `class_addmethod` handler registered at `ligase~.c:5620`, NOT the audio thread) — `src/ligase~.c:4713`. The channel-aware `midi <note> [vel] [channel]` handler (P2, already in tree). When `channel == grain_midi_channel` (`:4731`) it does `sch->pitch_control.midi_note = note; sch->pitch_control.midi_enabled = 1; x->midi_msg_active = 1;` (`:4732-4734`); when `channel == smear_midi_channel` (`:4738`) it writes the smear dest (`:4739-4742`). **This is exactly where a voice enters the pool** — today it overwrites one scalar; POLY appends to a small array. (Confirms the safety claim: pool WRITES are on the message thread, pool READS in the perform-thread trigger loops — the canonical single-writer/single-reader split.)
  - **inlet-19 perform write** — `src/ligase~.c:499-507`. In `PITCH_MODE_MIDI` and `!x->midi_msg_active`, reads `midi_in[0]` (a `t_sample` signal carrying a bare note number, never a channel) and overwrites `pitch_control.midi_note`. Suppressed once a `midi` message has claimed the grain dest (`midi_msg_active`, declared `src/ligase~.c:305`).
- **`scheduler_trigger_grain`** — `src/grain.c:751`. Spawns ONE grain. **Takes no pitch argument** — it reads `sched->pitch_control` internally. In the `PITCH_MODE_MIDI` case (`grain.c:861-868`, case label at `:861`) it computes `current_semitone = sched->pitch_control.midi_note - 60` at `grain.c:864` (guarded by `if (sched->pitch_control.midi_enabled)`, `:863`), then `final_speed = base_speed * semitones_to_speed(current_semitone)` at `:865`. The fine-tune sample folds in next (`grain.c:871-879`), then `final_speed` is clamped to ±4.0 (`grain.c:887-901`), then snapshotted into the grain at `grain->increment = final_speed;` (`grain.c:1036`) along with `position`/`amplitude`/`pan`/`saw`/`splice_start`/`splice_end` (`grain.c:1035-1045`). **Once stored, the grain is pitch-independent** — it advances by its own frozen `increment` and never re-reads `pitch_control`.
- **`semitones_to_speed`** — `src/grain.c:86`, `powf(2.0f, semitones/12.0f)`. The transposition kernel each voice feeds. ⟦V⟧ It is **`static`** (file-local to `grain.c`), so the per-voice override MUST live inside `scheduler_trigger_grain` (which it does — the override changes only the `note` fed into this same kernel); no call site outside `grain.c` calls it directly, so the `int voice_note, int voice_active` params on `scheduler_trigger_grain` are the entire surface the trigger sites touch.
- **`grain_t`** — `src/types.h:290-303`. The per-grain snapshot: `position`, `increment` (= speed), `amplitude`, `pan`, `saw_cycles`, `saw_depth`, `splice_start`, `splice_end`, `next`. `increment` is the field that freezes each voice's pitch at trigger time.

### Where grains are spawned per trigger (the three POLY extension points)

- **`ligase_process_grains`** — `src/ligase~.c:1103`. The perform-thread routine holding three mutually-exclusive playhead-mode blocks. Each has a per-DSP-sample loop with a `grain_trigger_counter >= grain_trigger_period` gate that, when it fires, calls `scheduler_trigger_grain(...)` **exactly once** with the shared `grain_pos`/`x->speed`/`splice_start`/`splice_end`/`x->amplitude`/`x->pan`/`x->saw_cycles`/`x->saw_depth`. These are the only grain-spawn points:
  - **STATIC** — `src/ligase~.c:1162` (inside the `PLAYHEAD_MODE_STATIC` trigger gate). **POLY extension point #1.** The `grain_start`-wrap bang (`:1145-1151`) and `prev_grain_start` update (`:1176`) are OUTSIDE the trigger gate and untouched.
  - **SCANNING** — `src/ligase~.c:1191`. **POLY extension point #2.** The playhead advance (`x->playback_position += x->scan_rate`, `:1204`), `wrap_to_splice` (`:1211`), splice-end bang (`:1214-1216`), pending-nav (`:1219-1232`), and one-shot stop/`break` (`:1236-1240`) are all OUTSIDE the trigger gate and MUST stay untouched.
  - **CLOCK_ADVANCE** — `src/ligase~.c:1312`. **POLY extension point #3.** The clock-bang advance block (`:1255-1297`) is OUTSIDE the trigger gate and untouched.

### The grain pool / budget gate

- **`scheduler_allocate_grain`** — `src/grain.c:701`. Pulls from `free_list`; samples `max_grains` via `maxgrains_range` (`:704-712`, clamped to `[1, pool_size]`), counts the `active_list` (`:714-720`), and **returns NULL if `active_count >= sampled_max_grains`** (`:721`). `scheduler_trigger_grain` handles the NULL gracefully — it logs once and returns without spawning (`grain.c:769-777`). **This is the budget gate N voices × overlap will hit.**
- **pool sizing** — `src/types.h:610-620`. `grain_pool` / `pool_size` / `free_list` / `active_list` / `max_grains`. ⟦V⟧ The **soft live cap default is `max_grains = 4`** (set in `scheduler_create`, `grain.c:494`). The constants `DEFAULT_MAX_GRAINS = 200` and `MAX_POOL_SIZE = 2000` (`types.h:607-608`) bound `pool_size` (the **physical** free-list size, config-driven), NOT the soft cap. So today at most 4 grains are live at once regardless of pool size; **this 4 is the number N voices multiply against.**
- **`scheduler_create`** — `src/grain.c:473`. `memset(sched, 0, ...)` (`:487`), `max_grains = 4` (`:494`), pitch defaults incl. `midi_note = 60` / `midi_enabled = 0` (`:564-565`), and (P2, already present) `grain_midi_channel = 1` / `smear_midi_channel = 2` (`:588-589`). The POLY flag + voice array default-init goes here.

### Precedent / analogy (the same budget reasoning, already in tree)

- **`MAX_STUT_GRAINS`** — `src/types.h:67`. The Stut mode already documents *"Voice-pool size: total simultaneous grains across OVERLAPPING triggers … banging the trigger layers stutters (up to 64 voices) instead of clobbering the previous one."* The identical multiplication-of-live-grains reasoning applies to N chord voices — this plan is "do that, but the multiplier is the chord's voice count and the per-voice difference is pitch."

## Design

### The voice POOL (`src/types.h`, in `scheduler_t` near `grain_midi_channel` at `:683`)

A tiny, POD, fixed array — read lock-free by the perform thread, written by the control thread. Placed adjacent to `grain_midi_channel`/`smear_midi_channel` (`types.h:683-684`, before the closing `} scheduler_t;` at `:693`):

```c
#define MAX_VOICES 8   // chord cap; budget ceiling is pool_size, practical ceiling is max_grains*N

    // CHORDAL POLY voice pool. WRITTEN by ligase_midi (control thread), READ by the three trigger
    // loops in ligase_process_grains (perform thread). POD + single-writer/single-reader; no locks.
    int poly_enabled;            // 0 = mono (default) -> trigger sites take the single-call path
    int voice_note[MAX_VOICES];  // active MIDI notes (each transposed vs 60 at spawn)
    int voice_age[MAX_VOICES];   // monotonic insert order, for oldest-note stealing
    int voice_count;             // number of active voices; 0 = mono path
    int voice_next_age;          // monotonic counter handed to each new voice
```

Default `poly_enabled = 0`, `voice_count = 0` ⇒ the mono path is untouched (the `memset` at `grain.c:487` already zeroes all of this; `scheduler_create` adds explicit lines for clarity, below).

### POLY mode gate (`src/ligase~.c`, a new `poly` message)

A `poly <0|1>` message handler (`ligase_poly`, registered next to `ligase_midi` at `ligase~.c:5620`) sets `scheduler->poly_enabled`. Every trigger-site loop and the `ligase_midi` voice-write branch on this single flag. With it `0` (the create-time default) the code path is **byte-for-byte today's single-call mono path**. This surfaces the POLY toggle WITHOUT touching `PITCH_MODE_*` semantics — POLY is orthogonal to the pitch *source* (a POLY chord is always MIDI-note-driven; the per-voice note replaces the scalar `midi_note - 60`).

### Per-voice pitch override on `scheduler_trigger_grain` (`src/grain.c:751`)

The cleanest shape (keeps the per-grain snapshot semantics intact): add an explicit per-voice override parameter rather than mutating shared state.

```c
// was: void scheduler_trigger_grain(scheduler_t*, float position, float speed,
//            uint32_t splice_start, uint32_t splice_end, float amplitude, float pan,
//            float saw_cycles, float saw_depth);
void scheduler_trigger_grain(scheduler_t *sched, float position, float speed,
        uint32_t splice_start, uint32_t splice_end, float amplitude, float pan,
        float saw_cycles, float saw_depth,
        int voice_note, int voice_active);   // NEW: per-voice MIDI note; voice_active=0 -> ignore (mono)
```

In the `PITCH_MODE_MIDI` case (`grain.c:861-868`), when `voice_active` is set use the voice's note instead of the shared scalar:

```c
        case PITCH_MODE_MIDI:
            {
                int note = voice_active ? voice_note : sched->pitch_control.midi_note;
                if (voice_active || sched->pitch_control.midi_enabled) {
                    current_semitone = note - 60;                    // same kernel as today
                    final_speed = base_speed * semitones_to_speed(current_semitone);
                }
            }
            break;
```

Everything downstream is **reused verbatim** so each voice gets an independent, correctly-clamped frozen increment:
- the fine-tune sample/fold (`grain.c:871-879`) runs **per voice** — each voice gets its own fine offset and recomputed `final_speed`;
- the ±4.0 clamp (`grain.c:887-901`) runs **per voice** — a voice transposed past ±48 semitones clamps **without affecting the others**;
- the snapshot `grain->increment = final_speed;` (`grain.c:1036`) freezes that voice's pitch.

The two mono call sites that DON'T pass a voice (none, after this plan — all three trigger sites pass `voice_active`) and any non-MIDI mode pass `voice_active = 0`, so the function behaves exactly as today.

> The alternative (write `pitch_control.midi_note = voice_note[v]` then call the unchanged function) also works since it is the same thread reading and writing — but it mutates shared state inside the per-voice loop and is easy to get subtly wrong. The explicit parameter is the recommended shape and is what the trigger-site snippets below assume. ⟦V⟧ Note: `last_semitone` (`grain.c:882`) is written every call; with N voices it ends each tick holding the LAST voice's semitone. The outlet-3 note-change detector (`ligase~.c:1753`) keys off `midi_note`, not `last_semitone`, in MIDI mode, so this is benign — but flag it (GATE A) so the chord doesn't spuriously bang outlet 3 per voice.

### Per-trigger loop change (the three sites)

At each of `ligase~.c:1162` / `:1191` / `:1312`, replace the single call with a per-voice loop gated on POLY. Snapshot `voice_count` **once** at the top of the block (tear-tolerance, below):

```c
    // (at the top of the trigger block, once:)
    int vc = x->scheduler->voice_count;            // snapshot once for this block (tear-tolerant)
    int poly = x->scheduler->poly_enabled && vc > 0;

    // (at the trigger gate, replacing the single call:)
    if (poly) {
        for (int v = 0; v < vc; v++) {
            scheduler_trigger_grain(x->scheduler, grain_pos, x->speed,
                splice_start, splice_end, x->amplitude, x->pan, x->saw_cycles, x->saw_depth,
                x->scheduler->voice_note[v], 1);   // per-voice note; voice_active = 1
        }
    } else {
        scheduler_trigger_grain(x->scheduler, grain_pos, x->speed,
            splice_start, splice_end, x->amplitude, x->pan, x->saw_cycles, x->saw_depth,
            0, 0);                                  // mono: voice_active = 0 (today's behavior)
    }
```

**Only the spawn call multiplies.** `grain_pos` / `x->speed` / `splice_start` / `splice_end` / `x->amplitude` / `x->pan` / `x->saw_cycles` / `x->saw_depth` are **shared across all voices** — so all voices read the same playhead position and splice and stay phase-locked; only their playback **increment (pitch)** differs. The surrounding playhead-advance / wrap / splice-end-bang / pending-nav / one-shot-stop logic (SCANNING `:1203-1240`, CLOCK `:1255-1297`, STATIC `:1145-1176`) is **entirely outside the loop and untouched**.

### How notes enter the pool (`ligase_midi`, `src/ligase~.c:4731`)

In the `channel == grain_midi_channel` branch (`:4731-4734`), branch on `poly_enabled`:

- **POLY off (default):** keep the existing single-scalar last-note-wins write (`midi_note = note; midi_enabled = 1; midi_msg_active = 1;`) — byte-identical to today.
- **POLY on:** append `note` to the voice pool:
  - if `note` already present in `voice_note[0..voice_count)`, refresh its age (re-trigger, don't duplicate);
  - else if `voice_count < MAX_VOICES`, write `voice_note[voice_count] = note; voice_age[voice_count] = voice_next_age++;` **then** `voice_count++` (write the slot BEFORE bumping the count — see safety);
  - else (**full → steal oldest**) find the slot with the minimum `voice_age`, overwrite its `note` and set its age to `voice_next_age++` (count unchanged).
  - set `midi_enabled = 1`; do NOT set `midi_msg_active`-driven scalar in POLY (the scalar is the mono path).

A **note-OFF** path removes a voice: a `midi <note> 0` (velocity 0 in `argv[1]`) — or a dedicated `noteoff <note>` message — finds `note` in the pool, and removes it by **swapping the last active slot down and decrementing `voice_count`** (decrement-after-clear ordering, below). When `voice_count` returns to 0, the cloud naturally goes silent on the next tick (the mono branch with `voice_count == 0` spawns nothing in POLY, or the user turns POLY off to resume the scalar).

> A **chord message** (e.g. `chord 60 64 67`) is sugar over the same pool: clear the pool, then append each note (capped at `MAX_VOICES`, stealing if a single chord exceeds the cap). Whether the surface is the per-note `midi` message, a `chord` list, or both is **GATE A**.

### Grain-pool budget strategy (the honest part)

Live grains = (active voices) × (per-voice overlap), where overlap = `grain_length / grain_trigger_period` (how many triggers fit inside one grain's lifetime). The gate is `scheduler_allocate_grain` (`grain.c:721`): once `active_count >= sampled_max_grains` it returns NULL, and **`scheduler_trigger_grain` swallows the NULL gracefully** (logs once, `grain.c:769-777`).

⟦V⟧ **The default soft cap is `max_grains = 4`** (`grain.c:494`). With N voices, the very first voice(s) in the per-voice loop win every available slot and **later voices are starved (silent)** — a 3-note chord at default settings could render as 1–2 notes. This is the central CPU/quality tradeoff and must be confronted, not hidden. Options (GATE A):

1. **Simplest, safe first cut — let the global cap stand + document it.** N voices share the 4-slot budget; high `voice_count` thins per-voice density. NULL-returns are already handled. Ship this, then refine if voice starvation is audible. Downside: chords sound uneven at default `max_grains`.
2. **Per-voice share.** Compute `per_voice = max(1, sampled_max_grains / vc)` and enforce it per voice (either temporarily scope the cap around each per-voice call, or track a per-voice active count). Fairer, bounded CPU, but needs the allocator to know "which voice."
3. **Scale the cap with voice count.** Effectively raise the live cap to `max_grains * vc` when POLY is on, so each voice keeps its mono density. Highest fidelity, **highest CPU** (linear in N) — every live grain is processed every sample in `scheduler_process`, so a chord of N notes at mono overlap is N× the mono CPU. Must stay under `pool_size` (default 200, hard cap 2000) or grains silently drop (handled, but the chord audibly thins).

**Recommendation:** ship option 1 (cap stands) but **bump the practical default** so chords aren't starved — e.g. document that POLY users raise `max_grains` (or it auto-scales by `voice_count` per option 3 with a ceiling of `min(max_grains * MAX_VOICES, pool_size)`). The hard ceiling is `pool_size`; N × overlap MUST stay under it. Decide at GATE A.

### Default = monophonic (byte-identical)

With `poly_enabled = 0` AND `voice_count = 0` (the create-time defaults):
- the three trigger sites take the `else` branch — a single `scheduler_trigger_grain(..., 0, 0)` call, where `voice_active = 0` makes the function read `pitch_control.midi_note` exactly as today;
- `ligase_midi` takes the existing scalar last-note-wins write;
- the inlet-19 perform write (`ligase~.c:499`) and `midi_msg_active` arbitration are untouched.

So a patch that never sends `poly 1` is **bit-for-bit identical to today's external** — same grain count, same pitch path, same MIDI behavior.

### Initialization (`src/grain.c`, in `scheduler_create` near the pitch init at `:564`)

The `memset` at `grain.c:487` already zeroes the pool; add explicit lines next to the existing `midi_note = 60` / `midi_enabled = 0` defaults for clarity and intent:

```c
    // CHORDAL POLY voice pool defaults (memset already zeroed). Explicit for clarity:
    sched->poly_enabled  = 0;   // mono by default -> trigger sites take the single-call path
    sched->voice_count   = 0;   // no active voices
    sched->voice_next_age = 0;  // monotonic age counter for oldest-note stealing
```

This guarantees default = today's monophonic behavior.

## Steps & gates

### GATE A (approval) — open decisions, owner sign-off before coding

1. **`maxvoices` default (`MAX_VOICES`).** Recommend **8** (covers triads/7ths/9ths; tiny POD array). Confirm vs 4 (cheaper, common chord sizes) or 16.
2. **Note-stealing policy.** Recommend **steal the oldest note** (min `voice_age`) when the pool is full, per the owner decision. Confirm vs steal-lowest / steal-highest / ignore-new (reject the chord's extra notes).
3. **Pool-budget strategy** (the load-bearing one — see Design): **(1)** global cap stands + document, **(2)** per-voice share `max_grains/vc`, or **(3)** scale the cap to `max_grains * vc` (ceiling `pool_size`). Recommend **(1) for the first cut + raise the practical `max_grains` for POLY**, refine to (2)/(3) if starvation is audible. This decision sets the CPU ceiling.
4. **How voices map: MIDI vs a chord message.** Recommend **both** — the channel-aware `midi <note> [vel] [ch]` appends/removes per-note voices (vel 0 = note-off), and a `chord 60 64 67` list sets the whole pool at once. Confirm vs MIDI-only (simpler) or chord-message-only.
5. **Does the smear/resonator pitch also go poly, or stay mono?** Recommend **smear stays MONO** in this plan (the smear dest is a single resonator frequency — `grain_smear_set_frequency` sets one `freq_hz`; "poly smear" would mean N parallel resonators, a much larger change). The smear MIDI route (`ligase_midi` `:4738`) keeps its last-note-wins scalar. Confirm.
6. **Outlet-3 / `last_semitone` per-voice bang.** ⟦V⟧ `scheduler_trigger_grain` writes `last_semitone` every call (`grain.c:882`); N voices leave it holding the last voice's value. Recommend the outlet-3 MIDI detector (keyed on `midi_note`, `ligase~.c:1753`) is left as-is so a chord does NOT spuriously bang outlet 3 per voice. Confirm no per-voice note-change bang is wanted.

### Step 1 → GATE B (types + init, no behavior change)

Add `MAX_VOICES` + the `poly_enabled` / `voice_note[]` / `voice_age[]` / `voice_count` / `voice_next_age` fields to `scheduler_t` (`types.h:683`, before the brace at `:693`). Add the explicit defaults in `scheduler_create` (`grain.c:564`). **GATE:** `make clean && make` warning-free; a fresh object reports `poly_enabled == 0` / `voice_count == 0` and the trigger sites + `ligase_midi` are unchanged ⇒ identical behavior.

### Step 2 → GATE C (per-voice override on the spawn function)

Add the `int voice_note, int voice_active` params to `scheduler_trigger_grain` (`grain.c:751`) and the extern prototype (`ligase~.c:61`). Override the `PITCH_MODE_MIDI` semitone (`grain.c:861`) with the voice's note when `voice_active`. Update the three call sites to pass `(0, 0)` for now (mono, no behavior change yet). **GATE:** `make clean && make` warning-free; with `(0,0)` at every site the output is byte-for-byte today's; the fine-tune + ±4.0 clamp + increment snapshot run per call exactly as before.

### Step 3 → GATE D (POLY gate + trigger-site loops)

Add the `poly <0|1>` message (`ligase_poly`, registered near `ligase~.c:5620`) setting `poly_enabled`. Wrap each of the three trigger calls (`:1162` / `:1191` / `:1312`) in the per-voice loop, snapshotting `voice_count` once at the top of each block. **GATE:** `make clean && make` warning-free; with `poly 0` (default) the single-call `else` branch runs (mono unchanged); with `poly 1` and a manually-seeded pool, N grains spawn per tick. The surrounding playhead/splice/SOS logic is provably untouched (diff shows only the spawn call wrapped).

### Step 4 → GATE E (voice pool writes in `ligase_midi` + budget)

Extend the `channel == grain_midi_channel` branch (`ligase~.c:4731`) to append/steal into the pool when `poly_enabled` (keeping the scalar path when off). Add the note-off path (vel 0) and, per GATE A.4, the `chord` message. Implement the chosen budget strategy (GATE A.3) in/around `scheduler_allocate_grain` (`grain.c:701`). **GATE:** `make clean && make` warning-free; a 3-note chord seats 3 voices; a 4th note past `MAX_VOICES` steals the oldest; vel 0 removes a voice; default `max_grains` behavior documented.

### Step 5 → GATE F (verify, headless)

Build; run the acceptance patches below under `pd -nogui -nosound -stderr -path . <patch>.pd` (each loadbangs `\; pd dsp 1` so perform runs), recording the granular output via `writesf~` and reading the spectral content. Confirm all acceptance criteria; confirm `poly 0` is a no-op against a captured mono baseline WAV. Update the manual to document `poly`, the voice pool, the chord message, oldest-note stealing, and the budget/CPU note.

## Acceptance criteria (headless-testable with `pd -nogui -nosound`)

All via `pd -nogui -nosound -stderr -path . <patch>.pd`; record a pitched tone into the reel, granulate, capture output via `writesf~`, and FFT the captured WAV for the per-voice spectral peaks.

1. **A chord spawns N× the grains at the right speeds.** `poly 1` then a C-major triad (`midi 60 / 64 / 67` on the grain channel, or `chord 60 64 67`). Each grain-trigger tick spawns **3** grains (count the `scheduler_trigger_grain` entries, or the live grains) at increments `2^((60-60)/12)=1.0`, `2^((64-60)/12)≈1.260`, `2^((67-60)/12)≈1.498` × base speed — i.e. the captured WAV shows three spectral peaks at root / major-third / fifth ratios. Verifies the per-voice loop + the per-voice `PITCH_MODE_MIDI` override + `semitones_to_speed`.
2. **Mono default is unchanged.** A patch that never sends `poly 1` produces a WAV **bit-identical** to a pre-change baseline (same grain count, same single pitch, same MIDI scalar behavior). `make clean && make` warning-free. Verifies the `voice_active = 0` / `voice_count == 0` mono path.
3. **Stealing at `maxvoices`.** With `MAX_VOICES = 8` (or the GATE A value), send 9 distinct notes in POLY. The pool holds 8; the 9th **steals the oldest** (the first note sent is the one dropped) — the FFT shows the 8 newest pitches, not the first. Verifies `voice_age` oldest-note stealing.
4. **Note-off removes a voice.** Seat a 3-note chord, then `midi 64 0` (vel 0). The pool drops to 2 voices (root + fifth); the major-third peak disappears from the FFT; grain count per tick drops to 2. Verifies the note-off swap-down path.
5. **Per-voice clamp independence.** A chord with one extreme voice (e.g. `midi 60 127` — note 127 transposes +67 semitones, past the ±48 the ±4.0 clamp allows). The high voice clamps to ±4.0 increment **without affecting** the in-range voice (note 60 still rings at 1.0× speed). Verifies the per-voice fine-tune + clamp (`grain.c:871-901`).
6. **Budget honesty (documented behavior).** With default `max_grains = 4` and a 3-voice chord at mono overlap, confirm the live-grain count tracks the chosen budget strategy (GATE A.3): under strategy (1) the cap is shared (some voice thinning is expected and documented); under (3) the cap scales and all three voices keep density. The test asserts the *documented* behavior, not an absence of thinning. Verifies the budget strategy is implemented as decided, not silently wrong.

## Risks / out-of-scope

**Risks**

- **Grain-pool starvation (the central one).** ⟦V⟧ Default `max_grains = 4` (`grain.c:494`) means N voices fight over 4 live slots; the per-voice loop fills them in order, so later voices in the loop are silenced (`scheduler_allocate_grain` NULL, `grain.c:721`). A 3-note chord can render as 1–2 notes at default settings. Mitigation is the GATE A.3 budget strategy; the first cut must at minimum **document** this and recommend raising `max_grains` for POLY. CPU is linear in live-grain count (every grain processed every sample in `scheduler_process`), so a faithful N-voice chord is N× the mono CPU — the budget share IS the CPU governor.
- **Audio-thread shared-structure tearing.** The voice pool is **written** by `ligase_midi` (control thread, `ligase~.c:4731`) and **read** by the three perform-thread trigger loops. No locks (the Pd audio thread must not block). Mitigation by single-writer/single-reader discipline + careful ordering: **write the note slot BEFORE bumping `voice_count`** (reader never sees a count covering an unwritten slot); on note-off **clear/swap BEFORE decrementing** (or just **snapshot `voice_count` once at the top of each trigger block**, as the design does — a torn read at worst spawns one stale or one fewer grain for a single tick, never reads out of bounds since `MAX_VOICES` is fixed and slots are POD ints). `midi_msg_active` arbitration (`ligase~.c:305/499`) must keep gating the inlet-19 single-note writer so it doesn't fight the pool.
- **Per-grain pitch snapshot is sacred.** Each grain freezes `increment` at trigger (`grain.c:1036`) and never re-reads pitch. The per-voice override MUST be applied **at spawn** so the frozen increment already carries that voice's transposition — do NOT try to retune live grains. The fine-tune sample + ±4.0 clamp (`grain.c:871-901`) run **inside** each per-voice call so each voice is independently, correctly clamped.
- **`last_semitone` / outlet-3 per-voice noise.** ⟦V⟧ `last_semitone` (`grain.c:882`) ends each tick holding the last voice's value; if any outlet-3 path were keyed on it in POLY it could bang per voice. The MIDI detector keys on `midi_note` (`ligase~.c:1753`), so this is benign today — but GATE A.6 pins it so a future change doesn't regress it.
- **Zero-init footgun.** `voice_note`/`voice_age` zero-init to 0 (a valid note / age); guarded by `voice_count` (only `[0, voice_count)` is read) and `poly_enabled`. The explicit `scheduler_create` defaults + the count gate prevent a stray slot from sounding — same "garbage-enabled clobbers live value" class the codebase documents at `grain.c:477-487`.

**Out of scope**

- **Reading B (a voice = its own splice / material / playhead).** Per-voice splices, per-voice playheads, per-voice SOS/recording, per-voice scan — **all explicitly out.** The owner achieves Reading B by instantiating **parallel `ligase~` objects** in the patch, so `ligase~` stays single-playhead by construction. This plan multiplies only the pitch of grains read from the ONE shared playhead.
- **Polyphonic smear/resonator.** N parallel resonators is a separate, larger feature; the smear dest stays a single `freq_hz` (mono, last-note-wins) — GATE A.5.
- **Touching the playhead / splice / SOS / recording paths.** All three playhead blocks' position advance, `wrap_to_splice`, splice-end bang, pending-nav, one-shot stop, and SOS/recording logic stay exactly as-is. Only the single `scheduler_trigger_grain` call at each site multiplies.
- **MIDI-out of the chord, velocity-to-amplitude per voice, per-voice pan spread.** Not built here (the chord shares the one `amplitude`/`pan`); candidate follow-ups, not this plan.
- **Microtonal / fractional voice pitches.** Voices are integer MIDI notes transposed vs 60, same integer-semitone discipline as the existing MIDI path.
