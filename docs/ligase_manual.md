---
title: "ligase~ — Reference Manual"
subtitle: "Granular synthesizer / sampler / looper / delay for Pure Data"
author: "Steven Benjamin"
date: "© 2025 · GNU General Public License v2"
---

# OVERVIEW

ligase~ is a granular synthesizer/sampler/looper/delay with real-time recording, reel/splice management, allpass smear, filter, distortion and chaotic parameter modulation inspired by features of the Morphagene, Audiomulch’s DLGranulator and Tidalcycles.

ligase~ implements asynchronous granular synthesis with splice-based sample organization. The engine operates on a fixed-size stereo buffer (10 minutes at the host sample rate) divided into segments called splices.

Components

Reel: Circular stereo buffer (MAX_REEL_SECONDS * 48000 samples)

Scheduler: Grain pool manager (configurable 1-2000 grains, default 200)

Recorder: Real-time input capture with sound-on-sound mixing

Envelope: Table-based amplitude shaping (parabolic, trapezoidal, cosine)

Grain pool allocation is configurable via ligase.conf file placed in the same directory as the external.

Format: max_grains = N (range 1-2000).

Grain Generation

1. Trigger timing controlled by IOT (interonset time): 0.0005-2.0 seconds

2. Grain size: 0.001-2.0 seconds (default 0.1)

3. Playhead position determines grain start point within current splice

4. Speed parameter controls playback rate and direction (-4.0 to 4.0)

5. Envelope applied via table lookup with linear interpolation

6. Maximum concurrent grains: 1 to pool_size

Grain Mixing

Constant-power stereo panning (0=left, 0.5=center, 1=right)

Per-grain amplitude scaling: 0.0-2.0 (default 1.0)

Summation of all active grains per sample

No automatic gain compensation

# INLETS

1. Audio L - Left input + Messages + Bangs(metro/clock)

2. Audio R - Right input

3. GrainSize - Grain duration (0.001-10 seconds)

4. GrainStart - Normalized start position (0-1)

5. Speed - Playback rate (-4 to 4)

6. Organize - Splice selection (0-1)

7. ScanRate - Scan speed for mode 2 (-1000 to 1000)

8. SoS - Sound-on-sound mix (0-1)

9. IOT - Interonset time (0.0005-2 seconds)

10. MaxGrains - Max concurrent grains (1-pool_size)

11. GDelay Time - Delay time (0-10 seconds)

12. GDelay Feedback - Feedback amount (0-1) (DD-4/Bencina only)

13. GDelay Tone - Lowpass filter (0-1) (DD-4/Bencina only)

14. GDelay Mix - Dry/wet mix (0-1)

15. Smear Mix - Allpass smear blend (0-1)

16. Moog Cutoff - Filter cutoff (20-20000 Hz)

17. Moog Resonance - Filter resonance (0-4)

18. Moog Mix - Filter dry/wet (0-1)

19. MIDI Pitch - MIDI note (1-127, 0=inactive)

20. Envelope Skew - Shape skew (0-1)

21. Amplitude - Grain amplitude (0-2)

22. Pan - Stereo position (0-1)
23. Morph cursor X - CV drive for the morph surface (engage with morph_cursor 1; see MORPH)
24. Morph cursor Y - CV drive for the morph surface

Unconnected inlets default to stored values (set via messages).

Zero values from unconnected inlets do not overwrite stored parameters.

# OUTLETS

1. Signal - Audio output left

2. Signal - Audio output right

3. Bang - Splice end/wrap notification (modes 2 & 3)

4. Bang - Grain onset (every N grains, set via grain_bang_rate)

5. Float - modout1 (generator send, see PARAMETER RANGES & MODULATION)

6. Float - modout2

7. Float - modout3

8. Float - modout4

9. List - State query output (see QUERY STATE)

10. Signal - scope_x~ (scope tap X; see SCOPE)

11. Signal - scope_y~ (scope tap Y; see SCOPE)

# MESSAGES

File Operations

load <filename> - Load a 32-bit float stereo WAV (any rate; plays at the host rate). Resolved relative to the patch directory.
save <filename> - Save current reel to WAV

Playback Control

play <0|1> - Start/stop playback
record <0|1> - Start/stop recording
recsplice - Record and create splice on stop
recinput - Input-only recording, create splice on stop
loop <0|1> - 1=loop forever (default), 0=one-shot (stop at splice end)
trigger - (Re)start playback from the start of the current splice (one-shot re-arm; does not cut active grains)

Recording Configuration

sos <0-1> - Sound-on-sound mix level
sos_mode <0|1> - 0=record only, 1=crossfade (default)

Splice Navigation

shift <delta> - Move splice selection (integer)
organize <0-1> - Select splice by normalized value
splice - Add splice marker at current position

Splice Management

clear_splices - Remove all markers, clear buffer
clear_splices_except_current - Keep only current splice
clear_current_splice - Remove current splice
splice_join_right - Join current with right neighbor
splice_join_all - Join all splices into one

Splice Behavior

splice_create_pos <0|1|2> - 0=playback pos, 1=right, 2=end
splice_jump <0|1> - Jump to new splice on creation
splice_finish_nav <0|1> - Finish playback before nav
splice_split <0|1> - Allow/prevent splitting current

Playhead

playhead <1|2|3> - Static/Scanning/Clock Advance
scanrate <float> - Scan rate for mode 2
speed <-4 to 4> - Playback speed/direction
clock_advance_quant <0|1> - Use current/quantized length
clockstop - Stop clock, keep last BPM

Grain Parameters

grainsize <0.001-10> - Grain duration in seconds
grainstart <0-1> - Position within splice where grains begin (0=start, 1=end)
iot <0.0005-2> - Interonset time in seconds
maxgrains <1-pool_size> - Max concurrent grains
amplitude <0-2> - Grain amplitude
pan <0-1> - Stereo position (0=left, 0.5=center, 1=right)
pan_mode <0|1|2> - Mono panning / Stereo balance / Spatial 3D
spatial <sphere|nbody> [inst 0-3] [body 0-2] - Physics source for pan_mode 2
spatial_width <0-1> - Spatial stereo spread (0=center, 1=full)
spatial_depth <0-1> - Distance-to-level amount (default 0 = off)
spatial_tilt <0-1> - Elevation tilt amount (reserved; default 0 = off)
saw_cycles <0-64> - Saw envelope modulation cycles
saw_depth <0-1> - Saw envelope modulation depth
envelope <0|1|2|3|4> - Parabolic/Trapezoidal/Cosine/Gaussian/Exponential
env_skew <0-1> - Envelope shape skew
grain_bang_rate <N> - Bang outlet every N grains (0=off)

Envelope

envelope <0|1|2> - Parabolic/Trapezoidal/Cosine
env_skew <0-1> - Envelope shape skew

IOT Quantization

timesig <num>/<denom> - Time signature
quantize <1|2|4|8|16|32|64|128> - Note subdivision
quant <0-1> - Quantization amount

Grain Size Quantization

gs_timesig <num>/<denom>
gs_quantize <subdivision>
gs_quant <0-1>

Delay Quantization

delay_timesig <num>/<denom>
delay_quantize <subdivision>
delay_quant <0-1>

Grain Delay

gdelay_time <0-9.5> - Delay time in seconds
delay_glide <0-5000> - DD-4 delay-time glide/smoothing in ms (de-zipper; default 20)
gdelay_feed <0-1> - Feedback amount
gdelay_tone <0-1> - Lowpass filter (0=dark, 1=bright)
gdelay_mix <0-1> - Dry/wet mix
gdelay_clear - Clear delay buffer

Distortion

distortion <0-1> - Drive intensity
distortion_enable <0|1> - Enable/bypass
distortion_oversampling <1|2|4|8> - Oversampling factor
distortion_position <0|1> - Per-grain/Post-mix
dist_waveshaper_mode <0|1|2|3|4> - Tanh/Arctan/Asymmetric/Blend/Polynomial
distortion_pre_hp_freq <30-500> - Pre-HP frequency
distortion_pre_hp_mix <0-1> - Pre-HP mix
distortion_post_lp_freq <2400-10000> - Post-LP frequency
distortion_post_lp_mix <0-1> - Post-LP mix
distortion_notch_freq <Hz> - Notch center frequency
distortion_notch_bw <Hz> - Notch bandwidth
distortion_notch_mix <0-1> - Notch mix
dist_emphasis_mode <0|1> - HP/LP pre-emphasis
dist_emphasis_freq <100-5000> - Emphasis frequency
dist_pregain <0.1-10> - Pre-saturation gain
dist_curve_blend <0-1> - Tanh/arctan morph
dist_drive_pos <1-20> - Positive drive (asymmetric)
dist_drive_neg <1-20> - Negative drive (asymmetric)
dist_poly_c1 <-10 to 10> - Linear coefficient
dist_poly_c2 <-10 to 10> - Quadratic coefficient
dist_poly_c3 <-10 to 10> - Cubic coefficient

Smear (Allpass Spectral Effect)

smear_frequency <Hz> - Allpass center frequency (default 800)
smear_resonance <0-0.999> - Pole radius / smear sharpness (default 0.7)
smear_stages <0-48> - Number of allpass sections / smear depth (default 12)
smear_feedback <-0.99 to 0.99> - Global feedback (0 = pure smear, toward ±1 = resonant)
(mix is signal inlet 15, 0-1)

Smear Pitch (resonator pitch — note->Hz; at high feedback the smear rings at smear_frequency)

smear_pitch_source <0-4> - 0=off / 1=semitone / 2=scale / 3=midi / 4=pattern (off = manual smear_frequency owns it)
smear_pitch_semitones <n> - Fixed transpose vs the A440 reference (auto-selects source 1)
smear_note <note> [ref_note] [ref_hz] - Set a note directly (default ref note 69 = 440 Hz); auto-selects source 3
smear_pitch_scale <semitones...> - Scale for the smear SCALE + PATTERN sources
smear_pitch_rand_type <gen> - Stochastic generator for the smear SCALE source (rand_N/perlin_*/...)
smear_pitch_fine <cents> - ±50-cent fine tune on the smear pitch (modulatable: param_range smear_pitch_fine)
pattern smear_pitch <tokens...> - Mini-notation scale-degree pattern on the smear pitch (see PATTERNS)

Resonator Bank (grains excite a bank of tuned smear voices — see SMEAR > Resonator Bank)

smear_mode <0|1> - 0=single smear voice (default), 1=bank of tuned voices excited by the granular bus
smear_bank_mix <0-1> - Bank dry/wet (independent of inlet 15's single-voice mix; default 0)
smear_bank_feedback <-0.99 to 0.99> - Shared per-voice feedback (ring/sustain of the body)
smear_bank_resonance <0-0.999> - Shared pole radius (high ≈0.995-0.999 = tight tuning)
smear_bank_stages <0-48> - Shared allpass sections per voice (default 8)
(voice tuning/count comes from smear_pitch_scale — each degree is one voice, max 16)

Moogladder Filter

moog_cutoff <20-20000> - Cutoff frequency (Hz)
moog_resonance <0-4> - Resonance amount
moog_mix <0-1> - Dry/wet mix
moog_enable <0|1> - Enable/bypass

Delay Modes

delay_mode <0|1|2> - DD-4/Bencina/Stut
stut - Trigger stut effect
stut_reps <1-16> - Stut repetitions
stut_reduction <0-1> - Gain decay per repeat
stut_spacing <ms> - Spacing between repeats
stut_length <ms> - Slice length each repeat replays (independent of spacing)
stut_length_mode <0|1> - 0=independent length, 1=tie to grainsize
stut_length_quantize <1-128> - Note subdivision for a tempo-locked slice length
stut_length_quant <0-1> - Blend slice length toward the note grid
bencina_iot <ms> - Bencina grain spacing
bencina_grainsize <seconds> - Bencina grain size
bencina_wrap <0|1> - Global delay / loop wrap (default 0)
bencina_clear - Clear bencina grains
bencina_pan - per-grain stereo spread; a modulation target: `param_range bencina_pan <min> <max>` + `rand_type rand_1 bencina_pan` (base = inlet 22 / pan when the range is off)
bencina_spread <0-1> - position-scatter / cloud graininess (default 1.0 = full). Lower = tamer/smoother (0 = coherent grains, stereo cloud from pan only)
bencina_edge <0-1> - grain edge-round / de-click (default 0 = OFF; envelope+skew edges left intact, incl. their clickiness). Higher ramps each grain in/out (up to half a grain)
bencina_level <gain> - wet makeup gain into the tanh soft-limit (default 6.0). Higher = louder/more saturated; tanh keeps the output bounded to ±1 so it can't clip

Sphere Simulation

sphere_kick <instance> <vx> <vy> <vz> - Apply velocity impulse
sphere_kick_rand <instance> <0-50> - Kick in a random direction (event)
sphere_damping <instance> <0-1> - Damping coefficient
sphere_elasticity <instance> <0-1> - Bounce elasticity
sphere_spin <instance> <-10 to 10> - Orbit: rotate velocity about the y-axis (0 = off)
sphere_reset <instance> - Reset to initial state
sphere_mode <instance> <0-6> - Output mode (X/Y/Z/VelX/VelY/VelZ/VelMag)

Source Shape (see SOURCE SHAPE)

waveform_phase <instance> <0-1> - Readout phase offset for sine/saw/square (default 0)
square_pw <instance> <0.05-0.95> - Square pulse width / duty (default 0.5)
saw_skew <instance> <0-1> - Saw skew: 0 ramp up, 0.5 triangle, 1 ramp down (default 0)
lorenz_sigma <instance> <1-20> - Lorenz sigma (default 10)
lorenz_rho <instance> <1-60> - Lorenz rho, the chaos knob (default 28)
lorenz_beta <instance> <0.5-8> - Lorenz beta (default 8/3)

Parameter Ranging

param_range <param> <min> <max> - Set parameter range
rand_type <type_instance> <param> - Assign generator
param_lock <param1> <param2> <param3>... - Disable modulation and lock parameter(s) at their current modulated value.
param_invert <param> <0|1> - Invert a parameter's modulation output (mirror around its range).
param_slew <param> <0-1> - Exponential smoothing on a parameter's modulation output.
param_base_value <param> <value> - Set the PERLIN_2D Y-coordinate (decorrelation) for a parameter.

Generators

rand_1, rand_2, rand_3, rand_4
perlin_1d_1, perlin_1d_2, perlin_1d_3, perlin_1d_4
perlin_2d_1, perlin_2d_2, perlin_2d_3, perlin_2d_4
lorenz_1, lorenz_2, lorenz_3, lorenz_4
nbody_1, nbody_2, nbody_3, nbody_4
sphere_1, sphere_2, sphere_3, sphere_4
saw_1, saw_2, saw_3, saw_4
sine_1, sine_2, sine_3, sine_4
square_1, square_2, square_3, square_4

Generator Modulation Outlet Send

The four send outlets (modout1–modout4) are configured like any modulatable parameter — by
addressing the param name modoutN with the unified messages. There are no dedicated
modout_source/modout_range commands:
  rand_type <type_instance> modoutN   - assign a generator to outlet N
  param_range modoutN <min> <max>     - set the outlet's output range

Modulation Matrix (N->M routing; see MODULATION MATRIX)

matrix_connect <source> <dest> <depth> - Add/update a routing connection (depth signed, dest units)
  Destinations, per-block: gdelay, gdelay_feed, gdelay_tone, gdelay_mix, moog_cutoff,
  moog_resonance, moog_mix, smear_frequency, smear_resonance, smear_stages, smear_feedback,
  scanrate, organize, sos, iot, env_skew, modout1-4
  Destinations, per-block harmonic (see MODULATING THE SCALE): scale_root, smear_scale_root,
  pitch_scale_slot, smear_pitch_scale_slot, scale_rotate, smear_scale_rotate
  Destinations, per-grain (applied at grain trigger): speed, grainsize, grain_start,
  amplitude, pan, pitch_fine
matrix_disconnect <source> <dest> - Disable a connection (kept; re-connect re-enables)
matrix_clear - Remove all connections (matrix inert)
matrix_dump - Post current connections to the console
env_follow_ms <0-60000> - Envelope follower release time in ms (default 30; 0 = instant)

Scope (signal outlets 10/11; see SCOPE)

scope_tap <family> [inst 1-4] - Route a source to the scope outlets. Families:
  sine|saw|square|perlin|rand|folw (Y = readout, X = 2 Hz sweep ramp),
  lorenz|nbody|sphere (X/Y = the sim's x/z plane — the butterfly / orbits),
  grain (the GRAIN CONSTELLATION, one grain per sample), grainsum (cloud
  amplitude silhouette + sweep). Default: lorenz 1. A monitor — never captured
  by snapshots, never morphed.

Noise Frequency

noise_freq <scale> - Set all generator speeds
noise_freq_1|2|3|4 <scale> - generator instance speeds

N-Body Configuration

nbody_mode <instance> <mode> - Set output mode (0-10)
nbody_epsilon <instance> <0.01-1> - Softening parameter
nbody_damping <instance> <0-0.1> - Energy damping
nbody_pump <instance> <0-0.01> - Energy injection
nbody_reset <instance> - Reset to initial conditions

Lorenz Configuration (SOURCE SHAPE)

lorenz_sigma <instance> <1-20> - Attractor sigma (default 10)
lorenz_rho <instance> <1-60> - Attractor rho / chaos knob (default 28)
lorenz_beta <instance> <0.5-8> - Attractor beta (default 8/3)
lorenz_reset <instance> - Reset to initial conditions

Waveform Shape (SOURCE SHAPE)

waveform_phase <instance> <0-1> - Readout phase offset for that instance's sine/saw/square
square_pw <instance> <0.05-0.95> - Square pulse width / high-duty fraction
saw_skew <instance> <0-1> - Saw skew: 0 ramp up, 0.5 triangle, 1 ramp down

Sphere Configuration

sphere_kick <instance> <vx> <vy> <vz> - Apply velocity impulse
sphere_kick_rand <instance> <0-50> - Random-direction kick x strength (event, not captured)
sphere_damping <instance> <0-1> - Damping coefficient
sphere_elasticity <instance> <0-1> - Bounce elasticity
sphere_spin <instance> <-10 to 10> - Rotate velocity about the y-axis per update (orbit; default 0)
sphere_reset <instance> - Reset to initial state
sphere_mode <instance> <0-6> - Output: X/Y/Z/VelX/VelY/VelZ/VelMag

Pitch Control

pitch_mode <0|1|2|3|4|5> - Off/Semitones/Range/Scale/MIDI/Pattern
pitch_semitones <-24 to 24> - Fixed transposition
pitch_range <min> <max> - Semitone range
pitch_rand_type <type> - Random generator for pitch
pitch_scale <note1> <note2> ... <noteN> - Scale definition (semitones; writes the ACTIVE slot)
pitch_fine <cents> - ±50-cent fine tune on the grain pitch (modulatable: param_range pitch_fine)

Scale Slots & Harmonic Layer (see SCALE SLOTS / MODULATING THE SCALE)

pitch_scale_slot <0-15> - Select the grain-side active scale slot (A-P); modulatable, stepped
pitch_scale_to <slot> <semis...> - Write a slot without selecting it (no semis = clear the slot)
scale_root <-24 to 24> - Semitone offset applied AFTER degree lookup (grain dest); modulatable
scale_root_quant <0|1> - 1 = round the applied root to integer semitones (default 1)
scale_rotate <n> - Degree-index offset with wrap (modal interchange, grain dest); modulatable
smear_pitch_scale_slot <0-15> - Smear-side active slot select (bank retunes next block)
smear_pitch_scale_to <slot> <semis...> - Write a smear slot without selecting it
smear_scale_root <-24 to 24> - Smear-side root offset (also shifts the resonator bank)
smear_scale_root_quant <0|1> - Quantize the applied smear root (default 1)
smear_scale_rotate <n> - Smear-side degree rotate (SCALE pick + PATTERN stepper; not the bank)

MIDI (channel-aware; from Pd [notein] -> note/vel/channel)

midi <note> [vel] [channel] - Channel-routed MIDI note. Same channel for grain+smear = unison; different = separate
midi_channel <grain_ch> <smear_ch> - Set both routing channels (equal = unison); default grain 1, smear 2
pitch_channel <ch> - MIDI channel routed to the GRAIN pitch destination
smear_pitch_channel <ch> - MIDI channel routed to the SMEAR pitch destination

CHORDAL POLY (N-transposition chord from the ONE shared playhead)

poly <0|1> - Enable/disable POLY mode (default 0 = mono). Requires pitch_mode 4 (MIDI) to sound.
chord <n1> [n2] ... - Set the whole voice pool at once (max 8 notes; empty list clears/silences)
  In POLY mode each grain-trigger tick spawns one grain PER active voice, each transposed to that
  voice's MIDI note vs middle C (60). The playhead/splice/SOS/recording are untouched. On the GRAIN
  channel: `midi <note> [vel]` appends a voice (vel 0 = note-off, removes it); the pool holds up to 8
  notes (MAX_VOICES) and steals the OLDEST note when a 9th distinct note arrives. The SMEAR pitch
  destination stays MONO (last-note-wins). Default poly 0 is bit-identical to the mono scalar path.
  BUDGET: all voices share the soft grain cap `max_grains` (default 4), so a chord can starve later
  voices at default settings. Raise it for POLY (e.g. `maxgrains 32`); the hard ceiling is pool_size.

Patterns (TidalCycles Mini-Notation)

pattern <param|pitch|slot> <tokens...> - Step-sequence a parameter (or pitch) from a mini-notation pattern
pattern event <action> <tokens...> - FIRE events from a pattern: grain | splice | retrig | gate | bang ('trigger' = accepted alias)
pattern_cycle <N/D> <N/D> ... - Quantization-cycle length as musical durations at the detected BPM (default = 1 bar)
pattern_clear <param|pitch|slot> - Detach a pattern; restore the prior source (pitch -> OFF; slot also resets an event tag)
pattern_debug <0|1> - Log step / event / semitone changes to stderr (off by default)

Tokens are space-separated: values 0..1 for params or scale degrees for pitch; < > alternation
(one per cycle); [ ] subdivision (nestable); @N weight; *N repeat; !N replicate; ~ rest; v(k,n) Euclidean
rhythm (escape the comma in a message box: 1(3\,8)); rev reverses the preceding group. See PATTERNS.

Morph / Metasurface (snapshot interpolation surface)

snapshot <id> - Capture the whole patch into snapshot id (0-63)
snapshot_recall <id> - Jump straight to a captured snapshot
snapshot_clear <id> - Forget a snapshot
morph_point <id> <x> <y> - Place a snapshot at (x,y) on the [0,1]^2 surface (alias morph_place)
morph_unplace <id> - Remove a snapshot's surface point
morph <x> <y> - Move the cursor -> blend all placed snapshots by distance (the live morph)
morph_x <v> / morph_y <v> - Move one cursor axis
morph_power <p> - IDW sharpness (default 2; higher = more local)
morph_interp <0|1> - Kernel: 0=IDW/Shepard (default), 1=natural-neighbour (local, sampled Sibson)
morph_cursor <0|1> - 0=message cursor (default); 1=CV cursor (signal inlets 22/23 drive x/y per block)
morph_include <name...> / morph_exclude <name...> - Limit which params the morph applies (all / a param / a group: pitch, smear_pitch, fx)
morph_route <x> <y> <rate> <curve> - Append a route waypoint (rate sec, curve 0-4)
morph_run [loop] / morph_stop / morph_pause - Play / halt the route. morph_route_clear empties it.
morph_save <file> / morph_load <file> - Save / load the whole surface (every snapshot + points + cursor + route) to a .morph file
morph_state - Dump the surface LAYOUT (points/route/cursor/power) as re-sendable messages to the state outlet
morph_export <file> / morph_import <file> - Text (.txt) export/import of the WHOLE surface incl. snapshot bodies (human-readable, build-portable)

See MORPH / METASURFACE for the full model.

Defaults

Core Parameters

grainsize: 0.1 seconds
grainstart: 0.5 (center of splice)
speed: 1.0 (normal forward)
iot: 0.1 seconds
maxgrains: 4
amplitude: 1.0
pan: 0.5 (center)
pan_mode: 0 (constant-power mono panning)
saw_cycles: 0.0 (off)
saw_depth: 0.0 (off)
env_skew: 0.5 (symmetric)

Envelope

type: ENVELOPE_COSINE
table_length: 4096 samples

Playhead

mode: PLAYHEAD_MODE_STATIC (mode 1)
scanrate: 1.0

Recording

sos: 0.5 (50/50 mix)
sos_mode: 1 (Morphagene-style crossfade)
mode: RECORD_MODE_OVERDUB

Splice

create_pos: 0 (at playback position)
jump: 0 (stay in current)
finish_nav: 0 (immediate)
split: 0 (allow split)

Effects

smear mix: 0.0 (dry, inlet 15)
smear_frequency: 800 Hz
smear_resonance: 0.7
smear_stages: 12
smear_feedback: 0.0
smear_mode: 0 (single voice)
smear_bank_mix: 0.0 (bank dry)
smear_bank_stages: 8 (per voice)
gdelay_time: 0.0 (off)
gdelay_feed: 0.0
gdelay_tone: 0.5
gdelay_mix: 0.0
delay_mode: 0 (DD-4)
delay_quant_note: 16 (1/16)
delay_quant_amount: 0.0 (disabled)
stut_reps: 4
stut_reduction: 0.5
stut_spacing: 62.5 ms
bencina_iot: 50.0 ms
bencina_grainsize: 0.1 seconds
bencina_wrap: 0 (global)
distortion: 0.0 (clean)
distortion_enable: 1
distortion_oversampling: 4x
distortion_position: 1 (post-mix)
moog_cutoff: ~1000 Hz
moog_resonance: 0.0
moog_mix: 0.0
moog_enable: 1

Distortion Filters

pre_hp_freq: 30 Hz
pre_hp_mix: 0.5
post_lp_freq: 16000 Hz
post_lp_mix: 0.5
notch_freq: 3000 Hz
notch_bandwidth: 500 Hz
notch_mix: 0.0 (inactive)
emphasis_freq: 800 Hz
emphasis_mode: 0, EMPHASIS_MODE_HP
pregain: 1.0

Pitch Control

mode: PITCH_MODE_OFF
semitones: 0.0
midi_note: 60 (C4)

Quantization

All quant_amount: 0.0 (off)
timesig: 4/4
subdivision: 4 (quarter note)

Noise Generators

All frequency_scale: 1.0 (normal)
All rand_type: RAND_TYPE_RAND
All rand_instance: 0 (generator 1)

Pool

pool_size: 200 grains (configurable via ligase.conf)

Buffer

Sample rate: follows the host (Pd) rate — the reel capacity and saved WAVs use it (44.1/48/96 kHz etc.)
Max length: 600 seconds
Channels: 2 (stereo)

# PLAYBACK CONTROL

Transport

play <float> Start/stop grain triggering. 0 stops, non-zero starts.

When started:

- Checks reel length > 0. Posts error and aborts if empty.

- Initializes playback_position to current splice start.

- Sets is_playing and is_triggering flags.

When stopped:

- Clears is_playing and is_triggering.

- Active grains finish their envelopes.

Default: 0 (stopped)

record <float> Overdub into the CURRENT splice — Time Lag Accumulation. Argument: 0 stops, non-zero starts.

The record head loops within the current splice bounds, re-recording what is heard (the granular playback of the splice, mixed with the live input) back into the same splice. Because the splice being recorded is also the one being granulated, successive passes accumulate — layers build and any pitch transposition compounds across loops. SOS sets the input-vs-feedback balance (the feedback amount); the feedback is held just below unity and soft-limited so it sustains/decays rather than running away. Cross-splice recording is done by shifting the current splice (which moves the bounds) — overdub never extends the reel.

- Sets RECORD_MODE_OVERDUB; record head wraps within the current splice
- Stop: posts "recording stopped" (no new splice created)

recsplice  Record a NEW splice of what is heard.

- Sets RECORD_MODE_NEW_SPLICE
- Records the SOS-mixed monitor signal (live input crossfaded with the granular playback, per SOS) — i.e. "what you hear". SOS acts as a VCA on the recorded signal.
- Posts starting position (current reel length); creates the splice when recording stops

recinput  Record the live INPUT only — the one mode where SOS does NOT act as a VCA.

- Sets RECORD_MODE_INPUT_ONLY
- Captures the raw input directly (no SOS, no feedback); ignores the sos parameter
- Creates a splice when stopped

Playhead Modes

playhead 1|2|3 Set playhead behavior.

Mode 1: Static (default)

Playback position fixed. grain_start parameter (inlet 4) sets grain spawn location directly
Grain position: splice_start + (grain_start × splice_length)

Detects grain_start parameter wraps (change > 0.5), bangs outlet 3. No playback_position advancement.

Mode 2: Scanning

Playback position advances continuously at scan_rate (samples per sample).

Grain position: playback_position + (grain_start × splice_length)

Advancement: playback_position += scan_rate every sample

Wraps at splice boundaries, bangs outlet 3 on wrap. Checks pending splice navigation after wrap.

Negative scan_rate = reverse.

Mode 3: Clock Advance

Playback position advances only when clock bang received (inlet 1).

Advance amount: grain length × sample_rate

Grain length source: current or quantized (see clock_advance_quant)

Grain position: playback_position + (grain_start × splice_length) between bangs Wraps, bangs outlet, checks pending navigation like Mode 2.

scanrate <float> Set scanning speed for Mode 2.

1.0 = real-time forward

-1.0 = real-time reverse

0.0 = static

Default: 1.0

clock_advance_quant 0|1 Select grain length sourcefor Mode 3 advance.

0 = current actual grain length (varies per grain)

1 = quantized grid length (requires valid BPM and gs_quant_grid_ms > 0)

Default: 0

Splice Structure

- Array of position markers (0-300 maximum)
- Each splice defined by start marker, extends to next marker or buffer end
- Current splice selected via organize parameter (0.0-1.0 maps to splice index)
- Shift message: relative navigation (shift 1 = next, shift -1 = previous)

Splice Bounds

Grains read exclusively within current splice boundaries. Position wraps
at splice_end back to splice_start. This creates independent loop regions
within the buffer.

Splice Navigation

organize <float> Navigate to splice by normalizedposition (0.0-1.0).

Maps to index: int(value × (splice_count - 1))

Clamps input to [0.0, 1.0]

If splice_finish_nav = 0: immediate navigation, resets playback_position to splice start

If splice_finish_nav = 1: queues navigation, executes after current splice wraps

shift <int> Move splice selection by delta.

Adds delta to current_splice index.

Clamps to [0, count-1]. No wraparound.

Examples: shift 1 = next, shift -1 = previous

If splice_finish_nav = 1: queues as pending

If splice_finish_nav = 0: immediate

Splice Creation

splice  Create splice marker.

Position determined by splice_create_pos:

- 0: at playback_position (if 0 and reel exists, uses reel_length)

- 1: at current splice end boundary

- 2: at reel end

Rate limited: minimum 10ms between creation

Posts error if MAX_SPLICES (64) reached

splice_create_pos 0|1|2 Set splice creation positionmode.

0 = at playback position (default)

1 = right of current splice boundary

2 = at end of reel (Morphagene append)

splice_jump 0|1 Control navigation after creatingnew splice.

0 = stay in current splice (default)

1 = jump to newly created splice immediately

splice_finish_nav 0|1 Control navigation timing fororganize and shift messages.

0 = immediate navigation (default)

1 = queue navigation, execute after current splice wraps

splice_split 0|1 Control splice splitting behavior.

0 = allow splitting current splice (default)

1 = preserve current splice length

One-Shot Playback (loop / trigger)

loop <0|1> sets whether playback loops at the splice end:

  loop 1   (default) the playhead wraps at the splice boundary and keeps playing — the normal looping behavior.
  loop 0   one-shot: when the playhead reaches the END of the splice, playback STOPS on its own.

One-shot is orthogonal to the playhead mode — it is meaningful when the playhead advances (Scanning and
Clock-Advance modes; in Static mode there is no advancing playhead, so loop has no effect). play 1 and the
dedicated trigger message already start from the beginning of the current splice, so a one-shot pass runs
start-to-end and then stops.

trigger re-arms a one-shot from the start of the current splice:

  trigger   restart playback at splice_start.

Crucially, stopping (or re-triggering) NEVER cuts grains that are already sounding — already-scheduled grains
ring out to their natural envelope end. So you can fire trigger repeatedly for a rhythmic, overlapping one-shot
texture without clicks. (Use trigger rather than play 1 to re-arm: play 1 prints a status dump on every call.)

bang is the tempo clock (see TIMING & QUANTIZATION), so the one-shot (re)trigger is its own message, not a
bang. If a splice navigation is queued at the boundary, the navigation WINS and playback continues into the
next splice; the one-shot stop fires only when nothing is queued.

# RECORDING CONFIGURATION

Record Modes

Three recording modes, set by message commands. Mode determines recording behavior and splice creation.

record <float> Overdub mode. Argument: 0 stops, non-zerostarts.

- Sets RECORD_MODE_OVERDUB

- Start: positions record_position at current_splice_start

- Records into current splice bounds

- Continues past splice boundary into adjacent splice (cross-splice recording)

- Mixing controlled by crossfade_mix (sos value)

- Stop: Posts "recording stopped" only

recsplice New splice mode.

- Sets RECORD_MODE_NEW_SPLICE

- Start: positions record_position at reel length (append)

- Stores new_splice_start = current reel length

- Mixing controlled by crossfade_mix (sos value)

- Stop: Creates splice marker at new_splice_start position, sets as current splice

- Posts splice creation confirmation with position

recinput Input-only mode.

- Sets RECORD_MODE_INPUT_ONLY

- Start: positions record_position at reel length (append)

- Stores new_splice_start = current reel length

- Records input directly, ignores sos/crossfade_mix completely

- Stop: Creates splice marker at new_splice_start position, sets as current splice

- Posts splice creation confirmation with position

Default mode: RECORD_MODE_OVERDUB

SOS Mode Control

sos_mode 0|1 Set Sound-on-Sound operational mode.

Mode 0: Record Only

- Output: Constant-power crossfade between input and granular

  - sos = 0.0: 100% granular (input_gain = 0.0, granular_gain = 1.0)

  - sos = 0.5: Equal power mix (both ≈ 0.707)

  - sos = 1.0: 100% input (input_gain = 1.0, granular_gain = 0.0)

  - Formula: out = input × sin(sos × π/2) + granular × cos(sos × π/2)

- Recording: happens before output mixing

  - Records raw input only (not the mixed output)

  - crossfade_mix forced to 1.0 (full replacement, no sound-on-sound)

  - sos parameter does NOT affect recording in this mode

  - Always replaces buffer content with input signal

- Output and recording are decoupled:

  - Recording: Raw input → buffer (replace mode)

  - Output: Input + granular constant-power mix (monitoring)

Mode 1: Morphagene (default)

- Output: Constant-power crossfade between input and granular

  - output = input × sin(sos × π/2) + granular × cos(sos × π/2)

  - sos = 0.0: 100% granular (input_gain = 0.0, granular_gain = 1.0)

  - sos = 0.5: Equal power mix (both ≈ 0.707)

  - sos = 1.0: 100% input (input_gain = 1.0, granular_gain = 0.0)

  - sos from signal inlet (inlet 8) or stored sos_value

  - Uses inlet value if non-zero, else uses stored value

  - Clamped to [0.0, 1.0]

  - NaN/Inf protection via isfinite() check

- Recording: what gets written depends on the record mode (see RECORDING CONFIGURATION):

  - recinput (INPUT_ONLY): the raw live input — SOS bypassed (the one non-VCA mode)

  - recsplice (NEW_SPLICE): the monitored output above — "what you hear" (the SOS crossfade of input and granular) — written to a new splice

  - record (OVERDUB): Time Lag Accumulation into the current splice. The monitored output is fed back into the splice each pass (SOS sets the input-vs-feedback balance), so layers and pitch accumulate; held stable by a sub-unity feedback coefficient and a tanh soft-limiter

- When no playback (reel length = 0): the monitored output is input × sos (linear VCA). recsplice/overdub capture that; recinput captures the raw input.

Default: 1 (Morphagene mode)

SOS Crossfade Parameter

sos <float> Set crossfade mix value.

Range:

0.0 to 1.0 (clamped in processing)
Default:
0.5

# SPLICE NAVIGATION

Direct Navigation

shift <int> Navigate by relative offset.

Adds delta to current_splice index. Clamps to [0, count-1]. No wraparound at boundaries.

- Positive: move right through array

- Negative: move left through array

- 0 stops at first splice

- count-1 stops at last splice

If finish_before_nav = 1: calculates target with wraparound ((target % count) + count) % count, stores as pending, executes after playback wraps at splice boundary.

If finish_before_nav = 0: immediate navigation.

organize <float> Navigate by normalized position.

Maps input [0.0-1.0] to discrete splice index: int(value × (count - 1))

Clamps input to [0.0, 1.0] before mapping.

Examples: 0.0 = first splice, 0.5 = middle splice, 1.0 = last splice

If finish_before_nav = 1: calculates target, stores as pending.

If finish_before_nav = 0: immediate navigation.

Splice Creation

splice Create splice marker.

Position determined by create_position setting:

Mode
0 (default): At playback_position
- 
If playback_position = 0 and reel exists, uses reel_length instead
- 
If split_current = 1 and position would split current splice (between splice_start and

splice_end), moves to splice_end

Mode
1: At current splice end boundary (splice_end)
Mode
2: At reel end (reel_length)
Rate limited: minimum 10ms between creation. Posts error if violated.

Maximum: 64 splices (MAX_SPLICES). Posts error if exceeded.

After creation, if jump_to_new = 1: sets current_splice to new index, resets playback_position to new splice start.

splice_create_pos 0|1|2 Set creation position mode. 0 = at playback position

1 = right of current splice

2 = at end of reel

Default: 0

splice_jump 0|1 Control navigation after splice creation.

0 = stay in current splice (default)

1 = jump to newly created splice, reset playback_position

splice_finish_nav 0|1 Control navigation timing forshift and organize.

0 = immediate navigation (default)

1 = queue navigation, execute when playback wraps at splice boundary

Pending navigation stored in pending_splice. Cleared after execution.

splice_split 0|1 Control current splice splitting.

0 = allow splitting current splice (default)

1 = preserve current splice length

When enabled (mode 1), if create_position = 0 and calculated position falls within current splice bounds, position moves to splice_end.

Splice Removal

clear_splices  Remove all splice markers, clear entire audio buffer.

Calls splice_clear() and reel_clear(). Destructive operation. Sets count = 0, current_splice = 0.

clear_splices_except_current  Remove all splices except current, clear buffer

except current splice audio.

Destructive. Extracts current splice bounds, clears buffer outside bounds, reorganizes markers:

- If current has right neighbor: creates two markers (position 0 and splice_length)

- If current extends to reel end: creates one marker (position 0)

Current splice moves to position 0. Sets current_splice = 0.

clear_current_splice  Remove current splice marker immediately.

Uses splice_remove() to delete marker at current_splice index. Shifts subsequent markers down. Active grains finish naturally.

Adjusts current_splice index if needed:

- If count becomes 0: current_splice = 0

- If current_splice >= count: current_splice = count - 1

Posts count of remaining splices.

Splice Joining

splice_join_right  Join current splice with right neighbor.

Calculates right neighbor with wraparound: (current_splice + 1) % count

Removes marker at right neighbor (boundary between them). Updates current_splice index if right_splice <= current_splice.

No effect if only one splice exists.

splice_join_all  Join all splices into one continuoussplice. Calls splice_clear(). Entire reel becomes single splice with no markers. Sets count = 0, current_splice = 0.

Posts number of splices joined.

# PLAYHEAD MODES

Three modes control how playback position advances and where grains spawn within the current splice.

playhead 1|2|3 Set playhead mode.

Mode 1: Static (default)

Playback position fixed. grain_start signal (inlet 4) directly controls grain spawn location.

Grain position: splice_start + (grain_start × splice_length)

grain_start range: 0.0 to 1.0

0.0 = splice start, 0.5 = splice middle, 1.0 = splice end

Wrap detection: When grain_start change exceeds 0.5 (indicating wrap from 1.0→0.0 or 0.0→1.0), bangs outlet 3.

playback_position: Never advances. Remains at value set by play message (splice start).

grain_start tracking: Stores prev_grain_start each block for wrap detection.

Mode 2: Scanning

Playback position advances continuously. grain_start offsets grain spawn relative to moving playhead.

Grain position: playback_position + (grain_start × splice_length) Wraps to stay within splice bounds.

Advancement: playback_position += scan_rate every audio sample

scan_rate units: samples per sample

1.0 = real-time forward

1.0- = real-time reverse

0.0 = static (no advancement)

2.0 = double-speed forward

Boundary wrapping:

When playback_position >= splice_end: wraps by subtracting splice_length

When playback_position < splice_start: wraps by adding splice_length

Bangs outlet 3 on wrap. Checks pending_splice navigation after wrap, executes if queued.

Mode 3: Clock Advance

Playback position advances only when clock bang received (inlet 1). Grains spawn at static position between bangs.

Grain position: playback_position + (grain_start × splice_length) Wraps to stay within splice bounds.

Advancement trigger: Bang to inlet 1 sets clock_bang_received flag.

Advance amount: grain_length × sample_rate (converted from seconds to samples)

Grain length source:

Determined by clock_advance_quant setting and current state.

If clock_advance_quant = 0: uses quantized_grain_size (current grain length from perform cycle)

If clock_advance_quant = 1 AND gs_quant_grid_ms > 0 AND bpm > 1.0: Uses gs_quant_grid_ms / 1000.0 (quantized grid length in seconds)

Otherwise: uses quantized_grain_size

Per-bang behavior:

Flag checked once per perform cycle. If set, advances playback_position, wraps at boundaries, bangs outlet 3 if wrapped, checks pending navigation, then clears flag.

Grains trigger normally at IOT rate at current playback_position while flag is clear.

Control Messages

scanrate <float> Set scan rate for Mode 2.

Applied per audio sample as: playback_position += scan_rate

Default: 1.0

clock_advance_quant 0|1 Select grain length sourcefor Mode 3 advancement.

0 = use current grain length (varies per grain)

1 = use quantized grid length (requires valid BPM and gs_quant_grid_ms)

Default: 0

# GRAIN PARAMETERS

Interonset Time

iot <float> Set time interval between grain starts.

Range: 0.0005 to 2.0 seconds

Clamped to bounds.

Converted to grain_trigger_period: int(iot × sample_rate) samples

When iot < grain_size: grains overlap

When iot > grain_size: grains separate

When iot = grain_size: continuous stream

Signal inlet 9: updates if value in range [0.001, 2.0], else keeps current value.

Quantization: If quant_amount > 0 and BPM valid:

grain_trigger_period = base_period × (1 - quant_amount) + quant_period × quant_amount

Minimum grain_trigger_period enforced: 1 sample (prevents CPU overload)

Default: 0.1 seconds (100ms)

Grain Size

grainsize <float> Set grain duration.

Range: 0.001 to 10.0 seconds

Clamped to bounds.

Signal inlet 3: updates if value in range [0.001, 10.0], else keeps current value.

Default: 0.1 seconds (100ms)

Grain Start Position

grainstart <float> Set normalized grain spawn positionwithin splice.

Range: 0.0 to 1.0

0.0 = splice start, 0.5 = splice middle, 1.0 = splice end

Signal inlet 4: updates if value in range [0.0, 1.0]

Special handling: if inlet reads 0.0 and stored value is non-zero, assumes inlet unconnected and preserves stored value.

Interacts with playhead mode:

- Mode 1 (Static): grain_pos = splice_start + (grainstart × splice_length)

- Mode 2 (Scanning): grain_pos = playback_position + (grainstart × splice_length)

- Mode 3 (Clock): grain_pos = playback_position + (grainstart × splice_length)

Default: 0.5 (middle)

Maximum Grains

maxgrains <int> Set maximum concurrent active grains. Range: 1 to pool_size

pool_size read from ligase.conf (default: 200)

Clamped to [1, pool_size]

Controls polyphony limit. Grain scheduler uses free-list allocation. When limit reached, oldest grains replaced.

Signal inlet 10: updates if value in range [1, pool_size], else keeps current value.

Default: 4

Amplitude

amplitude <float> Set grain output level.

Range: 0.0 to 2.0

Clamped to bounds.

Applied during grain triggering. Passed to scheduler_trigger_grain().

Signal inlet 21: amplitude_in (read from perform cycle)

Default: 0.75

Per-grain amplitude scaling and randomization

There are two operational modes based on param_range configuration:

Mode 1: Direct Control (param_range disabled, default)

- Uses amplitude value directly from inlet or message

- No per-grain randomization

- Consistent amplitude across all grains

Mode 2: Modulated Range (param_range enabled)

- Samples amplitude from [min, max] range per grain

- Uses inlet/message value as base for sampling

- Random source determines variation pattern

- Examples:

param_range amplitude 0.5 1.0 → grains vary 0.5-1.0

rand_type perlin_1d_1 amplitude → smooth amplitude evolution

rand_type rand_1 amplitude → random per-grain levels

Pan Mode

pan_mode <int> Select panning algorithm.

Mode 0: Constant-Power Mono Panning (default)

Sums stereo source to mono before panning

Each grain becomes a point source in the stereo field

Ideal for spatial positioning and grain cloud diffusion

Mode 1: Stereo Balance

Preserves stereo width of source material

Applies constant-power gains independently to L/R channels

Maintains stereo image character while adjusting balance

Ideal for stereo sources where width should be preserved

Mode 2: Spatial 3D (physics-driven placement)

Each grain freezes the 3D position of a physics simulation (the bouncing sphere or one n-body body) at the moment it is triggered, and is placed in the stereo field by that position for its whole life. Overlapping grains sit at different points of the trajectory, so the cloud spreads and moves through space as the simulation orbits.

Select the driving simulation with spatial <sphere|nbody> [instance 0-3] [body 0-2] (default: sphere instance 0; for nbody, body 1 — the orbiting body), then engage with pan_mode 2. The grain is treated as a mono point source (like mode 0) and placed by a front-biased azimuth derived from the sim's horizontal plane (x = left/right, z = depth), so the whole orbit stays in the frontal arc — a stereo field cannot render "behind you." Constant-power law throughout.

spatial_width <0-1> scales the spread: 0 collapses every grain to center, 1 is full left/right. spatial_depth <0-1> optionally makes far (back) positions quieter — default 0 (off). spatial_tilt is reserved for elevation and currently inert.

Default: 0 (constant-power mono panning)

Pan Law

All modes use constant-power panning to maintain consistent perceived loudness:

pan_angle = pan * (π/2)

left_gain = cos(pan_angle)   // 1.0 → 0.707 → 0.0

right_gain = sin(pan_angle)  // 0.0 → 0.707 → 1.0

This ensures cos²(θ) + sin²(θ) = 1 at all pan positions, preventing the "hole in the middle" effect where center-panned signals sound quieter.

At center (pan=0.5): Both channels at 0.707 (-3dB) = 0dB total power

Pan Modulation

Pan can be modulated per-grain using param_range:

Direct Control (param_range disabled)

Uses pan value directly from signal inlet 22 or message

No per-grain randomization

All grains at same stereo position

Default behavior

Modulated Range (param_range enabled):

Samples pan position from [min, max] range per grain

Uses inlet/message value as base for sampling

Random source determines spatial pattern

Creates spatial diffusion effects

Examples:

param_range pan 0.0 1.0

rand_type perlin_1d_1 pan    ← smooth stereo movement

param_range pan 0.3 0.7

rand_type rand_1 pan          ← random center-focused placement

param_range pan 0.0 0.5

rand_type lorenz_1 pan        ← chaotic left-side positioning

Spatial Cloud Effects:

Wide range + fast modulation = diffuse stereo cloud

Narrow range + slow modulation = subtle stereo shimmer

Per-grain randomization creates spatial depth impossible with traditional panning

Envelope Shape

envelope 0|1|2 Set grain envelope type.

Type 0: Parabolic (Welch window)

Formula: 1 - 4(x - 0.5)²

Smooth, gentle envelope. Quadratic curve.

Type 1: Trapezoidal

Linear attack (10% default), sustain, linear release (10% default). Hard attack/release with flat sustain. Percentages adjusted by skew.

Type 2: Cosine (Hann window, default)

Formula: 0.5 × (1 - cos(2π × x))

Smoothest envelope. Reduces spectral artifacts.

Envelope table length: 4096 samples

Preserves skew value when type changes.

Default: ENVELOPE_COSINE (type 2)

Envelope Skew

Skew remaps time axis before envelope calculation, shifting peak position while maintaining smoothness.

env_skew <float> Set envelope attack/decay balance.

Range: 0.0 to 1.0

Clamped to bounds.

0.5 = symmetric (peak at center)

< 0.5 = shorter attack, longer decay (peak shifts left)

>0.5 = longer attack, shorter decay (peak shifts right)

Regenerates envelope table immediately on change.

Signal inlet 20: updates if value in range [0.001, 1.0] and different from current, else keeps current value.

Saw Envelope Modulation

Per-grain sawtooth wave carved into the envelope. Creates jagged, rhythmic amplitude spikes within each grain. The saw wave modulates the envelope shape: at depth=0 the envelope is unmodified, at depth=1 the saw wave fully carves the envelope into sharp peaks.

Formula: env_val = base_env * (1.0 - depth * (1.0 - saw_val))

saw_cycles <float>  Number of sawtooth cycles per grain duration.

Range: 0.0 to 64.0. Default: 0.0 (off).

0.0 = no modulation. 1.0 = one ramp per grain. Higher values = more spikes. Non-integer values produce partial final cycles.

Set via message only (no signal inlet; not param_range-modulatable).

saw_depth <float>  Modulation intensity.

Range: 0.0 to 1.0. Default: 0.0 (off).

0.0 = pure base envelope. 1.0 = maximum jaggedness (spikes drop to zero between peaks).

Set via message only (no signal inlet; not param_range-modulatable).

Grain Bang Output

grain_bang_rate <int> Set grain onset bang rate.

Range: 0 to 1000

0 = disabled (no bangs)

1 = bang every grain

2 = bang every 2nd grain

N = bang every Nth grain

Bangs outlet 4 when grain_bang_counter reaches grain_bang_rate. Counter resets on rate change.

# TIMING & QUANTIZATION

Clock Management

Clock bangs to inlet 1 establish tempo. BPM calculated from the interval between bangs.

Bang to inlet 1: Calculate BPM and update quantization grids.

First bang: stores timestamp, no BPM calculation

Second bang onward: calculates interval, derives BPM

BPM calculation: 60000.0 / interval_ms

Sets clock_running flag. Recalculates all active quantization grids (IOT, grain size, delay) using new BPM.

In playhead Mode 3: sets clock_bang_received flag for playhead advance.

clockstop Clears clock_running flag.

Preserves last calculated BPM and quantization grids for continued use. Posts current BPM value.

IOT Quantization

Snaps interonset time to rhythmic grid.

timesig <symbol> Set time signature for IOT quantization.

Format: "numerator/denominator" (e.g., "7/5", "4/4") Requires symbol argument. Parses using sscanf().

Validation: numerator > 0, denominator > 0

Posts error if invalid format or values.

Stores: time_sig_numerator, time_sig_denominator

Default: 4/4

quantize <int> Set IOT quantization note subdivision. Valid values: 1, 2, 4, 8, 16, 32, 64, 128

Represents note division (16 = 1/16 note)

Stores quant_note. Recalculates quant_grid_ms if BPM > 0:

Grid calculation:

ms_per_whole_note = (60000.0 / bpm) × 4.0

quant_grid_ms = ms_per_whole_note / quant_note

Posts grid size in milliseconds if BPM available, otherwise defers calculation.

Default: 16 (1/16 note)

quant <float> Set IOT quantization amount (blend).

Range: 0.0 to 1.0

0.0 = no quantization (free timing)

1.0 = full quantization (locked to grid)

0.5 = 50% blend

Applied in perform cycle:

grain_trigger_period = base_period × (1 - quant_amount) + quant_period × quant_amount

Requires: quant_amount > 0, bpm > 1.0, quant_grid_ms > 0

Default: 0.0 (no quantization)

Grain Size Quantization

Snaps grain duration to rhythmic grid.

gs_timesig <symbol> Set time signature for grain size quantization.

Format: "numerator/denominator"

Validation and parsing identical to timesig.

Stores: gs_time_sig_numerator, gs_time_sig_denominator

Default: 4/4

gs_quantize <int> Set grain size quantization notesubdivision. Valid values: 1, 2, 4, 8, 16, 32, 64, 128

Calculation identical to quantize.

Stores gs_quant_note. Recalculates gs_quant_grid_ms if BPM > 0.

Default: 16 (1/16 note)

gs_quant <float> Set grain size quantization amount.

Range: 0.0 to 1.0

Blend between free grain_size and quantized grid.

Applied in perform cycle:

quant_grain_size_sec = gs_quant_grid_ms / 1000.0

quantized_grain_size = grain_size × (1 - gs_quant_amount) + quant_grain_size_sec × gs_quant_amount

Result assigned to scheduler->grain_size.

Requires: gs_quant_amount > 0, bpm > 1.0, gs_quant_grid_ms > 0

Default: 0.0 (no quantization)

Delay Time Quantization

Snaps grain delay time to rhythmic grid.

delay_timesig <symbol> Set time signature for delayquantization.

Format: "numerator/denominator"

Validation and parsing identical to timesig.

Stores: delay_time_sig_numerator, delay_time_sig_denominator

Default: 4/4

delay_quantize <int> Set delay quantization note subdivision. Valid values: 1, 2, 4, 8, 16, 32, 64, 128

Calculation identical to quantize.

Stores delay_quant_note. Recalculates delay_quant_grid_ms if BPM > 0.

Default: 16 (1/16 note)

delay_quant <float> Set delay quantization amount.

Range: 0.0 to 1.0

Blend between free delay_time and quantized grid.

Applied in perform cycle:

quant_delay_sec = delay_quant_grid_ms / 1000.0

quantized_delay = current_delay × (1 - delay_quant_amount) + quant_delay_sec × delay_quant_amount

Result assigned to grain_delay->delay_time.

Requires: delay_quant_amount > 0, bpm > 1.0, delay_quant_grid_ms > 0

Default: 0.0 (no quantization)

Pattern Cycle

pattern_cycle <N/D> <N/D> ... sets the length of the TidalCycles pattern cycle as musical durations at the
detected BPM (e.g. pattern_cycle 4/4 3/8 = 2.750 s at 120 BPM). It uses the same grid math as the quantize
messages but drives a separate, free-running clock for the pattern step-sequencer. See PATTERNS.

# PITCH & SPEED

pitch_mode 0|1|2|3|4|5 Six pitch modes control relationship between speed parameter and
playback rate.

Mode 0 - OFF (default)

speed <float>

Speed parameter = playback rate directly.

speed_range <min> <max> active if enabled.

Formula: final_speed = sample_param_range(speed_range, speed_inlet)

Mode 1 - SEMITONES

Speed parameter = base pitch.

Fixed semitone transposition applied.

speed_range bypassed.

Formula: final_speed = speed * 2^(semitones/12)

pitch_semitones <-24 to 24>

Example: pitch_semitones 7 = perfect fifth up

Mode 2 - RANGE

Speed parameter = base pitch.

Random semitone shift from range.

speed_range bypassed.

Formula: final_speed = speed * 2^(random_semitones/12)

Messages, see PARAMETER RANGES & MODULATION for generators:

pitch_range <min> <max>

pitch_rand_type <generator>

Example:

pitch_range -12 12 (±1 octave)

Mode 3 - SCALE

Speed parameter = base pitch.

Random note from scale/list of semitones (quantized pitch).

speed_range bypassed.

Formula: final_speed = speed * 2^(scale_note/12)

pitch_scale <note1> <note2> ... <noteN>

Example:

pitch_scale 0 2 4 5 7 9 11 (major scale)

Up to 128 notes per scale. pitch_scale writes the ACTIVE scale SLOT (slot 0 unless selected
otherwise) — sixteen slots per destination plus root/rotate modulation live in SCALE SLOTS and
MODULATING THE SCALE.

Mode 4 - MIDI

Speed parameter = base pitch (assumed C4 = 60).

MIDI note input transposes from base.

speed_range bypassed.

Formula: final_speed = speed * 2^((midi_note - 60)/12)

MIDI input via inlet 19 (1-127, 0 = inactive)

Final speed clamped: -4.0 to 4.0 (±48 semitones / 4 octaves)

MIDI mode "latches" to the last valid note until pitch_mode changes. Switching back to pitch_mode 4 will resume with this latched note.

Mode 5 - PATTERN

Speed parameter = base pitch.

Scale degrees sequenced by a TidalCycles mini-notation pattern (see PATTERNS), stepped on the
BPM-locked pattern cycle clock. Each pattern token is a scale DEGREE indexed into pitch_scale, with
octave wrap: on an N-note scale, degree N is the root one octave up (+12), degree N+1 the second
degree up an octave, and so on (negative degrees wrap downward).

speed_range bypassed.

Formula: final_speed = speed * 2^(scale_degree_semitone/12)

Load a scale first, then send a pitch pattern (this sets pitch_mode 5 automatically):

pitch_scale 0 2 4 5 7 9 11
pattern pitch < 0 4 7 >              (one degree per cycle: root, fifth, octave)
pattern pitch [ 0 1 2 3 4 5 6 7 ]   (a run up the scale into the next octave)

Pitch is applied per grain, reading the current cycle step, so it is tempo-locked (not grain-locked):
sparse grains can skip steps, dense grains re-trigger the same step. A rest (~) holds the previous
note. pattern_clear pitch returns to pitch_mode 0 (OFF). With no pitch_scale loaded the result is
unison until one is sent.

Fine Tune

pitch_fine <cents> applies an overall ±50-cent (±half-semitone) detune ON TOP of whatever the pitch mode
produces — a Moog-style fine knob. It is sampled per grain and is a modulation TARGET, so it can drift or
wobble:

  pitch_fine 7                            slightly sharp (7 cents)
  param_range pitch_fine -0.5 0.5
  rand_type sine pitch_fine               slow chorus-like detune

The dedicated pitch_fine message takes CENTS (−50..50); the param_range target is in SEMITONES (−0.5..0.5,
where ±0.5 = ±50 cents). pitch_fine 0 is a no-op.

Two pitch destinations

ligase~ has TWO independent pitch destinations: the GRAIN pitch (this section — drives playback speed) and
the SMEAR/resonator pitch (see SMEAR > Smear Pitch — drives the allpass center frequency). Each has its own
source (off / semitone / scale / MIDI / pattern) and its own fine tune (pitch_fine / smear_pitch_fine), so you
can, for example, run a scale-pattern on the smear while playing the grains live over MIDI.

Channel-Aware MIDI Routing

The MIDI signal inlet (inlet 19) carries a bare note number with no channel. To play the two destinations
from one keyboard and route by channel, use the MESSAGE form, fed from a Pd [notein] (note / velocity /
channel):

  midi <note> [vel] [channel]

A note is routed to a destination when its channel matches that destination's channel:

  midi_channel <grain_ch> <smear_ch>      set both at once (default grain 1, smear 2)
  pitch_channel <ch>                      grain channel only
  smear_pitch_channel <ch>                smear channel only

  - Same channel for both  => one note drives BOTH  => UNISON.
  - Different channels      => each follows only its own channel => SEPARATE.
  - A note on a channel no destination listens to is dropped.

Velocity is accepted (for [notein] compatibility) but not used for pitch. Valid note 1..127, channel 1..16.
Wire [notein] -> [pack f f f] (note, velocity, channel) -> a "midi $1 $2 $3" message box.

Backward compatibility: the inlet-19 signal MIDI (pitch_mode 4) keeps working exactly as before. The first
time a `midi` message routes a note to the grain channel it takes over the grain destination (one source of
truth) and the inlet is ignored; if you never send a `midi` message, nothing changes.

Chordal Polyphony (POLY mode)

POLY mode plays a CHORD as N simultaneous pitch transpositions of the SAME grain stream from the ONE shared
playhead — not independent playheads. It is orthogonal to everything else: the playhead, splice, SOS, and
recording paths are untouched; only the per-voice pitch of the grains changes. (For truly independent
material/playheads per voice, instantiate parallel ligase~ objects.)

  poly 1                                  enable POLY (default 0 = mono)
  pitch_mode 4                            REQUIRED: POLY transpositions only apply in MIDI pitch mode
  chord 60 64 67                          seat a C-major triad (root/third/fifth) in the voice pool
  midi 60 100 1                           append note 60 (vel>0) to the grain-channel voice pool
  midi 64 0 1                             note-off: velocity 0 removes note 64 from the pool

When POLY is on, every grain-trigger tick spawns one grain per active voice, each at that voice's playback
speed (2^((note-60)/12) x the base speed). Voices enter the pool via the channel-aware `midi` message on the
grain channel (vel 0 = note-off) and/or the `chord` list (which replaces the whole pool at once). The pool
holds up to 8 notes (MAX_VOICES); a 9th distinct note STEALS THE OLDEST note in the pool. Re-sending a note
already in the pool just refreshes its age (no duplicate). The SMEAR/resonator pitch destination stays MONO
(last-note-wins) — POLY only multiplies the grain pitch.

Grain-pool budget (important): all voices share the soft grain cap `max_grains` (default 4). Because the
per-voice loop fills grain slots in order, a chord at the default cap can starve its later voices (a triad may
render as 1-2 notes). Raise the cap for POLY so every voice keeps density, e.g. `maxgrains 32`; the hard
ceiling is pool_size (from ligase.conf). CPU scales with the live-grain count, i.e. roughly N x the mono cost
for an N-note chord at the same per-voice density.

Backward compatibility: with poly 0 (the default) the grain-trigger sites take the single-call scalar path and
the `midi` grain-channel write is the unchanged last-note-wins scalar — bit-identical to the pre-POLY external.

# SCALE SLOTS

Each pitch destination (GRAIN and SMEAR) holds SIXTEEN scale slots, A-P: A-D are the primary row,
E-P two six-slot banks for progressions and set-lists. One slot per destination is ACTIVE — it is
the scale every lookup reads (SCALE mode's stochastic pick, the PATTERN degree stepper, and, on the
smear side, the resonator bank's size and tuning).

  pitch_scale_slot <0-15>                 select the grain-side active slot
  smear_pitch_scale_slot <0-15>           select the smear-side active slot
  pitch_scale_to <slot> <semis...>        write a slot WITHOUT selecting it (no semis = clear)
  smear_pitch_scale_to <slot> <semis...>  same, smear side
  pitch_scale / smear_pitch_scale         (unchanged) write the ACTIVE slot

Slot 0 IS the legacy scale: with no slot messages ever sent, pitch_scale writes slot 0 and slot 0
sounds — every pre-slot patch behaves identically. Selecting a slot is a lookup-table swap applied
at block rate: inherently click-free (grains already sounding keep their frozen pitch; new grains
pick from the new slot). On the smear side a slot switch retunes/resizes the resonator bank on the
next block (the bank is dry while the active slot is empty).

  pitch_scale_to 0 0 4 7                  I    (C major triad)
  pitch_scale_to 1 5 9 12                 IV
  pitch_scale_to 2 7 11 14                V
  pitch_scale_slot 1                      move to the IV chord — no click, no retyping

`pitch_scale_slot` / `smear_pitch_scale_slot` are also modulation targets (stepped): see
MODULATING THE SCALE. Empty slots are skipped by the text export (count 0), so surfaces with a
few slots stay compact.

# MODULATING THE SCALE

The harmonic layer makes the scale itself addressable by every modulation system. Six per-block
destinations exist in BOTH the param_range/pattern vocabulary (param_range / rand_type / pattern)
and the matrix destination table (matrix_connect):

  scale_root / smear_scale_root            semitone offset, clamp ±24, applied AFTER degree lookup
  pitch_scale_slot / smear_pitch_scale_slot  STEPPED slot select (value rounds to 0-15)
  scale_rotate / smear_scale_rotate        STEPPED degree-index offset, wraps into the scale

SCALE ROOT — transposition as a destination. The offset joins after the degree lookup, so the
scale SHAPE is untouched: the polygon rotates. With scale_root_quant 1 (the default) the APPLIED
offset rounds to integer semitones — a wandering root wanders IN-KEY; quant 0 frees it into
continuous drift. The smear root also shifts the whole resonator bank (the chord transposes with
the key). The Coltrane payoff:

  matrix_connect lorenz1 scale_root 4     the key wanders with the weather, quantized in-key

SCALE ROTATE — modal interchange. Rotating a major scale's degree indexing yields its modes
without changing the pitch set (no octave carry — octaves live in the degree lists). It offsets
the stochastic pick and the PATTERN stepper; it deliberately does not touch the resonator bank
(reordering a static bank's voices changes nothing audible).

SLOT SELECT — chord progressions on the cycle clock. The slot destinations are stepped (values
round to 0-15), which makes progressions one pattern away. The Giant Steps move — three slots a
major third apart, cycled by the clock:

  pitch_scale_to 0 0 4 7
  pitch_scale_to 1 4 8 11
  pitch_scale_to 2 8 12 15
  pattern pitch_scale_slot 0 1 2          steps the key every third of the pattern cycle

An LFO sweeping slots (matrix_connect sine1 pitch_scale_slot 8) walks the set-list instead.

CAPTURE & MORPH (schema v5). All sixteen slots per destination, the active-slot indices, roots,
quant flags and rotates are snapshot state (walker fields scale_root, pitch_scale_slot,
scale_rotate, pitch_scale_a..p, smear_* — see the SNAPSHOT EXPANDER vocabulary). They join the
pitch-side selection-tree groups (`morph_exclude pitch` / `smear_pitch`), NOT `sources`. SNAP
under an active matrix connection records the BASE, never the momentary wobble. During a morph
BLEND, scale_root interpolates as a scalar (musical to glide when quantized); slots, quant and
rotate step by argmax; and the sounding SCALE follows the stochastic source-pick rule — each
grain (grain dest) / each block (smear dest) picks ONE contributing snapshot's scale with
probability proportional to the kernel weights, so semitone values are NEVER interpolated and a
blend between different-scale snapshots never sounds an out-of-key pitch (the blend is a harmonic
crossfade DENSITY). v1-v4 export files import cleanly: their scale lands in the active slot with
root 0 — the old behavior exactly.

The scale polygon and running patterns are visible on the scope outlets: see SCOPE
(scope_tap scale / scope_tap pattern).

# DELAY

Delay effect applied to the mixed grain output before recording. Three modes available: DD-4 (analog-style), Bencina (pitch-preserving granular), and Stut (quantized rhythmic). All modes share a 9.5-second stereo circular buffer (sized to the host rate) and common control parameters (time, feedback, tone, mix). Mode-specific parameters are set via messages.

Parameters

gdelay_time <float> Set delay time.

Range: 0.0 to 9.5 seconds

Clamped to bounds.

Smoothed internally by the delay_glide parameter (see below) to prevent the pitch-zip when the delay time changes.

delay_glide <float> Glide (smoothing) time in milliseconds for DD-4 delay-time changes.

Range: 0.0 to 5000.0 ms. Default: 20 ms.

DD-4 is a single moving read tap, so changing the delay time sweeps the tap and repitches the signal; an abrupt change is heard as a zipper/click. delay_glide one-pole-smooths the delay time toward its target over the set time, turning an abrupt jump into a clean glide. It applies to BOTH message-driven and signal-inlet-driven (inlet 11 / CV) delay-time changes, so it de-zippers a control or CV sweeping the delay time. 0 = instant (no glide; the old zippery behavior). Larger = slower, smoother sweep. (Bencina does not need this — its grains capture a fixed read offset at trigger time, so a delay-time change only affects newly triggered grains, not a continuous tap. Stut has no delay tap.)

Note: delay_glide is message-controlled (there is no free signal inlet for it); it sets HOW the delay time is smoothed, while the delay time itself is what you drive from inlet 11 / a control.

Signal inlet 11: In DD-4/Bencina modes, updates delay time if value in range (0.0, 10.0]. In Stut mode (delay_mode 2) it is SIGNAL-DRIVEN too: the same 0-10 input is mapped linearly to stut_reps 1-16 (0->1, 10->16). Inlets 12 and 13 are likewise signal-driven in Stut — inlet 12 (feedback range 0-1) passes straight through to stut_reduction (0-1, same range), and inlet 13 (tone range 0-1) maps EXPONENTIALLY to stut_spacing 1-5000 ms (0->1 ms, 0.5->~71 ms, 1->5000 ms). This lets one physical control/CV per inlet drive the parameter across all three delay modes. See "Setting stut parameters" below.

gdelay_feed <float> Set feedback amount.

Range: 0.0 to 1.0

0.0 = single echo

1.0 = infinite regeneration

Clamped to bounds.

Feedback applied after tone filter in feedback loop (DD-4 and Bencina modes only; inactive in Stut mode).

Signal inlet 12: In DD-4/Bencina modes, updates feedback if value in range (0.0, 1.0]. In Stut mode (delay_mode 2), controls stut_reduction (gain decay per repeat, range 0.0-1.0).

gdelay_tone <float> Set tone character via one-poleIIR low-pass filter in feedback loop (DD-4 and Bencina modes only; inactive in Stut mode).

Range: 0.0 to 1.0

0.0 = dark, heavily filtered (analog-style decay)

1.0 = bright, no filtering (digital-style)

Clamped to bounds.

Filter coefficient applied directly: filtered = tone × delayed + (1 - tone) × lpf_state

Higher values pass more high frequencies. Lower values create darker, warmer repeats.

Signal inlet 13: In DD-4/Bencina modes, updates tone if value in range (0.0, 1.0]. In Stut mode (delay_mode 2), controls stut_spacing (repetition spacing in ms, range 1.0-5000.0).

gdelay_mix <float> Set dry/wet mix.

Range: 0.0 to 1.0

0.0 = 100% dry (grain output only, no delay)

1.0 = 100% wet (delayed signal only)

Clamped to bounds.

Output calculation: output = dry × (1 - mix) + wet × mix

Signal inlet 14: updates if value in range (0.0, 1.0], else keeps current value. Dry/wet mix; active in all delay modes.

gdelay_clear  Clear delay buffers and filterstate.

Zeros buffer_left, buffer_right, lpf_state_left, lpf_state_right.

Immediate silence in feedback loop.

Delay Modes

delay_mode <0|1|2>  Select delay algorithm.

DD-4 Mode (delay_mode 0)

Default mode. Classic analog-style delay with a single read head at a fixed distance behind the write head. Feedback and tone filter operate in the feedback loop.

Architecture: input + feedback is written to the buffer. A single read head reads at write_pos minus delay_time with linear interpolation. The delayed signal passes through a one-pole IIR low-pass filter (the tone control), then feedback is calculated from the filtered output.

Active inlets: all four (time, feedback, tone, mix).

Delay time is smoothed internally (coefficient 0.001, approximately 20ms transition at 48kHz) to prevent clicks during time changes.

Bencina Mode (delay_mode 1)

Pitch-preserving granular delay based on the Ross Bencina "tapped delay line" architecture. Instead of a single read head, spawns up to 32 micro-grains inside the delay buffer. Each grain captures its OWN read position into the delay line — scattered behind the base delay by a random amount up to one grain length (scaled by bencina_spread) — and reads from there at unity playback rate. The per-grain position scatter is what makes it a diffuse granular CLOUD rather than a plain delay: overlapping grains read different points of the recent past instead of the identical delayed sample.

Two character controls (both leave the default grainy sound untouched):
- bencina_spread <0-1> (default 1.0 = full scatter): the cloud's graininess/diffusion. Summing decorrelated grains makes the level fluctuate at the grain rate (the grainy texture); lower = tamer/smoother (0 = coherent grains, with the stereo cloud then coming from bencina_pan only). Skew (the envelope skew) is the other main texture control.
- bencina_edge <0-1> (default 0 = OFF): an optional raised-cosine round on each grain's in/out. OFF leaves the envelope and its skew edges exactly as-is — including the onset "clickiness" at steep skew, which is a usable character. Raise it to de-click (e.g. for Gaussian/Exponential windows, which don't reach zero at their edges) at the cost of that bite.

Pitch: playback rate is fixed at 1.0 — Bencina does NOT transpose. Pitch in ligase is handled deliberately elsewhere (the allpass smear and the morphagene tape speed), so the delay stays pitch-true. The "pitch-preserving" name refers to changing the delay TIME without a Doppler sweep: new grains adopt the new delay while old ones finish, crossfading instead of sliding the tap.

Architecture: grains are triggered continuously at a fixed interval (bencina_iot). Each grain reads its captured (scattered) position, applies the main envelope shape as a window (the SAME envelope object/shape as the granular engine), and uses constant-power panning. The scatter width scales with bencina_grainsize and bencina_spread. The grain sum is overlap-normalized (so density sets texture, not level), passes through the tone filter, and the filtered output is fed back into the buffer before the write head advances. This true feedback creates recursive granulation — delayed grains get re-granulated — producing shimmer and evolving textures at higher feedback values.

Stereo cloud (bencina_pan): each grain's pan is chosen the same way the main granular engine chooses per-grain pan — from the bencina_pan modulation range, sampled per grain. It is a standard modulation target: `param_range bencina_pan <min> <max>` sets the spread (the span = stereo WIDTH, where the span sits = SKEW), `rand_type rand_1 bencina_pan` makes it random per grain. With the range off, every grain takes the BASE pan, which rides the pan inlet (inlet 22) / `pan` message — so the pan knob centers the cloud, exactly like every other modulatable param. Examples: `param_range bencina_pan 0 1` = full random L–R cloud; `param_range bencina_pan 0 0.35` = a narrower cloud skewed left; range off + `pan 0.15` = the whole (mono) cloud sitting left. Pan does NOT affect the time scatter or pitch — it's purely where each grain sits in the field. (To run the cloud pan independent of the pan inlet, enable its modulation range — modulation overrides the inlet, as with any param.)

Wet level / saturation: the wet output is driven through a makeup gain (bencina_level, default 6.0) into a tanh soft-limit. The makeup sits well above unity to compensate for the position scatter, which sums grains incoherently and is therefore quieter than a coherent stack; tanh then bounds the result to ±1 (rounding the transient peaks) so it can never hard-clip, even when high feedback drives the buffer hot. bencina_level rides the loudness: lower for a subtler wet, higher for a louder/more saturated one (tanh keeps it bounded either way) — e.g. 2 ≈ subtle, 6 ≈ default/prominent, 12 ≈ hot/saturated. Pushed hard, expect gentle analog-style saturation in the tails rather than digital clipping. The feedback path itself is NOT boosted or limited, so the recirculation loop gain stays = feedback (stable for feedback < 1).

Active inlets: all four (time, feedback, tone, mix). Time controls the distance of the moving tap from the write head. Feedback and tone operate in the grain output path.

bencina_iot <float>  Spacing between grain triggers (milliseconds). Controls grain density.

Range: 1.0 to 1000.0 ms. Default: 50.0 ms (20 grains/sec).

Lower values = denser cloud, more overlap between grains. For rhythmic effects, set to a musical subdivision (e.g. 125ms = 1/8 note at 120 BPM).

bencina_grainsize <float>  Individual grain length (seconds).

Range: 0.001 to 2.0 seconds. Default: 0.1 seconds (100ms).

Longer grains = smoother, more blurred output. Shorter grains = more granular texture. Applied to future grains only (existing grains keep their size).

bencina_wrap <0|1>  Buffer wrap mode.

0 = global (default): a straight grain delay — each grain reads at write_head − (delay_time × sr), wrapping within the whole delay buffer. 1 = loop: confines each grain's read to the most recent stretch of the delay buffer (a short looping window ≈ the current splice length), creating phasing/layering. Both wrap within the delay line itself, in delay-buffer space — not reel coordinates. (Bencina needs delay_time > 0 to produce any wet; at delay_time = 0 there is no offset to read from.)

Applied to all existing and future grains.

bencina_clear  Clear all active bencina grains. Immediate silence. Resets active grain count to 0.

Stut Mode (delay_mode 2)

Quantized rhythmic delay. Unlike DD-4 and Bencina which run continuously, Stut is event-triggered via the stut message. When triggered, it captures the current buffer write position and schedules N repetitions at fixed time intervals, each with exponentially decaying gain.

Architecture: the stut message captures the buffer write position at the moment of triggering. It then schedules stut_reps grains (up to MAX_STUT_REPS = 16 per trigger), each with a countdown timer (i × spacing) and a gain value (gain_reduction ^ i). During processing, each grain counts down; when its timer reaches zero it replays the captured slice — the `spacing`-length stretch of audio just before the trigger — scaled by its gain, with a short edge fade to avoid clicks. This produces decaying rhythmic repeats (a stutter) of the captured moment, not single-sample clicks.

Polyphony / layering: grains are allocated from a 64-voice pool (MAX_STUT_GRAINS) into FREE slots, so re-triggering stut while a previous stutter is still ringing LAYERS the new echo train on top of the old one (each train keeps its own captured slice, timing and decay) rather than clobbering it. Banging the trigger therefore stacks overlapping stutters up to the 64-voice limit; it does not restart or click. When the pool is saturated, further repeats in the newest train are dropped (oldest grains keep playing to their natural end).

Active inlets: 11, 12, 13, mix. In Stut mode the three delay inlets are SIGNAL-DRIVEN and reused for stut, mapping their delay-native range to the stut parameter so one physical control/CV per inlet works across all three modes:
  - Inlet 11 (delay-time range 0-10) -> stut_reps 1-16, linear.
  - Inlet 12 (feedback range 0-1)    -> stut_reduction 0-1, direct (same range).
  - Inlet 13 (tone range 0-1)         -> stut_spacing 1-5000 ms, exponential (0->1 ms, 1->5000 ms).
The MESSAGES stut_reps/stut_reduction/stut_spacing set the parameter in its OWN native units (a raw count, 0-1, and milliseconds), and apply when the inlet isn't driving (unconnected/near-zero in headless 1; headless 0 honors the literal inlet value). Each repeat replays the captured slice with per-repetition gain decay — there is no feedback loop or tone filter in the stut signal path. The mix parameter controls dry/wet blend as usual. stut_reps/reduction/spacing are also param_range modulation targets (stut_reps; reduction/spacing via the gdelay_feed/gdelay_tone ranges).

stut  Trigger a stut sequence. Captures the current splice boundaries and write position. Schedules repetitions immediately. If delay quantization is active (delay_quant > 0, BPM running), uses the quantized grid spacing instead of stut_spacing.

stut_reps <int>  Number of repetitions per trigger.

Range: 1 to 16. Default: 4. See "Setting stut parameters" below.

Setting stut parameters (signal inlets + messages)

stut_reps, stut_reduction and stut_spacing are SIGNAL-DRIVEN on inlets 11/12/13 (so a physical
control/CV can drive them), and can also be set by message. Both work; the inlet wins whenever it
is driving (the standard inlet behaviour).

Inlet mapping (inlets 11/12/13 keep their delay-native input range; in Stut that range is mapped
to the stut parameter, so ONE control per inlet serves DD-4, Bencina and Stut):

  Inlet 11  delay-time range 0-10   -> stut_reps      1-16    (linear: 0->1, 10->16)
  Inlet 12  feedback range   0-1    -> stut_reduction 0-1     (direct — same range)
  Inlet 13  tone range       0-1    -> stut_spacing   1-5000ms (exponential: 0->1ms, 0.5->~71ms, 1->5000ms)

The exponential map on inlet 13 keeps the musically useful spacings spread across the control
instead of bunched at one end. (Inlet 12 needs no remapping — feedback and reduction are both 0-1.)

Messages take the parameter's OWN native units, not the mapped range:
  stut_reps <1-16>            stut_reduction <0.0-1.0>            stut_spacing <1-5000 ms>
A message holds whenever the inlet isn't driving — i.e. the inlet is unconnected/near-zero in
headless 1, or you simply aren't patching it. (In headless 0 the literal inlet value is honored,
so a control at 0 maps to reps 1 / reduction 0 / spacing 1 ms.) stut_reps/reduction/spacing are
also param_range modulation targets (reduction/spacing via the gdelay_feed/gdelay_tone ranges).

Note: stut_length and stut_length_mode have no dedicated inlet, so they remain message/modulation
controlled.

This is independent of headless mode — being message-only, stut parameters behave identically in
headless 0 and 1, and you never need to connect or disconnect inlets 11/12/13 for stut.

IMPORTANT — what makes stut audible. The stut wet only reaches the output through the
granular/delayed monitor path, which is gated by TWO controls in series:
  - gdelay_mix: dry granular vs stut wet WITHIN the delayed signal. Toward 1 = more stut.
  - sos: raw input vs the granular/delayed signal at the final monitor mix (Morphagene mode,
    the default: sos 1 = 100% raw input, sos 0 = 100% granular/delayed). At the DEFAULT sos
    0.5, half the output is raw input (no stut at all) and the delayed half is diluted — so
    changing reps/reduction/spacing barely moves the sound. To clearly hear stut and its
    parameters, set sos LOW (toward 0) so the granular/delayed path dominates the monitor.
So if stut "does nothing" audibly, check sos before anything else: a high sos drowns it in
dry input. (This is independent of whether the stut messages took — the trigger prints
`stut triggered (reps N, reduction R, spacing S ms, slice L ms)` so you can confirm the
values the engine actually received.)

Recommended initialization (headless 1):

  delay_mode 2         select Stut
  sos 0                monitor the granular/delayed path (NOT raw input) — without this the
                       stut is buried under dry input regardless of gdelay_mix
  gdelay_mix 1         within the delayed path, hear the wet (mix 0 = dry granular only)
  stut_reps 4          repeat count (default 4)
  stut_reduction 0.7   gain decay per repeat (default 0.5; higher = more repeats audible)
  stut_spacing 125     ms between repeats (default 62.5)
  stut_length 30       slice length per repeat; < spacing gives audible gaps between repeats
  ... then send `stut` to trigger the sequence.

Do not enable param_range stut_reps unless you want the count modulated; an enabled range
overrides both the stut_reps message and inlet 11.

stut_reduction <float>  Gain reduction factor per repeat. Each repetition's gain = reduction ^ repetition_number.

Range: 0.0 to 1.0. Default: 0.5.

0.0 = maximum reduction (only the first repeat is audible). 0.5 = each repeat is half the volume of the previous. 1.0 = no reduction (all repeats at full volume).

Example with 4 reps, reduction 0.5: gains are [1.0, 0.5, 0.25, 0.125].

stut_spacing <float>  Base spacing between repetitions (milliseconds).

Range: 1.0 to 5000.0 ms. Default: 62.5 ms (1/16 note at 120 BPM).

Overridden by delay quantization grid when active (see below).

Stut Slice Length (what each repeat replays)

Each repeat replays a slice of the captured granular output. Its LENGTH is independent of the
spacing, so: length < spacing = gated repeats (gaps); length = spacing = gapless; length >
spacing = overlapping/denser. The length comes from one of two modes:

stut_length_mode <0|1>  0 = independent length (default, uses stut_length / its quantization);
1 = tie the slice length to the current grainsize (one granular grain per repeat).

stut_length <float>  Independent slice length in milliseconds (mode 0). Range 1.0–5000.0.
Default: 62.5 ms.

stut_length_quantize <1|2|4|8|16|32|64|128>  Note subdivision for a tempo-locked slice length
(computed from BPM, same pattern as delay_quantize). Default: 1/16.

stut_length_quant <0-1>  Blend toward the quantized note length: 0 = use stut_length (ms) as-is,
1 = use the note grid, in-between = blend. Active only when BPM > 1 and the clock is running.

Example: at 120 BPM, stut_length_quantize 8 + stut_length_quant 1 → each repeat replays a
250 ms (1/8-note) slice, regardless of stut_spacing.

Stut and Delay Quantization

The stut trigger integrates with the delay quantization system. When all three conditions are met — delay_quant amount > 0, BPM > 1.0, and clock running — the stut trigger uses delay_quant_grid_ms as the spacing between repetitions instead of stut_spacing. This locks stut repeats to the tempo grid.

Example: at 120 BPM with delay_quantize 16 and delay_quant 1.0, stut repeats are spaced at 125ms (1/16 note). Changing delay_quantize to 8 spaces them at 250ms (1/8 note).

Note: delay quantization also affects DD-4 delay time (blending toward the grid), but does not affect Bencina grain spacing. Use bencina_iot directly for Bencina timing.

Per-Mode Parameter Summary

Inlet (range)    DD-4            Bencina         Stut (mapped from the same input range)
inlet 11 (0-10)  delay time      delay time      -> stut_reps 1-16 (linear)
inlet 12 (0-1)   feedback        feedback        -> stut_reduction 0-1 (direct)
inlet 13 (0-1)   tone            tone            -> stut_spacing 1-5000 ms (exponential)
gdelay_mix       Active          Active          Active
delay_quant      Blends time     No effect       Overrides spacing
Triggering       Continuous      Continuous      Event (stut msg)
Read pattern     Fixed offset    Moving tap      Captured position
Max voices       1               32              64 (16 per trigger)

- Inlets 11/12/13 are signal-driven in all three modes and keep ONE input range each (the delay range 0-10 / 0-1 / 0-1). DD-4 and Bencina use that range directly (time/feedback/tone); Stut maps the same range to reps/reduction/spacing, so a single physical control/CV per inlet works in every mode. The stut_reps/reduction/spacing messages address the parameters in their native units (count / 0-1 / ms) and apply when the inlet isn't driving.

All three modes share the circular delay buffer and write head. Mode-specific parameters only affect their respective mode. Switching modes via delay_mode takes effect immediately.

# SMEAR (ALLPASS SPECTRAL EFFECT)

A cascade of tunable second-order allpass sections applied to the grain output. Each section delays frequencies near its center by different amounts; cascading many of them smears transients and disperses the spectrum — a rich spectral delay that can blur, morph, and animate the sound. With feedback engaged the cascade becomes a resonator with metallic, comb-like tones. It is a time-domain effect (a handful of multiply-adds per sample) — light on CPU and unconditionally stable.

Smear is a monitoring effect — it is not recorded into the reel. Applied after delay and before distortion in the effects chain.

Signal inlet 15: smear mix (0.0-1.0). 0.0 = dry bypass. 1.0 = full wet. Dry/wet blend of the input and the allpass output.

smear_frequency <Hz>  Center frequency where each allpass section's group delay peaks — the focus of the smear.

Range: 20 Hz to ~0.45 × sample rate. Default: 800 Hz.

Sweeping it moves the smeared/resonant region up and down the spectrum.

smear_resonance <0-0.999>  Pole radius of the allpass sections — the sharpness of the smear.

Default: 0.7.

Higher values concentrate the group delay near the center frequency, giving a tighter, more pronounced (and, with feedback, more metallic) character. Lower values give a gentler, broader smear.

smear_stages <0-48>  Number of allpass sections cascaded — the depth of the smear.

Default: 12.

More stages = longer dispersion and stronger transient smearing. 0 = no effect (the wet path passes through).

smear_feedback <-0.99 to 0.99>  Global feedback around the whole cascade.

Default: 0.

0 = pure dispersive smear. Toward ±0.99 the cascade rings into a resonator/comb (metallic tones). The allpass cascade is exactly unity-gain, so any value below 1 stays stable. Negative feedback shifts the resonant tuning.

Resonator mode (playing it as a pitched bell)

At high feedback (≈0.9-0.99) the smear becomes a pitched, bell-like resonator: the loop rings at the frequencies where its total phase wraps, producing a comb of partials.

- smear_frequency tunes the pitch — sweep it to play the resonator up and down the spectrum.
- smear_stages sets the comb density: more stages lower the fundamental and pack in more partials.
- smear_resonance sharpens each partial (longer, more focused ring).
- Negative feedback selects a different (more hollow/odd-harmonic) set of partials.

Because the wet output is bounded (unity-gain allpass), the resonator can be driven straight into the Moog filter without the level running away — the filter then shapes the ring.

Smear Pitch (resonator pitch)

At high feedback the smear rings at smear_frequency — so it can be PLAYED like a resonator. Rather than
setting a raw Hz, drive it from a musical note. This is a second pitch destination, fully independent of the
grain pitch:

  smear_pitch_source <0-4>   0=off (manual smear_frequency owns it) / 1=semitone / 2=scale / 3=midi / 4=pattern

Sources:
  - semitone:  smear_pitch_semitones <n>            fixed transpose vs the reference (auto-selects source 1)
  - midi:      smear_note <note> [ref_note] [ref_hz] one explicit note (or route MIDI by channel — see
               PITCH & SPEED > Channel-Aware MIDI Routing). Auto-selects source 3.
  - scale:     smear_pitch_scale <semitones...> + smear_pitch_rand_type <gen>   random notes from a scale
  - pattern:   smear_pitch_scale <scale...> then  pattern smear_pitch <tokens...>   mini-notation (see PATTERNS)

Mapping: hz = ref_hz * 2^((note − ref_note)/12). The default reference is note 69 = 440 Hz (A4), so note 60 ≈
261.6 Hz (middle C). smear_note can re-reference: smear_note 60 0 261.63 makes note 0 = middle C. The resulting
Hz is bounded to [20, 0.45·samplerate].

Precedence: when smear pitch is enabled (source ≠ off) the note OWNS smear_frequency for the block, and the
manual smear_frequency message / smear_frequency_range modulation are BYPASSED — exactly as engaging a grain
pitch mode bypasses the speed range. smear_pitch_source 0 hands control back to the manual/modulated frequency.

Fine tune: smear_pitch_fine <cents> adds a ±50-cent detune (modulatable: param_range smear_pitch_fine, in
semitones) on top of the smear note — the same fine-tune role the grains get from pitch_fine.

Example — arpeggiate the resonator (root / fifth / octave, one per cycle):

  smear_resonance 0.9
  smear_feedback 0.85
  smear_pitch_scale 0 2 4 5 7 9 11
  pattern smear_pitch < 0 4 7 >

Resonator Bank (smear_mode 1 — grains excite a tuned body)

A distinct smear mode: instead of ONE resonator voice, a BANK of up to 16 tuned voices — each a full
smear allpass cascade — is excited by the granular+delay output. The grains become the EXCITER
(broadband dust, transients, clouds) and the bank becomes the INSTRUMENT: strike the body with grain
dust and it rings a chord. Classic sympathetic-string / excited-physical-body topology.

  smear_mode <0|1>   0 = single voice (default — the identical path described above); 1 = bank.
                     Hard switch, no crossfade (the exciter is continuous, so the seam is small).

Tuning — the chord IS the smear-pitch scale. The bank has no pitch path of its own: load
smear_pitch_scale <semitones...> and each scale degree becomes one voice, tuned by the same
hz = ref_hz * 2^(semitone/12) mapping as the single resonator (A440 reference by default;
smear_pitch_fine detunes the whole body). Voice count = the scale's note count (capped at 16).
Change the scale and the bank re-tunes on the next block. With no scale loaded the bank has zero
voices and passes the signal through dry (the smear_mode message reminds you).

  smear_pitch_scale 0 4 7 12   then   smear_mode 1
  -> a 4-voice body ringing root / major 3rd / 5th / octave above 440 Hz.

Bank controls (shared across all voices in v1):

  smear_bank_mix <0-1>            Bank dry/wet. Independent of inlet 15 (which stays the SINGLE
                                  voice's mix). 0 = dry bypass (default).
  smear_bank_feedback <-0.99-0.99> Per-voice feedback — sustain/ring length of the body.
  smear_bank_resonance <0-0.999>  Pole radius shared by all voices. For TIGHT tuning use a high
                                  value (≈0.995-0.999): the closer to 1, the more exactly each
                                  voice's ring lands on its note; lower values loosen the comb
                                  around the tuned center (a broader, more dispersed body).
  smear_bank_stages <0-48>        Allpass sections per voice (default 8 — lower than the single
                                  voice's 12 to keep N*stages in CPU budget). More stages = denser
                                  partial comb per voice, more CPU.

Gain staging: each voice keeps the unity-gain cascade topology (|feedback| < 0.99 is unconditionally
stable) and the voice sum is pre-scaled by 1/N, so a full chord never clips on correlated transients;
the final output clamp remains the hard backstop. Like the single smear, the bank is a monitoring
effect — never recorded into the reel — and sits at the same point in the chain (after delay, before
distortion). smear_mode 0 returns to the single voice, bit-exact with the pre-bank behavior.

Recipe — grain dust ringing a minor chord:

  smear_pitch_scale 0 3 7
  smear_mode 1
  smear_bank_mix 0.7
  smear_bank_feedback 0.92
  smear_bank_resonance 0.998

# DISTORTION

Multi-mode waveshaping with pre/post filters and oversampling. Two position modes: per-grain (experimental) or post-mix (default). In post-mix mode, distortion is applied only AFTER recording as a monitoring effect.

Control

distortion_enable 0|1 Enable/disable distortion processing.

0 = bypass (passthrough)

1 = enabled

Set via message only (distortion has no signal inlet).

distortion <float> Set distortion drive amount.

Range: 0.0 to 1.0

Maps to current_drive via lookup curve.

0.0 = clean (minimal drive)

1.0 = saturated (maximum drive)

distortion_position 0|1 Set distortion placement in signal chain.

Mode 0: Per-grain (experimental)

Applied to each grain sample during synthesis, before envelope. No oversampling. Uses grain_distortion_process_sample().

Signal chain: read → distort → envelope → pan → mix

Distortion IS recorded in this mode (affects grain output before recording).

Mode 1: Post-mix (default)

Applied to final output AFTER all recording operations.

Uses oversampling. Uses grain_distortion_process_block().

Signal chain: grains → delay → [RECORDING] → smear → distortion → Moog → dac~

Distortion NOT recorded (monitoring/output only).

This placement prevents distortion artifacts from accumulating in overdub recordings.

Default: 1 (post-mix)

distortion_oversample_factor 1|2|4|8 Set oversampling multiplier for post-mix mode. Valid values: 1, 2, 4, 8 ?

Posts error if invalid value provided.

Higher values reduce aliasing at cost of CPU.

4x provides good quality/performance balance.

Per-grain mode ignores this setting (no oversampling).

Default: 4

Waveshaper Modes

dist_waveshaper_mode 0|1|2|3|4 Select waveshaping algorithm.

Mode 0: tanh (default)

Pure hyperbolic tangent.

Aggressive odd harmonics. Warm, tube-like saturation.

Mode 1: arctan

Pure arctangent.

Smooth odd harmonics. Gentler than tanh.

Mode 2: asymmetric

Different drive for positive/negative cycles.

Generates even harmonics.

Uses drive_pos and drive_neg parameters.

Mode 3: blend

Morph between tanh and arctan.

Uses dist_curve_blend parameter (0.0 = tanh, 1.0 = arctan).

Mode 4: polynomial

User-defined polynomial: output = c1×x + c2×x² + c3×x³

Uses poly_c1, poly_c2, poly_c3 coefficients.

Precise harmonic control.

Default: 0 (tanh)

dist_curve_blend <float> Blend amount for Mode 3.

Range: 0.0 to 1.0

0.0 = full tanh (aggressive)

1.0 = full arctan (smooth)

dist_drive_pos <float> Positive cycle drive for Mode2. Range: 1.0 to 20.0

Higher values = more saturation on positive peaks.

dist_drive_neg <float> Negative cycle drive for Mode2.

Range: 1.0 to 20.0

Higher values = more saturation on negative peaks.

dist_poly_c1 <float> Linear coefficient for Mode 4.

Range: -10.0 to 10.0

Controls fundamental and odd harmonics.

Default: 1.0 (linear passthrough)

dist_poly_c2 <float> Quadratic coefficient for Mode 4.

Range: -10.0 to 10.0

Controls even harmonics (asymmetry).

dist_poly_c3 <float> Cubic coefficient for Mode 4.

Range: -10.0 to 10.0

Controls additional odd harmonics.

Pre-emphasis / De-emphasis (Inner Filters)

Frequency-dependent pre-distortion boost with matched post-distortion cut. Affects harmonic content without changing overall tonal balance.

dist_emphasis_mode 0|1 Set pre-emphasis filter type.

0 = HP (high-pass): boost highs before distortion → brighter saturation

1 = LP (low-pass): boost lows before distortion → darker saturation

Argument >= 0.5 selects LP, < 0.5 selects HP.

Default: 0 (HP mode)

dist_emphasis_freq <float> Set emphasis corner frequency. Range: 100 to 5000 Hz

Higher freq = more extreme emphasis effect.

dist_pregain <float> Set pre-distortion gain stage.

Range: 0.1 to 10.0

Applied before emphasis filters and waveshaper.

1.0 = unity1.0 = drive harder

< 1.0 = soften

Outer Filters

Applied outside emphasis/waveshaper core. Shape frequency response without affecting emphasis balance.

distortion_pre_hp_freq <float> Pre-tanh highpass frequency.

Range: 30 to 500 Hz

1-pole filter (6dB/octave).

Removes DC and low mud before saturation.

distortion_pre_hp_mix <float> Pre-tanh highpass dry/wet.

Range: 0.0 to 1.0

0.0 = full passthrough (no filtering)

1.0 = full filtered

distortion_post_lp_freq <float> Post-tanh lowpassfrequency.

Range: 2400 to 10000 Hz

2-pole Butterworth (12dB/octave).

Tames high-frequency harmonics after saturation.

distortion_post_lp_mix <float> Post-tanh lowpass dry/wet.

Range: 0.0 to 1.0

0.0 = full bright (no filtering)

1.0 = full dark

distortion_notch_freq <float> Notch filter centerfrequency.

Range: 20 Hz to (sample_rate/2 - 100) Hz

Bandstop filter for targeted frequency removal.

distortion_notch_bw <float> Notch filter bandwidth.

Range: 10 to 5000 Hz

Narrower = more surgical cut.

distortion_notch_mix <float> Notch filter dry/wet.

Range: 0.0 to 1.0

0.0 = inactive (no notching)

1.0 = full notch applied

Default: 0.0 (inactive)

# MOOG LADDER FILTER

Classic 4-pole (24dB/octave) Moog ladder lowpass filter. Four cascaded one-pole stages with resonance feedback. Filter is applied only AFTER recording as a monitoring effect.

Parameters

moog_cutoff <float> Set filter cutoff frequency.

Range: 20 to 20000 Hz

Clamped to Nyquist frequency (sample_rate / 2).

Formula: f = 2 × sin(π × fc / fs)

Cutoff coefficient clamped to [0.0, 1.0] for stability.

Signal inlet 16: updates if value in range [20.0, 20000.0], else keeps current value.

moog_resonance <float> Set resonance feedback amount.

Range: 0.0 to 4.0

0.0 = no resonance (flat lowpass)

3.5+ = self-oscillation (pitched sine tone at cutoff frequency)

4.0 = maximum feedback

Feedback formula: fb = resonance × (1.0 + 0.5 × f⁵)

Nonlinear compensation prevents resonance drop at high cutoff frequencies.

Signal inlet 17: updates if value in range [0.0, 4.0], else keeps current value.

moog_mix <float> Set dry/wet mix.

Range: 0.0 to 1.0

0.0 = 100% dry (bypass, no filtering)

1.0 = 100% wet (fully filtered)

Output: output = dry × (1 - mix) + wet × mix

Signal inlet 18: updates if value in range [0.0, 1.0], else keeps current value.

moog_enable 0|1 Enable/disable filter processing.

0 = bypass (passthrough)

1 = enabled

When disabled or mix = 0.0, filter bypasses without processing.

# PARAMETER RANGES & MODULATION

Per-grain parameter variation system. Each parameter can vary within a specified range driven by one of nine continuous generator types (rand, Perlin 1D/2D, Lorenz, N-body, sphere, saw, sine, square — four instances each), or by a step-sequencer PATTERN (see PATTERNS). ~30 parameters across grain synthesis, timing/playback, delay, distortion, the smear (smear_frequency / smear_resonance / smear_stages / smear_feedback), and the modulation outlets are modulatable.

Modulation Sources

Nine continuous generator types, each with 4 independent instances (1-4), plus a step-sequencer pattern source (RAND_TYPE_PATTERN, below):

- RAND_TYPE_RAND

Basic pseudo-random. Seeded per instance. Uniform distribution [0.0, 1.0]. Fast, uncorrelated white noise variation.

- RAND_TYPE_PERLIN_1D

1D Perlin noise. Smooth continuous modulation. Coordinate advances by iot × noise_frequency_scale per grain. Decorrelated via instance offsets: [0, 1000, 2003, 3001]. Returns normalized [0.0, 1.0].

- RAND_TYPE_PERLIN_2D

2D Perlin noise. X-coordinate advances per grain, Y-coordinate = base parameter value.

Coupling between parameter value and modulation depth. Decorrelated via instance offsets: [0, 5000, 10007, 15013]. Returns normalized [0.0, 1.0].

- RAND_TYPE_LORENZ

Lorenz attractor. Bounded chaotic motion. Evolves via update iterations: int(iot × 100 × noise_frequency_scale). Capped 1-50 iterations per grain for stability. Output axis rotates per instance: 0=X, 1=Y, 2=Z (mod 3). Different initial positions per instance for decorrelation. Returns normalized [0.0, 1.0].

- RAND_TYPE_NBODY

N-body gravitational simulation (3-body problem). Bounded chaotic orbital motion.

Evolves via update iterations like Lorenz. Output mode selectable per instance (0-10):

Body positions, velocities, distances, angular momentum, total energy. Returns normalized [0.0, 1.0].

- RAND_TYPE_SPHERE

3D sphere physics simulation (STK-based). A sphere bounces within a bounded space with configurable damping and elasticity. Each instance can be "kicked" with a velocity impulse. Output mode selectable per instance (0-6): 0=Position X, 1=Position Y, 2=Position Z, 3=Velocity X, 4=Velocity Y, 5=Velocity Z, 6=Velocity Magnitude. Returns normalized [0.0, 1.0].

sphere_kick <instance> <vx> <vy> <vz>  Apply velocity impulse to sphere instance. Creates sudden, decaying motion patterns.

sphere_kick_rand <instance> <0-50>  Fire the same kick in a RANDOM unit direction × strength — the one-argument "KICK!" gesture. An event, not state: never captured by snapshots.

sphere_damping <instance> <0-1>  Set damping coefficient. Higher values = faster energy decay.

sphere_elasticity <instance> <0-1>  Set bounce elasticity. 1.0 = perfect elastic bounce. Lower values absorb energy on wall contact.

sphere_spin <instance> <-10 to 10>  SOURCE SHAPE: rotate the sphere's VELOCITY vector about the y-axis by spin × dt per update — an energy-neutral curl (speed magnitude preserved, so it cannot blow up) that composes with damping/elasticity/kick and turns a kicked sphere into an ORBIT. Audible as a periodic stereo sweep under pan_mode 2. Default 0 = off.

sphere_reset <instance>  Reset sphere to initial position and zero velocity.

sphere_mode <instance> <0-6>  Set output mode. 0-2 = position axes, 3-5 = velocity axes, 6 = velocity magnitude.

- RAND_TYPE_SAW

Sawtooth wave LFO. Unipolar ramp from 0.0 to 1.0. Phase advances by iot × noise_frequency_scale per grain trigger. Creates predictable linear sweep patterns for modulation. Shapeable readout (see SOURCE SHAPE): waveform_phase offsets it, saw_skew folds it (0 ramp up, 0.5 triangle, 1 ramp down).

- RAND_TYPE_SINE

Sine wave LFO. Unipolar 0.0 to 1.0 (mapped from bipolar sine). Phase advances by iot × noise_frequency_scale per grain trigger. Creates smooth, predictable oscillating modulation. Shapeable readout (see SOURCE SHAPE): waveform_phase offsets it.

- RAND_TYPE_SQUARE

Square wave LFO. Bipolar switching: 0.0 low, 1.0 high, high for square_pw of each cycle (default 0.5 = 50% duty). Phase advances by iot × noise_frequency_scale per grain trigger. Creates alternating on/off modulation patterns. Shapeable readout (see SOURCE SHAPE): waveform_phase offsets it, square_pw sets the duty.

- RAND_TYPE_PATTERN

Step sequencer driven by a TidalCycles mini-notation pattern (see PATTERNS) — NOT a continuous generator. Uses one of 8 pattern SLOTS (not the 4 generator instances). The loaded pattern holds a value for each step's slice of the BPM-locked cycle; the sampler reads that value (0..1) and maps it to the parameter's min..max through the usual invert/map/slew tail. Attach with `pattern <param> <tokens...>` (auto-assigns a slot) or `rand_type pattern_N <param>` (point a parameter at an already-loaded slot N, 1-8). Detach + restore the prior source with `pattern_clear <param>`.

Modulatable Parameters Include:

- Grain synthesis (sampled per grain trigger):

speed, grainsize, grainstart, amplitude, pan, maxgrains, env_skew, pitch_fine (grain fine tune, ±0.5 semitone)

- Timing / playback (sampled per DSP block):

iot (interonset time), scanrate, organize (splice select), sos (sound-on-sound)

- Effects (sampled per DSP block):

gdelay (delay time), gdelay_feed, gdelay_tone, gdelay_mix, moog_cutoff, moog_resonance, moog_mix, smear_frequency, smear_resonance, smear_stages, smear_feedback, smear_pitch_fine (smear fine tune, ±0.5 semitone)

- Distortion:

distortion (intensity), dist_emphasis_freq, dist_pregain, dist_curve_blend, dist_drive_pos, dist_drive_neg, dist_poly_c1, dist_poly_c2, dist_poly_c3

- Stut (sampled per DSP block):

stut_reps

- Bencina/PPG (sampled per DSP block):

bencina_iot, bencina_grainsize

- Bencina pan (sampled PER GRAIN, like the main pan — for the stereo cloud):

bencina_pan

- Smear / allpass (sampled per DSP block):

smear_frequency, smear_resonance, smear_stages, smear_feedback

- Modulation outlets (the four send outlets are themselves modulatable targets):

modout1, modout2, modout3, modout4

Messages

- param_range <param_name> <min> [max] Set parameter modulation range.

  - Two-argument form (single value): Disables range, sets fixed value.
Example: param_range speed 1.0→ speed fixed at 1.0

  - Three-argument form (range): Enables modulation between min and max.
Example: param_range speed 0.5 2.0→ speed varies 0.5-2.0

- rand_type <type_instance> [param_name] Set modulation source for parameter(s).

  - type_instance format: {type}_{N} where N = 1-4

  - Types: rand_1...rand_4, perlin_1d_1...perlin_1d_4, perlin_2d_1...perlin_2d_4,
lorenz_1...lorenz_4, nbody_1...nbody_4, sphere_1...sphere_4,
saw_1...saw_4, sine_1...sine_4, square_1...square_4

  - One-argument form: Sets all parameters to specified source. Example:

rand_type perlin_1d_2

  - 
Two-argument form: Sets single parameter to specified source. Example:

rand_type lorenz_3 amplitude

- 
noise
_freq <float> Set frequency scale for all 4 modulation instances.

  - 
Range:
> 0.0 (1.0 = normal speed)

  - 
Affects
Perlin coordinate advancement and Lorenz/N-body iteration count.

- 
noise
_freq_1
<float>

- 
noise
_freq_2
<float>

- 
noise
_freq_3
<float>

- 
noise
_freq_4
<float>

Set frequency scale for specific instance (1-4). Independent control of each modulation instance speed.

Lock

Sends current modulated values on outlet 9 then stops modulation.

param_lock <param1> <param2> <param3> ...

Example:

param_range pan 0.2 0.8

rand_type perlin_1d_1 pan

param_lock pan → "ligase~: pan locked at 0.547"

param_lock pan amplitude grainsize → locks all three at current values

Disables range modulation only.

Returns to inlet or last message value.

Without feedback routing:

→ Just stops modulation

→ Returns to inlet/message control

→ Values go to outlet 9 but aren't applied

With feedback routing (outlet 9 → inlet 1):

→ Stops modulation

→ Captures and applies current modulated values

→ "Locks" parameters at modulated state

N-Body Control

N-body instances have additional physics parameters (1-4):

- nbody_epsilon <instance> <value> Set softening parameter (0.001-0.1). Prevents singularities during close approaches.

- nbody_damping <instance> <value> Set velocity damping (0.0-0.1). Higher values = faster energy loss, more stable orbits.

- nbody_pump <instance> <amount> <interval> Set energy pump interval (velocity increment, iteration interval). Adds velocity periodically to sustain motion

- nbody_G <instance> <value> Set gravitational constant(0.1-10.0).

- nbody_mode <instance> <mode> Set output extraction mode (0-10).

11 output modes per instance:

0: Body 0 X position (instance 1 default)

1: Body 1 Y position (instance 2 default)

2: Body 2 X position (instance 3 default)

3: Distance body 0-1 (instance 4 default)

4: Velocity magnitude body 0

5: Velocity magnitude body 1

6: Velocity magnitude body 2

7: Distance body 0-2

8: Distance body 1-2

9: Angular momentum magnitude

10: Total system energy

Auto-normalizing bounds track min/max values.

Sampling Timing

- Per grain trigger: speed, grainsize, grainstart, amplitude,pan, and maxgrains

- Per DSP block (smooth variation): iot, gdelay, moog_cutoff,moog_resonance, moog_mix.

Modulation Mapping

Output value: min + random_value × (max - min). Linear mapping using normalized random_value [0.0, 1.0] from the generator.

Generator Outlet Sends

The four modulation outlets (modout1–modout4) are first-class modulatable parameters. Configure
them with the same unified messages used for any parameter, addressing them by name
(modout1…modout4); there are no separate modout_source/modout_range commands:

  rand_type <type_instance> modoutN   - assign a generator (any of the nine types, instance 1-4) to outlet N
  param_range modoutN <min> <max>     - set the outlet's output range
  param_slew / param_invert modoutN   - smooth / invert the outlet, as for any parameter

Per-Outlet Base Value

Set PERLIN_2D Y-coordinate for parameter decorrelation.

param_base_value <param> <value>

Example:

rand_type perlin_2d_1 modout1

rand_type perlin_2d_1 modout2

param_base_value modout1 0.2

param_base_value modout2 0.8

Result:

modout1 and modout2 use same instance but different Y-coordinates, producing decorrelated patterns.

Notes:

Default: 0.5 for all parameters

Only affects PERLIN_2D (ignored for other generator types)

Enables spatial decorrelation without using multiple instances

Parameter Slewing

Apply exponential smoothing to modulation output.

param_slew <param> <coefficient>

Example:

rand_type rand_1 modout1

param_range modout1 0 1

param_slew modout1 0.9   → Very smooth, slow tracking

param_slew modout1 0.5   → Moderate smoothing

param_slew modout1 0.0   → Instant (no smoothing)

Range:

0.0: Instant response (disabled)

0.5: Moderate smoothing

0.99: Very slow tracking

1.0: Frozen (no updates)

Notes:

Algorithm: smoothed = smoothed × slew + new × (1 slew)

Works with all generator types

Generator Reset

Reset Perlin or Lorenz generators to initial state.

perlin_reset <instance>

lorenz_reset <instance>

Example:

perlin_reset 1 → Resets Perlin 1D/2D coordinates to 0

lorenz_reset 2 → Resets Lorenz attractor to initial position

nbody_reset 3 → Resets N-body system (existing feature)

Perlin Reset:

Resets noise_1d_coord and noise_2d_coord_x to 0.0

Useful for synchronizing modulation patterns

Lorenz Reset:

Returns x, y, z to initial values (x0, y0, z0)

# SOURCE SHAPE

Every modulation generator family has settable SHAPE parameters (all per instance 1-4,
all clamped at the setter, all defaults = the pre-shape behaviour exactly). They are the
engine half of the SOURCE SHAPE panel cluster.

Waveform readout shaping (sine/saw/square N are three views of ONE shared oscillator per
instance; these shape how each view READS the shared phase — the phase advance itself is
untouched, so the three stay phase-locked siblings):

waveform_phase <instance> <0-1>  Phase OFFSET added at readout for that instance's sine,
saw and square. 0.25 shifts sine by 90 degrees. Default 0.

square_pw <instance> <0.05-0.95>  Square pulse width: the square is high for pw of each
cycle (the high stretch sits at the end of the cycle, so the default 0.5 is bit-identical
to the historical readout). Default 0.5.

saw_skew <instance> <0-1>  Saw rise/fall fold: rise time = (1-skew) of the period.
0 = ramp up (default, the historical saw), 0.5 = symmetric triangle, 1 = ramp down.

Lorenz attractor parameters (the textbook constants, unlocked):

lorenz_sigma <instance> <1-20>  Default 10.
lorenz_rho <instance> <1-60>  THE musical knob: below ~24 the orbit settles toward a fixed
point (readout goes static — lorenz_reset recovers it), 28 = the classic attractor,
higher = wilder excursions. Default 28.
lorenz_beta <instance> <0.5-8>  Default 8/3.
The clamps keep the fixed-dt integrator bounded; the existing divergence flush in the
update remains the backstop.

Sphere motion (see also the Sphere Simulation messages):

sphere_spin <instance> <-10 to 10>  Energy-neutral velocity curl about the y-axis (orbit).
Default 0.
sphere_kick_rand <instance> <0-50>  Random-direction kick (event; not snapshot state).

Capture: all SOURCE SHAPE parameters except sphere_kick_rand are snapshot state since
capture schema v4, in the selection-tree group "sources" — capture/recall, the cursor
blend, the Snapshot Expander (snapbuf_set saw_skew_2 0.5 …) and the text export address
them by name (waveform_phase_1..4, square_pw_1..4, saw_skew_1..4, lorenz_sigma/rho/
beta_1..4, sphere_spin_1..4). `morph_exclude sources` keeps them live across recalls.
Restores re-apply through the same clamps as the setters.

Each instance has different starting position:

Instance 1: (0.1, 0.0, 0.0)

Instance 2: (5.0, 10.0, 20.0)

Instance 3: (-3.0, -5.0, 10.0)

Instance 4: (8.0, 15.0, 30.0)

N-body Reset:

Resets to hierarchical initial configuration

Each instance has different mass distribution

Notes:

Instance range: 1-4

Does not reset frequency scaling (perlin_frequency)

Does not reset N-body physics parameters (epsilon, damping, etc.)

Defaults

All parameter ranges initialized to: min: 0.0, max: 1.0, rand_type: RAND_TYPE_RAND (instance 1), enabled: 0 (disabled). All noise_frequency_scale defaults to 1.0.

# SCOPE

The last two outlets (10 = scope_x~, 11 = scope_y~) are a pair of audio-rate signal
taps for VISUALIZING the modulation sources and the grain cloud. Feed them into an XY
oscilloscope — plugdata's [oscilloscope~], any external scope, or hardware via a
DC-coupled interface — and the engine's most visual objects become visible: the Lorenz
butterfly, sphere orbits, n-body dances, and a live constellation of every sounding
grain. All values are bipolar, scaled to ±1.

Select what the pair carries with one message:

scope_tap <family> [inst]

  sine | saw | square | perlin | rand [inst 1-4]
      Y = that generator's readout (the SAME shaped readout the modulation
      system uses, so waveform_phase / square_pw / saw_skew show on screen);
      X = an internal sweep ramp. rand shows the generator's last emitted
      value without disturbing its sequence.
  folw
      Y = the input envelope follower (mono mix); X = sweep. Watch what the
      matrix's env_mono source hears.
  lorenz [inst]
      X = the attractor's x-axis, Y = its z-axis: THE BUTTERFLY. This is the
      power-on default (lorenz 1).
  nbody [inst]
      X/Y = body 1's x/z position (the "planet" — the readable orbit in every
      stock configuration).
  sphere [inst]
      X/Y = the sphere's x/z position. sphere_spin draws circles; kicks and
      bounces draw what they sound like.
  grain
      The GRAIN CONSTELLATION (the default grain view; below).
  grainsum
      Y = the summed loudness of all active grains — the cloud's amplitude
      silhouette, soft-scaled with tanh(sum/4) so a few full-scale grains sit
      mid-screen and dense clouds saturate toward 1; X = sweep.
  scale [grain|smear]
      THE SCALE POLYGON: the beam steps through the active slot's pitch
      classes, one degree per sample — X = cos(2pi*pc/12), Y = sin(2pi*pc/12).
      Every point sits ON the unit circle at an active pitch class; the
      APPLIED scale_root is included, so root motion spins the polygon and
      slot switches reshape it live. Default destination: grain.
  pattern [slot 0-7]
      A RUNNING PATTERN as a waveform: X = the cycle-phase ramp, Y = the
      slot's current value (sample-and-hold) — steps read as plateaus, the
      step silhouette of the pattern. Inactive slot parks the beam at (0,0).

[inst] is the generator instance 1-4 (default 1; folw/grain/grainsum take none;
scale takes grain|smear, pattern takes a pattern slot 0-7).

THE GRAIN CONSTELLATION (scope_tap grain). The scope scans the active grain pool
round-robin, ONE GRAIN PER OUTPUT SAMPLE — a Vectrex-style vector-display refresh:

  X = the grain's current position within its splice, normalized to ±1
  Y = its instantaneous envelope value x amplitude (its loudness right now)

On an XY display the whole granular engine appears at once as a cloud of dots: the
horizontal spread IS the position scatter (grainstart / organize / spray), the height
IS the envelope shape, the density IS voices/IOT — and it breathes with the music.
Poly chords show multiple position clusters; one-shot tails thin out and sink; stolen
voices blink. With no grains active the beam parks at center (0,0).

Rate semantics: generator taps are sample-and-hold — the generators advance per grain
trigger (tied to IOT), so their values genuinely step; the scope shows exactly the
staircase the engine modulates with. The X sweep ramp runs at a fixed, readable 2 Hz.
The constellation and the audio-rate ramp are true per-sample signals.

The scope routing is a MONITOR, not voice state: like the matrix pins, scope_tap is
never captured by snapshots, never morphed, and absent from the export vocabulary.
Outlets appended last: every pre-scope patch is untouched.

# MODULATION MATRIX

An N-to-M routing layer ON TOP of the per-parameter range system above. Where param_range binds
exactly ONE generator to one parameter, the matrix routes MANY sources to MANY destinations, each
connection with its own signed depth, and all contributions to a destination are SUMMED. It is a
thin additive overlay: the param_range base value (or the plain scalar when the range is disabled)
is computed exactly as before, then the matrix sum is added on top and the result is clamped to
the destination's musical range. With zero connections the matrix is completely inert and every
parameter behaves exactly as without it.

Messages

matrix_connect <source> <dest> <depth>

Add a connection (or update its depth if the same source/dest pair already exists — this also
re-enables a disconnected pair). Depth is SIGNED and in the DESTINATION'S OWN UNITS: the source
value s in [0,1] is centered at 0.5, and the contribution is depth × (s − 0.5) × 2 — i.e. the
source swings the destination by ±depth around its base. Example:

  matrix_connect sine1 moog_cutoff 2000   → cutoff swings ±2000 Hz around its base
  matrix_connect env_mono gdelay_mix 0.4  → input level pushes the delay mix ±0.4

Up to 32 connections. Unknown source or destination names are rejected with an error and leave
the matrix unchanged.

matrix_disconnect <source> <dest> - Disable that connection (the slot is kept; a later
matrix_connect of the same pair re-enables it in place).

matrix_clear - Remove all connections. Instantly restores exact pre-matrix behavior.

matrix_dump - Post the current connection list (index, source, dest, depth, disabled flags).

env_follow_ms <0-60000> - Set the envelope follower's release time in milliseconds (default 30;
0 = instant follow). See "Input envelope follower" below.

Sources

The matrix source vocabulary is SEPARATE from the rand_type generator names (param_range bindings
are untouched by the matrix and vice versa):

  sine1-4, saw1-4, square1-4   - the waveform LFO instances (lfo1-4 are aliases for sine1-4)
  perlin1-4                    - 1D Perlin noise instances
  lorenz1-4, nbody1-4, sphere1-4 - the chaotic/physics generator instances
  rand1-4                      - the seeded random instances
  pattern0-7                   - pattern slot caches (see PATTERNS); unloaded slots read neutral 0.5
  env_l, env_r, env_mono       - the input envelope follower (left / right / mono mix)

Generator instances read the SAME state the param_range system reads, so e.g. lorenz1 in the
matrix and rand_type lorenz_1 on a range track the same attractor. Generator/pattern sources
advance at the grain-trigger rate (like the range system); reading rand1-4 from the matrix
advances the shared per-instance random seed.

Destinations

Per-BLOCK parameters (applied once per DSP block):

  gdelay, gdelay_feed, gdelay_tone, gdelay_mix
  moog_cutoff, moog_resonance, moog_mix
  smear_frequency, smear_resonance, smear_stages, smear_feedback
  scanrate, organize, sos, iot, env_skew
  modout1-4
  scale_root, smear_scale_root (semitones, clamp ±24 — the applied offset honors
    scale_root_quant), pitch_scale_slot, smear_pitch_scale_slot (STEPPED: rounds to
    slot 0-15), scale_rotate, smear_scale_rotate (STEPPED: degrees, wraps into the
    scale) — see MODULATING THE SCALE

Per-GRAIN parameters (applied at each grain trigger):

  speed, grainsize, grain_start, amplitude, pan, pitch_fine
  (grainstart is accepted as an alias for grain_start)

Per-grain destination sums are computed once per DSP block (sources are per-block values) and
added to each grain's value at trigger time, AFTER the param_range sample and BEFORE the same
clamps the engine already applies (grainsize 0.01-2 s, amplitude 0-2, pan 0-1, grain_start 0-1
normalized, speed ±4, pitch_fine ±0.5 semitones). The offsets are purely per-grain: the shared
parameter bases (the values the inlets/messages own and queries report) are NEVER written by the
matrix, so a per-grain connection composes with — and cleanly disconnects from — everything else.

The speed destination applies in ALL pitch modes: the offset is added to the final pitch-derived
speed (MIDI / scale / pattern pitch keep working), so a pin on speed is a detune/drift AROUND the
note, not a pitch-source bypass. pitch_fine offsets add to the sampled fine value, bounded to the
±0.5-semitone fine-tune range.

Onset/pitch detection sources are planned for v2.

Behavior notes

- Additive over param_range: if the destination's range is ENABLED, the matrix sum is added to the
  range-sampled value each block (both modulations combine). If the range is DISABLED, the matrix
  alone drives the destination around its current scalar base — a destination no longer needs an
  enabled range to be modulated.
- Clamping: the matrix apply site clamps the summed result to the destination's musical range
  (e.g. moog_cutoff 20-20000 Hz, gdelay 0-9.5 s, mixes 0-1). Overshoot from many connections or a
  hot input simply saturates at the clamp — no instability. Exception: modout1-4 are NOT clamped
  (they are patch-out floats; scale them in the patch).
- modout as destination: a matrix connection to modoutN emits floats even when that modout's
  param_range is disabled/unassigned; with only the matrix driving it the emitted value is
  0.5 + sum (neutral center plus the overlay). With a generator assigned via rand_type/param_range
  the matrix sum is added on top of the sampled range value.
- gdelay_feed applies in Stut mode too (it retargets to stut reduction, which shares the same 0-1
  meaning). gdelay_tone is NOT matrix-applied in Stut mode (there it maps to spacing in ms —
  different units); the range modulation still applies as usual.
- smear_frequency connections are bypassed while the smear pitch destination is enabled (the pitch
  system owns the frequency then, exactly like the smear_frequency_range).
- Capture transparency: snapshots record the PRE-MODULATION base, never a momentary matrix value.
  The 11 FX params capture from their message-set shadow values, the per-grain destinations never
  write the shared fields at all, and the self-read transport params (scanrate in particular)
  capture the matrix's tracked base while a connection is active — so a SNAP taken mid-wobble
  stores the knob value, and recalling it restores the base the modulation was riding on.
  (Connections themselves are never captured: pins are physical — see snapshots/morphing.)

Input envelope follower

The follower makes the instrument listen to its own input: a rectified PEAK follower over the live
input signal (instant attack, one-pole release, default ~30 ms, set via env_follow_ms). It is
computed once per DSP block and exposed as the matrix sources env_l / env_r / env_mono (mono =
0.5×(L+R)). The follower output is not hard-capped: input is normally ≤1.0, and the destination
clamp catches any overshoot from hot input × high depth. Typical use:

  matrix_connect env_mono moog_cutoff 4000   → the filter opens with the input level
  matrix_connect env_mono modout1 1          → patch the input envelope out to the Pd graph

Since a [0,1] source is centered at 0.5, a silent input contributes −depth (the follower sits at
0). To bias a destination so silence = base and signal pushes up, raise the base by depth (or use
a modout and offset in the patch).

Example

  param_range moog_cutoff 800 800          (fixed 800 Hz base, range enabled)
  matrix_connect lorenz1 moog_cutoff 600   (chaos wobbles the cutoff ±600 Hz)
  matrix_connect env_mono moog_cutoff 3000 (input level opens it up to ±3000 Hz more)
  matrix_connect env_mono pan 0.5          (input level spreads each grain's pan ±0.5)
  matrix_connect sine1 grainsize 0.05      (LFO breathes every grain's length ±50 ms)
  matrix_dump
  matrix_clear                             (back to exactly the range-only behavior)

# PATTERNS (TIDALCYCLES MINI-NOTATION)

A step-sequencing layer adapted from TidalCycles mini-notation. A pattern distributes a list of values
(or scale degrees) evenly over a musical CYCLE and can drive any modulatable parameter or pitch — reusing
the existing BPM detection, the quantization grid math, the modulation ranges, and the scale system rather
than adding a parallel engine.

Syntax (space-separated tokens; NO commas)

A pattern is a sequence of space-separated tokens:

  0.7           a value (0..1 for parameters; a scale DEGREE for pitch; the event ARG for event patterns)
  ~             a rest (holds the previous value / note; an event pattern stays SILENT on a rest)
  [ a b c ]     subdivision group — children evenly split the parent's time slice; nestable to any depth
  < a b c >     alternation — ONE child per cycle, advancing each cycle (Tidal "slowcat")
  a@3           weight — this step takes 3x the time of an unweighted step
  a*3           repeat — three copies of a within this step (subdivides the slot)
  a!3           replicate — three separate sibling steps
  a(k,n)        Euclidean rhythm — this step becomes n sub-steps with a on the k Bjorklund pulse
                positions and rests elsewhere (see EUCLIDEAN RHYTHMS below)
  rev           reverse the preceding group in time (see REV below)

Brackets and angles are standalone tokens: write "[ a b ]", not "[a b]". Bare commas as SEPARATORS are
unsupported — in TidalCycles a separating comma means STACK (parallel layers), which does not apply here,
and Pd treats an unescaped comma as a message separator. The only comma in the grammar is INSIDE an
Euclid suffix, where it must be escaped in a Pd message box: 1(3\,8).

Examples:

  pattern moog_cutoff 0.0 0.5 1.0          three even steps across the cycle
  pattern moog_cutoff [ 0.2 0.4 ] 0.8      0.2 and 0.4 share the first half, 0.8 takes the second half
  pattern moog_cutoff 0.2@3 0.8            0.2 for 3/4 of the cycle, 0.8 for the last 1/4
  pattern amplitude < 0.2 1.0 >            alternates 0.2 (one cycle), 1.0 (next), repeating

The cycle clock

Patterns step on a free-running clock locked to the detected BPM (set, as elsewhere, by banging the object
at the tempo — two bangs establish the BPM). Until a tempo is known the clock holds. The cycle LENGTH is one
bar of 4/4 by default, or set it explicitly:

  pattern_cycle <N/D> <N/D> ...

Each N/D segment is "N notes of value 1/D" at the current BPM; the cycle length is their sum, using the same
grid math as the quantize messages. At 120 BPM:

  pattern_cycle 4/4 3/8     = 4 quarter-notes (2.000 s) + 3 eighth-notes (0.750 s) = 2.750 s

pattern_cycle with no arguments resets to the default 1-bar cycle. Valid denominators: 1, 2, 4, 8, 16, 32,
64, 128. This is a fifth, independent clock — it does not disturb the IOT / grain-size / delay / stut grids.

Patterns on parameters

  pattern <param> <tokens...>

Loads the pattern and attaches it to <param> (any name accepted by param_range / rand_type — e.g. moog_cutoff,
smear_frequency, amplitude, gdelay_mix, modout1..4). The step value (0..1) maps to the parameter's range, so
set a range first:

  param_range moog_cutoff 200 4000
  pattern moog_cutoff 0.0 1.0        steps the cutoff between 200 and 4000 Hz across the cycle

The pattern is a modulation SOURCE (RAND_TYPE_PATTERN), so param_invert and param_slew apply normally (slew
glides between steps; leave slew at 0 for hard steps). Attaching auto-enables the range; a single-value range
(min == max) is widened to [0, 1] so the pattern is audible.

Up to 6 param + event patterns can run at once (8 slots; slot 7 is reserved for pitch and slot 6 for
smear_pitch, leaving a shared 0-5 auto-pool); each gets its own slot automatically, and re-sending
pattern <param> (or the same event action) reuses that pattern's slot.

  pattern_clear <param>              detach; restore the parameter's previous generator / source

Advanced (two-step form): pattern <N> with a bare slot number 0-7 loads slot N WITHOUT attaching, and
rand_type pattern_N <param> then points a parameter at slot N.

Patterns on pitch

  pattern pitch <tokens...>

Tokens are scale DEGREES indexed into the loaded pitch_scale (see PITCH & SPEED, Mode 5); this sets pitch_mode
5 automatically. Degrees wrap with octave compensation — on a 7-note scale, degree 7 is the root one octave up
(+12 semitones).

  pitch_scale 0 2 4 5 7 9 11
  pattern pitch [ 0 1 2 3 4 5 6 7 ]      runs up the major scale into the next octave
  pattern pitch < 0 4 7 >                root / fifth / octave, one per cycle

Pitch is applied per grain (tempo-locked, not grain-locked): sparse grains can skip steps, dense grains
re-trigger the same step. A rest (~) holds the previous note. pattern_clear pitch returns to pitch_mode 0 (OFF).

Patterns that FIRE events

  pattern event <action> <tokens...>          action = grain | splice | retrig | gate | bang

Where a param/pitch pattern SETS a value that downstream code samples, an EVENT pattern FIRES a discrete
action on each step: once per non-rest step, exactly when the step boundary passes on the BPM-locked cycle
clock (tempo-locked, quantized to the same grid as every other pattern; a held step never re-fires, and
rests — including the off-positions of an Euclidean rhythm — are silent). The step VALUE is the event's
argument:

  grain    fire a burst of <value> grains (1..16) at the current playhead (splice start + grainstart
           offset) with the current speed / pan / amplitude / saw settings
  splice   select splice <value> (wrapped into range) — applied at the NEXT splice wrap, like shift.
           Block order: a splice event only QUEUES the jump, so a grain burst in the same step still
           fires at the current splice
  retrig   restart playback from the start of the current splice (value ignored) — active grains finish
  gate     value != 0 starts the transport, 0 stops it (same flags as play 1/0); gate 0 does NOT cut
           active grains — they play out
  bang     bang the grain-onset outlet (outlet 4; value ignored)

  pattern event grain [ 3 ~ 1 1 ]     a 3-grain cluster on beat 1, nothing on 2, one grain on 3 and 4
  pattern event splice [ 0 1 2 ]      walk splices 0 -> 1 -> 2 across the cycle
  pattern event gate [ 1 0 ]          transport on for the first half-cycle, off for the second

'trigger' is accepted as an alias for 'event' (pattern trigger grain ... is the same message). Prefer
'event': a BARE trigger message is the separate transport re-trigger — the two never collide at dispatch,
but reading 'pattern trigger' next to 'trigger' invites confusion.

Event patterns share the same 6-slot auto-pool as param patterns (slots 0-5; 6 and 7 stay reserved for
smear/grain pitch), so params + events together are capped at 6 simultaneous patterns. Re-sending the same
action replaces its pattern (no duplicates); clear one with pattern_clear <slot> (the slot number is shown
when the pattern is armed), which also resets the slot's event tag. Events fire regardless of whether the
transport is running (that is the point of gate/retrig); grain bursts need audio in the reel, and each
burst bangs the grain-onset outlet once when grain_bang_rate > 0.

Euclidean rhythms

  a(k,n)      e.g.  1(3,8)  — as a Pd message-box token:  1(3\,8)

Expands one step into n evenly-spaced sub-steps: a lands on the k pulse positions of the canonical
Bjorklund/Toussaint distribution (rotated to start on a pulse) and the n-k off-positions are rests. It is
pure notation — the result is an ordinary step table, so it works on params, pitch, and event patterns
alike, and composes with @N (weight the whole group) and !N (replicate the group):

  pattern event grain [ 1(3\,8) ]     x..x..x.  — bursts on the 3-of-8 Euclid pulses
  pattern moog_cutoff 0.8(5\,8)       x.xx.xx.  — value 0.8 on 5-of-8 pulses, rest (hold) elsewhere

k = 0 gives all rests, k >= n all pulses; n is capped at 64 (the step-table limit). a*N cannot combine
with a(k,n) on the same step — both expand the step, so v*N(k,n) is rejected as ambiguous. NOTE the
escaped comma \, — an unescaped comma would split the Pd message.

rev

  rev         reverse the preceding group in time (parse-time transform)

Placed after a group (or after a run of bare steps), rev reverses that group's step order, recursing into
nested [ ] subdivisions; < > alternation ORDER is preserved (rev acts within a cycle) but each member's
contents are reversed. rev rev restores the original.

  pattern speed [ 0.1 0.2 0.3 ] rev     plays 0.3 0.2 0.1
  pattern speed 0.1 0.2 0.3 rev         same — with no preceding group, reverses the steps so far

Notes

- Parse errors (unbalanced brackets, unknown parameter, bad token) are reported and leave the previous pattern
  intact.
- Single-level alternation only: < > may not be nested directly inside another < > (rejected). [ ] subdivision
  nests freely.
- pattern_debug 1 logs step changes, fired events, and applied-semitone changes to stderr (a development
  aid; off by default).

# MORPH / METASURFACE

A 2D interpolation surface for the WHOLE patch, modelled on Ross Bencina's Metasurface (AudioMulch). You
capture snapshots of the entire sound, drop each as a point on a square, then move a cursor around the
square — at any cursor position every parameter is set to a distance-weighted blend of the surrounding
snapshots. Sit the cursor exactly on a point and you get that snapshot back unchanged; move between points
and the sound morphs smoothly. Routes then let the cursor drive ITSELF along a timed path.

The whole thing is control-rate and sits ON TOP of the existing engine — it only writes the same fields the
normal messages write, and **modulation keeps running through a morph** (it interpolates the modulation
band, it doesn't freeze it).

## Snapshots

snapshot <id> captures the current patch into slot id (0-63); snapshot_recall <id> jumps straight back to it
(no blend); snapshot_clear <id> forgets it.

A snapshot stores: every modulation range (the min/max band, generator, slew, invert of all 45 modulatable
params), the scalar base of each continuous parameter (grain size/start, speed, amplitude, pan, saw, sos,
iot, the pitch + smear-pitch values, the quant amounts), every discrete mode/enum/channel (playhead mode,
pitch mode, smear-pitch source, pan mode, sos mode, MIDI channels, …), both pitch scale lists, and the
playable-FX scalar bases (moog cutoff/resonance/mix, smear frequency/resonance/stages/feedback, delay
time/feedback/tone/mix).

Since text schema v3 a snapshot ALSO carries the modulation sources' own "weather" params — the
per-instance rate scales noise_freq_1..4, the envelope-follower release env_follow_ms, the sphere
physics (sphere_damping/sphere_elasticity 1-4 and output modes), and the N-body physics (nbody_G /
nbody_damping / nbody_epsilon / nbody_pump amount + interval 1-4 and output modes). Motion CHARACTER
travels with the voice: retuning perlin_2's rate for scene B no longer retroactively changes scene A.
If you prefer the old behaviour — one global rate knob sweeping every scene at once — exclude the
group: `morph_exclude sources`.

Schema v4 extends the same group with the SOURCE SHAPE params (see SOURCE SHAPE):
waveform_phase_1..4, square_pw_1..4, saw_skew_1..4, lorenz_sigma/rho/beta_1..4 and
sphere_spin_1..4 — a voice's waveform character, attractor tuning and orbit spin all recall
with it (restored through the setters' clamps; sphere_kick_rand is an event and is not
captured). The same `morph_exclude sources` keeps them live.

snapshot_recall (like the cursor blend and snapbuf_apply) honors the selection tree: excluded
parameters keep their live values across a recall.

## The surface and the cursor

morph_point <id> <x> <y> (alias morph_place) drops snapshot id at (x,y) on the normalised [0,1] x [0,1]
surface; morph_unplace <id> removes it. Place as many as you like, anywhere.

morph <x> <y> moves the cursor and blends immediately (this is the live "play the surface" control; morph_x
and morph_y move one axis each, e.g. from two CV/knob sources). Blend rules:

- Continuous fields (range min/max, scalar bases) — distance-weighted average, each in its own units (Hz
  blends in Hz, semitones in semitones).
- Discrete fields (modes, generators, scale lists) — snap to the NEAREST (highest-weighted) snapshot; they
  cannot be averaged.
- Cursor exactly on a point — that snapshot is reproduced exactly (no overshoot).

morph_power <p> sets the IDW sharpness (default 2.0; higher pulls the blend tighter to the nearest point).

CV cursor: morph_cursor 1 hands the cursor to the two rightmost signal inlets (morph X / morph Y),
so a physical XY joystick or any CV can drive the surface at signal rate (clamped to [0,1]); morph_cursor 0
returns to the message cursor. A running route overrides both.
morph_interp selects the weighting kernel — 0 = IDW/Shepard (cheap, global), 1 = natural-neighbour (a
sampled/grid Sibson approximation: local, no overshoot, the faithful Metasurface character; a touch more
CPU, best for a static cursor than a fast route).

## Limiting which parameters morph

By default the morph applies to the whole patch. morph_exclude <name...> drops parameters from the
blend — an excluded parameter keeps whatever manual / modulation / inlet control owns it and the
cursor leaves it untouched; morph_include <name...> adds them back, and morph_include all resets to
the full patch. Snapshots still CAPTURE everything either way — this only changes what a restore
APPLIES: the cursor blend, snapshot_recall, and the expander's snapbuf_apply all honor it (Bencina's
Parameter Selection Tree).

Targets: all; a single parameter — amplitude, pan, speed, grainsize, grainstart, moog_cutoff,
moog_resonance, moog_mix, smear_frequency, smear_resonance, smear_stages, smear_feedback, gdelay,
gdelay_feedback, gdelay_tone, gdelay_mix; or a group — pitch (the grain pitch -> playback speed),
smear_pitch (the resonator note), fx (moog + smear-resonator + delay + distortion), sources (the
generator/weather params captured since schema v3: noise_freq_1..4, env_follow_ms, the sphere and
N-body physics params and output modes — plus, since schema v4, the SOURCE SHAPE params:
waveform_phase/square_pw/saw_skew, lorenz_sigma/rho/beta and sphere_spin, all 1-4). Each name
covers that parameter's modulation band, its scalar base, and any mode it owns.

  morph_exclude pitch        # morph the timbre but hold the notes fixed
  morph_exclude fx           # morph grain + pitch but leave the effects alone
  morph_exclude sources      # rates/physics stay global "weather" (pre-v3 behaviour)
  morph_include all          # back to morphing everything

## Routes (automated cursor paths)

A route is a list of waypoints the cursor walks through over time — an automation envelope on the morph
itself (this is the part AudioMulch left as future work).

morph_route <x> <y> <rate> <curve> appends a waypoint: rate = seconds to traverse that leg, curve = the
easing (0 linear, 1 ease-in, 2 ease-out, 3 ease-in-out, 4 hold). morph_route_clear empties the path.

morph_run [loop] starts walking from the current cursor (loop wraps end→start; without it the route halts at
the final waypoint). morph_stop ends it; morph_pause freezes at the current cursor.

## Example

  # build two sounds and capture them
  smear_pitch_semitones 0 \; smear_resonance 0.9 \; moog_cutoff 400
  snapshot 0 \; morph_point 0 0 0
  smear_pitch_semitones 12 \; smear_resonance 0.3 \; moog_cutoff 6000
  snapshot 1 \; morph_point 1 1 0
  # play the surface live
  morph 0.5 0                 # halfway: smear pitch +6 semis, resonance 0.6, cutoff ~3200
  # or sweep automatically over 4 seconds, eased
  morph_route 1 0 4 3 \; morph_run

## Persistence

morph_save <file> writes the ENTIRE morph state — every captured snapshot PLUS the whole surface
(points, cursor, route) — to a single .morph file (path resolved relative to the patch, exactly like
load/save for the reel; a .morph extension is added if you leave it off). morph_load <file> reads it
back — into the same patch or a fresh instance — and re-applies the cursor so the live sound jumps to
the loaded blend. The file carries a version/layout header, so a .morph written by an incompatible
build is refused rather than mis-read.

morph_state is the human-readable counterpart: it dumps the surface LAYOUT — morph_power, a
morph_point per placed snapshot, a morph_route per waypoint, and the morph cursor — to the state
outlet as RE-SENDABLE messages. Route that outlet to a [text] or message boxes to inspect, edit,
version-control, or embed a layout in a patch; replaying it restores the geometry over snapshots
that already exist (so pair it with morph_save/morph_load, or a re-capture, for the snapshot
bodies — those hold hundreds of values each and are not dumped as messages).

morph_export <file> / morph_import <file> are the fully portable option: a human-readable .txt that
holds EVERYTHING (every snapshot body + the surface), written as a logical-field schema rather than a
memory image — so unlike the binary .morph it survives across builds whose struct layout differs (a
"ligase_morph <version>" header guards the schema). The current schema is version 5 (v3 appended the
generator/sources params, v4 the SOURCE SHAPE params, v5 the HARMONIC LAYER — the 16 scale slots per
destination, active-slot indices, scale_root/quant and scale_rotate; empty slots are written
count-only so files stay compact); older v1-v4 files still import cleanly — their missing fields
simply keep the values the engine has at import time (on a fresh instance that means slot-0/root-0:
the old file's scale lands in the active slot, exactly the pre-slot behavior), and the "exclude"
line's indices are remapped from the older layout by file version, so old exclude lines keep
selecting the same fields. The snapshot lines are long (one per snapshot, hundreds of numbers) but it is plain
text you can read, diff, and edit.

Three persistence routes, then: morph_save/morph_load (binary, complete, fastest, build-specific);
morph_export/morph_import (text, complete, human-readable + build-portable); morph_state (messages,
layout only — for inspection or embedding a layout in a patch).

All three also carry the SELECTION TREE (your morph_include/morph_exclude choices): the binary file
stores it in the struct, the text file adds an "exclude <indices>" line, and morph_state emits it as
re-sendable "morph_include all" + "morph_exclude <indices>" messages. So a saved surface restores not
just the snapshots and layout but also which parameters the morph is allowed to touch.

## Notes

- A morphed scalar base of an inlet-tracked parameter (grain size, speed, amplitude, …) only takes effect
  while that signal inlet is unpatched — a live CV inlet overrides it the same block (the engine's standard
  live-CV-wins precedence). The modulation BAND of those params always morphs.
- Distortion-enhancement and stut/bencina scalar bases are not captured yet (their modulation bands are);
  they hold their current value across a recall.
- morph_include/morph_exclude limit which parameters the morph applies (see "Limiting which parameters
morph"). Both kernels ship: morph_interp 0 = IDW/Shepard (default), 1 = natural-neighbour (a mesh-free sampled
Sibson approximation). The CV signal-inlet cursor is implemented (morph_cursor 1, inlets 22/23).

# SNAPSHOT EXPANDER (EDIT BUFFER)

Snapshots on their own are write-only: you can capture, recall, blend and export them, but you cannot
look INSIDE one, and you cannot adjust one without recalling it into the live engine — hostile for
live use, where preparing the next scene must not wreck the current one. The Snapshot Expander is a
modular-synth style sidecar for exactly that: ONE edit buffer — patch memory's classic workbench —
that you load snapshots into, inspect, and edit **completely offline**.

**The cold-edit contract.** The edit buffer is never read by the audio pipeline. Every snapbuf_set is
cold; the live engine changes on exactly two deliberate acts:

- **snapbuf_apply** (panel: ASSIGN) — buffer -> live engine, through the same path as a snapshot
  recall: it writes bases + bands + discretes, honors morph_include/morph_exclude, and never touches
  matrix pins.
- **snapbuf_store <id>** (panel: STORE) — buffer -> snapshot slot. **If that slot is placed on the
  morph surface and part of an active blend, the field changes shape on the next block.** That is the
  point — reshape a corner of the surface mid-set — and it is safe because it only ever happens on the
  explicit STORE, never as a knob side-effect. Storing to an unplaced slot changes nothing live.

**Audition / compare (v1.1 — the explicit opt-in).** `snapbuf_audition 1` TEMPORARILY applies the
buffer so you can hear it: the current live voice is captured to a revert slot first (modulation-
transparent, like any capture), then the buffer lands through the usual masked-restore path.
`snapbuf_audition 0` restores the pre-audition voice exactly. `snapbuf_compare` is an A/B toggle
over the same latch (A = your live voice, B = the buffer). Wire a panel button's press/release to
`snapbuf_audition 1`/`0` for the momentary hardware gesture. Rules: buffer edits made WHILE
auditioning stay cold (toggle off/on to hear them); `snapbuf_apply` during an audition COMMITS —
the buffer becomes the real live voice and the revert is discarded; knob/message moves you make
during an audition are overwritten by the revert (the latch owns the live voice while held);
excluded fields (`morph_exclude`) stay live through both directions of the round-trip.

## Messages

  snapbuf_load <id>          copy stored snapshot -> buffer
  snapbuf_from_live          capture the CURRENT voice -> buffer (the snapshot capture path:
                             modulation-transparent — bases, never the momentary wobble)
  snapbuf_set <field> ...    edit one field in the buffer (see grammar below)
  snapbuf_get <field> [sub]  report one field/subfield on the state outlet (outlet 9),
                             prefixed "snapbuf", e.g.  snapbuf amplitude_range min 0.2
  snapbuf_dump               the whole buffer as RE-SENDABLE snapbuf_set lines on outlet 9
                             (a panel populates every display from one dump; replaying the
                             dump reconstructs the buffer)
  snapbuf_store <id>         buffer -> slot (keeps the slot's surface placement; see above)
  snapbuf_apply              buffer -> live engine (the ONLY committing touchpoint;
                             during an audition it COMMITS and discards the revert)
  snapbuf_audition <0|1>     v1.1: temporarily hear the buffer (1) / exact revert (0)
  snapbuf_compare            v1.1: A/B toggle over the audition latch
  snapbuf_clear              re-init the buffer to empty

## Field addressing (the export-schema vocabulary)

Fields use the text-export schema's logical names — the same enumeration, so the expander and the
.txt schema can never diverge. Four kinds:

- **Bands** (`<param>_range`, all 45): per-subfield —
  `snapbuf_set moog_cutoff_range min 200` (subfields: min max enabled rand_type rand_instance
  base_value slew invert) — or the whole band in export order:
  `snapbuf_set amplitude_range 0.1 0.5 1 2 0 0.3 0.5 0`.
- **Scalars** (`amplitude`, `moog_cutoff`, `gdelay_time`, `noise_freq_2`, `env_follow_ms`,
  `sphere_damping_1`, `nbody_G_3`, and the v4 SOURCE SHAPE params `waveform_phase_1..4`,
  `square_pw_1..4`, `saw_skew_1..4`, `lorenz_sigma/rho/beta_1..4`, `sphere_spin_1..4`, …):
  `snapbuf_set amplitude 0.42`, `snapbuf_set saw_skew_2 0.5`.
- **Discretes** (`pan_mode`, `maxgrains`, `playhead`, `pitch_mode`, `nbody_mode_1`,
  `sphere_mode_4`, `nbody_pump_interval_2`, …): `snapbuf_set pan_mode 1`.
- **Scale lists**: whole-list set, matching the live message —
  `snapbuf_set pitch_scale 0 4 7` / `snapbuf_set smear_pitch_scale 0 3 7`. Since schema v5 the
  sixteen SCALE SLOTS per destination address the same way (`snapbuf_set pitch_scale_b 3 6 9`,
  `smear_pitch_scale_a..p`), and the harmonic scalars/discretes join the vocabulary:
  `scale_root`, `smear_scale_root` (scalars); `pitch_scale_slot`, `smear_pitch_scale_slot`,
  `scale_root_quant`, `smear_scale_root_quant`, `scale_rotate`, `smear_scale_rotate` (discretes).

An unknown field or subfield is a pd error and leaves the buffer untouched. `snapbuf_get <band>`
without a subfield reports all 8 values in export order.

## Workflow

  # perform on the current scene, prepare the next one offline
  snapbuf_load 3                          # pull scene B into the workbench
  snapbuf_get moog_cutoff                 # look inside (outlet 9: "snapbuf moog_cutoff 800")
  snapbuf_set moog_cutoff 1200            # cold edits — the live sound never flinches
  snapbuf_set amplitude_range min 0.3
  snapbuf_store 3                         # write it back to the slot...
  snapbuf_apply                           # ...or land it on the live engine right now

## Notes

- One buffer (the slots are the storage; the buffer is a workbench). No audition/compare in v1 —
  edits are audible only after ASSIGN.
- snapbuf_apply respects the selection tree: with `morph_exclude sources` the applied voice leaves
  the live generator rates untouched, exactly like a recall.
- snapbuf_from_live inherits capture transparency: with the matrix wobbling a destination, the
  buffer records the knob value (the tracked base), never base+offset.

# QUERY STATE

Export and inspect ligase~ state via outlet 9.

Useful for creating presets or routing values to other instances:

query <param> → Output single parameter value

get_params → Output all 20 parameter values

Debugging and inspection:

get_inlets → Print inlet guide to console

get_ranges → Output active param_range settings

get_generators → Output active rand_type assignments

get_state → Output everything (params + ranges + generators)

query bpm → returns calculated bpm

get_inlets

Print inlet documentation to console (no outlet).

Output: Console only

Shows current values and message syntax

query

Query single parameter value.

query <param>

20 queryable parameters:

speed, grainsize, grainstart, organize, scanrate, sos, iot, maxgrains,
gdelay, gdelay_feed, gdelay_tone, gdelay_mix, distortion,
moog_cutoff, moog_resonance, moog_mix, midi, env_skew, amplitude, pan

Per-block parameters - store modulated values when param_range.enabled:
gdelay_time, gdelay_feedback, gdelay_tone, gdelay_mix
moog_cutoff, moog_resonance, moog_mix
organize, sos, env_skew, iot

Per-grain parameters - Sample once per block for query when modulation enabled:

speed, grainsize, grainstart, amplitude, pan, maxgrains, distortion

query <param> and get_params return:

Inlet/ last message value when modulation is disabled

Current modulated value when modulation is active

Locked value when param_lock has been applied

Unconnected inlets preserve stored values if headless mode is enabled

get_params

Export all 20 parameter values.

Fixed order (speed → grainsize → ... → pan)

get_ranges

Export active parameter range configurations.

Output format:

param_range <param> <min> <max>

Only outputs enabled ranges

Messages can be sent back to restore range configuration

get_generators

Export active generator assignments.

Output format:

rand_type <generator>_<instance> <param>

Generator types:
rand, perlin_1d, perlin_2d, lorenz, nbody

Instance numbers 1-4

Only outputs modout1-4 (scheduler generators not accessible)

Does NOT export perlin_frequency or nbody settings

get_state

Export complete state (all three subsystems).

Output order:
1. All parameter values (20 messages)
2. All enabled ranges (variable)
3. All generator assignments (variable)

Calls get_params + get_ranges + get_generators

All messages are routable lists

Limitations

No quantization state - timesig/quant settings not exported
