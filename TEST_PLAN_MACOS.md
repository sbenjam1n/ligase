# ligase~ Test Plan — macOS + Audio Interface (sample-rate / buffering / reel I/O)

Companion to `AUTOMATED_TEST_PROCEDURE.md` (Linux, headless). This plan covers macOS,
verifies the **B1** sample-rate/buffering fixes and the **B2** reel-I/O fixes, and includes
**hardware-in-the-loop** testing with the attached **Focusrite** interface.

- **Pd binary (this Mac):** `/Applications/Pd-0.51.1.app/Contents/Resources/bin/pd`
- **Build:** `make clean && make` → `ligase~.pd_darwin` (the committed `src/*.o` are stale/other-platform; always `make clean` first).
- **Analysis tool:** `sox` (via `brew install sox`).
- **macOS note:** there is no `timeout(1)` by default. Use `brew install coreutils` → `gtimeout`, OR rely on the self-terminating patches (loadbang → `… → \; pd quit`), which is how the test patches below are wired.

---

## ⚠️ Prerequisite — the Focusrite must be enumerated by CoreAudio

As of 2026-06-16, `system_profiler SPAudioDataType` lists only: MacBook Pro Mic/Speakers,
Soundflower (2ch/64ch), Multi-Output — **no Focusrite/Scarlett device is currently visible.**
Hardware tests (Tier 2) cannot run until it appears. Before Tier 2:

```bash
system_profiler SPAudioDataType | grep -iE "focusrite|scarlett|clarett"
```
Must print the device. If empty: power-cycle / reconnect the interface, confirm it in
**Audio MIDI Setup**, and note its **supported sample rates** (Scarlett: typically 44100,
48000, 88200, 96000, 176400, 192000) and its CoreAudio device name (needed for `pd -audiooutdev`).

---

## Tier 1 — Headless, sample-rate sweep (no interface needed)

Proves the engine is rate-correct at every rate using Pd's `-r` flag and `-nosound` (DSP runs
internally via `\; pd dsp 1`). Run each at **44100, 48000, 96000**.

### T1.1 Build
```bash
make clean && make       # clean compile, produces ligase~.pd_darwin
```

### T1.2 Record/buffer integrity at each rate  (extends test_auto.pd)
For `SR in 44100 48000 96000`:
```bash
PD=/Applications/Pd-0.51.1.app/Contents/Resources/bin/pd
rm -f /tmp/ligase_test.wav
$PD -nogui -nosound -r $SR -stderr -path . test_auto.pd 2>&1 | tee /tmp/pd_$SR.log
sox /tmp/ligase_test.wav -n stat 2>&1 | grep -E "RMS|Maximum amplitude|Length"
```
**PASS:** non-silent recording (RMS > 0.05) at **every** rate; `Length` ≈ the recorded
seconds **independent of SR** (this is the core "consistent buffering" check — duration must
not scale with sample rate).

### T1.3 Delay actually delays — full range, every rate  (NEW patch `test_delay.pd`)
The B1 headline regression. New self-terminating patch: feed a single click into `ligase~`,
set `delay_mode 0` (DD-4), `gdelay_mix 1`, `gdelay_feed 0`, and a **long** delay
`gdelay_time 8.0`; record output; measure the click echo offset with sox.
For `SR in 44100 48000 96000`:
```bash
$PD -nogui -nosound -r $SR -stderr -path . test_delay.pd 2>&1 | tee /tmp/pd_delay_$SR.log
# measure first echo position
sox /tmp/ligase_delay.wav -n stat 2>&1 | grep -E "Length|Maximum amplitude"
```
**PASS:** a delayed copy appears at **≈8.0 s at every rate** (pre-fix: at 96 kHz the 8 s tap
exceeds the 48k-sized 4.75 s buffer and is silently clamped → echo missing/wrong = the bug).
**FAIL:** echo absent, or its delay time scales with SR.

### T1.4 No clipping/instability with distortion engaged, every rate  (NEW `test_dist.pd`)
Drive a sustained tone through `ligase~` with `distortion_enable 1`, `distortion 0.7`,
`moog_resonance 3.0`. Record; check for blow-ups.
**PASS:** `Maximum amplitude ≤ 1.0` and no NaN/Inf at every rate (pre-fix: 48k-tuned IIR
coeffs at 96k detune/destabilize → soft-clip blow-ups = "clips and drops out").

### T1.5 Live sample-rate change  (NEW `test_sr_switch.pd`)
Start DSP at 48k, then mid-run send `\; pd dsp 0` and relaunch logic… (simplest: two
sequential `pd` runs at different `-r` from the same patch state is N/A headless; instead
assert via T1.3/T1.4 across rates that re-`dsp` at a new rate re-inits cleanly). Manual
live-switch is covered in Tier 2.

---

## Tier 2 — Hardware-in-the-loop with the Focusrite (the real bug report)

Requires the prerequisite above. Run the **GUI** Pd against the Focusrite at each of its
native rates. This reproduces the user's exact setup.

### T2.1 Open at the interface's rate
In **Audio MIDI Setup**, set the Focusrite to **44100**, then **96000** (repeat the suite at each).
Launch Pd selecting the Focusrite for I/O (GUI: Media ▸ Audio Settings, or
`pd -audiodev <n> -r <SR>`). Open `ligase~-osc.pd` (or a minimal test patch).

### T2.2 Audio sanity
Play audio through `ligase~` (granulate a loaded reel or live input). **PASS:** clean audio,
**no clicks/dropouts** at 44.1 and 96 kHz. (This is the primary symptom — must be clean.)

### T2.3 Delay on hardware
Engage DD-4 delay with a long `gdelay_time` (e.g. 6–8 s). **PASS:** audible delay at the set
time at both rates. **FAIL (pre-fix):** no delay / wrong time at 96 kHz.

### T2.4 Live interface swap
With Pd running, switch the output device between **built-in (44.1k)** and **Focusrite (96k)**
in Audio Settings. **PASS:** ligase~ re-initializes (Pd re-calls the dsp method) and audio +
delay stay correct after the switch; no sustained glitch.

### T2.5 Plug/unplug consistency
Confirm ligase~ buffers and plays correctly **with and without** the interface attached
(fall back to built-in 44.1k). **PASS:** identical musical behavior either way.

---

## Tier 3 — Reel load/save on macOS (B2; run from a Finder-launched Pd)

Deferred to B2 but recorded here so the suite is complete.
- **T3.1** Launch Pd.app from **Finder** (CWD `/`). Open a patch saved in `~/Desktop/ligasetest/`.
  Send `save myreel.wav` (relative). **PASS:** file appears in `~/Desktop/ligasetest/` (next to
  the patch), not `/`. **FAIL (pre-fix):** silent failure / nothing written.
- **T3.2** `load myreel.wav` (relative) round-trips the audio. **PASS:** reel reloads, non-silent.
- **T3.3** Bad path → a clear error in the **Pd window** (not just stderr).

---

## Pass/Fail summary matrix

| Check | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| T1.2 record non-silent + duration SR-independent | ☐ | ☐ | ☐ |
| T1.3 long delay tap at correct time | ☐ | ☐ | ☐ |
| T1.4 no distortion blow-up | ☐ | ☐ | ☐ |
| T2.2 hardware audio clean (no dropouts) | ☐ | — | ☐ |
| T2.3 hardware delay correct | ☐ | — | ☐ |
| T2.4 live interface swap re-inits | ☐ (swap) | | ☐ (swap) |

New patches to author for Tier 1 automation: `test_delay.pd`, `test_dist.pd` (model on the
existing `test_auto.pd`/`test_playback.pd` loadbang→delay→`pd quit` pattern).
