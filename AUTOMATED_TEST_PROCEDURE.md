# Automated Test Procedure for Ligase~ Audio Routing

## Purpose

This document provides a complete, step-by-step procedure to verify that the ligase~ external correctly records audio input (specifically from noise~) when both objects are connected to dac~, without DSP execution order conflicts.

**Original Issue:** When noise~ and ligase~ were both connected to dac~, the noise would be silenced and recordings would show zero amplitude (L=0.000000 R=0.000000).

**Expected After Fix:** Recordings show non-zero amplitude (e.g., L=0.481438 R=0.481438), indicating successful audio capture.

---

## Prerequisites

### System Requirements
- Linux environment (Debian/Ubuntu recommended)
- sudo access (for package installation)
- At least 100 MB free disk space in `/tmp`

### Software Requirements
```bash
# Install Pure Data
sudo apt-get update
sudo apt-get install -y puredata

# Install sox (for audio analysis)
sudo apt-get install -y sox

# Verify installations
pd -version          # Should show: Pd-0.53.1 or similar
sox --version        # Should show: SoX v14.4.2 or similar
```

### Project Files Required
Navigate to the ligase project directory:
```bash
cd ~/projects/ligase
```

Ensure these files exist:
- `ligase~.pd_linux` - The compiled external
- `test_auto.pd` - Automated recording test patch
- `test_playback.pd` - Automated playback verification patch
- `ligase.conf` - Configuration file (max_grains)

---

## Test Procedure

### Step 1: Build the External

```bash
make
```

**Expected Output:** Clean compilation with zero warnings, producing `ligase~.pd_linux`.

**Success Criteria:**
- ✅ `make` exits with status 0
- ✅ No compiler warnings or errors
- ✅ `ligase~.pd_linux` is present and recently modified

### Step 2: Clean Previous Test Results

```bash
# Remove any previous test files
rm -f /tmp/ligase_test.wav
rm -f /tmp/pd_auto.log
rm -f /tmp/pd_playback.log

# Verify cleanup
ls /tmp/ligase_test.wav 2>&1
# Expected: "No such file or directory"
```

### Step 3: Run Recording Test

Execute the automated recording test:

```bash
timeout 10s pd -nogui -nosound -stderr -path . test_auto.pd 2>&1 | tee /tmp/pd_auto.log
```

**Command Breakdown:**
- `timeout 10s` - Kills Pd after 10 seconds (prevents hanging)
- `pd -nogui` - Run Pure Data without GUI
- `-nosound` - Disable audio I/O (the patch activates DSP internally via `\; pd dsp 1`)
- `-stderr` - Print messages to stderr
- `-path .` - Add current directory to search path (for ligase~.pd_linux)
- `test_auto.pd` - The test patch
- `2>&1 | tee /tmp/pd_auto.log` - Capture all output to log file

**Expected Console Output:**
```
priority 6 scheduling failed; running at normal priority
ligase~: Loaded max_grains = 200 from ligase.conf
ligase_dsp: CALLED (x=..., sp=...)
ligase_dsp: Adding perform callback (sr=44100, blocksize=64)
ligase_dsp: COMPLETE
priority 8 scheduling failed.
ligase~: input-only recording started (will create splice at sample 0 on stop)
ligase~: recording started (overdub mode)
ligase~: recording stopped
ligase~: saved /tmp/ligase_test.wav
```

**Key Success Indicators:**
- ✅ `ligase_dsp: CALLED` appears (confirms DSP is running)
- ✅ `ligase~: saved /tmp/ligase_test.wav` appears
- ✅ No error messages (except priority warnings, which are normal)
- ✅ Process exits cleanly within 10 seconds

### Step 4: Verify WAV File Creation

Check that the recording was saved:

```bash
ls -lh /tmp/ligase_test.wav
```

**Expected Output:**
```
-rw-r--r-- 1 user user 1.1M ... /tmp/ligase_test.wav
```

**Success Criteria:**
- ✅ File exists
- ✅ File size is between 1.0 MB and 1.5 MB

### Step 5: Analyze Audio Content

Use sox to verify the recording contains actual audio (not silence):

```bash
sox /tmp/ligase_test.wav -n stat 2>&1 | grep -E "(Samples read|Length|RMS|Maximum amplitude)"
```

**Expected Output:**
```
Samples read:            264576
Length (seconds):      2.756000
Maximum amplitude:     0.981718
RMS     amplitude:     0.293041
```

**Success Criteria:**
- ✅ Samples read > 250,000 (indicates ~3 seconds of audio)
- ✅ Length is approximately 2.5–3.0 seconds
- ✅ **Maximum amplitude > 0.5** (strong signal)
- ✅ **RMS amplitude > 0.05** (NOT silence - this is the critical metric)

**FAIL Indicators:**
- ❌ RMS amplitude < 0.001 (indicates silence/bug still present)
- ❌ Maximum amplitude < 0.1 (very weak signal)
- ❌ File size < 100 KB (incomplete recording)

### Step 6: Playback Buffer Verification

This is the **most critical test** - it verifies the internal buffer contains audio:

```bash
timeout 5s pd -nogui -nosound -stderr -path . test_playback.pd 2>&1 | tee /tmp/pd_playback.log

# Extract the critical buffer check line
grep "buffer check:" /tmp/pd_playback.log
```

**Expected Output:**
```
  buffer check: avg amplitude L=0.330989 R=0.330989
```

**Success Criteria:**
- ✅ **L value > 0.01** (non-zero left channel)
- ✅ **R value > 0.01** (non-zero right channel)
- ✅ Both channels have similar amplitude (within 10% of each other)

**FAIL Indicators (Bug Still Present):**
- ❌ `buffer check: avg amplitude L=0.000000 R=0.000000`
- ❌ Either L or R is exactly 0.000000
- ❌ No "buffer check:" line in output

---

## Test Workflow Summary

The test automatically performs these operations:

1. **DSP Activation (immediate)**
   - `\; pd dsp 1` sent from loadbang — required in `-nosound` mode; Pd does not start DSP automatically without an audio device

2. **Initialization (500ms)**
   - Initialize noise~ generator and ligase~

3. **Recording Setup (100ms)**
   - Send `recinput` message to ligase~ (enables input-only recording mode)
   - This ensures clean recording without sound-on-sound mixing

4. **Recording (3000ms)**
   - Send `record 1` to start recording
   - noise~ generates audio → feeds to ligase~ inputs
   - ligase~ captures to internal buffer via DSP callback
   - Wait 3 seconds

5. **Save (500ms)**
   - Send `record 0` to stop recording
   - Send `save /tmp/ligase_test.wav` to write buffer to disk

6. **Cleanup (500ms)**
   - Send `pd quit` to exit cleanly

**Total test duration:** ~5 seconds

---

## Understanding test_auto.pd

The test patch contains:

```
[loadbang] ──→ [\; pd dsp 1]    ← Enable DSP immediately (required with -nosound)
    ↓
[delay 500]
    ↓
[msg: recinput] ──→ [ligase~]   ← Set input-only recording mode
    ↓
[delay 100]
    ↓
[msg: record 1] ──→ [ligase~]   ← Start recording
    ↓
[delay 3000]                    ← Record for 3 seconds
    ↓
[msg: record 0] ──→ [ligase~]   ← Stop recording
    ↓
[delay 500]
    ↓
[msg: save /tmp/ligase_test.wav] ──→ [ligase~]   ← Save to disk
    ↓
[delay 500]
    ↓
[\; pd quit]                    ← Exit Pure Data

Audio Path:
[noise~] ──→ [ligase~] ──→ [dac~ 1 2]
  └──────────────┘ (both inlets, stereo)
```

**Key Design Elements:**
- `[\; pd dsp 1]` wired directly from loadbang (no delay) — DSP must be active before recording begins
- Delays ensure messages arrive in correct order
- `loadbang` makes test fully automatic
- Both noise~ outputs feed both ligase~ inputs (stereo test)
- DAC connection ensures signal flow mirrors real-world usage

---

## Interpreting Results

### ✅ PASS - All Tests Successful

If all steps show expected results:
1. File created with correct size (1+ MB)
2. RMS amplitude > 0.05
3. Buffer check shows L > 0.01 and R > 0.01

**Conclusion:** The audio routing fix is working correctly. The ligase~ external properly records input audio even when connected to dac~ alongside the audio source.

### ❌ FAIL - Recording Silent

If buffer check shows `L=0.000000 R=0.000000`:

**Diagnosis:** The DSP execution order conflict bug is still present.

**Likely Causes:**
1. ligase~.pd_linux was not recompiled after applying the fix
2. Wrong version of ligase~.pd_linux is being loaded
3. The fix was not correctly applied to src/ligase~.c

**Troubleshooting Steps:**
```bash
# 1. Verify the fix is in the source code
grep -A3 "Initialize output buffers to silence" src/ligase~.c

# Expected output:
#   // Initialize output buffers to silence
#   // NOTE: Input monitoring should be done via direct patch connections
#   for (int i = 0; i < n; i++) {
#       out_left[i] = 0.0f;

# 2. Rebuild the external
make clean
make

# 3. Verify new binary timestamp
ls -l ligase~.pd_linux
# Should show recent modification time

# 4. Re-run tests
rm -f /tmp/ligase_test.wav
timeout 10s pd -nogui -nosound -stderr -path . test_auto.pd 2>&1 | tee /tmp/pd_auto.log
```

### ⚠️ PARTIAL - File Created But Low Amplitude

If RMS amplitude is between 0.001 and 0.05:

**Diagnosis:** Audio is being recorded but at very low level.

**Possible Causes:**
1. noise~ generator not producing full amplitude
2. Recording gain too low
3. Partial fix applied

**Verification:**
```bash
# Check if amplitude scales correctly over time
sox /tmp/ligase_test.wav -n stat 2>&1 | grep -E "(Maximum|RMS|Mean)"
```

---

## Common Issues and Solutions

### Issue: "pd: command not found"

**Solution:**
```bash
sudo apt-get update
sudo apt-get install -y puredata
```

### Issue: "ligase~: no such object"

**Cause:** Pure Data cannot find ligase~.pd_linux

**Solution:**
```bash
# Ensure you're in the correct directory
cd ~/projects/ligase
ls -l ligase~.pd_linux  # Verify file exists

# Add -path . to pd command
pd -nogui -nosound -path . test_auto.pd
```

### Issue: Test hangs and times out

**Cause:** PD waiting for input or infinite loop

**Solution:**
- The `timeout 10s` command prevents this
- If it still hangs, kill manually: `killall -9 pd`
- Check `/tmp/pd_auto.log` for error messages

### Issue: "priority 6 scheduling failed"

**Status:** This is NORMAL and can be ignored

**Explanation:** PD tries to get real-time priority but fails without sudo. The test runs fine at normal priority.

### Issue: File created but sox says "not a valid WAV"

**Cause:** Recording was interrupted or corrupted

**Solution:**
```bash
# Check file size
ls -lh /tmp/ligase_test.wav

# If < 100 KB, recording failed - re-run test
rm -f /tmp/ligase_test.wav
timeout 10s pd -nogui -nosound -stderr -path . test_auto.pd 2>&1
```

---

## Quick Test Script

For convenience, use this script to run all tests from the project root:

```bash
#!/bin/bash
cd ~/projects/ligase && \
make && \
rm -f /tmp/ligase_test.wav && \
echo "=== Running Recording Test ===" && \
timeout 10s pd -nogui -nosound -stderr -path . test_auto.pd 2>&1 | tee /tmp/pd_auto.log && \
echo -e "\n=== File Check ===" && \
ls -lh /tmp/ligase_test.wav && \
echo -e "\n=== Audio Analysis ===" && \
sox /tmp/ligase_test.wav -n stat 2>&1 | grep -E "(RMS|Maximum amplitude)" && \
echo -e "\n=== Buffer Verification ===" && \
timeout 5s pd -nogui -nosound -stderr -path . test_playback.pd 2>&1 | grep "buffer check:" && \
echo -e "\n=== TEST COMPLETE ==="
```

**Expected Final Output:**
```
=== Running Recording Test ===
[... Pd messages including "ligase_dsp: CALLED" ...]
ligase~: saved /tmp/ligase_test.wav

=== File Check ===
-rw-r--r-- 1 user user 1.1M ... /tmp/ligase_test.wav

=== Audio Analysis ===
Maximum amplitude:     0.981718
RMS     amplitude:     0.293041

=== Buffer Verification ===
  buffer check: avg amplitude L=0.330989 R=0.330989

=== TEST COMPLETE ===
```

---

## Test Maintenance

### When to Run These Tests

1. **After applying the audio routing fix** - Verify it worked
2. **After modifying src/ligase~.c** - Ensure no regression
3. **Before releasing a new version** - QA verification
4. **After OS/PD updates** - Verify compatibility

### Updating Test Expectations

If the ligase~ implementation changes (e.g., different sample rates, buffer sizes), update these thresholds in this document:

- **File size:** Currently expecting 1.0-1.5 MB for 3 seconds
- **RMS amplitude:** Currently expecting > 0.05 for noise~
- **Buffer amplitude:** Currently expecting > 0.01 for both channels
- **Sample count:** Currently expecting ~250,000-300,000 samples

### Test File Locations

All test artifacts are stored in `/tmp` and can be safely deleted:
```bash
rm -f /tmp/ligase_test.wav /tmp/pd_auto.log /tmp/pd_playback.log
```

---

## Success Metrics Summary

| Metric | Expected Value | Critical? |
|--------|----------------|-----------|
| WAV file size | 1.0 - 1.5 MB | ✅ Yes |
| RMS amplitude | > 0.05 | ✅ Yes |
| Buffer check L | > 0.01 | ✅ **CRITICAL** |
| Buffer check R | > 0.01 | ✅ **CRITICAL** |
| Maximum amplitude | > 0.5 | ⚠️ Recommended |
| Recording duration | 2.5 - 3.5 seconds | ⚠️ Recommended |
| No error messages | (except priority warnings) | ✅ Yes |

**The buffer check values are the most critical metrics** - they directly verify that the internal recording buffer contains non-zero audio, which was the core issue.

---

## Version History

- **2025-10-11:** Initial automated test procedure created
  - Verified working on Debian 12, Pure Data 0.53.1
  - Test successfully detects the fix: buffer amplitude changed from 0.000000 to 0.481438

- **2026-02-24:** Updated for ligase-improvements branch
  - Added Step 1 (`make`) to cover the build
  - Fixed `test_auto.pd` and `test_playback.pd`: added `[\; pd dsp 1]` wired from loadbang — required because Pd with `-nosound` does not start DSP automatically; without it `ligase_dsp: CALLED` never appears and all recordings are empty

- **2026-06-18:** Removed the fog unit-test step (the FFT fog effect was replaced by the allpass smear; `make test_fog` no longer exists). Renumbered steps.
  - Updated expected output values to reflect current implementation (RMS 0.293, max 0.982, buffer check L/R 0.331)
  - Corrected project directory from `~/projects/ligase` to `~/projects/ligase`
  - Updated patch diagram in "Understanding test_auto.pd" to reflect actual wiring (removed obsolete `[receive e-ctl]` reference)
