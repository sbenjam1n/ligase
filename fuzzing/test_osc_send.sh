#!/bin/bash
# Test ligase~ OSC message handling via oscsend
# Sends OSC messages to ligase~-osc.pd abstraction on port 9001
#
# Usage:
#   1. Start Pd with OSC test patch
#   2. Run: ./test_osc_send.sh

PORT=9001
HOST=localhost
DELAY=0.01
SENT=0
ERRORS=0

send_osc() {
    local addr="$1"
    shift
    if oscsend "$HOST" "$PORT" "$addr" "$@" 2>/dev/null; then
        ((SENT++))
    else
        echo "  ERROR: oscsend $HOST $PORT $addr $@"
        ((ERRORS++))
    fi
    sleep "$DELAY"
}

echo "========================================"
echo "ligase~ OSC Message Test"
echo "========================================"
echo "Target: $HOST:$PORT"
echo ""

# ---- File ----
echo "[file]"
send_osc /ligase/file/load s "test.wav"
send_osc /ligase/file/save s "output.wav"

# ---- Transport ----
echo "[transport]"
send_osc /ligase/transport/play f 1
send_osc /ligase/transport/record f 1
send_osc /ligase/transport/record f 0
send_osc /ligase/transport/recsplice
send_osc /ligase/transport/recinput
send_osc /ligase/transport/clockstop
send_osc /ligase/transport/play f 0

# ---- Splice ----
echo "[splice]"
send_osc /ligase/splice/add
send_osc /ligase/splice/shift f 1
send_osc /ligase/splice/shift f -1
send_osc /ligase/splice/organize f 0.5
send_osc /ligase/splice/create_pos f 0
send_osc /ligase/splice/jump f 1
send_osc /ligase/splice/finish_nav f 0
send_osc /ligase/splice/split f 0
send_osc /ligase/splice/join_right
send_osc /ligase/splice/join_all
send_osc /ligase/splice/clear_except
send_osc /ligase/splice/clear_current
send_osc /ligase/splice/clear

# ---- Playhead ----
echo "[playhead]"
send_osc /ligase/playhead/mode f 1
send_osc /ligase/playhead/mode f 2
send_osc /ligase/playhead/scanrate f 1.0
send_osc /ligase/playhead/speed f 1.0
send_osc /ligase/playhead/speed f -1.0
send_osc /ligase/playhead/clock_advance_quant f 0
send_osc /ligase/playhead/headless f 0

# ---- Grain ----
echo "[grain]"
send_osc /ligase/grain/grainsize f 0.1
send_osc /ligase/grain/grainstart f 0.0
send_osc /ligase/grain/envelope f 0
send_osc /ligase/grain/envelope f 2
send_osc /ligase/grain/env_skew f 0.5
send_osc /ligase/grain/maxgrains f 50
send_osc /ligase/grain/iot f 0.05
send_osc /ligase/grain/amplitude f 0.8
send_osc /ligase/grain/pan f 0.5
send_osc /ligase/grain/pan_mode f 0
send_osc /ligase/grain/sos f 0.0
send_osc /ligase/grain/sos_mode f 1
send_osc /ligase/grain/saw_cycles f 4
send_osc /ligase/grain/saw_depth f 0.5
send_osc /ligase/grain/grain_bang_rate f 10

# ---- Timing ----
echo "[timing]"
send_osc /ligase/timing/bang
send_osc /ligase/timing/clockstop
send_osc /ligase/timing/timesig s "4/4"
send_osc /ligase/timing/quantize f 16
send_osc /ligase/timing/quant f 0.75
send_osc /ligase/timing/gs_timesig s "4/4"
send_osc /ligase/timing/gs_quantize f 8
send_osc /ligase/timing/gs_quant f 0.5
send_osc /ligase/timing/delay_timesig s "4/4"
send_osc /ligase/timing/delay_quantize f 4
send_osc /ligase/timing/delay_quant f 0.5

# ---- Delay ----
echo "[delay]"
send_osc /ligase/delay/mode f 0
send_osc /ligase/delay/time f 0.3
send_osc /ligase/delay/feedback f 0.5
send_osc /ligase/delay/tone f 0.7
send_osc /ligase/delay/mix f 0.5
send_osc /ligase/delay/clear
send_osc /ligase/delay/mode f 1
send_osc /ligase/delay/bencina_iot f 50.0
send_osc /ligase/delay/bencina_grainsize f 0.1
send_osc /ligase/delay/bencina_wrap f 0
send_osc /ligase/delay/bencina_clear
send_osc /ligase/delay/mode f 2
send_osc /ligase/delay/stut
send_osc /ligase/delay/stut_reps f 4
send_osc /ligase/delay/stut_reduction f 0.5
send_osc /ligase/delay/stut_spacing f 100.0
send_osc /ligase/delay/mode f 0

# ---- Fog ----
echo "[fog]"
send_osc /ligase/fog/smear_enable f 1
send_osc /ligase/fog/smear_bins f 8
send_osc /ligase/fog/smear_onset_curve f 1
send_osc /ligase/fog/smear_onset_amount f 0.5
send_osc /ligase/fog/mag_cutoff f 5.0
send_osc /ligase/fog/mag_resonance f 2.0
send_osc /ligase/fog/phase_cutoff f 5.0
send_osc /ligase/fog/specmagfilter_enable f 1
send_osc /ligase/fog/specmagfilter_onset_curve f 0
send_osc /ligase/fog/specmagfilter_onset_amount f 0.5
send_osc /ligase/fog/stereo_filter_mode f 0
send_osc /ligase/fog/position f 0
send_osc /ligase/fog/smear_enable f 0
send_osc /ligase/fog/specmagfilter_enable f 0

# ---- Distortion ----
echo "[distortion]"
send_osc /ligase/distortion/enable f 1
send_osc /ligase/distortion/intensity f 0.5
send_osc /ligase/distortion/oversampling f 4
send_osc /ligase/distortion/position f 0
send_osc /ligase/distortion/pre_hp_freq f 200.0
send_osc /ligase/distortion/pre_hp_mix f 0.5
send_osc /ligase/distortion/post_lp_freq f 5000.0
send_osc /ligase/distortion/post_lp_mix f 0.5
send_osc /ligase/distortion/notch_freq f 1000.0
send_osc /ligase/distortion/notch_bw f 100.0
send_osc /ligase/distortion/notch_mix f 0.5
send_osc /ligase/distortion/emphasis_mode f 0
send_osc /ligase/distortion/emphasis_freq f 2000.0
send_osc /ligase/distortion/pregain f 3.0
send_osc /ligase/distortion/waveshaper_mode f 0
send_osc /ligase/distortion/waveshaper_mode f 1
send_osc /ligase/distortion/waveshaper_mode f 2
send_osc /ligase/distortion/waveshaper_mode f 3
send_osc /ligase/distortion/waveshaper_mode f 4
send_osc /ligase/distortion/curve_blend f 0.5
send_osc /ligase/distortion/drive_pos f 5.0
send_osc /ligase/distortion/drive_neg f 5.0
send_osc /ligase/distortion/poly_c1 f 1.0
send_osc /ligase/distortion/poly_c2 f 2.0
send_osc /ligase/distortion/poly_c3 f 3.0
send_osc /ligase/distortion/enable f 0

# ---- Moog ----
echo "[moog]"
send_osc /ligase/moog/enable f 1
send_osc /ligase/moog/cutoff f 2000.0
send_osc /ligase/moog/resonance f 2.0
send_osc /ligase/moog/mix f 0.7
send_osc /ligase/moog/fb_threshold f 0.8
send_osc /ligase/moog/fb_saturation f 0.5
send_osc /ligase/moog/enable f 0

# ---- Modulation ----
echo "[modulation]"
send_osc /ligase/modulation/param_range sff speed 0.5 2.0
send_osc /ligase/modulation/param_range sff iot 0.01 0.1
send_osc /ligase/modulation/rand_type ss rand_1 speed
send_osc /ligase/modulation/rand_type ss perlin_1d_1 iot
send_osc /ligase/modulation/noise_freq f 3.0
send_osc /ligase/modulation/noise_freq_1 f 2.0
send_osc /ligase/modulation/noise_freq_2 f 4.0
send_osc /ligase/modulation/param_range sff speed 0 0

# ---- Pitch ----
echo "[pitch]"
send_osc /ligase/pitch/mode f 1
send_osc /ligase/pitch/semitones f 7
send_osc /ligase/pitch/mode f 2
send_osc /ligase/pitch/range ff -12 12
send_osc /ligase/pitch/rand_type s perlin_1d_1
send_osc /ligase/pitch/mode f 3
send_osc /ligase/pitch/scale iiiii 0 2 4 7 9
send_osc /ligase/pitch/mode f 0

# ---- N-Body ----
echo "[nbody]"
send_osc /ligase/nbody/epsilon ff 1 0.5
send_osc /ligase/nbody/damping ff 1 0.99
send_osc /ligase/nbody/pump fff 1 0.5 10.0
send_osc /ligase/nbody/G ff 1 1.0
send_osc /ligase/nbody/mode ff 1 5
send_osc /ligase/nbody/reset f 1
send_osc /ligase/nbody/perlin_reset f 1
send_osc /ligase/nbody/lorenz_reset f 1

# ---- Sphere ----
echo "[sphere]"
send_osc /ligase/sphere/kick ffff 1 0.5 0.3 0.1
send_osc /ligase/sphere/damping ff 1 0.99
send_osc /ligase/sphere/elasticity ff 1 0.8
send_osc /ligase/sphere/mode ff 1 3
send_osc /ligase/sphere/reset f 1

# ---- Query ----
echo "[query]"
send_osc /ligase/query/get_inlets
send_osc /ligase/query/get_params
send_osc /ligase/query/get_ranges
send_osc /ligase/query/get_generators
send_osc /ligase/query/get_state
send_osc /ligase/query/query s speed

echo ""
echo "========================================"
echo "RESULTS"
echo "========================================"
echo "Messages sent: $SENT"
echo "Errors:        $ERRORS"

if [ "$ERRORS" -eq 0 ]; then
    echo "All OSC messages sent successfully!"
else
    echo "Some messages failed to send."
    exit 1
fi
