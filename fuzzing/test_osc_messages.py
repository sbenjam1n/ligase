#!/usr/bin/env python3
"""Test all ligase~ message handlers via Pd's FUDI protocol.

Connects to a running Pd instance via netreceive on port 3001
and sends every known ligase~ message with valid arguments,
boundary values, and stress tests.

Usage:
    1. Start Pd: pd -nogui -noaudio -stderr patches/osc_msg_test.pd
    2. Run: python3 test_osc_messages.py
"""

import socket
import time
import sys

HOST = "localhost"
PORT = 3001
DELAY = 0.005  # 5ms between messages

# All messages organized by category
# Format: (message_string, description)
MESSAGES = {
    # =========================================================================
    # TRANSPORT CONTROL
    # =========================================================================
    "transport": [
        ("play 1", "Start playback"),
        ("play 0", "Stop playback"),
        ("record 1", "Start recording"),
        ("record 0", "Stop recording"),
        ("recsplice", "Record splice"),
        ("recinput", "Record input only"),
        ("clockstop", "Stop clock"),
    ],

    # =========================================================================
    # SPLICE MANAGEMENT
    # =========================================================================
    "splice": [
        ("splice", "Add splice marker"),
        ("shift 1", "Shift splice forward"),
        ("shift -1", "Shift splice backward"),
        ("shift 0", "Shift splice zero"),
        ("organize 0", "Organize min"),
        ("organize 0.5", "Organize middle"),
        ("organize 1", "Organize max"),
        ("splice_create_pos 0", "Create at playback pos"),
        ("splice_create_pos 1", "Create at right"),
        ("splice_create_pos 2", "Create at end"),
        ("splice_jump 0", "Don't jump to new"),
        ("splice_jump 1", "Jump to new splice"),
        ("splice_finish_nav 0", "Immediate nav"),
        ("splice_finish_nav 1", "Finish before nav"),
        ("splice_split 0", "Allow split"),
        ("splice_split 1", "Preserve current"),
        ("send_splice_msg 0", "Splice msg mode 0"),
        ("send_splice_msg 1", "Splice msg mode 1"),
        ("clear_splice_msg 0", "Clear splice msg"),
        ("splice_join_right", "Join with right"),
        ("splice_join_all", "Join all splices"),
        ("clear_splices_except_current", "Clear except current"),
        ("clear_current_splice", "Clear current splice"),
        ("clear_splices", "Clear all splices"),
    ],

    # =========================================================================
    # PLAYHEAD CONTROL
    # =========================================================================
    "playhead": [
        ("playhead 1", "Static playhead"),
        ("playhead 2", "Scanning playhead"),
        ("playhead 3", "Clock advance playhead"),
        ("scanrate 1.0", "Normal scan rate"),
        ("scanrate 0.5", "Half scan rate"),
        ("scanrate -1.0", "Reverse scan"),
        ("scanrate 0.0", "Zero scan rate"),
        ("speed 1.0", "Normal speed"),
        ("speed 0.5", "Half speed"),
        ("speed 2.0", "Double speed"),
        ("speed -1.0", "Reverse"),
        ("speed 0.0", "Stopped"),
        ("speed 4.0", "Max speed"),
        ("speed -4.0", "Max reverse"),
        ("clock_advance_quant 0", "Use current length"),
        ("clock_advance_quant 1", "Use quantized length"),
        ("headless 0", "Normal mode"),
        ("headless 1", "Headless mode"),
    ],

    # =========================================================================
    # GRAIN PARAMETERS
    # =========================================================================
    "grain": [
        ("grainsize 0.001", "Min grain size"),
        ("grainsize 0.05", "50ms grains"),
        ("grainsize 0.1", "100ms grains"),
        ("grainsize 0.5", "500ms grains"),
        ("grainsize 1.0", "1 second grains"),
        ("grainsize 10.0", "Max grain size"),
        ("grainstart 0.0", "Start at beginning"),
        ("grainstart 0.5", "Start at middle"),
        ("grainstart 1.0", "Start at end"),
        ("envelope 0", "Parabolic envelope"),
        ("envelope 1", "Trapezoidal envelope"),
        ("envelope 2", "Cosine envelope"),
        ("envelope 3", "Gaussian envelope"),
        ("envelope 4", "Exponential envelope"),
        ("env_skew 0.0", "No skew"),
        ("env_skew 0.5", "Half skew"),
        ("env_skew 1.0", "Max skew"),
        ("maxgrains 1", "Single grain"),
        ("maxgrains 10", "10 grains"),
        ("maxgrains 50", "50 grains"),
        ("maxgrains 100", "100 grains"),
        ("iot 0.0005", "Min IOT"),
        ("iot 0.01", "10ms IOT"),
        ("iot 0.05", "50ms IOT"),
        ("iot 0.1", "100ms IOT"),
        ("iot 0.5", "500ms IOT"),
        ("iot 2.0", "Max IOT"),
        ("amplitude 0.0", "Silent"),
        ("amplitude 0.5", "Half volume"),
        ("amplitude 1.0", "Full volume"),
        ("amplitude 2.0", "Max amplitude"),
        ("pan 0.0", "Hard left"),
        ("pan 0.5", "Center"),
        ("pan 1.0", "Hard right"),
        ("pan_mode 0", "Mono panning"),
        ("pan_mode 1", "Stereo balance"),
        ("sos 0.0", "No sound-on-sound"),
        ("sos 0.5", "Half SOS"),
        ("sos 1.0", "Full SOS"),
        ("sos_mode 0", "Record only"),
        ("sos_mode 1", "Crossfade"),
        ("saw_cycles 0", "No saw modulation"),
        ("saw_cycles 8", "8 saw cycles"),
        ("saw_cycles 64", "Max saw cycles"),
        ("saw_depth 0.0", "No saw depth"),
        ("saw_depth 0.5", "Half saw depth"),
        ("saw_depth 1.0", "Full saw depth"),
        ("grain_bang_rate 0", "No grain bangs"),
        ("grain_bang_rate 1", "Bang every grain"),
        ("grain_bang_rate 10", "Bang every 10th"),
    ],

    # =========================================================================
    # TIMING & QUANTIZATION
    # =========================================================================
    "timing": [
        ("timesig 4 4", "4/4 time"),
        ("timesig 3 4", "3/4 time"),
        ("timesig 7 8", "7/8 time"),
        ("quantize 1", "Whole note"),
        ("quantize 4", "Quarter note"),
        ("quantize 8", "Eighth note"),
        ("quantize 16", "16th note"),
        ("quantize 32", "32nd note"),
        ("quantize 64", "64th note"),
        ("quantize 128", "128th note"),
        ("quant 0.0", "No quantization"),
        ("quant 0.5", "Half quantization"),
        ("quant 1.0", "Full quantization"),
        ("gs_timesig 4 4", "GS 4/4"),
        ("gs_quantize 8", "GS eighth"),
        ("gs_quant 0.5", "GS half quant"),
        ("delay_timesig 4 4", "Delay 4/4"),
        ("delay_quantize 4", "Delay quarter"),
        ("delay_quant 0.75", "Delay 75% quant"),
    ],

    # =========================================================================
    # GRAIN DELAY
    # =========================================================================
    "delay": [
        # DD-4 mode
        ("delay_mode 0", "DD-4 mode"),
        ("gdelay_time 0.0", "No delay"),
        ("gdelay_time 0.1", "100ms delay"),
        ("gdelay_time 0.3", "300ms delay"),
        ("gdelay_time 1.0", "1 second delay"),
        ("gdelay_time 5.0", "5 second delay"),
        ("gdelay_time 9.5", "Max delay"),
        ("gdelay_feed 0.0", "No feedback"),
        ("gdelay_feed 0.3", "30% feedback"),
        ("gdelay_feed 0.7", "70% feedback"),
        ("gdelay_feed 1.0", "Max feedback"),
        ("gdelay_tone 0.0", "Dark tone"),
        ("gdelay_tone 0.5", "Neutral tone"),
        ("gdelay_tone 1.0", "Bright tone"),
        ("gdelay_mix 0.0", "Dry"),
        ("gdelay_mix 0.5", "50/50 mix"),
        ("gdelay_mix 1.0", "Wet"),
        ("gdelay_clear", "Clear delay buffer"),

        # Bencina mode
        ("delay_mode 1", "Bencina mode"),
        ("bencina_iot 10.0", "10ms grain spacing"),
        ("bencina_iot 50.0", "50ms spacing"),
        ("bencina_iot 200.0", "200ms spacing"),
        ("bencina_iot 1000.0", "Max spacing"),
        ("bencina_grainsize 0.001", "Min grain size"),
        ("bencina_grainsize 0.05", "50ms grains"),
        ("bencina_grainsize 0.5", "500ms grains"),
        ("bencina_grainsize 2.0", "Max grains"),
        ("bencina_wrap 0", "Global wrap"),
        ("bencina_wrap 1", "Splice wrap"),
        ("bencina_clear", "Clear bencina"),

        # Stut mode
        ("delay_mode 2", "Stut mode"),
        ("stut", "Trigger stut"),
        ("stut_reps 1", "1 repetition"),
        ("stut_reps 4", "4 repetitions"),
        ("stut_reps 8", "8 repetitions"),
        ("stut_reps 16", "Max repetitions"),
        ("stut_reduction 0.0", "No decay"),
        ("stut_reduction 0.5", "50% decay"),
        ("stut_reduction 1.0", "Full decay"),
        ("stut_spacing 10.0", "10ms spacing"),
        ("stut_spacing 100.0", "100ms spacing"),
        ("stut_spacing 500.0", "500ms spacing"),

        # Back to DD-4
        ("delay_mode 0", "Back to DD-4"),
    ],

    # =========================================================================
    # FOG (SPECTRAL EFFECT)
    # =========================================================================
    "fog": [
        ("fog_smear_enable 1", "Enable smear"),
        ("fog_smear_bins 0", "No smear"),
        ("fog_smear_bins 4", "4-bin smear"),
        ("fog_smear_bins 16", "16-bin smear"),
        ("fog_smear_bins 32", "Max smear"),
        ("fog_smear_onset_curve 0", "Linear onset"),
        ("fog_smear_onset_curve 1", "Exponential onset"),
        ("fog_smear_onset_curve 2", "Logarithmic onset"),
        ("fog_smear_onset_amount 0.0", "No onset"),
        ("fog_smear_onset_amount 0.5", "Half onset"),
        ("fog_smear_onset_amount 1.0", "Full onset"),
        ("fog_mag_cutoff 0.1", "Min mag cutoff"),
        ("fog_mag_cutoff 5.0", "5 Hz cutoff"),
        ("fog_mag_cutoff 20.0", "Max mag cutoff"),
        ("fog_mag_resonance 0.1", "Min resonance"),
        ("fog_mag_resonance 2.0", "Medium resonance"),
        ("fog_mag_resonance 10.0", "Max resonance"),
        ("fog_phase_cutoff 0.1", "Min phase cutoff"),
        ("fog_phase_cutoff 5.0", "5 Hz phase cutoff"),
        ("fog_phase_cutoff 20.0", "Max phase cutoff"),
        ("fog_specmagfilter_enable 1", "Enable specmag filter"),
        ("fog_specmagfilter_onset_curve 0", "Linear specmag"),
        ("fog_specmagfilter_onset_curve 1", "Exp specmag"),
        ("fog_specmagfilter_onset_curve 2", "Log specmag"),
        ("fog_specmagfilter_onset_amount 0.0", "No specmag onset"),
        ("fog_specmagfilter_onset_amount 0.5", "Half specmag"),
        ("fog_specmagfilter_onset_amount 1.0", "Full specmag"),
        ("fog_stereo_filter_mode 0", "Shared stereo"),
        ("fog_stereo_filter_mode 1", "Independent stereo"),
        ("fog_position 0", "Per-grain fog"),
        ("fog_position 1", "Post-mix fog"),
        ("fog_smear_enable 0", "Disable smear"),
        ("fog_specmagfilter_enable 0", "Disable specmag"),
    ],

    # =========================================================================
    # DISTORTION
    # =========================================================================
    "distortion": [
        ("distortion_enable 1", "Enable distortion"),
        ("distortion 0.0", "Min intensity"),
        ("distortion 0.3", "Mild distortion"),
        ("distortion 0.7", "Heavy distortion"),
        ("distortion 1.0", "Max intensity"),
        ("distortion_oversampling 1", "No oversampling"),
        ("distortion_oversampling 2", "2x oversampling"),
        ("distortion_oversampling 4", "4x oversampling"),
        ("distortion_oversampling 8", "8x oversampling"),
        ("distortion_position 0", "Per-grain distortion"),
        ("distortion_position 1", "Post-mix distortion"),
        ("distortion_pre_hp_freq 30.0", "Min pre HP"),
        ("distortion_pre_hp_freq 200.0", "200 Hz pre HP"),
        ("distortion_pre_hp_freq 500.0", "Max pre HP"),
        ("distortion_pre_hp_mix 0.0", "No pre HP"),
        ("distortion_pre_hp_mix 0.5", "Half pre HP"),
        ("distortion_pre_hp_mix 1.0", "Full pre HP"),
        ("distortion_post_lp_freq 2400.0", "Min post LP"),
        ("distortion_post_lp_freq 5000.0", "5 kHz post LP"),
        ("distortion_post_lp_freq 10000.0", "Max post LP"),
        ("distortion_post_lp_mix 0.0", "No post LP"),
        ("distortion_post_lp_mix 1.0", "Full post LP"),
        ("distortion_notch_freq 100.0", "100 Hz notch"),
        ("distortion_notch_freq 1000.0", "1 kHz notch"),
        ("distortion_notch_bw 10.0", "Narrow notch"),
        ("distortion_notch_bw 200.0", "Wide notch"),
        ("distortion_notch_mix 0.0", "No notch"),
        ("distortion_notch_mix 1.0", "Full notch"),
        # Enhanced distortion
        ("dist_emphasis_mode 0", "HP emphasis"),
        ("dist_emphasis_mode 1", "LP emphasis"),
        ("dist_emphasis_freq 100.0", "100 Hz emphasis"),
        ("dist_emphasis_freq 2000.0", "2 kHz emphasis"),
        ("dist_emphasis_freq 5000.0", "5 kHz emphasis"),
        ("dist_pregain 0.1", "Min pregain"),
        ("dist_pregain 1.0", "Unity pregain"),
        ("dist_pregain 5.0", "5x pregain"),
        ("dist_pregain 10.0", "Max pregain"),
        ("dist_waveshaper_mode 0", "Tanh"),
        ("dist_waveshaper_mode 1", "Arctan"),
        ("dist_waveshaper_mode 2", "Asymmetric"),
        ("dist_waveshaper_mode 3", "Blend"),
        ("dist_waveshaper_mode 4", "Polynomial"),
        ("dist_curve_blend 0.0", "Min blend"),
        ("dist_curve_blend 0.5", "Half blend"),
        ("dist_curve_blend 1.0", "Max blend"),
        ("dist_drive_pos 1.0", "Min pos drive"),
        ("dist_drive_pos 10.0", "10x pos drive"),
        ("dist_drive_pos 20.0", "Max pos drive"),
        ("dist_drive_neg 1.0", "Min neg drive"),
        ("dist_drive_neg 10.0", "10x neg drive"),
        ("dist_drive_neg 20.0", "Max neg drive"),
        ("dist_poly_c1 0.0", "c1 zero"),
        ("dist_poly_c1 1.0", "c1 = 1"),
        ("dist_poly_c1 -5.0", "c1 = -5"),
        ("dist_poly_c1 10.0", "c1 = 10"),
        ("dist_poly_c2 0.0", "c2 zero"),
        ("dist_poly_c2 3.0", "c2 = 3"),
        ("dist_poly_c2 -10.0", "c2 = -10"),
        ("dist_poly_c3 0.0", "c3 zero"),
        ("dist_poly_c3 5.0", "c3 = 5"),
        ("dist_poly_c3 -10.0", "c3 = -10"),
        ("distortion_enable 0", "Disable distortion"),
    ],

    # =========================================================================
    # MOOG LADDER FILTER
    # =========================================================================
    "moog": [
        ("moog_enable 1", "Enable moog"),
        ("moog_cutoff 20.0", "Min cutoff"),
        ("moog_cutoff 500.0", "500 Hz cutoff"),
        ("moog_cutoff 2000.0", "2 kHz cutoff"),
        ("moog_cutoff 10000.0", "10 kHz cutoff"),
        ("moog_cutoff 20000.0", "Max cutoff"),
        ("moog_resonance 0.0", "No resonance"),
        ("moog_resonance 1.0", "Medium resonance"),
        ("moog_resonance 2.0", "High resonance"),
        ("moog_resonance 4.0", "Max resonance"),
        ("moog_mix 0.0", "Dry moog"),
        ("moog_mix 0.5", "Half moog"),
        ("moog_mix 1.0", "Wet moog"),
        ("moog_fb_threshold 0.5", "Half threshold"),
        ("moog_fb_threshold 1.0", "Full threshold"),
        ("moog_fb_saturation 0.5", "Half saturation"),
        ("moog_fb_saturation 1.0", "Full saturation"),
        ("moog_enable 0", "Disable moog"),
    ],

    # =========================================================================
    # PARAMETER MODULATION
    # =========================================================================
    "modulation": [
        # param_range tests
        ("param_range speed 0.5 2.0", "Speed range"),
        ("param_range iot 0.01 0.2", "IOT range"),
        ("param_range grainsize 0.01 0.5", "Grain size range"),
        ("param_range grainstart 0.0 1.0", "Grain start range"),
        ("param_range amplitude 0.3 1.0", "Amplitude range"),
        ("param_range pan 0.0 1.0", "Pan range"),
        ("param_range gdelay 0.1 0.5", "Delay range"),
        ("param_range distortion 0.0 1.0", "Distortion range"),
        ("param_range scanrate 0.5 2.0", "Scanrate range"),
        ("param_range maxgrains 1 50", "Max grains range"),
        # New fog modulation ranges
        ("param_range fog_mix 0.0 1.0", "Fog mix range"),
        ("param_range fog_smear_bins 0 16", "Fog smear bins range"),
        ("param_range fog_smear_onset 0.0 1.0", "Fog smear onset range"),
        ("param_range fog_mag_cutoff 0.1 20.0", "Fog mag cutoff range"),
        ("param_range fog_mag_resonance 0.1 10.0", "Fog mag resonance range"),
        ("param_range fog_phase_cutoff 0.1 20.0", "Fog phase cutoff range"),
        ("param_range fog_smf_onset 0.0 1.0", "Fog SMF onset range"),
        # New stut/bencina ranges
        ("param_range stut_reps 1 16", "Stut reps range"),
        ("param_range bencina_iot 10.0 500.0", "Bencina IOT range"),
        ("param_range bencina_grainsize 0.01 1.0", "Bencina grainsize range"),
        # Moog ranges
        ("param_range moog_cutoff 200.0 8000.0", "Moog cutoff range"),
        ("param_range moog_resonance 0.0 4.0", "Moog resonance range"),
        ("param_range moog_mix 0.0 1.0", "Moog mix range"),
        # Distortion ranges
        ("param_range dist_pregain 0.5 5.0", "Dist pregain range"),
        ("param_range dist_curve_blend 0.0 1.0", "Dist blend range"),
        ("param_range dist_drive_pos 1.0 10.0", "Dist drive pos range"),
        ("param_range dist_drive_neg 1.0 10.0", "Dist drive neg range"),
        ("param_range dist_poly_c1 -5.0 5.0", "Dist c1 range"),
        ("param_range dist_poly_c2 -5.0 5.0", "Dist c2 range"),
        ("param_range dist_poly_c3 -5.0 5.0", "Dist c3 range"),

        # param_base_value tests
        ("param_base_value speed 1.0", "Speed base"),
        ("param_base_value pan 0.5", "Pan base"),

        # param_slew tests
        ("param_slew speed 10.0", "Speed slew"),
        ("param_slew iot 5.0", "IOT slew"),

        # param_invert
        ("param_invert speed 1", "Invert speed"),
        ("param_invert speed 0", "Un-invert speed"),

        # param_lock
        ("param_lock speed 1", "Lock speed"),
        ("param_lock speed 0", "Unlock speed"),

        # rand_type assignments
        ("rand_type rand_1 speed", "Rand for speed"),
        ("rand_type perlin_1d_1 iot", "Perlin 1D for IOT"),
        ("rand_type perlin_2d_1 grainsize", "Perlin 2D for grainsize"),
        ("rand_type lorenz_1 pan", "Lorenz for pan"),
        ("rand_type nbody_1 amplitude", "N-body for amplitude"),
        ("rand_type sphere_1 grainstart", "Sphere for grainstart"),
        ("rand_type perlin_1d_2 gdelay", "Perlin for delay"),
        ("rand_type rand_3 distortion", "Rand for distortion"),

        # noise_freq
        ("noise_freq 1.0", "All generators 1 Hz"),
        ("noise_freq 5.0", "All generators 5 Hz"),
        ("noise_freq_1 2.0", "Gen 1 at 2 Hz"),
        ("noise_freq_2 3.0", "Gen 2 at 3 Hz"),
        ("noise_freq_3 4.0", "Gen 3 at 4 Hz"),
        ("noise_freq_4 5.0", "Gen 4 at 5 Hz"),

        # Disable all ranges
        ("param_range speed 0 0", "Disable speed range"),
        ("param_range iot 0 0", "Disable IOT range"),
        ("param_range grainsize 0 0", "Disable grainsize range"),
        ("param_range pan 0 0", "Disable pan range"),
    ],

    # =========================================================================
    # PITCH CONTROL
    # =========================================================================
    "pitch": [
        ("pitch_mode 0", "Pitch off"),
        ("pitch_mode 1", "Semitones mode"),
        ("pitch_semitones 0", "No transposition"),
        ("pitch_semitones 12", "+1 octave"),
        ("pitch_semitones -12", "-1 octave"),
        ("pitch_semitones 7", "+perfect fifth"),
        ("pitch_semitones -24", "-2 octaves"),
        ("pitch_semitones 24", "+2 octaves"),
        ("pitch_mode 2", "Range mode"),
        ("pitch_range -12 12", "Octave range"),
        ("pitch_range -7 7", "Fifth range"),
        ("pitch_rand_type rand_1", "Random pitch"),
        ("pitch_rand_type perlin_1d_1", "Perlin pitch"),
        ("pitch_mode 3", "Scale mode"),
        ("pitch_scale 0 2 4 5 7 9 11", "Major scale"),
        ("pitch_scale 0 2 3 5 7 8 10", "Minor scale"),
        ("pitch_scale 0 3 5 7 10", "Minor pentatonic"),
        ("pitch_scale 0 2 4 7 9", "Major pentatonic"),
        ("pitch_scale 0 1 5 7 10", "Japanese scale"),
        ("pitch_mode 4", "MIDI mode"),
        ("pitch_mode 0", "Pitch off again"),
    ],

    # =========================================================================
    # N-BODY SIMULATION
    # =========================================================================
    "nbody": [
        ("nbody_epsilon 1 0.1", "Instance 1 epsilon"),
        ("nbody_epsilon 2 0.5", "Instance 2 epsilon"),
        ("nbody_epsilon 3 1.0", "Instance 3 epsilon"),
        ("nbody_epsilon 4 0.01", "Instance 4 epsilon"),
        ("nbody_damping 1 0.99", "Instance 1 damping"),
        ("nbody_damping 2 0.95", "Instance 2 damping"),
        ("nbody_pump 1 0.5 10.0", "Instance 1 pump"),
        ("nbody_pump 2 1.0 5.0", "Instance 2 pump"),
        ("nbody_G 1 1.0", "Instance 1 G"),
        ("nbody_G 2 0.5", "Instance 2 G"),
        ("nbody_G 3 2.0", "Instance 3 G"),
        ("nbody_mode 1 0", "Mode 0 inst 1"),
        ("nbody_mode 1 5", "Mode 5 inst 1"),
        ("nbody_mode 1 10", "Mode 10 inst 1"),
        ("nbody_reset 1", "Reset instance 1"),
        ("nbody_reset 2", "Reset instance 2"),
        ("nbody_reset 3", "Reset instance 3"),
        ("nbody_reset 4", "Reset instance 4"),
        ("perlin_reset 1", "Reset perlin 1"),
        ("perlin_reset 2", "Reset perlin 2"),
        ("lorenz_reset 1", "Reset lorenz 1"),
        ("lorenz_reset 2", "Reset lorenz 2"),
    ],

    # =========================================================================
    # SPHERE PHYSICS
    # =========================================================================
    "sphere": [
        ("sphere_kick 1 0.5 0.3 0.1", "Kick sphere 1"),
        ("sphere_kick 2 -0.5 0.8 -0.3", "Kick sphere 2"),
        ("sphere_kick 3 1.0 -1.0 0.5", "Kick sphere 3"),
        ("sphere_kick 4 0.0 0.0 1.0", "Kick sphere 4"),
        ("sphere_damping 1 0.99", "Sphere 1 damping"),
        ("sphere_damping 2 0.5", "Sphere 2 damping"),
        ("sphere_elasticity 1 0.8", "Sphere 1 elasticity"),
        ("sphere_elasticity 2 1.0", "Sphere 2 elasticity"),
        ("sphere_mode 1 0", "Sphere mode 0"),
        ("sphere_mode 1 3", "Sphere mode 3"),
        ("sphere_mode 1 6", "Sphere mode 6"),
        ("sphere_reset 1", "Reset sphere 1"),
        ("sphere_reset 2", "Reset sphere 2"),
        ("sphere_reset 3", "Reset sphere 3"),
        ("sphere_reset 4", "Reset sphere 4"),
    ],

    # =========================================================================
    # QUERY / INTROSPECTION
    # =========================================================================
    "query": [
        ("get_inlets", "Query inlets"),
        ("get_params", "Query all params"),
        ("get_ranges", "Query all ranges"),
        ("get_generators", "Query generators"),
        ("get_state", "Export full state"),
        ("query speed", "Query speed"),
        ("query iot", "Query IOT"),
        ("query grainsize", "Query grainsize"),
        ("query pan", "Query pan"),
        ("query amplitude", "Query amplitude"),
        ("query gdelay", "Query delay"),
    ],

    # =========================================================================
    # BOUNDARY & STRESS TESTS
    # =========================================================================
    "boundary": [
        # Extreme values
        ("grainsize 0.0", "Zero grain size (should clamp)"),
        ("grainsize -1.0", "Negative grain size"),
        ("grainsize 999.0", "Huge grain size"),
        ("iot 0.0", "Zero IOT"),
        ("iot -1.0", "Negative IOT"),
        ("iot 999.0", "Huge IOT"),
        ("amplitude -1.0", "Negative amplitude"),
        ("amplitude 100.0", "Huge amplitude"),
        ("pan -1.0", "Negative pan"),
        ("pan 2.0", "Over-range pan"),
        ("speed 100.0", "Huge speed"),
        ("speed -100.0", "Huge negative speed"),
        ("gdelay_time -1.0", "Negative delay time"),
        ("gdelay_time 100.0", "Huge delay time"),
        ("gdelay_feed -1.0", "Negative feedback"),
        ("gdelay_feed 2.0", "Over-range feedback"),
        ("maxgrains 0", "Zero grains"),
        ("maxgrains -1", "Negative grains"),
        ("maxgrains 10000", "Huge grain count"),
        ("envelope -1", "Negative envelope"),
        ("envelope 99", "Invalid envelope"),
        ("playhead 0", "Invalid playhead 0"),
        ("playhead 99", "Invalid playhead 99"),
        ("delay_mode -1", "Negative delay mode"),
        ("delay_mode 99", "Invalid delay mode"),
        ("moog_cutoff -100.0", "Negative moog cutoff"),
        ("moog_cutoff 100000.0", "Huge moog cutoff"),
        ("moog_resonance -1.0", "Negative resonance"),
        ("moog_resonance 100.0", "Huge resonance"),
        ("fog_smear_bins -1", "Negative smear bins"),
        ("fog_smear_bins 1000", "Huge smear bins"),
        ("distortion -1.0", "Negative distortion"),
        ("distortion 100.0", "Huge distortion"),
        ("stut_reps 0", "Zero stut reps"),
        ("stut_reps -5", "Negative stut reps"),
        ("stut_reps 1000", "Huge stut reps"),
        ("bencina_iot 0.0", "Zero bencina IOT"),
        ("bencina_iot -100.0", "Negative bencina IOT"),
        ("bencina_grainsize 0.0", "Zero bencina grain"),
        ("bencina_grainsize -1.0", "Negative bencina grain"),

        # Rapid mode switching
        ("delay_mode 0", "Switch to DD-4"),
        ("delay_mode 1", "Switch to bencina"),
        ("delay_mode 2", "Switch to stut"),
        ("delay_mode 0", "Back to DD-4"),
        ("delay_mode 1", "Back to bencina"),
        ("delay_mode 2", "Back to stut"),
        ("delay_mode 0", "DD-4 again"),

        # Rapid state changes
        ("play 1", "Start play"),
        ("record 1", "Start record during play"),
        ("play 0", "Stop play while recording"),
        ("record 0", "Stop record"),
        ("play 1", "Restart play"),
        ("recsplice", "Recsplice during play"),
        ("play 0", "Stop"),
    ],

    # =========================================================================
    # CROSS-SYSTEM STRESS
    # =========================================================================
    "stress": [
        # Enable everything simultaneously
        ("play 1", "Start playback"),
        ("distortion_enable 1", "Enable distortion"),
        ("moog_enable 1", "Enable moog"),
        ("fog_smear_enable 1", "Enable fog smear"),
        ("fog_specmagfilter_enable 1", "Enable specmag"),
        ("delay_mode 1", "Bencina mode"),
        ("gdelay_mix 0.8", "High delay mix"),
        ("gdelay_feed 0.9", "High feedback"),
        ("distortion 0.8", "High distortion"),
        ("moog_cutoff 1000.0", "Moog at 1kHz"),
        ("moog_resonance 3.0", "High resonance"),
        ("moog_mix 0.7", "Wet moog"),
        ("grainsize 0.01", "Very small grains"),
        ("iot 0.005", "Very fast IOT"),
        ("maxgrains 100", "Max grains"),
        ("speed 2.0", "Double speed"),
        ("amplitude 1.5", "Hot signal"),
        # Rapid parameter changes
        ("grainsize 0.5", "Change grain size"),
        ("grainsize 0.001", "Tiny grains"),
        ("grainsize 5.0", "Huge grains"),
        ("iot 2.0", "Slow IOT"),
        ("iot 0.001", "Fast IOT"),
        ("speed -2.0", "Reverse"),
        ("speed 4.0", "Fast forward"),
        # Mode switches under load
        ("delay_mode 0", "DD-4 under load"),
        ("delay_mode 2", "Stut under load"),
        ("stut", "Trigger stut under load"),
        ("delay_mode 1", "Bencina under load"),
        # Cleanup
        ("play 0", "Stop"),
        ("distortion_enable 0", "Disable dist"),
        ("moog_enable 0", "Disable moog"),
        ("fog_smear_enable 0", "Disable fog"),
        ("fog_specmagfilter_enable 0", "Disable specmag"),
        ("gdelay_mix 0.0", "Dry delay"),
        ("gdelay_feed 0.0", "No feedback"),
    ],
}


def send_fudi(sock, message):
    """Send a FUDI (Pd protocol) message."""
    fudi = message + ";\n"
    sock.sendall(fudi.encode("utf-8"))


def main():
    print("=" * 70)
    print("ligase~ Message Handler Test")
    print("=" * 70)
    print(f"Connecting to Pd at {HOST}:{PORT}...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((HOST, PORT))
        print("Connected!\n")
    except Exception as e:
        print(f"ERROR: Could not connect to Pd: {e}")
        print("Make sure Pd is running with patches/osc_msg_test.pd")
        sys.exit(1)

    total_sent = 0
    total_categories = 0
    errors = []

    for category, messages in MESSAGES.items():
        total_categories += 1
        print(f"[{category}] Sending {len(messages)} messages...")

        for msg, desc in messages:
            try:
                send_fudi(sock, msg)
                total_sent += 1
                time.sleep(DELAY)
            except Exception as e:
                errors.append((category, msg, str(e)))
                print(f"  ERROR sending '{msg}': {e}")

        # Small pause between categories
        time.sleep(0.05)

    # Final queries to verify state
    print("\n[final] Sending final state queries...")
    for msg in ["get_params", "get_ranges", "get_generators", "get_state"]:
        try:
            send_fudi(sock, msg)
            total_sent += 1
            time.sleep(0.01)
        except Exception as e:
            errors.append(("final", msg, str(e)))

    # Send quit after a delay
    time.sleep(1.0)
    print("\nSending quit command...")
    try:
        send_fudi(sock, "pd quit")
    except Exception:
        pass

    sock.close()

    print("\n" + "=" * 70)
    print("RESULTS")
    print("=" * 70)
    print(f"Categories tested: {total_categories}")
    print(f"Messages sent:     {total_sent}")
    print(f"Send errors:       {len(errors)}")

    if errors:
        print("\nErrors:")
        for cat, msg, err in errors:
            print(f"  [{cat}] {msg}: {err}")
        return 1
    else:
        print("\nAll messages sent successfully!")
        return 0


if __name__ == "__main__":
    sys.exit(main())
