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
- **Spectral processing** — FFT-based grain fog effect

## Signal Flow

```
grains → delay → [RECORDING] → output → fog → Moog → distortion → dac~​ 
```

## Requirements

- [Pure Data](https://puredata.info/) (Pd)
- C compiler (GCC, Clang, or MSVC)
- Sample rate: 48kHz

## Building

Compile as a Pure Data external for your platform:

**Linux:**
```bash
gcc -fPIC -shared -o ligase~.pd_linux *.c -I. -lm
```

**macOS:**
```bash
cc -fPIC -bundle -undefined dynamic_lookup -o ligase~.pd_darwin *.c -I. -lm
```

**Windows (MinGW):**
```bash
gcc -shared -o ligase~.dll *.c -I. -lm -L<pd-path>/bin -lpd
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

See [ligase_manual.txt](ligase_manual.pdf) for full documentation covering all inlets, outlets, messages, parameters, and default values.

## License

Copyright (C) 2025 Steven Benjamin

Licensed under the [GNU General Public License](https://www.gnu.org/licenses/gpl-3.0.html).
