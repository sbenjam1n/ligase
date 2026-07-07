#!/usr/bin/env python3
# panel_layout.py — the ligase~ control surface AS DATA (single source of truth).
#
# Consumed by:
#   emit_svg.py   the silkscreen (docs/ui/ligase_synthi_panel.svg) — visual spec
#   emit_pd.py    the working instrument (pd/ligase_panel.pd + pd/ligase_xpndr.pd)
#   gen_panel.py  thin compatibility wrapper around emit_svg
#
# Every interactive control is one record in CONTROLS:
#   id        unique stem -> Pd receive symbol lgR_<id> (scriptability / MIDI-map hook)
#   kind      knob | switch | button | toggle | led | jack   (svg primitive)
#   svg       exact argument dict for the SVG primitive (None = not drawn by the
#             section walker; pd-only controls and controls drawn by bespoke SVG
#             blocks, e.g. the joystick crosshair, carry svg=None)
#   bind      what the control DOES in the patch:
#               ("inlet", n)            -> [hsl] -> [pack f 20] -> [line~] -> signal inlet n
#               ("msg", sel)            -> value v -> "<sel> v"
#               ("msgmap", [m0,m1,..])  -> switch position i -> message m[i] (None = no-op)
#               ("bang", msg)           -> [bng] -> "<msg>"
#               ("toggle", sel)         -> [tgl] -> "<sel> 0|1"
#               ("special", name)       -> bespoke emit_pd wiring (matrix, splice nav,
#                                          reel I/O, SOURCE SHAPE, joystick, XPNDR...)
#               None                    -> panel-side only (display / not yet bound)
#   lo/hi/default   numeric range + power-on value (ENGINE units; the ranges follow
#             src/ligase~.c update_inlets validation windows, NOT always the printed
#             silkscreen legend — e.g. grainsize/iot inlets are SECONDS 0.001-2)
#   init_send True -> the loadbang default-broadcast programs this control's value
#             into the engine at patch load (False for anything with side effects:
#             record arming, file I/O, momentary buttons)

W, H = 1914, 1464   # H grew Seq 83: the SEQ/SCALE band joins the chassis below the main sections
MAINW = 1520


def _c(id, kind, svg, bind=None, lo=0.0, hi=1.0, default=0.0, init_send=True, note=None):
    return dict(id=id, kind=kind, svg=svg, bind=bind,
                lo=lo, hi=hi, default=default, init_send=init_send, note=note)


CONTROLS = [
    # ---------- header jacks (audio I/O; wired adc~/dac~ in the patch) ----------
    _c("jack_inl", "jack", dict(x=560, y=50, label="IN L", sub="IN 1 · bang = clock", col="blue"), ("special", "adc_l"), init_send=False),
    _c("jack_inr", "jack", dict(x=625, y=50, label="IN R", sub="IN 2", col="blue"), ("special", "adc_r"), init_send=False),
    _c("jack_outl", "jack", dict(x=692, y=50, label="OUT L", sub=None, col="red"), ("special", "dac_l"), init_send=False),
    _c("jack_outr", "jack", dict(x=748, y=50, label="OUT R", sub=None, col="red"), ("special", "dac_r"), init_send=False),

    # ---------- A. GRANULAR ENGINE ----------
    _c("grainsize", "knob", dict(x=114, y=132, name="GRAIN SIZE", rng="1 ms – 10 s", inlet="IN 3", cap="white", pos=0.35),
       ("inlet", 3), lo=0.001, hi=2.0, default=0.1, note="inlet units = SECONDS, engine window 0.001-2.0"),
    _c("start", "knob", dict(x=232, y=132, name="START", rng="0 – 1", inlet="IN 4", cap="white", pos=0.5),
       ("inlet", 4), lo=0.0, hi=1.0, default=0.0),
    _c("speed", "knob", dict(x=350, y=132, name="SPEED", rng="−4 – +4", inlet="IN 5", cap="white", pos=0.62),
       ("inlet", 5), lo=-4.0, hi=4.0, default=1.0),
    _c("density", "knob", dict(x=468, y=132, name="DENSITY", rng="IOT 1 ms – 2 s", inlet="IN 9", cap="white", pos=0.3),
       ("inlet", 9), lo=0.001, hi=2.0, default=0.1, note="IOT in SECONDS"),
    _c("voices", "knob", dict(x=586, y=132, name="VOICES", rng="1 – 200", inlet="IN 10", cap="white", pos=0.15),
       ("inlet", 10), lo=1, hi=200, default=32),
    _c("level", "knob", dict(x=704, y=132, name="LEVEL", rng="0 – 2", inlet="IN 21", cap="white", pos=0.55),
       ("inlet", 21), lo=0.0, hi=2.0, default=1.0),

    # ---------- B. TAPE REEL / RECORD ----------
    _c("organize", "knob", dict(x=114, y=248, name="ORGANIZE", rng="0 – 1", inlet="IN 6", cap="green", pos=0.4),
       ("inlet", 6), lo=0.0, hi=1.0, default=0.0),
    _c("sos", "knob", dict(x=232, y=248, name="S.O.S.", rng="0 – 1", inlet="IN 8", cap="green", pos=0.7),
       ("inlet", 8), lo=0.0, hi=1.0, default=0.0),
    _c("recmode", "switch", dict(x=360, y=240, w=96, labels=["INPUT", "SPLICE", "OVRDUB"], sel=1, title="REC MODE"),
       ("special", "recmode"), lo=0, hi=2, default=2, init_send=False,
       note="stored mode; RECORD ON fires recinput/recsplice/record 1 per mode (recinput/recsplice START a take)"),
    _c("record", "button", dict(x=462, y=238, label="RECORD", w=52, lit=True),
       ("special", "record"), lo=0, hi=1, default=0, init_send=False, note="[tgl] in the patch: ON=arm per recmode, OFF=record 0"),
    _c("play", "button", dict(x=462, y=262, label="PLAY", w=52, lit=False),
       ("toggle", "play"), lo=0, hi=1, default=0, init_send=False, note="[tgl]: play 1 / play 0 (bare 'play' STOPS)"),
    _c("loop", "toggle", dict(x=530, y=240, label="LOOP / 1-SHOT", on=True),
       ("toggle", "loop"), lo=0, hi=1, default=1),
    _c("reel_load", "button", dict(x=630, y=238, label="SELECT REEL", w=84, lit=False),
       ("special", "openpanel_load"), init_send=False),
    _c("reel_save", "button", dict(x=630, y=262, label="EXPORT REEL", w=84, lit=False),
       ("special", "savepanel_save"), init_send=False),

    # ---------- C. PLAYHEAD + SPLICE SELECT ----------
    _c("playhead", "switch", dict(x=126, y=356, w=118, labels=["STATIC", "SCAN", "CLOCK"], sel=1, title="MODE · playhead 1/2/3"),
       ("msgmap", ["playhead 1", "playhead 2", "playhead 3"]), lo=0, hi=2, default=1),
    _c("scan", "knob", dict(x=236, y=364, name="SCAN", rng="0 – 8", inlet="IN 7", cap="green", pos=0.45),
       ("inlet", 7), lo=0.0, hi=8.0, default=1.0),
    _c("quantize", "knob", dict(x=296, y=364, name="GRID", rng="1/1 – 1/128", inlet="MSG", cap="grey", pos=0.55, small=True,
                                group=("QUANTIZE", 296, 364)),
       ("msg", "quantize"), lo=0, hi=128, default=0, init_send=False),
    _c("quant", "knob", dict(x=352, y=364, name="AMOUNT", rng="0 – 1", inlet="MSG", cap="grey", pos=0.6, small=True),
       ("msg", "quant"), lo=0.0, hi=1.0, default=0.0, init_send=False),
    _c("clkadv", "toggle", dict(x=426, y=354, label="CLK-ADV QUANT", on=False),
       ("toggle", "clock_advance_quant"), lo=0, hi=1, default=0, init_send=False),
    _c("splice_led", "led", dict(x=562, y=360, digits="03", label="current splice"),
       ("special", "splice_led"), note="panel-side counter; engine reports current splice on the CONSOLE only (post), not outlet 9"),
    _c("splice_data", "knob", dict(x=622, y=364, name="DATA", rng="0 – 63", inlet="MSG", cap="grey", pos=0.2, small=True),
       ("special", "splice_data"), lo=0, hi=63, default=0, init_send=False),
    _c("splice_enter", "button", dict(x=680, y=354, label="ENTER", w=46, lit=True),
       ("special", "splice_enter"), init_send=False,
       note="sends splice_finish_nav <DATA>; NB engine treats this as the 0/1 finish-before-nav flag (no jump-to-splice-N message exists)"),
    _c("splice_prev", "button", dict(x=734, y=354, label="◀", w=22, lit=False),
       ("special", "splice_prev"), init_send=False),
    _c("splice_next", "button", dict(x=760, y=354, label="▶", w=22, lit=False),
       ("special", "splice_next"), init_send=False),

    # ---------- D. GRAIN DELAY ----------
    _c("delay_mode", "switch", dict(x=120, y=472, w=108, labels=["DD-4", "BENCINA", "STUT"], sel=0, title="MODE"),
       ("msgmap", ["delay_mode 0", "delay_mode 1", "delay_mode 2"]), lo=0, hi=2, default=0),
    _c("dly_time", "knob", dict(x=230, y=480, name="TIME · REPS", rng="0–10 s · 1–16", inlet="IN 11", cap="blue", pos=0.4),
       ("inlet", 11), lo=0.0, hi=10.0, default=0.0, note="SECONDS; stut maps 0-10 -> reps 1-16"),
    _c("dly_feed", "knob", dict(x=334, y=480, name="REGEN · DECAY", rng="0 – 1", inlet="IN 12", cap="blue", pos=0.5),
       ("inlet", 12), lo=0.0, hi=1.0, default=0.3),
    _c("dly_tone", "knob", dict(x=438, y=480, name="TONE · SPACE", rng="0–1 · 1–5000 ms", inlet="IN 13", cap="blue", pos=0.6),
       ("inlet", 13), lo=0.0, hi=1.0, default=0.5),
    _c("dly_mix", "knob", dict(x=534, y=480, name="MIX", rng="0 – 1", inlet="IN 14", cap="blue", pos=0.35),
       ("inlet", 14), lo=0.0, hi=1.0, default=0.0),
    _c("delay_quantize", "knob", dict(x=600, y=480, name="GRID", rng="1/1 – 1/128", inlet="MSG", cap="grey", pos=0.55, small=True,
                                      group=("QUANTIZE", 600, 480)),
       ("msg", "delay_quantize"), lo=0, hi=128, default=0, init_send=False),
    _c("delay_quant", "knob", dict(x=656, y=480, name="AMOUNT", rng="0 – 1", inlet="MSG", cap="grey", pos=0.6, small=True),
       ("msg", "delay_quant"), lo=0.0, hi=1.0, default=0.0, init_send=False),
    _c("stut_bang", "button", dict(x=722, y=470, label="STUT !", w=42, lit=True),
       ("bang", "stut"), init_send=False),
    _c("delay_glide", "knob", dict(x=722, y=506, name="GLIDE", rng="0–5000 ms", inlet="MSG", cap="grey", pos=0.2, small=True),
       ("msg", "delay_glide"), lo=0, hi=5000, default=0, init_send=False),

    # ---------- E. LADDER FILTER + SMEAR ----------
    _c("cutoff", "knob", dict(x=104, y=596, name="CUTOFF", rng="20 – 20 kHz", inlet="IN 16", cap="yellow", pos=0.75),
       ("inlet", 16), lo=20, hi=20000, default=20000),
    _c("resonance", "knob", dict(x=214, y=596, name="RESONANCE", rng="0 – 4", inlet="IN 17", cap="yellow", pos=0.3),
       ("inlet", 17), lo=0.0, hi=4.0, default=0.0),
    _c("flt_mix", "knob", dict(x=316, y=596, name="MIX", rng="0 – 1", inlet="IN 18", cap="yellow", pos=0.4),
       ("inlet", 18), lo=0.0, hi=1.0, default=0.0),
    _c("smr_mix", "knob", dict(x=434, y=596, name="MIX", rng="0 – 1", inlet="IN 15", cap="red", pos=0.35),
       ("inlet", 15), lo=0.0, hi=1.0, default=0.0),
    _c("smr_freq", "knob", dict(x=500, y=596, name="FREQ", rng="20Hz–0.45sr", inlet="MSG", cap="grey", pos=0.5, small=True),
       ("msg", "smear_frequency"), lo=20, hi=20000, default=440, init_send=False),
    _c("smr_res", "knob", dict(x=558, y=596, name="RESON", rng="0 – 0.999", inlet="MSG", cap="grey", pos=0.8, small=True),
       ("msg", "smear_resonance"), lo=0.0, hi=0.999, default=0.9, init_send=False),
    _c("smr_stages", "knob", dict(x=616, y=596, name="STAGES", rng="0 – 48", inlet="MSG", cap="grey", pos=0.25, small=True),
       ("msg", "smear_stages"), lo=0, hi=48, default=8, init_send=False),
    _c("smr_fdbk", "knob", dict(x=674, y=596, name="FDBK", rng="±0.99", inlet="MSG", cap="grey", pos=0.5, small=True),
       ("msg", "smear_feedback"), lo=-0.99, hi=0.99, default=0.0, init_send=False),
    _c("smr_mode", "switch", dict(x=729, y=594, w=64, labels=["SNGL", "BANK"], sel=0, title="MODE"),
       ("msgmap", ["smear_mode 0", "smear_mode 1"]), lo=0, hi=1, default=0),

    # ---------- F. GRAIN ENVELOPE + PITCH/MIDI ----------
    _c("env_type", "switch", dict(x=140, y=704, w=140, labels=["PARA", "TRAP", "COS", "GAUS", "EXP"], sel=2, title="TYPE"),
       ("msgmap", ["envelope 0", "envelope 1", "envelope 2", None, None]), lo=0, hi=4, default=2,
       note="engine implements 0-2 only; GAUS/EXP are silkscreen-forward positions (no message)"),
    _c("skew", "knob", dict(x=262, y=712, name="SKEW", rng="0 – 1", inlet="IN 20", cap="white", pos=0.5),
       ("inlet", 20), lo=0.0, hi=1.0, default=0.5),
    _c("saw_cycles", "knob", dict(x=322, y=712, name="SAW CYC", rng="0 – 64", inlet="MSG", cap="grey", pos=0.1, small=True),
       ("msg", "saw_cycles"), lo=0, hi=64, default=0, init_send=False),
    _c("saw_depth", "knob", dict(x=374, y=712, name="SAW DEP", rng="0 – 1", inlet="MSG", cap="grey", pos=0.0, small=True),
       ("msg", "saw_depth"), lo=0.0, hi=1.0, default=0.0, init_send=False),
    _c("midi_note", "knob", dict(x=466, y=712, name="MIDI NOTE", rng="1 – 127", inlet="IN 19", cap="blue", pos=0.5),
       ("inlet", 19), lo=1, hi=127, default=60),
    _c("pitch_mode", "switch", dict(x=594, y=704, w=150, labels=["OFF", "SEMI", "SCALE", "MIDI", "PATRN"], sel=3, title="PITCH MODE"),
       ("msgmap", ["pitch_mode 0", "pitch_mode 1", "pitch_mode 3", "pitch_mode 4", "pitch_mode 5"]), lo=0, hi=4, default=3,
       note="engine mode 2 (range) has no panel position"),
    _c("pitch_fine", "knob", dict(x=706, y=712, name="FINE", rng="±50 ¢", inlet="MSG", cap="grey", pos=0.5, small=True),
       ("msg", "pitch_fine"), lo=-50, hi=50, default=0, init_send=False),
    _c("poly", "toggle", dict(x=544, y=744, label="POLY ×8", on=True),
       ("toggle", "poly"), lo=0, hi=1, default=1),
    _c("chord", "button", dict(x=619, y=746, label="CHORD", w=48, lit=False),
       ("bang", "chord 0 4 7"), init_send=False, note="demo major triad"),

    # ---------- G. DISTORTION + OUTPUT/SPACE ----------
    _c("dist_on", "toggle", dict(x=96, y=822, label="ON / OFF", on=True),
       ("toggle", "distortion_enable"), lo=0, hi=1, default=1),
    _c("dist_emph", "switch", dict(x=178, y=820, w=66, labels=["HP", "LP"], sel=1, title="EMPHASIS"),
       ("msgmap", ["dist_emphasis_mode 0", "dist_emphasis_mode 1"]), lo=0, hi=1, default=1),
    _c("dist_preset", "knob", dict(x=272, y=828, name="PRESET", rng="1 – 8", inlet="MSG", cap="grey", pos=0.42),
       ("special", "dist_preset"), lo=1, hi=8, default=4, init_send=False),
    _c("pan", "knob", dict(x=444, y=828, name="PAN", rng="0 – 1", inlet="IN 22", cap="red", pos=0.5),
       ("inlet", 22), lo=0.0, hi=1.0, default=0.5),
    _c("pan_mode", "switch", dict(x=566, y=820, w=116, labels=["MONO", "STEREO", "SPATIAL"], sel=2, title="PAN MODE"),
       ("msgmap", ["pan_mode 0", "pan_mode 1", "pan_mode 2"]), lo=0, hi=2, default=2),
    _c("spatial_width", "knob", dict(x=670, y=828, name="WIDTH", rng="0 – 1 · spatial", inlet="MSG", cap="grey", pos=1.0, small=True),
       ("msg", "spatial_width"), lo=0.0, hi=1.0, default=1.0, init_send=False),
    _c("spatial_src", "switch", dict(x=566, y=862, w=96, labels=["SPHERE", "NBODY"], sel=0, title=None),
       ("msgmap", ["spatial sphere", "spatial nbody"]), lo=0, hi=1, default=0),

    # ---------- H. PRESETS / SNAPSHOTS (snapshots ARE the preset system) ----------
    # 4 rows x 8 slots; silkscreen numbers 1-32 -> engine snapshot slots 0-31
    *[_c(f"snap{r*8+i+1}", "button",
         dict(x=100 + i*62, y=934 + r*22, label=str(r*8+i+1), w=44, lit=(r == 0 and i == 0)),
         ("special", f"snap_slot{r*8+i}"), init_send=False)
      for r in range(4) for i in range(8)],
    _c("snap_store", "button", dict(x=610, y=934, label="STORE", w=54, lit=False), ("special", "snap_store"), init_send=False),
    _c("snap_recall", "button", dict(x=672, y=934, label="RECALL", w=54, lit=False), ("special", "snap_recall"), init_send=False),

    # ---------- SOURCE SHAPE (the matrix rows; FAMILY x INST routes RATE/A-D) ----------
    _c("shape_family", "switch", dict(x=944, y=632, w=252, labels=["SIN", "SAW", "SQR", "PERL", "LRNZ", "NBDY", "SPHR", "RAND", "FOLW"], sel=5, title="FAMILY"),
       ("special", "shape_family"), lo=0, hi=8, default=5, init_send=False),
    _c("shape_inst", "switch", dict(x=1098, y=632, w=60, labels=["1", "2", "3", "4"], sel=0, title="INST"),
       ("special", "shape_inst"), lo=0, hi=3, default=0, init_send=False),
    _c("shape_rate", "knob", dict(x=1154, y=634, name="RATE", rng="0.01-100 x", inlet=None, cap="yellow", pos=0.4, small=True),
       ("special", "shape_rate"), lo=0.01, hi=100, default=1.0, init_send=False),
    _c("shape_a", "knob", dict(x=1208, y=634, name="A", rng="", inlet=None, cap="yellow", pos=0.55, small=True),
       ("special", "shape_a"), lo=0.0, hi=1.0, default=0.5, init_send=False),
    _c("shape_b", "knob", dict(x=1260, y=634, name="B", rng="", inlet=None, cap="yellow", pos=0.3, small=True),
       ("special", "shape_b"), lo=0.0, hi=1.0, default=0.5, init_send=False),
    _c("shape_c", "knob", dict(x=1312, y=634, name="C", rng="", inlet=None, cap="yellow", pos=0.5, small=True),
       ("special", "shape_c"), lo=0.0, hi=1.0, default=0.5, init_send=False),
    _c("shape_d", "knob", dict(x=1364, y=634, name="D", rng="", inlet=None, cap="yellow", pos=0.65, small=True),
       ("special", "shape_d"), lo=0.0, hi=1.0, default=0.5, init_send=False),
    _c("shape_kick", "button", dict(x=1420, y=620, label="KICK !", w=46, lit=True), ("special", "shape_kick"), init_send=False),
    _c("shape_reset", "button", dict(x=1420, y=644, label="RESET", w=46, lit=False), ("special", "shape_reset"), init_send=False),
    _c("shape_mode", "switch", dict(x=1414, y=672, w=46, labels=["0", "1", "2"], sel=1, title=None),
       ("special", "shape_mode"), lo=0, hi=2, default=1, init_send=False),

    # ---------- JOYSTICK / MORPH (X/Y drawn by the bespoke joystick block) ----------
    _c("joy_x", None, None, ("inlet", 23), lo=0.0, hi=1.0, default=0.55, note="morph cursor X (CV; morph_cursor 1 engages)"),
    _c("joy_y", None, None, ("inlet", 24), lo=0.0, hi=1.0, default=0.45, note="morph cursor Y"),
    _c("morph_snap", "button", dict(x=1086, y=776, label="SNAP", w=58, lit=False),
       ("special", "morph_snap"), init_send=False, note="snapshot <selected PRESET slot>"),
    _c("morph_run", "button", dict(x=1086, y=804, label="ROUTE RUN", w=74, lit=True), ("bang", "morph_run 1"), init_send=False),
    _c("morph_stop", "button", dict(x=1086, y=832, label="STOP", w=58, lit=False), ("bang", "morph_stop"), init_send=False),
    _c("morph_pause", "button", dict(x=1086, y=860, label="PAUSE", w=58, lit=False), ("bang", "morph_pause"), init_send=False),
    _c("morph_kernel", "switch", dict(x=1086, y=896, w=72, labels=["IDW", "NN"], sel=0, title="KERNEL"),
       ("msgmap", ["morph_interp 0", "morph_interp 1"]), lo=0, hi=1, default=0),
    _c("morph_power", "knob", dict(x=1086, y=944, name="POWER", rng="IDW sharp · MSG", inlet=None, cap="grey", pos=0.4, small=True),
       ("msg", "morph_power"), lo=0.5, hi=8.0, default=2.0, init_send=False),

    # ---------- SCOPE (display = exact twin of the joystick pad; controls right) ----------
    _c("scope_tap", "switch", dict(x=1416, y=800, w=68, labels=["FOLW", "GRN"], sel=0, title="TAP"),
       ("msgmap", ["scope_tap folw", "scope_tap grain"]), lo=0, hi=1, default=0, init_send=False,
       note="loadbang default is scope_tap lorenz 1 (GATE A); this switch narrows to folw/grain"),
    _c("scope_view", "switch", dict(x=1416, y=852, w=62, labels=["XY", "SWP"], sel=0, title="VIEW"),
       None, lo=0, hi=1, default=0, init_send=False, note="display-side concept; no engine message"),

    # ---------- MATRIX depth policy controls (pd-only; GATE A.4) ----------
    _c("mx_depth", None, None, ("special", "mx_depth"), lo=0.0, hi=4.0, default=1.0,
       note="[nbx] magnitude of the depth applied when a pin is placed"),
    _c("mx_pol", None, None, ("special", "mx_pol"), lo=0, hi=1, default=0,
       note="[tgl] 0 = + (white pin), 1 = - (green pin)"),

    # ---------- XPNDR (snapshot expander; second canvas) ----------
    _c("xp_led", "led", dict(x=1588, y=130, digits="02", label="buffer holds snap"), ("special", "xp_led")),
    _c("xp_slot", "knob", dict(x=1658, y=134, name="DATA", rng="slot 0 – 63", inlet="MSG", cap="grey", pos=0.25, small=True),
       ("special", "xp_slot"), lo=0, hi=63, default=0, init_send=False),
    _c("xp_load", "button", dict(x=1820, y=122, label="LOAD", w=84, lit=False), ("special", "xp_load"), init_send=False),
    _c("xp_fromlive", "button", dict(x=1820, y=148, label="FROM LIVE", w=84, lit=False), ("special", "xp_fromlive"), init_send=False),
    _c("xp_page", "switch", dict(x=1697.0, y=258, w=312, labels=["GRAIN", "TAPE", "DELAY", "FILTR", "SMEAR", "ENV", "PITCH", "SPACE"], sel=3, title="PAGE"),
       ("special", "xp_page"), lo=0, hi=7, default=0, init_send=False),
    _c("xp_param", "switch", dict(x=1697.0, y=306, w=312, labels=["1", "2", "3", "4", "5", "6", "7", "8"], sel=1, title="PARAM"),
       ("special", "xp_param"), lo=0, hi=7, default=0, init_send=False),
    _c("xp_value_led", "led", dict(x=1604, y=404, digits="0.42", label="stored value", w=96, h=30, ghost="8.88", fs=22), ("special", "xp_value_led")),
    _c("xp_value", "knob", dict(x=1704, y=404, name="VALUE", rng="scalar · detent = discrete", inlet="MSG", cap="white", pos=0.42),
       ("special", "xp_value"), lo=-20000, hi=20000, default=0, init_send=False,
       note="raw field units ([nbx] in the patch — fields span ms/Hz/semitones)"),
    _c("xp_min", "knob", dict(x=1586, y=520, name="MIN", rng="band low", inlet=None, cap="blue", pos=0.3, small=True),
       ("special", "xp_min"), lo=0.0, hi=1.0, default=0.0, init_send=False),
    _c("xp_max", "knob", dict(x=1654, y=520, name="MAX", rng="band high", inlet=None, cap="blue", pos=0.7, small=True),
       ("special", "xp_max"), lo=0.0, hi=1.0, default=1.0, init_send=False),
    _c("xp_slew", "knob", dict(x=1722, y=520, name="SLEW", rng="smooth", inlet=None, cap="blue", pos=0.2, small=True),
       ("special", "xp_slew"), lo=0.0, hi=1.0, default=0.0, init_send=False),
    _c("xp_enabled", "toggle", dict(x=1790, y=514, label="ENABLED", on=True), ("special", "xp_enabled"), lo=0, hi=1, default=1, init_send=False),
    _c("xp_invert", "toggle", dict(x=1858, y=514, label="INVERT", on=False), ("special", "xp_invert"), lo=0, hi=1, default=0, init_send=False),
    _c("xp_source", "switch", dict(x=1661, y=598, w=210, labels=["OFF", "PERL", "LRNZ", "NBDY", "SPHR", "RAND", "PAT"], sel=1, title="SOURCE"),
       ("special", "xp_source"), lo=0, hi=6, default=0, init_send=False),
    _c("xp_inst", "switch", dict(x=1830, y=598, w=64, labels=["1", "2", "3", "4"], sel=0, title="INST"),
       ("special", "xp_inst"), lo=0, hi=3, default=0, init_send=False),
    _c("xp_store", "button", dict(x=1580, y=708, label="STORE", w=62, lit=False), ("special", "xp_store"), init_send=False),
    _c("xp_assign", "button", dict(x=1670, y=708, label="ASSIGN", w=62, lit=True), ("special", "xp_assign"), init_send=False),
    _c("xp_audition", "button", dict(x=1762, y=708, label="AUDITION", w=66, lit=False),
       ("special", "xp_audition"), lo=0, hi=1, default=0, init_send=False, note="[tgl] in the patch: snapbuf_audition 1/0"),
    _c("xp_compare", "button", dict(x=1843, y=708, label="A/B", w=40, lit=False), ("bang", "snapbuf_compare"), init_send=False),

    # ---------- MONITOR (condensed: horizontal VU + inline knobs + 2-line legend) ----------
    _c("master", "knob", dict(x=1762, y=838, name="MASTER", rng="amplitude · IN 21", inlet=None, cap="red", pos=0.6),
       None, lo=0.0, hi=2.0, default=1.0, init_send=False,
       note="duplicate of LEVEL (inlet 21); left unwired in the patch — two line~ writers on one signal inlet would SUM"),
    _c("bank_mix", "knob", dict(x=1836, y=838, name="BANK MIX", rng="0 – 1", inlet="MSG", cap="grey", pos=0.0, small=True),
       ("msg", "smear_bank_mix"), lo=0.0, hi=1.0, default=0.0, init_send=False),
]

CONTROL_BY_ID = {c["id"]: c for c in CONTROLS}
assert len(CONTROL_BY_ID) == len(CONTROLS), "duplicate control id"


# ---------- PRESTO-PATCH MATRIX ----------
# (display name, engine name) — engine vocabulary from mod_source_names/mod_dest_names
# in src/ligase~.c. 16 sources x 22 destinations = the panel subset of the full matrix.
MATRIX_SRCS = [
    ("SIN 1", "sine1"), ("SAW 1", "saw1"), ("SQR 1", "square1"),
    ("PERL 1", "perlin1"), ("PERL 2", "perlin2"), ("LRNZ 1", "lorenz1"),
    ("NBDY 1", "nbody1"), ("SPHR 1", "sphere1"), ("RAND 1", "rand1"),
    ("PAT 0", "pattern0"), ("PAT 1", "pattern1"), ("PAT 2", "pattern2"),
    ("PAT 3", "pattern3"), ("ENV L", "env_l"), ("ENV R", "env_r"), ("ENV M", "env_mono"),
]
MATRIX_DSTS = [
    ("DLY TIME", "gdelay"), ("DLY FEED", "gdelay_feed"), ("DLY TONE", "gdelay_tone"),
    ("DLY MIX", "gdelay_mix"), ("CUTOFF", "moog_cutoff"), ("RESON", "moog_resonance"),
    ("FLT MIX", "moog_mix"), ("SMR FRQ", "smear_frequency"), ("SMR RES", "smear_resonance"),
    ("SMR STG", "smear_stages"), ("SMR FB", "smear_feedback"), ("SCAN", "scanrate"),
    ("ORGANIZE", "organize"), ("S.O.S.", "sos"), ("IOT", "iot"), ("SKEW", "env_skew"),
    ("SPEED", "speed"), ("SIZE", "grainsize"), ("START", "grain_start"),
    ("AMP", "amplitude"), ("PAN", "pan"), ("FINE", "pitch_fine"),
]
# decorative pins on the silkscreen (row, col) -> cap color
MATRIX_PINS = {(3, 0): "white", (15, 4): "white", (8, 7): "white", (9, 13): "green",
               (5, 15): "white", (13, 3): "green", (0, 11): "white", (8, 17): "white",
               (0, 21): "green"}


# ---------- SOURCE SHAPE — per-family meaning of RATE/A/B/C/D ----------
# Families (the FAMILY radio order):
SHAPE_FAMILIES = ["SIN", "SAW", "SQR", "PERL", "LRNZ", "NBDY", "SPHR", "RAND", "FOLW"]
# Meaning entry: knob -> {family_index: (selector, form, lo, hi)}
#   form "inst2":   "<sel> <inst 1-4> <value>"      (two-arg per-instance selector)
#   form "suffix":  "<sel>_<inst 1-4> <value>"      (per-instance selector NAME)
#   form "global":  "<sel> <value>"                 (no instance)
# lo/hi rescale the 0-1 panel knob into engine units (None = knob value passes raw).
# A knob with no entry for the current family sends NOTHING (the printed-legend rule).
_RATE_FAMS = [0, 1, 2, 3, 4, 5, 6, 7]   # all but FOLW: rate = IOT x noise_freq scale
SHAPE_MEANINGS = {
    "shape_rate": {f: ("noise_freq", "suffix", None, None) for f in _RATE_FAMS},
    "shape_a": {
        0: ("waveform_phase", "inst2", 0.0, 1.0),
        1: ("waveform_phase", "inst2", 0.0, 1.0),
        2: ("waveform_phase", "inst2", 0.0, 1.0),
        3: ("noise_freq", "suffix", 0.01, 100.0),          # PERL: FREQ (same scale as RATE)
        4: ("lorenz_sigma", "inst2", 0.1, 20.0),
        5: ("nbody_G", "inst2", 0.01, 5.0),
        6: ("sphere_damping", "inst2", 0.0, 1.0),
        8: ("env_follow_ms", "global", 0.0, 2000.0),       # FOLW: RELEASE ms
    },
    "shape_b": {
        1: ("saw_skew", "inst2", 0.0, 1.0),
        2: ("square_pw", "inst2", 0.05, 0.95),
        4: ("lorenz_rho", "inst2", 0.1, 56.0),
        5: ("nbody_damping", "inst2", 0.0, 1.0),
        6: ("sphere_elasticity", "inst2", 0.0, 1.0),
    },
    "shape_c": {
        4: ("lorenz_beta", "inst2", 0.1, 8.0),
        5: ("nbody_epsilon", "inst2", 0.01, 1.0),
        6: ("sphere_spin", "inst2", 0.0, 10.0),
    },
    # D: NBDY pump amount; SPHR kick strength is CONSUMED by the KICK button (no send)
    "shape_d": {
        5: ("nbody_pump", "inst2", 0.0, 1.0),
    },
}
# RESET button per family (selector taking <inst 1-4>); families absent = no-op
SHAPE_RESET = {3: "perlin_reset", 4: "lorenz_reset", 5: "nbody_reset", 6: "sphere_reset"}
# MODE switch per family ("<sel> <inst> <mode>")
SHAPE_MODE = {5: "nbody_mode", 6: "sphere_mode"}
# KICK: sphere only — "sphere_kick <inst> <s> <s> <s>", s = D knob * KICK_SCALE
SHAPE_KICK_FAMILY = 6
SHAPE_KICK_SCALE = 5.0


# ---------- XPNDR — PAGE x PARAM -> snapbuf field-name map (curated 8x8) ----------
# Entry = (value_field, band_field):
#   value_field  scalar/discrete snapbuf field for the VALUE box (None = no value field)
#   band_field   *_range field the MODULATION BAND cluster addresses (None = no band)
# Vocabulary = morph_fields[] in src/ligase~.c (snapbuf_set/get names).
XPNDR_PAGES = ["GRAIN", "TAPE", "DELAY", "FILTR", "SMEAR", "ENV", "PITCH", "SPACE"]
XPNDR_FIELDS = {
    "GRAIN": [("grainsize", "grainsize_range"), ("grainstart", "grainstart_range"),
              ("speed", "speed_range"), ("iot", "iot_range"),
              ("maxgrains", "maxgrains_range"), ("quant", None),
              ("gs_quant", None), ("quantize", None)],
    "TAPE": [("scanrate", "scanrate_range"), ("organize", "organize_range"),
             ("sos", "sos_range"), ("sos_mode", None), ("playhead", None),
             ("clock_advance_quant", None), ("timesig_num", None), ("timesig_den", None)],
    "DELAY": [("gdelay_time", "gdelay_range"), ("gdelay_feed", "gdelay_feed_range"),
              ("gdelay_tone", "gdelay_tone_range"), ("gdelay_mix", "gdelay_mix_range"),
              ("delay_quant", None), ("delay_quantize", None),
              ("stut_length", None), ("stut_length_quant", None)],
    "FILTR": [("moog_cutoff", "moog_cutoff_range"), ("moog_resonance", "moog_resonance_range"),
              ("moog_mix", "moog_mix_range"), (None, "distortion_range"),
              (None, "dist_pregain_range"), (None, "dist_curve_blend_range"),
              (None, "dist_drive_pos_range"), (None, "dist_drive_neg_range")],
    "SMEAR": [("smear_frequency", "smear_frequency_range"), ("smear_resonance", "smear_resonance_range"),
              ("smear_stages", "smear_stages_range"), ("smear_feedback", "smear_feedback_range"),
              ("smear_pitch_semitones", "smear_pitch_semitones_range"),
              ("smear_pitch_fine", "smear_pitch_fine_range"),
              ("smear_pitch_enabled", None), ("smear_pitch_source", None)],
    "ENV": [("saw_cycles", "saw_cycles_range"), ("saw_depth", "saw_depth_range"),
            (None, "env_skew_range"), ("env_follow_ms", None), ("grain_bang_rate", None),
            ("outlet3_mode", None), (None, None), (None, None)],
    "PITCH": [("pitch_semitones", "pitch_semitones_range"), ("pitch_fine", "pitch_fine_range"),
              ("pitch_mode", None), ("pitch_midi_note", None), ("grain_midi_channel", None),
              ("pitch_pattern_slot", None), ("smear_ref_note", None), ("smear_note", None)],
    "SPACE": [("amplitude", "amplitude_range"), ("pan", "pan_range"), ("pan_mode", None),
              (None, "modout1_range"), (None, "modout2_range"), (None, "modout3_range"),
              (None, "modout4_range"), ("headless", None)],
}
# XPNDR band SOURCE radio -> numeric rand_type codes (types.h rand_type_t):
# OFF=NONE(0), PERL=PERLIN_1D(2), LRNZ=LORENZ(4), NBDY=NBODY(5), SPHR=SPHERE(6),
# RAND=RAND(1), PAT=PATTERN(10). rand_instance is 0-based.
XPNDR_SOURCE_CODES = [0, 2, 4, 5, 6, 1, 10]


# ---------- DISTORTION preset knob -> message bundles (a preset = a message bundle) ----------
DIST_PRESETS = {
    1: ["distortion 0.1", "dist_waveshaper_mode 0", "dist_pregain 1"],
    2: ["distortion 0.3", "dist_waveshaper_mode 0", "dist_pregain 2", "dist_curve_blend 0.2"],
    3: ["distortion 0.4", "dist_waveshaper_mode 1", "dist_pregain 2.5", "dist_curve_blend 0.4"],
    4: ["distortion 0.5", "dist_waveshaper_mode 3", "dist_pregain 3", "dist_curve_blend 0.5"],
    5: ["distortion 0.6", "dist_waveshaper_mode 2", "dist_pregain 4",
        "dist_drive_pos 1.5", "dist_drive_neg 1"],
    6: ["distortion 0.7", "dist_waveshaper_mode 3", "dist_pregain 6", "dist_curve_blend 0.7"],
    7: ["distortion 0.85", "dist_waveshaper_mode 4", "dist_pregain 6",
        "dist_poly_c1 1", "dist_poly_c2 0.5", "dist_poly_c3 0.3"],
    8: ["distortion 1", "dist_waveshaper_mode 4", "dist_pregain 8",
        "dist_poly_c1 1", "dist_poly_c2 0.8", "dist_poly_c3 0.6"],
}


def controls_with_bind(kind):
    return [c for c in CONTROLS if c["bind"] and c["bind"][0] == kind]
