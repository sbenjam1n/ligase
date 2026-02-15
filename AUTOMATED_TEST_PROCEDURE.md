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
Navigate to the ligase_pd_project directory:
```bash
cd ~/projects/ligase_pd_project
```

Ensure these files exist:
- `ligase~.pd_linux` - The compiled external
- `test_recording.pd` - Automated recording test patch

---

## Test Procedure

### Step 1: Clean Previous Test Results

```bash
# Remove any previous test files
rm -f /tmp/ligase_test.wav
rm -f /tmp/pd_auto.log
rm -f /tmp/pd_playback.log

# Verify cleanup
ls /tmp/ligase_test.wav 2>&1
# Expected: "No such file or directory"
```

### Step 2: Run Recording Test

Execute the automated recording test:

```bash
timeout 10s pd -nogui -nosound -stderr -path . test_auto.pd 2>&1 | tee /tmp/pd_auto.log
```

**Command Breakdown:**
- `timeout 10s` - Kills PD after 10 seconds (prevents hanging)
- `pd -nogui` - Run Pure Data without GUI
- `-nosound` - Disable audio output (not needed for test)
- `-stderr` - Print messages to stderr
- `-path .` - Add current directory to search path (for ligase~.pd_linux)
- `test_auto.pd` - The test patch
- `2>&1 | tee /tmp/pd_auto.log` - Capture all output to log file

**Expected Console Output:**
```
priority 6 scheduling failed; running at normal priority
priority 8 scheduling failed.
ligase~: input-only recording started (will create splice at sample 0 on stop)
ligase~: recording started (overdub mode)
ligase~: recording stopped
ligase~: saved /tmp/ligase_test.wav
```

**Key Success Indicators:**
- ✅ No error messages (except priority warnings, which are normal)
- ✅ "ligase~: saved /tmp/ligase_test.wav" appears
- ✅ Process exits cleanly (within 10 seconds)

### Step 3: Verify WAV File Creation

Check that the recording was saved:

```bash
ls -lh /tmp/ligase_test.wav
file /tmp/ligase_test.wav
```

**Expected Output:**
```
-rw-r--r-- 1 user user 1.1M Oct 11 01:45 /tmp/ligase_test.wav
/tmp/ligase_test.wav: RIFF (little-endian) data, WAVE audio, IEEE Float, stereo 48000 Hz
```

**Success Criteria:**
- ✅ File exists
- ✅ File size is between 1.0 MB and 1.5 MB
- ✅ File type is "WAVE audio"
- ✅ Format is "stereo 48000 Hz" or "stereo 44100 Hz"

### Step 4: Analyze Audio Content

Use sox to verify the recording contains actual audio (not silence):

```bash
sox /tmp/ligase_test.wav -n stat 2>&1 | grep -E "(Samples read|Length|RMS|Maximum amplitude)"
```

**Expected Output:**
```
Samples read:            264576
Length (seconds):      2.756000
Maximum amplitude:     0.998733
RMS     amplitude:     0.104593
```

**Success Criteria:**
- ✅ Samples read > 250,000 (indicates ~3 seconds of audio)
- ✅ Length is approximately 2.5-3.0 seconds
- ✅ **Maximum amplitude > 0.5** (strong signal)
- ✅ **RMS amplitude > 0.05** (NOT silence - this is the critical metric)

**FAIL Indicators:**
- ❌ RMS amplitude < 0.001 (indicates silence/bug still present)
- ❌ Maximum amplitude < 0.1 (very weak signal)
- ❌ File size < 100 KB (incomplete recording)

### Step 5: Playback Buffer Verification

This is the **most critical test** - it verifies the internal buffer contains audio:

```bash
timeout 5s pd -nogui -nosound -stderr -path . test_playback.pd 2>&1 | tee /tmp/pd_playback.log

# Extract the critical buffer check line
grep "buffer check:" /tmp/pd_playback.log
```

**Expected Output:**
```
  buffer check: avg amplitude L=0.481438 R=0.481438
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

1. **Initialization (500ms)**
   - Start Pure Data DSP
   - Initialize noise~ generator

2. **Recording Setup (100ms)**
   - Send `recinput` message to ligase~ (enables input-only recording mode)
   - This ensures clean recording without sound-on-sound mixing

3. **Recording (3000ms)**
   - Send `record 1` to start recording
   - noise~ generates audio → feeds to ligase~ inputs
   - ligase~ captures to internal buffer
   - Wait 3 seconds

4. **Save (500ms)**
   - Send `record 0` to stop recording
   - Send `save /tmp/ligase_test.wav` to write buffer to disk

5. **Cleanup (500ms)**
   - Send `pd quit` to exit cleanly

**Total test duration:** ~5 seconds

---

## Understanding test_auto.pd

The test patch contains:

```
[loadbang]           ← Triggers on patch load
    ↓
[delay 500]          ← Wait for DSP initialization
    ↓
[msg: recinput]      ← Set input-only recording mode
    ↓
[delay 100]
    ↓
[msg: record 1]      ← Start recording
    ↓
[delay 3000]         ← Record for 3 seconds
    ↓
[msg: record 0]      ← Stop recording
    ↓
[delay 500]
    ↓
[msg: save /tmp/ligase_test.wav]  ← Save to disk
    ↓
[delay 500]
    ↓
[msg: pd quit]       ← Exit Pure Data

Audio Path:
[noise~] ──→ [ligase~ 500] ──→ [dac~ 1 2]
               ↑
               └─ [receive e-ctl] ← Control messages
```

**Key Design Elements:**
- Uses `[receive e-ctl]` for message routing (avoids PD's direct connection issues)
- Delays ensure messages arrive in correct order
- `loadbang` makes test fully automatic
- Both noise~ channels feed both ligase~ inputs (stereo test)
- DAC connection verifies no interference between objects

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
cd ~/projects/ligase_pd_project
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

For convenience, use this one-liner to run all tests:

```bash
#!/bin/bash
cd ~/projects/ligase_pd_project && \
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
[... PD messages ...]
ligase~: saved /tmp/ligase_test.wav

=== File Check ===
-rw-r--r-- 1 user user 1.1M Oct 11 01:45 /tmp/ligase_test.wav

=== Audio Analysis ===
Maximum amplitude:     0.998733
RMS     amplitude:     0.104593

=== Buffer Verification ===
  buffer check: avg amplitude L=0.481438 R=0.481438

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
