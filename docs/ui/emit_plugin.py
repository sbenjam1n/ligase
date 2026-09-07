#!/usr/bin/env python3
# emit_plugin.py — the FIFTH emitter from panel_layout.py: the plugin's host-parameter table.
#
# Consumed by plugin/LigasePlugin.cpp (C table) and plugin/ui/bridge.js (JSON): which panel
# controls are DAW parameters, in which units, and how each one reaches the engine
# (signal inlet / message selector / message map / toggle / plugin-side special).
#
#   python3 docs/ui/emit_plugin.py            -> plugin/ligase_params.h + plugin/ui/params.json
#
# Parameter policy (Plans/vst_plugin.md GATE A.3, widened): every continuous or switched panel
# control with an engine binding is automatable — the 24 signal inlets (incl. the morph cursor),
# the message knobs, the mode switches and toggles — plus the transport (record arming per REC
# MODE, play), the distortion preset, MASTER (a plugin-side output gain), two momentary triggers
# (STUT, RETRIG) and a few plugin-only settings (clock source, MIDI channels, velocity->level).
# Cold-editing surfaces (SOURCE SHAPE, XPNDR, SEQ/SCALE, matrix, morph surface) stay message
# driven from the panel: they are edit workflows, not automation lanes.
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import panel_layout as PL  # noqa: E402

GROUP_OF = {}
_cur = None
for line in open(os.path.join(HERE, "panel_layout.py"), encoding="utf-8"):
    s = line.strip()
    if s.startswith("# ----------") and s.endswith("----------"):
        _cur = s.strip("# -").strip()
    elif s.startswith("_c(") or s.startswith("*[_c("):
        cid = s.split('"')[1]
        GROUP_OF[cid] = _cur or "MISC"

UNITS = {
    "grainsize": "s", "density": "s", "dly_time": "s", "cutoff": "Hz", "smr_freq": "Hz",
    "delay_glide": "ms", "pitch_fine": "ct", "midi_note": "note", "seq_root": "st",
}

# Panel controls that become parameters even though their bind is "special"
SPECIALS_AS_PARAMS = {
    "recmode": dict(kind="special", sel="recmode"),
    "record":  dict(kind="special", sel="record"),
    "dist_preset": dict(kind="special", sel="dist_preset"),
    "master":  dict(kind="special", sel="master"),
}
SKIP_IDS = {"scope_view"}   # display-only concept

# Plugin-only parameters (not on the silkscreen)
EXTRA_PARAMS = [
    dict(id="stut_trig", name="STUT (trigger)", group="D. GRAIN DELAY", kind="trigger", sel="stut", lo=0, hi=1, default=0),
    dict(id="retrig", name="RETRIG (trigger)", group="C. PLAYHEAD + SPLICE SELECT", kind="trigger", sel="trigger", lo=0, hi=1, default=0),
    dict(id="clock_src", name="CLOCK SOURCE", group="PLUGIN", kind="special", sel="clock_src", lo=0, hi=1, default=1,
         labels=["FREE", "HOST"], note="HOST = beat bangs from the DAW transport drive every quantize grid"),
    dict(id="midi_ch_grain", name="MIDI CH (GRAIN)", group="PLUGIN", kind="msg2", sel="midi_channel", lo=1, hi=16, default=1, is_int=True),
    dict(id="midi_ch_smear", name="MIDI CH (SMEAR)", group="PLUGIN", kind="msg2", sel="midi_channel", lo=1, hi=16, default=2, is_int=True),
    dict(id="midi_vel_amp", name="VELOCITY -> LEVEL", group="PLUGIN", kind="special", sel="midi_vel_amp", lo=0.0, hi=1.0, default=0.0),
    dict(id="midi_bend_cents", name="PITCH BEND RANGE", group="PLUGIN", kind="special", sel="midi_bend_cents", lo=0, hi=50, default=50, is_int=True,
         note="pitch bend -> pitch_fine, +/- this many cents at full bend"),
]
OUTPUT_PARAMS = [
    ("vu_l", "OUT L PEAK"), ("vu_r", "OUT R PEAK"), ("o_splice", "SPLICE"), ("o_splices", "SPLICE COUNT"),
    ("o_playing", "PLAYING"), ("o_recording", "RECORDING"), ("o_bpm", "BPM"), ("o_reel_sec", "REEL SECONDS"),
    ("o_grains", "ACTIVE GRAINS"), ("o_voices", "VOICES"),
    ("o_modout1", "MODOUT 1"), ("o_modout2", "MODOUT 2"), ("o_modout3", "MODOUT 3"), ("o_modout4", "MODOUT 4"),
]


def label_of(c):
    svg = c.get("svg") or {}
    for key in ("name", "label", "title"):
        if svg.get(key):
            return str(svg[key])
    return c["id"].replace("_", " ").upper()


def build():
    params = []
    for c in PL.CONTROLS:
        cid, bind = c["id"], c.get("bind")
        if cid in SKIP_IDS or cid.startswith("seq_") or cid.startswith("xp_") or cid.startswith("shape_") or cid.startswith("snap"):
            continue
        if cid in ("mx_depth", "mx_pol", "reel_load", "reel_save", "splice_led", "splice_data", "splice_enter",
                   "splice_prev", "splice_next", "chord", "morph_snap", "morph_run", "morph_stop", "morph_pause"):
            continue
        kind = None
        entry = dict(id=cid, name=label_of(c), group=GROUP_OF.get(cid, "MISC"), lo=c["lo"], hi=c["hi"],
                     default=c["default"], init_send=bool(c.get("init_send", True)))
        if bind is None:
            if cid in SPECIALS_AS_PARAMS:
                entry.update(SPECIALS_AS_PARAMS[cid])
            else:
                continue
        else:
            b = bind[0]
            if b == "inlet":
                if cid in ("joy_x", "joy_y"):
                    entry.update(kind="special", sel="morph")     # `morph <x> <y>` from both axes
                else:
                    sel = PL.INLET_SELECTORS.get(int(bind[1]))
                    stut = PL.INLET_STUT_SELECTORS.get(int(bind[1]))
                    entry.update(kind="inlet", inlet=int(bind[1]), sel=sel, stut=stut)
            elif b == "msg":
                entry.update(kind="msg", sel=bind[1])
            elif b == "msgmap":
                entry.update(kind="msgmap", map=list(bind[1]))
                entry["labels"] = list((c.get("svg") or {}).get("labels") or [])
                entry["is_int"] = True
            elif b == "toggle":
                entry.update(kind="toggle", sel=bind[1], is_bool=True)
            elif b == "bang":
                continue
            elif b == "special":
                if cid not in SPECIALS_AS_PARAMS:
                    continue
                entry.update(SPECIALS_AS_PARAMS[cid])
                if cid == "recmode":
                    entry["labels"] = ["INPUT", "SPLICE", "OVRDUB"]; entry["is_int"] = True
                if cid == "record":
                    entry["is_bool"] = True
                if cid == "dist_preset":
                    entry["is_int"] = True
            else:
                continue
        if cid in ("quantize", "delay_quantize"):
            sel = entry["sel"]
            entry.update(kind="msgmap", map=[None] + ["%s %d" % (sel, 1 << k) for k in range(8)],
                         labels=["OFF", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128"],
                         lo=0, hi=8, default=0, is_int=True, pow2=True, init_send=False)
        if cid == "master":
            entry.update(kind="special", sel="master")
        if cid == "voices":
            entry["is_int"] = True
        entry["unit"] = UNITS.get(cid, "")
        if cid in ("cutoff", "smr_freq"):
            entry["log"] = True
        params.append(entry)
    params.extend(EXTRA_PARAMS)
    for oid, oname in OUTPUT_PARAMS:
        params.append(dict(id=oid, name=oname, group="OUTPUTS", kind="output", lo=0.0, hi=1.0, default=0.0))
    # numeric ranges of outputs
    for p in params:
        if p["kind"] == "output":
            if p["id"] in ("o_splice", "o_splices", "o_grains"): p["hi"] = 2000; p["is_int"] = True
            if p["id"] == "o_bpm": p["hi"] = 999
            if p["id"] == "o_reel_sec": p["hi"] = 600
            if p["id"] == "o_voices": p["hi"] = 8; p["is_int"] = True
            if p["id"].startswith("o_modout"): p["lo"] = -1e6; p["hi"] = 1e6
            if p["id"].startswith("vu_"): p["hi"] = 4
    return params


def c_str(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_header(params, path):
    out = []
    out.append("/* AUTO-GENERATED by docs/ui/emit_plugin.py from docs/ui/panel_layout.py — DO NOT EDIT. */")
    out.append("#ifndef LIGASE_PARAMS_H\n#define LIGASE_PARAMS_H\n")
    out.append("typedef enum { LP_INLET, LP_MSG, LP_MSG2, LP_MSGMAP, LP_TOGGLE, LP_SPECIAL, LP_TRIGGER, LP_OUTPUT } lp_kind_t;\n")
    out.append("typedef struct {\n    const char *id; const char *name; const char *group; lp_kind_t kind;\n"
               "    int inlet; const char *sel; const char *stut; const char *const *map; int nmap; const char *const *labels;\n"
               "    float lo, hi, def; int is_int, is_bool, is_log, init_send, pow2; const char *unit;\n} lp_param_t;\n")
    tables = []
    for p in params:
        if p.get("map"):
            tables.append("static const char *const lp_map_%s[] = { %s };" % (p["id"], ", ".join("NULL" if m is None else c_str(m) for m in p["map"])))
        if p.get("labels"):
            tables.append("static const char *const lp_labels_%s[] = { %s };" % (p["id"], ", ".join(c_str(m) for m in p["labels"])))
    out.extend(tables)
    out.append("\n/* DISTORTION preset knob: a preset = a ';'-joined message bundle (panel_layout.DIST_PRESETS) */")
    out.append("static const char *const LIGASE_DIST_PRESETS[8] = {")
    for k in range(1, 9):
        out.append("    %s," % c_str("; ".join(PL.DIST_PRESETS[k])))
    out.append("};")
    out.append("\nstatic const lp_param_t LIGASE_PARAMS[] = {")
    kindmap = dict(inlet="LP_INLET", msg="LP_MSG", msg2="LP_MSG2", msgmap="LP_MSGMAP", toggle="LP_TOGGLE",
                   special="LP_SPECIAL", trigger="LP_TRIGGER", output="LP_OUTPUT")
    for p in params:
        out.append("    { %s, %s, %s, %s, %d, %s, %s, %s, %d, %s, %sf, %sf, %sf, %d, %d, %d, %d, %d, %s }," % (
            c_str(p["id"]), c_str(p["name"]), c_str(p["group"]), kindmap[p["kind"]],
            int(p.get("inlet", -1)), c_str(p["sel"]) if p.get("sel") else "NULL", c_str(p["stut"]) if p.get("stut") else "NULL",
            ("lp_map_%s" % p["id"]) if p.get("map") else "NULL", len(p.get("map") or []),
            ("lp_labels_%s" % p["id"]) if p.get("labels") else "NULL",
            repr(float(p["lo"])), repr(float(p["hi"])), repr(float(p["default"])),
            int(bool(p.get("is_int"))), int(bool(p.get("is_bool"))), int(bool(p.get("log"))),
            int(bool(p.get("init_send", True))), int(bool(p.get("pow2"))), c_str(p.get("unit", ""))))
    out.append("};")
    out.append("#define LIGASE_PARAM_COUNT %d" % len(params))
    out.append("#define LIGASE_PARAM_SPACE 64   /* engine snapshot slot reserved for the plugin's live-voice capture */")
    for i, p in enumerate(params):
        out.append("#define LP_%s %d" % (p["id"].upper(), i))
    out.append("\n#endif")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


def main():
    params = build()
    emit_header(params, os.path.join(ROOT, "plugin", "ligase_params.h"))
    os.makedirs(os.path.join(ROOT, "plugin", "ui"), exist_ok=True)
    with open(os.path.join(ROOT, "plugin", "ui", "params.json"), "w", encoding="utf-8") as f:
        json.dump([dict(index=i, **p) for i, p in enumerate(params)], f, indent=1)
    n_in = sum(1 for p in params if p["kind"] != "output")
    print("[emit_plugin] %d parameters (%d inputs, %d outputs) -> plugin/ligase_params.h, plugin/ui/params.json"
          % (len(params), n_in, len(params) - n_in))
    for p in params:
        if p["kind"] != "output":
            print("   %-16s %-8s %-22s [%g..%g] def %g" % (p["id"], p["kind"], p.get("sel") or ("inlet %d" % p.get("inlet", -1)), p["lo"], p["hi"], p["default"]))


if __name__ == "__main__":
    main()
