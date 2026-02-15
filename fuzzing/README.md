# ligase~ Fuzzing Infrastructure

Comprehensive fuzzing harness for testing ligase~ Pure Data external with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).

## Overview

This fuzzing infrastructure tests ligase~ for:
- **Memory safety issues**: buffer overflows, use-after-free, heap corruption
- **Undefined behavior**: integer overflow, NaN/Inf handling, uninitialized memory
- **State machine bugs**: splice management, recording modes, playhead transitions
- **Edge cases**: boundary conditions, extreme parameter values

## Architecture

```
fuzzing/
├── src/
│   ├── ligase_fuzzer.c       # Main fuzzer external
│   ├── corpus_manager.h      # Corpus data structures
│   ├── corpus_seeds.c        # Seed corpus from manual
│   └── corpus_selection.c    # AFL-style energy scheduling
├── patches/
│   └── fuzzer_test.pd        # Pure Data test patch
├── corpus/                   # Saved interesting inputs
├── crashes/                  # Crash logs and reproducers
├── Makefile                  # Build with sanitizers
├── run_fuzzer.sh             # Automated test runner
└── README.md                 # This file
```

## Building

### Prerequisites

```bash
sudo apt-get install puredata puredata-dev gcc make
```

### Compile Fuzzer

```bash
cd fuzzing
make
```

This builds:
- `ligase_fuzzer~.pd_linux` - Fuzzer external with sanitizers
- `ligase~_instrumented.pd_linux` - Instrumented ligase~ build

## Usage

### Quick Start

```bash
./run_fuzzer.sh
```

This runs 10,000 fuzz iterations with all input types enabled.

### Command-Line Options

```bash
./run_fuzzer.sh --iterations 100000    # Run 100k iterations
./run_fuzzer.sh --gui                  # Run with GUI visible
./run_fuzzer.sh --batch                # Non-interactive batch mode
```

### Manual Testing

1. Start Pure Data:
```bash
pd patches/fuzzer_test.pd
```

2. Configure fuzzer (in Pure Data patch):
```
[run 10000(         # Run 10000 iterations
[seed 12345(        # Set RNG seed for reproducibility
[enable_signal 1(   # Enable signal fuzzing
[enable_midi 1(     # Enable MIDI fuzzing
[enable_messages 1( # Enable message fuzzing
```

## Fuzzing Strategies

### 1. Signal Input Fuzzing

Generates adversarial audio signals:
- **Normal range**: Random values [-1, 1]
- **Extreme values**: ±1e38 (near float limits)
- **Denormals**: 1e-45 (smallest positive float)
- **NaN**: Not-a-Number edge cases
- **Infinity**: Positive/negative infinity
- **DC offset**: Large constant values (±100)
- **Impulse train**: Periodic spikes
- **Bit patterns**: Random bit patterns reinterpreted as floats

### 2. MIDI Input Fuzzing

Targets inlet 19 (MIDI pitch: 1-127, 0=inactive):
- Valid MIDI range (1-127)
- Boundary values (0, 127)
- Out-of-range values (128+, negative)
- Float edge cases (NaN, Infinity)
- Rapid note changes

### 3. Message Corpus Mutation

Based on comprehensive seed corpus derived from ligase~ manual:
- **200+ seed messages** covering all subsystems
- **AFL-style mutations**: bit flip, byte flip, arithmetic, splice, insert/delete
- **Dictionary-based**: Insert known message fragments
- **Interesting values**: Integer boundaries, float edge cases

### 4. State Transition Fuzzing

Rapid state changes to expose race conditions:
- play/record start/stop
- playhead mode switching
- splice creation/deletion
- Recording mode transitions

## Subsystems Tested

Fuzzing targets all ligase~ subsystems with weighted priority:

| Subsystem | Priority | Focus Areas |
|-----------|----------|-------------|
| Grain Core | 10 | IOT boundaries, maxgrains pool limits, grain size extremes |
| Splice Management | 10 | MAX_SPLICES (64) boundary, splice state machine |
| Recording | 10 | SOS mode transitions, overdub/recsplice/recinput |
| Modulation | 10 | N-body physics, Lorenz attractors, extreme noise frequencies |
| Distortion | 9 | Oversampling, waveshaper modes, filter coefficients |
| Moog Filter | 9 | Self-oscillation (resonance=4), Nyquist boundary |
| Pitch/Speed | 9 | Extreme semitones (±24), speed boundaries (±4) |
| Playhead | 9 | Mode transitions, scanrate extremes (±1000) |
| Delay | 8 | Feedback=1 (infinite), time boundary (9.5s) |
| Quantization | 8 | Unusual time signatures, extreme subdivisions (128) |
| Envelope | 7 | Skew boundaries, type switching |
| File I/O | 7 | Invalid filenames, missing files |
| State Query | 6 | Parameter query edge cases |

## Interpreting Results

### Success

```
ligase_fuzzer: completed 10000 iterations
No sanitizer errors detected
Fuzzer completed successfully
```

### Memory Error Example

```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
READ of size 4 at 0x... thread T0
    #0 0x... in grain_scheduler_trigger src/grain_scheduler.c:234
    #1 0x... in ligase_perform src/ligase.c:1456
```

### Undefined Behavior Example

```
src/modulation.c:789:12: runtime error: signed integer overflow:
2147483647 + 1 cannot be represented in type 'int'
```

## Crash Reproduction

When a crash is detected:

1. Check crash logs:
```bash
ls -lt crashes/
cat crashes/asan_YYYYMMDD_HHMMSS.log
```

2. Reproduce with same seed:
```
[seed 12345(
[run 10000(
```

3. Save crash input:
```bash
# Crashes are automatically saved to crashes/ directory
# Format: crash_<seed>_<iteration>.bin
```

## Advanced Usage

### Coverage-Guided Fuzzing

To enable SanitizerCoverage (for future libFuzzer integration):

1. Edit `Makefile`, uncomment:
```makefile
COVERAGE_FLAGS = -fsanitize-coverage=trace-pc-guard
```

2. Rebuild:
```bash
make clean && make
```

### Custom Corpus

Add your own seed messages to `src/corpus_seeds.c`:

```c
{"custom_message arg1 arg2", SUBSYS_CUSTOM, 10},
```

### Targeted Subsystem Testing

Enable only specific fuzzing types:

```
[enable_signal 0(     # Disable signal fuzzing
[enable_midi 0(       # Disable MIDI fuzzing
[enable_messages 1(   # Only message fuzzing
```

## Environment Variables

### AddressSanitizer (ASan)

```bash
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:symbolize=1"
```

Options:
- `detect_leaks=1` - Detect memory leaks
- `abort_on_error=1` - Stop on first error
- `symbolize=1` - Show function names in stack traces
- `detect_stack_use_after_return=1` - Detect stack UAF
- `check_initialization_order=1` - Detect initialization order bugs

### UndefinedBehaviorSanitizer (UBSan)

```bash
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
```

Options:
- `print_stacktrace=1` - Show stack traces for UB
- `halt_on_error=1` - Stop on first UB detection

## Known Issues

### False Positives

Some warnings may be expected:
- **Denormal handling**: ligase~ may handle denormals explicitly
- **Integer wrapping**: Intentional modulo arithmetic

### Performance

Sanitizers add ~2-5x overhead:
- Expect slower execution
- Use lower iteration counts for interactive testing
- Use batch mode for long runs

## Continuous Integration

### Automated Testing

```bash
# Run in CI pipeline
./run_fuzzer.sh --batch --iterations 100000
EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    echo "Fuzzing found issues!"
    exit 1
fi
```

### Nightly Fuzzing

```bash
# crontab entry for nightly fuzzing
0 2 * * * cd /path/to/fuzzing && ./run_fuzzer.sh --batch -i 1000000 > /var/log/ligase_fuzz.log 2>&1
```

## Contributing

To add new fuzzing strategies:

1. Edit `src/ligase_fuzzer.c`
2. Add new mutation types to `mutate_message()`
3. Update seed corpus in `src/corpus_seeds.c`
4. Document in this README

## References

- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- [AFL Fuzzer](https://lcamtuf.coredump.cx/afl/)
- [Pure Data Documentation](http://puredata.info/docs)
- ligase~ Manual: `../ligase_manual.docx.txt`

## License

Same as ligase~: GNU General Public License

## Contact

Report fuzzing bugs to the ligase~ maintainer.
