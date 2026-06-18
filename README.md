# ligase~

A granular synthesizer/sampler/looper/delay external for [Pure Data](https://puredata.info/) with real-time recording, splice-based sample organization, multi-mode filtering, distortion, and chaotic parameter modulation.

## Features

- **Asynchronous granular synthesis** with configurable grain pool (1-2000 grains)
- **Splice-based organization** — divide a 10-minute stereo buffer into up to 64 segments for non-linear playback
- **Real-time recording** with sound-on-sound mixing and three record modes
- **Three playhead modes** — static, scanning, and clock-advance
- **Multiple delay types** — DD-4 analog delay, pitch-preserving grain delay, and rhythmic stutter delay
- **Multi-mode distortion** — 5 waveshaper modes (tanh, arctan, asymmetric, blend, polynomial) with pre/post emphasis filters
- **Moog ladder filter** — classic 4-pole 24dB/octave lowpass with resonance
- **Chaotic parameter modulation** — random, Perlin noise (1D/2D), Lorenz attractor, and N-body gravitational simulation
- **Pitch modes** — semitone, range, scale, and MIDI pitch control
- **Timing quantization** — independent grids for IOT, grain size, and delay time with BPM and note division support
- **Allpass smear** — cascade of tunable allpass sections (spectral smear / dispersion); with feedback, a pitched bell-like resonator

## Signal Flow

```
grains → delay → [RECORDING] → smear → distortion → Moog → dac~
```

## Requirements

- [Pure Data](https://puredata.info/) (Pd)
- C compiler (GCC, Clang, or MSVC)
- Sample rate: 48kHz

## Building

The simplest way is the included `Makefile`, which detects your OS and links the
correct sources from `src/`:

```bash
make            # builds ligase~.pd_linux (Linux) or ligase~.pd_darwin (macOS)
make install    # copies the external (+ help patch) to ~/Documents/Pd/externals
```

On Linux the Makefile expects Pd headers at `/usr/local/include/pd`; on macOS at
`/Applications/Pd-0.53-2.app/Contents/Resources/src`. Adjust `PD_INCLUDE` in the
`Makefile` if yours differ.

To build by hand instead, compile every source in `src/`. The Pd API header
`m_pd.h` is vendored there, so `-Isrc` is all the include path you need:

**Linux:**
```bash
gcc -fPIC -shared -o ligase~.pd_linux src/*.c -Isrc -lm
```

**macOS:**
```bash
cc -bundle -undefined dynamic_lookup -o ligase~.pd_darwin src/*.c -Isrc -lm
```

**Windows (MinGW):**
```bash
gcc -shared -o ligase~.dll src/*.c -Isrc -L<pd-path>/bin -lpd
```

## Installation

Copy the compiled external (`ligase~.pd_linux`, `ligase~.pd_darwin`, or `ligase~.dll`) to your Pure Data externals directory, then create a `[ligase~]` object in your patch.

## Configuration

Optionally create a `ligase.conf` file in the same directory as the external:

```
max_grains = 200
```

This sets the maximum grain pool size (range 1-2000, default 200).

## Documentation

See [ligase_manual.pdf](ligase_manual.pdf) for full documentation covering all inlets, outlets, messages, parameters, and default values.

## License

 GNU General Public License v2 
 Copyright (C) 2025 Steven Benjamin


