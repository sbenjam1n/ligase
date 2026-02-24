#!/usr/bin/env python3
"""Generate ligase~-osc.pd - OSC to ligase~ message router.

Creates a Pure Data abstraction that receives OSC messages on a UDP port
and converts them to ligase~ messages. Uses mrpeach's udpreceive,
unpackOSC, and routeOSC objects.

OSC Address scheme: /ligase/<category>/<parameter>

Categories:
  /file, /transport, /splice, /playhead, /grain, /timing,
  /delay, /fog, /distortion, /moog, /modulation, /pitch,
  /nbody, /sphere, /query
"""

import sys

# Each category maps to a list of (osc_name, pd_message, arg_type)
# arg_type: 'f' = float, 's' = symbol, 'b' = bang (no args), 'l' = list/gimme, 'sf' = symbol+float
CATEGORIES = {
    "file": [
        ("load", "load", "s"),
        ("save", "save", "s"),
    ],
    "transport": [
        ("play", "play", "f"),
        ("record", "record", "f"),
        ("recsplice", "recsplice", "b"),
        ("recinput", "recinput", "b"),
        ("clockstop", "clockstop", "b"),
        ("bang", None, "bang"),  # special: sends bang
    ],
    "splice": [
        ("shift", "shift", "f"),
        ("organize", "organize", "f"),
        ("add", "splice", "b"),
        ("clear", "clear_splices", "b"),
        ("clear_except", "clear_splices_except_current", "b"),
        ("join_right", "splice_join_right", "b"),
        ("join_all", "splice_join_all", "b"),
        ("clear_current", "clear_current_splice", "b"),
        ("create_pos", "splice_create_pos", "f"),
        ("jump", "splice_jump", "f"),
        ("finish_nav", "splice_finish_nav", "f"),
        ("split", "splice_split", "f"),
        ("send_splice_msg", "send_splice_msg", "f"),
        ("clear_splice_msg", "clear_splice_msg", "f"),
    ],
    "playhead": [
        ("mode", "playhead", "f"),
        ("scanrate", "scanrate", "f"),
        ("speed", "speed", "f"),
        ("clock_advance_quant", "clock_advance_quant", "f"),
        ("headless", "headless", "f"),
    ],
    "grain": [
        ("grainsize", "grainsize", "f"),
        ("grainstart", "grainstart", "f"),
        ("envelope", "envelope", "f"),
        ("env_skew", "env_skew", "f"),
        ("maxgrains", "maxgrains", "f"),
        ("iot", "iot", "f"),
        ("amplitude", "amplitude", "f"),
        ("pan", "pan", "f"),
        ("pan_mode", "pan_mode", "f"),
        ("sos", "sos", "f"),
        ("sos_mode", "sos_mode", "f"),
        ("saw_cycles", "saw_cycles", "f"),
        ("saw_depth", "saw_depth", "f"),
        ("grain_bang_rate", "grain_bang_rate", "f"),
    ],
    "timing": [
        ("bang", None, "bang"),
        ("clockstop", "clockstop", "b"),
        ("timesig", "timesig", "l"),
        ("quantize", "quantize", "f"),
        ("quant", "quant", "f"),
        ("gs_timesig", "gs_timesig", "l"),
        ("gs_quantize", "gs_quantize", "f"),
        ("gs_quant", "gs_quant", "f"),
        ("delay_timesig", "delay_timesig", "l"),
        ("delay_quantize", "delay_quantize", "f"),
        ("delay_quant", "delay_quant", "f"),
    ],
    "delay": [
        ("time", "gdelay_time", "f"),
        ("feedback", "gdelay_feed", "f"),
        ("tone", "gdelay_tone", "f"),
        ("mix", "gdelay_mix", "f"),
        ("clear", "gdelay_clear", "b"),
        ("mode", "delay_mode", "f"),
        ("stut", "stut", "b"),
        ("stut_reps", "stut_reps", "f"),
        ("stut_reduction", "stut_reduction", "f"),
        ("stut_spacing", "stut_spacing", "f"),
        ("bencina_iot", "bencina_iot", "f"),
        ("bencina_grainsize", "bencina_grainsize", "f"),
        ("bencina_wrap", "bencina_wrap", "f"),
        ("bencina_clear", "bencina_clear", "b"),
    ],
    "fog": [
        ("smear_bins", "fog_smear_bins", "f"),
        ("smear_enable", "fog_smear_enable", "f"),
        ("smear_onset_curve", "fog_smear_onset_curve", "f"),
        ("smear_onset_amount", "fog_smear_onset_amount", "f"),
        ("mag_cutoff", "fog_mag_cutoff", "f"),
        ("mag_resonance", "fog_mag_resonance", "f"),
        ("phase_cutoff", "fog_phase_cutoff", "f"),
        ("specmagfilter_enable", "fog_specmagfilter_enable", "f"),
        ("specmagfilter_onset_curve", "fog_specmagfilter_onset_curve", "f"),
        ("specmagfilter_onset_amount", "fog_specmagfilter_onset_amount", "f"),
        ("stereo_filter_mode", "fog_stereo_filter_mode", "f"),
        ("position", "fog_position", "f"),
    ],
    "distortion": [
        ("enable", "distortion_enable", "f"),
        ("intensity", "distortion", "f"),
        ("oversampling", "distortion_oversampling", "f"),
        ("position", "distortion_position", "f"),
        ("oversample", "distortion_oversample", "f"),
        ("pre_hp_freq", "distortion_pre_hp_freq", "f"),
        ("pre_hp_mix", "distortion_pre_hp_mix", "f"),
        ("post_lp_freq", "distortion_post_lp_freq", "f"),
        ("post_lp_mix", "distortion_post_lp_mix", "f"),
        ("notch_freq", "distortion_notch_freq", "f"),
        ("notch_bw", "distortion_notch_bw", "f"),
        ("notch_mix", "distortion_notch_mix", "f"),
        ("emphasis_mode", "dist_emphasis_mode", "f"),
        ("emphasis_freq", "dist_emphasis_freq", "f"),
        ("pregain", "dist_pregain", "f"),
        ("waveshaper_mode", "dist_waveshaper_mode", "f"),
        ("curve_blend", "dist_curve_blend", "f"),
        ("drive_pos", "dist_drive_pos", "f"),
        ("drive_neg", "dist_drive_neg", "f"),
        ("poly_c1", "dist_poly_c1", "f"),
        ("poly_c2", "dist_poly_c2", "f"),
        ("poly_c3", "dist_poly_c3", "f"),
    ],
    "moog": [
        ("cutoff", "moog_cutoff", "f"),
        ("resonance", "moog_resonance", "f"),
        ("mix", "moog_mix", "f"),
        ("enable", "moog_enable", "f"),
        ("fb_threshold", "moog_fb_threshold", "f"),
        ("fb_saturation", "moog_fb_saturation", "f"),
    ],
    "modulation": [
        ("param_range", "param_range", "l"),
        ("param_base_value", "param_base_value", "l"),
        ("param_slew", "param_slew", "l"),
        ("param_invert", "param_invert", "l"),
        ("param_lock", "param_lock", "l"),
        ("rand_type", "rand_type", "l"),
        ("noise_freq", "noise_freq", "f"),
        ("noise_freq_1", "noise_freq_1", "f"),
        ("noise_freq_2", "noise_freq_2", "f"),
        ("noise_freq_3", "noise_freq_3", "f"),
        ("noise_freq_4", "noise_freq_4", "f"),
    ],
    "pitch": [
        ("mode", "pitch_mode", "f"),
        ("semitones", "pitch_semitones", "f"),
        ("range", "pitch_range", "l"),
        ("rand_type", "pitch_rand_type", "s"),
        ("scale", "pitch_scale", "l"),
    ],
    "nbody": [
        ("epsilon", "nbody_epsilon", "l"),
        ("damping", "nbody_damping", "l"),
        ("pump", "nbody_pump", "l"),
        ("G", "nbody_G", "l"),
        ("reset", "nbody_reset", "f"),
        ("mode", "nbody_mode", "l"),
        ("perlin_reset", "perlin_reset", "f"),
        ("lorenz_reset", "lorenz_reset", "f"),
    ],
    "sphere": [
        ("kick", "sphere_kick", "l"),
        ("damping", "sphere_damping", "l"),
        ("elasticity", "sphere_elasticity", "l"),
        ("reset", "sphere_reset", "f"),
        ("mode", "sphere_mode", "l"),
    ],
    "query": [
        ("get_inlets", "get_inlets", "b"),
        ("query", "query", "s"),
        ("get_params", "get_params", "b"),
        ("get_ranges", "get_ranges", "b"),
        ("get_generators", "get_generators", "b"),
        ("get_state", "get_state", "b"),
    ],
}


def generate_subcanvas(cat_name, params, x_offset=0, y_offset=0):
    """Generate a sub-canvas (subpatch) for a category."""
    lines = []
    objects = []  # (idx, type, text)
    connections = []

    # Canvas header
    w = max(600, len(params) * 70 + 100)
    lines.append(f"#N canvas 0 0 {w} 500 {cat_name} 0;")

    idx = 0
    # Inlet
    objects.append((idx, "obj", f"30 20 inlet"))
    inlet_idx = idx
    idx += 1

    # routeOSC with all parameter names
    osc_names = [p[0] for p in params]
    route_args = " ".join(f"/{n}" for n in osc_names)
    objects.append((idx, "obj", f"30 60 routeOSC {route_args}"))
    route_idx = idx
    idx += 1

    # Connect inlet -> routeOSC
    connections.append((inlet_idx, 0, route_idx, 0))

    # For each parameter, create the converter chain
    converter_indices = []
    x_pos = 30
    y_row1 = 110  # extractor row
    y_row2 = 150  # pack/symbol row
    y_row3 = 190  # list trim row (if needed)

    for i, (osc_name, pd_msg, arg_type) in enumerate(params):
        if arg_type == "bang":
            # Special bang: just send a bang
            objects.append((idx, "obj", f"{x_pos} {y_row1} b"))
            bang_idx = idx
            idx += 1
            connections.append((route_idx, i, bang_idx, 0))
            converter_indices.append(bang_idx)

        elif arg_type == "b":
            # No-arg message: send the symbol
            objects.append((idx, "obj", f"{x_pos} {y_row1} b"))
            b_idx = idx
            idx += 1
            objects.append((idx, "msg", f"{x_pos} {y_row2} {pd_msg}"))
            msg_idx = idx
            idx += 1
            connections.append((route_idx, i, b_idx, 0))
            connections.append((b_idx, 0, msg_idx, 0))
            converter_indices.append(msg_idx)

        elif arg_type == "f":
            # Float arg: prepend message name, trim to make it a proper message
            # list prepend <msg> turns input "0.5" into "list <msg> 0.5"
            # list trim turns "list <msg> 0.5" into "<msg> 0.5" (symbol first → selector)
            objects.append((idx, "obj", f"{x_pos} {y_row1} list prepend {pd_msg}"))
            prepend_idx = idx
            idx += 1
            objects.append((idx, "obj", f"{x_pos} {y_row2} list trim"))
            trim_idx = idx
            idx += 1
            connections.append((route_idx, i, prepend_idx, 0))
            connections.append((prepend_idx, 0, trim_idx, 0))
            converter_indices.append(trim_idx)

        elif arg_type == "s":
            # Symbol arg: prepend message name
            objects.append((idx, "obj", f"{x_pos} {y_row1} list prepend {pd_msg}"))
            prepend_idx = idx
            idx += 1
            objects.append((idx, "obj", f"{x_pos} {y_row2} list trim"))
            trim_idx = idx
            idx += 1
            connections.append((route_idx, i, prepend_idx, 0))
            connections.append((prepend_idx, 0, trim_idx, 0))
            converter_indices.append(trim_idx)

        elif arg_type == "l":
            # List/gimme: prepend message name
            objects.append((idx, "obj", f"{x_pos} {y_row1} list prepend {pd_msg}"))
            prepend_idx = idx
            idx += 1
            objects.append((idx, "obj", f"{x_pos} {y_row2} list trim"))
            trim_idx = idx
            idx += 1
            connections.append((route_idx, i, prepend_idx, 0))
            connections.append((prepend_idx, 0, trim_idx, 0))
            converter_indices.append(trim_idx)

        x_pos += 70

    # Outlet
    y_outlet = 240
    objects.append((idx, "obj", f"30 {y_outlet} outlet"))
    outlet_idx = idx
    idx += 1

    # Connect all converters to outlet
    for conv_idx in converter_indices:
        connections.append((conv_idx, 0, outlet_idx, 0))

    # Format Pd objects
    for obj_idx, obj_type, obj_text in objects:
        lines.append(f"#X {obj_type} {obj_text};")

    # Format connections
    for src, src_out, dst, dst_in in connections:
        lines.append(f"#X connect {src} {src_out} {dst} {dst_in};")

    return lines


def generate_main_patch(port=9000):
    """Generate the main ligase~-osc.pd patch."""
    lines = []
    lines.append("#N canvas 300 100 1000 700 12;")

    # In Pd, ALL top-level items (obj, text, msg, restore) get sequential indices.
    # We must track the global index to make connections work.
    idx = 0

    # META subpatch — counts as one object (the restore line)
    lines.append("#N canvas 0 0 450 300 META 0;")
    lines.append("#X text 10 10 DESCRIPTION OSC control interface for ligase~;")
    lines.append("#X text 10 30 AUTHOR ligase project;")
    lines.append("#X text 10 50 LICENSE BSD;")
    lines.append("#X restore 10 10 pd META;")
    idx += 1  # META = idx 0

    # Title text — each counts as an object
    lines.append("#X text 20 40 ligase~-osc - OSC to ligase~ message router;")
    idx += 1  # text = idx 1
    lines.append(f"#X text 20 60 Usage: [ligase~-osc <port>] (default: {port});")
    idx += 1  # text = idx 2
    lines.append("#X text 20 80 OSC Address scheme: /ligase/<category>/<parameter>;")
    idx += 1  # text = idx 3

    # Inlet (for passthrough)
    lines.append(f"#X obj 50 120 inlet;")
    inlet_idx = idx
    idx += 1

    # OSC receiver subpatch
    # udpreceive requires port at creation time, so use $1 directly
    # If $1 is 0 (no arg), default to {port}
    osc_recv_lines = []
    osc_recv_lines.append(f"#N canvas 0 0 450 300 osc_receiver 0;")
    osc_recv_lines.append(f"#X obj 30 30 udpreceive \\$1;")
    osc_recv_lines.append(f"#X obj 30 60 unpackOSC;")
    osc_recv_lines.append(f"#X obj 30 90 routeOSC /ligase;")
    osc_recv_lines.append(f"#X obj 30 120 outlet;")
    osc_recv_lines.append(f"#X text 150 30 Port from creation arg: [ligase~-osc <port>];")
    osc_recv_lines.append(f"#X connect 0 0 1 0;")
    osc_recv_lines.append(f"#X connect 1 0 2 0;")
    osc_recv_lines.append(f"#X connect 2 0 3 0;")
    osc_recv_lines.append(f"#X restore 500 120 pd osc_receiver;")
    lines.extend(osc_recv_lines)
    osc_recv_idx = idx
    idx += 1

    # Main router subpatch
    cat_names = list(CATEGORIES.keys())
    route_args = " ".join(f"/{c}" for c in cat_names)

    # Start the router subpatch
    router_lines = []
    router_lines.append(f"#N canvas 0 0 1200 800 osc_router 0;")

    r_idx = 0
    # Inlet
    router_lines.append(f"#X obj 30 20 inlet;")
    r_inlet = r_idx
    r_idx += 1

    # Main routeOSC
    router_lines.append(f"#X obj 30 60 routeOSC {route_args};")
    r_route = r_idx
    r_idx += 1

    router_lines.append(f"#X connect {r_inlet} 0 {r_route} 0;")

    # Generate each category subpatch
    subcanvas_indices = []
    x_pos = 30
    y_pos = 120
    for i, cat_name in enumerate(cat_names):
        params = CATEGORIES[cat_name]
        sub_lines = generate_subcanvas(cat_name, params)

        # Embed the subcanvas
        for sl in sub_lines:
            router_lines.append(sl)
        router_lines.append(f"#X restore {x_pos} {y_pos} pd {cat_name};")
        sub_idx = r_idx
        r_idx += 1

        router_lines.append(f"#X connect {r_route} {i} {sub_idx} 0;")
        subcanvas_indices.append(sub_idx)

        x_pos += 80
        if x_pos > 1100:
            x_pos = 30
            y_pos += 80

    # Outlet for router
    router_lines.append(f"#X obj 400 {y_pos + 80} outlet;")
    r_outlet = r_idx
    r_idx += 1

    # Connect all subcanvases to outlet
    for sub_idx in subcanvas_indices:
        router_lines.append(f"#X connect {sub_idx} 0 {r_outlet} 0;")

    router_lines.append(f"#X restore 500 160 pd osc_router;")
    lines.extend(router_lines)
    router_idx = idx
    idx += 1

    # Passthrough: inlet messages go straight to outlet
    lines.append(f"#X obj 50 160 t a;")
    trig_idx = idx
    idx += 1

    # Send/receive for routing
    lines.append(f"#X obj 500 200 s \\$0-osc-out;")
    send_idx = idx
    idx += 1

    lines.append(f"#X obj 50 200 r \\$0-osc-out;")
    recv_idx = idx
    idx += 1

    # Main outlet
    lines.append(f"#X obj 50 240 outlet;")
    outlet_idx = idx
    idx += 1

    # Print for debug
    lines.append(f"#X obj 500 240 print ligase~-osc;")
    print_idx = idx
    idx += 1

    # Connections
    # inlet -> trigger -> outlet (passthrough)
    lines.append(f"#X connect {inlet_idx} 0 {trig_idx} 0;")
    lines.append(f"#X connect {trig_idx} 0 {outlet_idx} 0;")

    # osc_receiver -> osc_router
    lines.append(f"#X connect {osc_recv_idx} 0 {router_idx} 0;")

    # osc_router -> send
    lines.append(f"#X connect {router_idx} 0 {send_idx} 0;")

    # receive -> outlet
    lines.append(f"#X connect {recv_idx} 0 {outlet_idx} 0;")

    return "\n".join(lines) + "\n"


def generate_help_patch():
    """Generate ligase~-osc-help.pd with documentation."""
    lines = []
    lines.append("#N canvas 50 50 1200 900 12;")
    lines.append("#X text 30 20 ligase~-osc - OSC Control Interface for ligase~;")
    lines.append("#X text 30 50 Control all ligase~ parameters via Open Sound Control (OSC);")
    lines.append("#X text 30 75 Requires: mrpeach library ([udpreceive] and [unpackOSC]);")
    lines.append("#X text 30 100 ------------------------------------------------------------;")
    lines.append("#X text 30 125 USAGE:;")
    lines.append("#X text 50 150 1. Create [ligase~-osc 9000];")
    lines.append("#X text 50 170 2. Connect outlet to [ligase~ 500] inlet;")
    lines.append("#X text 50 190 3. Send OSC messages to port 9000;")
    lines.append("#X text 30 220 ------------------------------------------------------------;")
    lines.append("#X text 30 245 OSC ADDRESS SCHEME: /ligase/<category>/<parameter>;")

    y = 280
    for cat_name, params in CATEGORIES.items():
        lines.append(f"#X text 30 {y} /{cat_name}:;")
        y += 20
        for osc_name, pd_msg, arg_type in params:
            arg_hint = {"f": "<float>", "s": "<symbol>", "b": "", "bang": "",
                        "l": "<args...>"}.get(arg_type, "")
            pd_hint = pd_msg if pd_msg else "bang"
            lines.append(f"#X text 50 {y} /ligase/{cat_name}/{osc_name} {arg_hint} -> {pd_hint};")
            y += 18
        y += 10

    lines.append(f"#X text 30 {y} ------------------------------------------------------------;")
    y += 25
    lines.append(f"#X text 30 {y} EXAMPLES (send with oscsend):;")
    y += 25
    examples = [
        "oscsend localhost 9000 /ligase/file/load s myfile.wav",
        "oscsend localhost 9000 /ligase/transport/play f 1",
        "oscsend localhost 9000 /ligase/grain/iot f 0.05",
        "oscsend localhost 9000 /ligase/delay/time f 0.3",
        "oscsend localhost 9000 /ligase/delay/feedback f 0.7",
        "oscsend localhost 9000 /ligase/fog/smear_bins f 8",
        "oscsend localhost 9000 /ligase/moog/cutoff f 2000",
        "oscsend localhost 9000 /ligase/distortion/enable f 1",
        "oscsend localhost 9000 /ligase/modulation/param_range sff speed 0.5 2.0",
        "oscsend localhost 9000 /ligase/pitch/mode f 3",
        "oscsend localhost 9000 /ligase/pitch/scale iiiii 0 2 4 7 9",
        "oscsend localhost 9000 /ligase/nbody/epsilon ff 1 0.5",
        "oscsend localhost 9000 /ligase/sphere/kick ffff 1 0.5 0.3 0.1",
        "oscsend localhost 9000 /ligase/query/get_state",
    ]
    for ex in examples:
        lines.append(f"#X text 50 {y} {ex};")
        y += 18

    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    output_dir = "/home/sbenja88/projects/ligase"

    # Generate main abstraction
    patch = generate_main_patch(port=9000)
    path = f"{output_dir}/ligase~-osc.pd"
    with open(path, "w") as f:
        f.write(patch)
    print(f"Generated: {path}")

    # Generate help patch
    help_patch = generate_help_patch()
    help_path = f"{output_dir}/ligase~-osc-help.pd"
    with open(help_path, "w") as f:
        f.write(help_patch)
    print(f"Generated: {help_path}")

    # Print stats
    total = sum(len(v) for v in CATEGORIES.values())
    print(f"\nCategories: {len(CATEGORIES)}")
    print(f"Total OSC endpoints: {total}")
    for cat, params in CATEGORIES.items():
        print(f"  /{cat}: {len(params)} endpoints")
