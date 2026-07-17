#!/usr/bin/env python3
# emit_pd.py — generate the WORKING Pd control surface from panel_layout.py.
#
# Emits:
#   pd/ligase_panel.pd   the instrument: one [ligase~] + every panel control live
#                        (vanilla-Pd GUI objects — [hsl]/[vsl]/[tgl]/[bng]/[nbx]/
#                        [hradio]/[cnv] — which load identically in plugdata and
#                        headless vanilla pd; GATE A.1)
#   pd/ligase_xpndr.pd   the Snapshot Expander as a standalone abstraction variant.
#                        It speaks only the lg_engine / lg_state9 send-receive buses,
#                        so it works next to ANY patch that publishes those buses
#                        (ligase_panel.pd does). The same canvas is embedded in
#                        ligase_panel.pd as the [pd xpndr] subpatch window (GATE A.2).
#
# Scriptability / MIDI hook: every control has a receive symbol lgR_<id> — messages
# like  [; lgR_grainsize 0.5(  set the GUI and drive the engine, exactly like a hand
# on the panel (this is also the MIDI-mapping hook: [ctlin] -> [s lgR_<id>]).
# Control outputs travel on send symbols lgS_<id> into generated wiring subpatches.
#
# Never hand-edit the .pd files; hand-tweaks go in panel_layout.py.
import os

import panel_layout as L

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pd")

# iemgui colors (pd 0.54 hex form)
BG, FG, LB = "#dfe1e3", "#26282b", "#26282b"
BG_IN, FG_IN = "#ece8da", "#26282b"     # signal-inlet controls (white caps)
BG_MX, FG_MX = "#15161a", "#4f9860"     # matrix pins


def fnum(v):
    s = f"{v:.6g}"
    return s


def sym(s):
    """iemgui label symbol: no spaces, no ';', no ','."""
    if not s:
        return "empty"
    out = s.replace(" ", "_").replace(";", ".").replace(",", ".")
    out = "".join(ch if ord(ch) < 128 else "." for ch in out)
    return out or "empty"


def esc_msg(s):
    return s.replace(",", " \\,").replace(";", " \\;").replace("$", "\\$")


class Canvas:
    def __init__(self, x, y, w, h, font=10, subname=None):
        if subname:
            self.header = f"#N canvas {x} {y} {w} {h} {subname} 0;"
        else:
            self.header = f"#N canvas {x} {y} {w} {h} {font};"
        self.lines = []
        self.n = 0

    def _add(self, line):
        self.lines.append(line)
        idx = self.n
        self.n += 1
        return idx

    def obj(self, x, y, text):
        return self._add(f"#X obj {int(x)} {int(y)} {text};")

    def msg(self, x, y, text):
        return self._add(f"#X msg {int(x)} {int(y)} {text};")

    def comment(self, x, y, text):
        return self._add(f"#X text {int(x)} {int(y)} {esc_msg(text)};")

    def connect(self, a, ao, b, bi):
        self.lines.append(f"#X connect {a} {ao} {b} {bi};")

    def sub(self, canvas, x, y, name):
        self.lines.append(canvas.header)
        self.lines.extend(canvas.lines)
        return self._add(f"#X restore {int(x)} {int(y)} pd {name};")

    def render(self):
        return self.header + "\n" + "\n".join(self.lines) + "\n"

    # ---- iemgui emitters (formats validated headless under vanilla pd 0.54) ----
    def hsl(self, x, y, w, lo, hi, snd, rcv, label, default, h=15):
        rng = (hi - lo) or 1.0
        val = int(round((default - lo) / rng * (w - 1) * 100))
        return self.obj(x, y, f"hsl {w} {h} {fnum(lo)} {fnum(hi)} 0 0 {snd} {rcv} "
                              f"{sym(label)} -2 -10 0 10 {BG_IN} {FG} {LB} {val} 1")

    def vsl(self, x, y, h, lo, hi, snd, rcv, label, default, w=15):
        rng = (hi - lo) or 1.0
        val = int(round((default - lo) / rng * (h - 1) * 100))
        return self.obj(x, y, f"vsl {w} {h} {fnum(lo)} {fnum(hi)} 0 0 {snd} {rcv} "
                              f"{sym(label)} 0 -10 0 10 {BG_IN} {FG} {LB} {val} 1")

    def tgl(self, x, y, snd, rcv, label, on, size=17, bg=BG, fg=FG):
        return self.obj(x, y, f"tgl {size} 0 {snd} {rcv} {sym(label)} 20 8 0 10 "
                              f"{bg} {fg} {LB} {1 if on else 0} 1")

    def bng(self, x, y, snd, rcv, label, size=18):
        return self.obj(x, y, f"bng {size} 250 50 0 {snd} {rcv} {sym(label)} 22 9 0 10 "
                              f"{BG} {FG} {LB}")

    def nbx(self, x, y, digits, lo, hi, snd, rcv, label, default):
        return self.obj(x, y, f"nbx {digits} 16 {fnum(lo)} {fnum(hi)} 0 0 {snd} {rcv} "
                              f"{sym(label)} 0 -10 0 10 {BG} {FG} {LB} {fnum(default)} 256")

    def hradio(self, x, y, n, snd, rcv, label, default, size=17):
        return self.obj(x, y, f"hradio {size} 1 0 {n} {snd} {rcv} {sym(label)} 0 -10 0 10 "
                              f"{BG} {FG} {LB} {int(default)}")

    def cnv(self, x, y, w, h, label, size=14):
        return self.obj(x, y, f"cnv 12 {int(w)} {int(h)} empty empty {sym(label)} 6 12 0 {size} "
                              f"#c8cacd #26282b 0")


def snd(cid):
    return f"lgS_{cid}"


def rcv(cid):
    return f"lgR_{cid}"


# ======================================================================
# GUI placement on the main canvas (positions follow the SVG silkscreen)
# ======================================================================
def place_controls(cv, controls):
    """Emit the GUI object for each layout control that has one. Returns nothing:
    machinery listens on lgS_* sends, so no direct connections are needed here."""
    for c in controls:
        cid, kind, svg, bind = c["id"], c["kind"], c["svg"], c["bind"]
        if cid.startswith("xp_"):
            continue                     # XPNDR controls live in the [pd xpndr] canvas
        if cid.startswith("seq_"):
            continue                     # SEQ/SCALE controls live in the [pd seq] canvas
        if svg is None or kind == "jack":
            continue
        x, y = svg["x"], svg["y"]
        if kind == "knob":
            small = svg.get("small", False)
            w = 70 if small else 110
            if bind and bind[0] == "special" and bind[1] in ("splice_data", "dist_preset"):
                cv.nbx(x - 20, y - 8, 4, c["lo"], c["hi"], snd(cid), rcv(cid), svg["name"], c["default"])
            else:
                cv.hsl(x - w // 2, y - 8, w, c["lo"], c["hi"], snd(cid), rcv(cid), svg["name"], c["default"])
        elif kind == "switch":
            n = len(svg["labels"])
            label = svg.get("title") or ".".join(svg["labels"])
            cv.hradio(x - n * 17 // 2, y - 8, n, snd(cid), rcv(cid), label, c["default"])
        elif kind == "button":
            # buttons that are really latching controls become toggles
            if bind and (bind[0] == "toggle" or (bind[0] == "special" and bind[1] == "record")):
                cv.tgl(x - 9, y - 9, snd(cid), rcv(cid), svg["label"], c["default"])
            else:
                cv.bng(x - 9, y - 9, snd(cid), rcv(cid), svg["label"])
        elif kind == "toggle":
            cv.tgl(x - 9, y - 9, snd(cid), rcv(cid), svg["label"], c["default"])
        elif kind == "led":
            cv.nbx(x - 28, y - 8, 6, -1e+37, 1e+37, "empty", rcv(cid), svg.get("label") or cid, 0)


# ======================================================================
# Wiring subpatch: msg / msgmap / toggle / bang bindings + specials
# ======================================================================
def build_wiring(controls):
    cv = Canvas(80, 80, 1000, 2000, subname="wiring")
    cv.comment(10, 10, "generated control wiring: lgS_* -> engine messages on lg_engine")
    yy = 40

    def eng(idx_out, obj_idx):
        s = cv.obj(900, yy, "s lg_engine")
        cv.connect(obj_idx, idx_out, s, 0)

    for c in controls:
        cid, bind = c["id"], c["bind"]
        if not bind:
            continue
        kind = bind[0]
        if kind == "msg":
            r = cv.obj(20, yy, f"r {snd(cid)}")
            m = cv.msg(20, yy + 25, f"{bind[1]} \\$1")
            s = cv.obj(20, yy + 50, "s lg_engine")
            cv.connect(r, 0, m, 0)
            cv.connect(m, 0, s, 0)
            yy += 80
        elif kind == "toggle":
            r = cv.obj(20, yy, f"r {snd(cid)}")
            m = cv.msg(20, yy + 25, f"{bind[1]} \\$1")
            s = cv.obj(20, yy + 50, "s lg_engine")
            cv.connect(r, 0, m, 0)
            cv.connect(m, 0, s, 0)
            yy += 80
        elif kind == "bang":
            r = cv.obj(20, yy, f"r {snd(cid)}")
            m = cv.msg(20, yy + 25, esc_msg(bind[1]))
            s = cv.obj(20, yy + 50, "s lg_engine")
            cv.connect(r, 0, m, 0)
            cv.connect(m, 0, s, 0)
            yy += 80
        elif kind == "msgmap":
            msgs = bind[1]
            r = cv.obj(20, yy, f"r {snd(cid)}")
            sel = cv.obj(20, yy + 25, "sel " + " ".join(str(i) for i in range(len(msgs))))
            s = cv.obj(20, yy + 75, "s lg_engine")
            cv.connect(r, 0, sel, 0)
            for i, mtext in enumerate(msgs):
                if mtext is None:
                    continue
                m = cv.msg(20 + 90 * i, yy + 50, esc_msg(mtext))
                cv.connect(sel, i, m, 0)
                cv.connect(m, 0, s, 0)
            yy += 105

    # ---- specials ----
    # RECORD arm: ON -> recinput/recsplice/record 1 per REC MODE; OFF -> record 0
    r = cv.obj(20, yy, f"r {snd('record')}")
    sel = cv.obj(20, yy + 25, "sel 1 0")
    fm = cv.obj(20, yy + 50, "f 2")
    rm = cv.obj(150, yy + 25, f"r {snd('recmode')}")
    sm = cv.obj(20, yy + 75, "sel 0 1 2")
    m0 = cv.msg(20, yy + 100, "recinput")
    m1 = cv.msg(110, yy + 100, "recsplice")
    m2 = cv.msg(200, yy + 100, "record 1")
    moff = cv.msg(300, yy + 100, "record 0")
    s = cv.obj(20, yy + 130, "s lg_engine")
    cv.connect(r, 0, sel, 0)
    cv.connect(sel, 0, fm, 0)
    cv.connect(rm, 0, fm, 1)
    cv.connect(fm, 0, sm, 0)
    cv.connect(sm, 0, m0, 0)
    cv.connect(sm, 1, m1, 0)
    cv.connect(sm, 2, m2, 0)
    cv.connect(sel, 1, moff, 0)
    for m in (m0, m1, m2, moff):
        cv.connect(m, 0, s, 0)
    yy += 165

    # Reel I/O
    r = cv.obj(20, yy, f"r {snd('reel_load')}")
    op = cv.obj(20, yy + 25, "openpanel")
    m = cv.msg(20, yy + 50, "load \\$1")
    s = cv.obj(20, yy + 75, "s lg_engine")
    cv.connect(r, 0, op, 0); cv.connect(op, 0, m, 0); cv.connect(m, 0, s, 0)
    r = cv.obj(220, yy, f"r {snd('reel_save')}")
    sp = cv.obj(220, yy + 25, "savepanel")
    m = cv.msg(220, yy + 50, "save \\$1")
    cv.connect(r, 0, sp, 0); cv.connect(sp, 0, m, 0); cv.connect(m, 0, s, 0)
    yy += 110

    # Splice nav: prev/next -> shift +-1 + panel-side counter; ENTER -> splice_finish_nav <DATA>
    rp = cv.obj(20, yy, f"r {snd('splice_prev')}")
    mp = cv.msg(20, yy + 25, "-1")
    rn = cv.obj(90, yy, f"r {snd('splice_next')}")
    mn = cv.msg(90, yy + 25, "1")
    t = cv.obj(20, yy + 50, "t b f")
    ms = cv.msg(160, yy + 75, "shift \\$1")
    s = cv.obj(160, yy + 100, "s lg_engine")
    fc = cv.obj(20, yy + 75, "f 0")           # counter memory
    pl = cv.obj(20, yy + 100, "+ 0")
    tt = cv.obj(20, yy + 125, "t f f")
    sd = cv.obj(90, yy + 150, f"s {rcv('splice_led')}")
    cv.connect(rp, 0, mp, 0); cv.connect(rn, 0, mn, 0)
    cv.connect(mp, 0, t, 0); cv.connect(mn, 0, t, 0)
    cv.connect(t, 1, pl, 1)                    # delta -> addend (fires first)
    cv.connect(t, 1, ms, 0)                    # delta -> shift msg
    cv.connect(ms, 0, s, 0)
    cv.connect(t, 0, fc, 0)                    # bang counter
    cv.connect(fc, 0, pl, 0)
    cv.connect(pl, 0, tt, 0)
    cv.connect(tt, 0, fc, 1)                   # store back
    cv.connect(tt, 1, sd, 0)                   # display
    re = cv.obj(300, yy, f"r {snd('splice_enter')}")
    fe = cv.obj(300, yy + 25, "f 0")
    rd = cv.obj(400, yy, f"r {snd('splice_data')}")
    me = cv.msg(300, yy + 50, "splice_finish_nav \\$1")
    cv.connect(re, 0, fe, 0); cv.connect(rd, 0, fe, 1)
    cv.connect(fe, 0, me, 0); cv.connect(me, 0, s, 0)
    yy += 185

    # PRESETS/SNAPSHOTS: slot buttons (4 rows x 8) store the slot number;
    # STORE/RECALL/SNAP act on the last-touched slot
    s = cv.obj(20, yy + 310, "s lg_engine")
    rs = cv.obj(20, yy + 230, f"r {snd('snap_store')}")
    rj = cv.obj(120, yy + 230, f"r {snd('morph_snap')}")
    ms_ = cv.msg(20, yy + 280, "snapshot \\$1")
    rr_ = cv.obj(320, yy + 230, f"r {snd('snap_recall')}")
    mr = cv.msg(320, yy + 280, "snapshot_recall \\$1")
    fs2 = cv.obj(20, yy + 255, "f 0")
    fr2 = cv.obj(320, yy + 255, "f 0")
    for i in range(32):
        row, col = divmod(i, 8)
        rr = cv.obj(20 + col * 85, yy + row * 55, f"r {snd(f'snap{i+1}')}")
        mm = cv.msg(20 + col * 85, yy + row * 55 + 25, str(i))
        cv.connect(rr, 0, mm, 0)
        cv.connect(mm, 0, fs2, 1)
        cv.connect(mm, 0, fr2, 1)
    cv.connect(rs, 0, fs2, 0); cv.connect(rj, 0, fs2, 0)
    cv.connect(rr_, 0, fr2, 0)
    cv.connect(fs2, 0, ms_, 0); cv.connect(fr2, 0, mr, 0)
    cv.connect(ms_, 0, s, 0); cv.connect(mr, 0, s, 0)
    yy += 345

    # DISTORTION preset knob -> preset message bundles
    r = cv.obj(20, yy, f"r {snd('dist_preset')}")
    ex = cv.obj(20, yy + 25, "expr int(\\$f1+0.5)")
    sel = cv.obj(20, yy + 50, "sel " + " ".join(str(k) for k in sorted(L.DIST_PRESETS)))
    s = cv.obj(20, yy + 130, "s lg_engine")
    cv.connect(r, 0, ex, 0)
    cv.connect(ex, 0, sel, 0)
    for i, k in enumerate(sorted(L.DIST_PRESETS)):
        m = cv.msg(20 + i * 110, yy + 90, esc_msg(", ".join(L.DIST_PRESETS[k])))
        cv.connect(sel, i, m, 0)
        cv.connect(m, 0, s, 0)
    yy += 165

    # Matrix depth policy value: depth * (1 - 2*pol)
    rd = cv.obj(20, yy, f"r {snd('mx_depth')}")
    rp = cv.obj(150, yy, f"r {snd('mx_pol')}")
    tp = cv.obj(150, yy + 25, "t b f")
    pk = cv.obj(20, yy + 50, "pack f f")
    ex = cv.obj(20, yy + 75, "expr \\$f1*(1-2*\\$f2)")
    sv = cv.obj(20, yy + 100, "s lg_mxdepth_val")
    cv.connect(rd, 0, pk, 0)
    cv.connect(rp, 0, tp, 0)
    cv.connect(tp, 1, pk, 1)
    cv.connect(tp, 0, pk, 0)
    cv.connect(pk, 0, ex, 0)
    cv.connect(ex, 0, sv, 0)
    yy += 135

    return cv


# ======================================================================
# SOURCE SHAPE routing layer (FAMILY x INST -> per-family selectors)
# ======================================================================
def build_shape(controls):
    cv = Canvas(120, 120, 1100, 1400, subname="shape_logic")
    cv.comment(10, 10, "SOURCE SHAPE routing: FAMILY x INST route RATE/A/B/C/D "
                       "to per-family selectors (panel_layout.SHAPE_MEANINGS)")
    yy = 40
    # family gates
    rf = cv.obj(20, yy, f"r {snd('shape_family')}")
    for k in range(len(L.SHAPE_FAMILIES)):
        e = cv.obj(20 + k * 115, yy + 25, f"expr \\$f1=={k}")
        sg = cv.obj(20 + k * 115, yy + 50, f"s lg_famok_{k}")
        cv.connect(rf, 0, e, 0)
        cv.connect(e, 0, sg, 0)
    yy += 90
    # instance (1-based)
    ri = cv.obj(20, yy, f"r {snd('shape_inst')}")
    p1 = cv.obj(20, yy + 25, "+ 1")
    si = cv.obj(20, yy + 50, f"s lg_shape_inst1")
    cv.connect(ri, 0, p1, 0)
    cv.connect(p1, 0, si, 0)
    yy += 90

    def eng_send(x, y):
        return cv.obj(x, y, "s lg_engine")

    # knob meaning chains
    for knob_id, fam_map in L.SHAPE_MEANINGS.items():
        for fam, (sel, form, lo, hi) in sorted(fam_map.items()):
            r = cv.obj(20, yy, f"r {snd(knob_id)}")
            g = cv.obj(20, yy + 25, "spigot")
            rg = cv.obj(150, yy, f"r lg_famok_{fam}")
            cv.connect(r, 0, g, 0)
            cv.connect(rg, 0, g, 1)
            head = g
            if lo is not None:
                e = cv.obj(20, yy + 50, f"expr {fnum(lo)}+({fnum(hi)}-{fnum(lo)})*\\$f1")
                cv.connect(head, 0, e, 0)
                head = e
            if form == "global":
                m = cv.msg(20, yy + 75, f"{sel} \\$1")
                cv.connect(head, 0, m, 0)
                s = eng_send(20, yy + 100)
                cv.connect(m, 0, s, 0)
            elif form == "inst2":
                pk = cv.obj(20, yy + 75, "pack f f")
                rr = cv.obj(280, yy + 50, "r lg_shape_inst1")
                m = cv.msg(20, yy + 100, f"{sel} \\$2 \\$1")
                cv.connect(head, 0, pk, 0)
                cv.connect(rr, 0, pk, 1)
                cv.connect(pk, 0, m, 0)
                s = eng_send(20, yy + 125)
                cv.connect(m, 0, s, 0)
            elif form == "suffix":
                pk = cv.obj(20, yy + 75, "pack f f")
                rr = cv.obj(280, yy + 50, "r lg_shape_inst1")
                sw = cv.msg(20, yy + 100, "\\$2 \\$1")       # -> (inst, v)
                rt = cv.obj(20, yy + 125, "route 1 2 3 4")
                cv.connect(head, 0, pk, 0)
                cv.connect(rr, 0, pk, 1)
                cv.connect(pk, 0, sw, 0)
                cv.connect(sw, 0, rt, 0)
                s = eng_send(20, yy + 200)
                for i in range(4):
                    m = cv.msg(20 + i * 130, yy + 160, f"{sel}_{i+1} \\$1")
                    cv.connect(rt, i, m, 0)
                    cv.connect(m, 0, s, 0)
                yy += 90
            yy += 160

    # D knob raw broadcast (SPHR kick strength store)
    r = cv.obj(20, yy, f"r {snd('shape_d')}")
    sD = cv.obj(20, yy + 25, "s lg_shape_dval")
    cv.connect(r, 0, sD, 0)
    yy += 60

    # KICK: sphere family only — sphere_kick <inst> <s> <s> <s>
    r = cv.obj(20, yy, f"r {snd('shape_kick')}")
    g = cv.obj(20, yy + 25, "spigot")
    rg = cv.obj(150, yy, f"r lg_famok_{L.SHAPE_KICK_FAMILY}")
    f = cv.obj(20, yy + 50, "f 0.5")
    rv = cv.obj(280, yy + 25, "r lg_shape_dval")
    mu = cv.obj(20, yy + 75, f"* {fnum(L.SHAPE_KICK_SCALE)}")
    pk = cv.obj(20, yy + 100, "pack f f")
    ri2 = cv.obj(280, yy + 75, "r lg_shape_inst1")
    m = cv.msg(20, yy + 125, "sphere_kick \\$2 \\$1 \\$1 \\$1")
    s = cv.obj(20, yy + 150, "s lg_engine")
    cv.connect(r, 0, g, 0); cv.connect(rg, 0, g, 1)
    cv.connect(g, 0, f, 0); cv.connect(rv, 0, f, 1)
    cv.connect(f, 0, mu, 0); cv.connect(mu, 0, pk, 0)
    cv.connect(ri2, 0, pk, 1)
    cv.connect(pk, 0, m, 0); cv.connect(m, 0, s, 0)
    yy += 185

    # RESET per family
    r = cv.obj(20, yy, f"r {snd('shape_reset')}")
    for i, (fam, sel) in enumerate(sorted(L.SHAPE_RESET.items())):
        g = cv.obj(20 + i * 200, yy + 25, "spigot")
        rg = cv.obj(120 + i * 200, yy, f"r lg_famok_{fam}")
        f = cv.obj(20 + i * 200, yy + 50, "f 1")
        ri3 = cv.obj(120 + i * 200, yy + 25, "r lg_shape_inst1")
        m = cv.msg(20 + i * 200, yy + 75, f"{sel} \\$1")
        s = cv.obj(20 + i * 200, yy + 100, "s lg_engine")
        cv.connect(r, 0, g, 0); cv.connect(rg, 0, g, 1)
        cv.connect(g, 0, f, 0); cv.connect(ri3, 0, f, 1)
        cv.connect(f, 0, m, 0); cv.connect(m, 0, s, 0)
    yy += 135

    # MODE per family: <sel> <inst> <mode>
    r = cv.obj(20, yy, f"r {snd('shape_mode')}")
    for i, (fam, sel) in enumerate(sorted(L.SHAPE_MODE.items())):
        g = cv.obj(20 + i * 250, yy + 25, "spigot")
        rg = cv.obj(120 + i * 250, yy, f"r lg_famok_{fam}")
        pk = cv.obj(20 + i * 250, yy + 50, "pack f f")
        ri4 = cv.obj(120 + i * 250, yy + 25, "r lg_shape_inst1")
        m = cv.msg(20 + i * 250, yy + 75, f"{sel} \\$2 \\$1")
        s = cv.obj(20 + i * 250, yy + 100, "s lg_engine")
        cv.connect(r, 0, g, 0); cv.connect(rg, 0, g, 1)
        cv.connect(g, 0, pk, 0); cv.connect(ri4, 0, pk, 1)
        cv.connect(pk, 0, m, 0); cv.connect(m, 0, s, 0)
    yy += 135

    return cv


# ======================================================================
# Matrix logic: one [sel 0 1] + connect/disconnect messages per pin
# ======================================================================
def build_matrix_logic():
    cv = Canvas(160, 160, 900, 400, subname="matrix_logic")
    cv.comment(10, 10, "generated 16x22 Presto-Patch wiring. pin ON -> matrix_connect "
                       "<src> <dest> <signed depth from the DEPTH/POL controls>; "
                       "pin OFF -> matrix_disconnect <src> <dest>")
    # shared depth appender
    rc = cv.obj(20, 60, "r lg_mxc")
    la = cv.obj(20, 85, "list append")
    rdv = cv.obj(200, 60, "r lg_mxdepth_val")
    lp = cv.obj(20, 110, "list prepend matrix_connect")
    lt = cv.obj(20, 135, "list trim")
    se = cv.obj(20, 160, "s lg_engine")
    cv.connect(rc, 0, la, 0)
    cv.connect(rdv, 0, la, 1)
    cv.connect(la, 0, lp, 0)
    cv.connect(lp, 0, lt, 0)
    cv.connect(lt, 0, se, 0)
    sd = cv.obj(200, 135, "s lg_engine")   # disconnect path target

    x0, y0 = 20, 220
    col = 0
    for i, (sdisp, seng) in enumerate(L.MATRIX_SRCS):
        for j, (ddisp, deng) in enumerate(L.MATRIX_DSTS):
            x = x0 + (col % 8) * 105
            y = y0 + (col // 8) * 110
            col += 1
            r = cv.obj(x, y, f"r {snd(f'mx_{i}_{j}')}")
            sl = cv.obj(x, y + 25, "sel 0 1")
            mth = cv.msg(x, y + 50, f"{seng} {deng}")
            mdis = cv.msg(x, y + 75, f"matrix_disconnect {seng} {deng}")
            sc = cv.obj(x + 40, y + 50, "s lg_mxc")
            cv.connect(r, 0, sl, 0)
            cv.connect(sl, 1, mth, 0)
            cv.connect(mth, 0, sc, 0)
            cv.connect(sl, 0, mdis, 0)
            cv.connect(mdis, 0, sd, 0)
    return cv


# ======================================================================
# Engine subpatch: ligase~, audio I/O, the 22 line~ CV chains, taps
# ======================================================================
def build_engine(controls):
    cv = Canvas(200, 100, 1400, 700, subname="engine")
    cv.comment(10, 8, "the single ligase~ instance. lg_engine bus -> left inlet; "
                      "state outlet 9 -> lg_state9 bus. every IN-badged control "
                      "arrives as [r lgS_*] -> [pack f 20] -> [line~] -> its signal inlet "
                      "(headless 0: the panel IS the hardware)")
    inlet_ctls = sorted(((c["bind"][1], c) for c in controls if c["bind"] and c["bind"][0] == "inlet"),
                        key=lambda t: t[0])
    lig = cv.obj(60, 420, "ligase~")
    adc = cv.obj(20, 40, "adc~")
    cv.connect(adc, 0, lig, 0)
    cv.connect(adc, 1, lig, 1)
    reng = cv.obj(20, 370, "r lg_engine")
    cv.connect(reng, 0, lig, 0)
    x = 100
    for n, c in inlet_ctls:
        r = cv.obj(x, 240, f"r {snd(c['id'])}")
        pk = cv.obj(x, 270, "pack f 20")
        ln = cv.obj(x, 300, "line~")
        cv.connect(r, 0, pk, 0)
        cv.connect(pk, 0, ln, 0)
        cv.connect(ln, 0, lig, n - 1)
        x += 120
    dac = cv.obj(60, 560, "dac~")
    cv.connect(lig, 0, dac, 0)
    cv.connect(lig, 1, dac, 1)
    el = cv.obj(200, 500, "env~")
    er = cv.obj(280, 500, "env~")
    svl = cv.obj(200, 530, f"s {rcv('vu_l')}")
    svr = cv.obj(280, 530, f"s {rcv('vu_r')}")
    cv.connect(lig, 0, el, 0)
    cv.connect(lig, 1, er, 0)
    cv.connect(el, 0, svl, 0)
    cv.connect(er, 0, svr, 0)
    st = cv.obj(400, 500, "s lg_state9")
    cv.connect(lig, 8, st, 0)
    cv.comment(500, 540, "scope taps: outlets 10/11 (scope_x~/scope_y~). connect an "
                         "[oscilloscope~] here in plugdata for the SCOPE display; "
                         "left unconnected for headless vanilla pd")
    # SCOPE XY tap (web build): window scope_x~/scope_y~ into two named arrays the
    # AudioWorklet reads via libpd_read_array to draw the live XY display. Harmless in
    # vanilla/plugdata (just two recording arrays + a 30 Hz metro); outlets 10/11 are
    # indices 9/10 (state = outlet 9 = index 8, connected above).
    cv.obj(700, 470, "table scope_x_arr 256")
    cv.obj(700, 500, "table scope_y_arr 256")
    sxw = cv.obj(500, 470, "tabwrite~ scope_x_arr")
    syw = cv.obj(600, 470, "tabwrite~ scope_y_arr")
    cv.connect(lig, 9, sxw, 0)
    cv.connect(lig, 10, syw, 0)
    slb = cv.obj(500, 410, "loadbang")
    smt = cv.obj(500, 440, "metro 33")
    cv.connect(slb, 0, smt, 0)
    cv.connect(smt, 0, sxw, 0)
    cv.connect(smt, 0, syw, 0)
    return cv


# ======================================================================
# Displays: 10 Hz poll -> readouts (GATE A.6)
# ======================================================================
def build_displays():
    cv = Canvas(240, 140, 700, 500, subname="displays")
    cv.comment(10, 10, "10 Hz [metro 100] poll: get_params -> outlet 9 -> readouts. "
                       "NB the engine reports the CURRENT SPLICE on the console only "
                       "(no outlet-9 selector); the splice LED is a panel-side counter.")
    rgo = cv.obj(20, 50, "r lg_poll_on")
    mt = cv.obj(20, 80, "metro 100")
    mg = cv.msg(20, 110, "get_params")
    se = cv.obj(20, 140, "s lg_engine")
    cv.connect(rgo, 0, mt, 0)
    cv.connect(mt, 0, mg, 0)
    cv.connect(mg, 0, se, 0)
    rs = cv.obj(20, 200, "r lg_state9")
    rt = cv.obj(20, 230, "route grainsize speed amplitude pan bpm")
    for i, nm in enumerate(("disp_grainsize", "disp_speed", "disp_amplitude", "disp_pan", "disp_bpm")):
        s = cv.obj(20 + i * 130, 270, f"s {rcv(nm)}")
        cv.connect(rt, i, s, 0)
    cv.connect(rs, 0, rt, 0)
    return cv


# ======================================================================
# XPNDR canvas body (shared by the [pd xpndr] subpatch and the standalone file)
# ======================================================================
def build_xpndr_body(cv, controls, standalone=False):
    C = {c["id"]: c for c in controls}
    cv.cnv(20, 10, 560, 24, "SNAPSHOT_EXPANDER_-_cold_edit_buffer_(snapbuf_*)", 14)
    if standalone:
        cv.comment(20, 40, "standalone variant: speaks only the lg_engine / lg_state9 buses "
                           "published by ligase_panel.pd (or any patch with [r lg_engine] -> "
                           "[ligase~] and outlet 9 -> [s lg_state9])")
    # ---- GUI ----
    def gx(cid):
        return C[cid]["svg"]["x"] - 1500 + 40 if C[cid]["svg"] else 40

    def gy(cid):
        return C[cid]["svg"]["y"] - 60 if C[cid]["svg"] else 60

    led = cv.nbx(gx("xp_led") - 20, gy("xp_led"), 5, 0, 63, "empty", rcv("xp_led"), "buffer_snap", 0)
    slot = cv.nbx(gx("xp_slot"), gy("xp_slot"), 4, 0, 63, snd("xp_slot"), rcv("xp_slot"), "DATA_slot", 0)
    ld = cv.bng(gx("xp_load"), gy("xp_load"), snd("xp_load"), rcv("xp_load"), "LOAD")
    fl = cv.bng(gx("xp_fromlive"), gy("xp_fromlive"), snd("xp_fromlive"), rcv("xp_fromlive"), "FROM_LIVE")
    pg = cv.hradio(gx("xp_page") - 68, gy("xp_page"), 8, snd("xp_page"), rcv("xp_page"), "PAGE", 0)
    pr = cv.hradio(gx("xp_param") - 68, gy("xp_param"), 8, snd("xp_param"), rcv("xp_param"), "PARAM", 0)
    val = cv.nbx(gx("xp_value") - 20, gy("xp_value"), 8, -1e+37, 1e+37, snd("xp_value"), rcv("xp_value"), "VALUE", 0)
    mn = cv.hsl(gx("xp_min") - 20, gy("xp_min"), 70, 0, 1, snd("xp_min"), rcv("xp_min"), "MIN", 0)
    mx = cv.hsl(gx("xp_max") - 20, gy("xp_max"), 70, 0, 1, snd("xp_max"), rcv("xp_max"), "MAX", 1)
    sw = cv.hsl(gx("xp_slew") - 20, gy("xp_slew"), 70, 0, 1, snd("xp_slew"), rcv("xp_slew"), "SLEW", 0)
    en = cv.tgl(gx("xp_enabled"), gy("xp_enabled"), snd("xp_enabled"), rcv("xp_enabled"), "ENABLED", True)
    iv = cv.tgl(gx("xp_invert"), gy("xp_invert"), snd("xp_invert"), rcv("xp_invert"), "INVERT", False)
    so = cv.hradio(gx("xp_source") - 40, gy("xp_source"), 7, snd("xp_source"), rcv("xp_source"), "SOURCE_OFF.PERL.LRNZ.NBDY.SPHR.RAND.PAT", 0)
    it = cv.hradio(gx("xp_inst"), gy("xp_inst"), 4, snd("xp_inst"), rcv("xp_inst"), "INST", 0)
    stb = cv.bng(gx("xp_store"), gy("xp_store"), snd("xp_store"), rcv("xp_store"), "STORE")
    asb = cv.bng(gx("xp_assign"), gy("xp_assign"), snd("xp_assign"), rcv("xp_assign"), "ASSIGN")
    au = cv.tgl(gx("xp_audition"), gy("xp_audition"), snd("xp_audition"), rcv("xp_audition"), "AUDITION", False)
    ab = cv.bng(gx("xp_compare"), gy("xp_compare"), snd("xp_compare"), rcv("xp_compare"), "A/B")
    cv.comment(40, 620, "PAGE x PARAM -> snapbuf field (curated map in panel_layout.py). "
                        "VALUE edits the scalar/discrete field; the BAND cluster edits "
                        "<field>_range subfields (MIN/MAX/SLEW are normalized 0-1 sliders "
                        "- raw field units for 0-1 params; retune ranges in panel_layout).")

    # ---- logic ----
    X, Y = 620, 40
    # address decode
    rpg = cv.obj(X, Y, f"r {snd('xp_page')}")
    rpr = cv.obj(X + 140, Y, f"r {snd('xp_param')}")
    tpr = cv.obj(X + 140, Y + 25, "t b f")
    pk = cv.obj(X, Y + 50, "pack f f")
    ex = cv.obj(X, Y + 75, "expr \\$f1*8+\\$f2")
    sel = cv.obj(X, Y + 100, "sel " + " ".join(str(i) for i in range(64)))
    cv.connect(rpg, 0, pk, 0)
    cv.connect(rpr, 0, tpr, 0)
    cv.connect(tpr, 1, pk, 1)
    cv.connect(tpr, 0, pk, 0)
    cv.connect(pk, 0, ex, 0)
    cv.connect(ex, 0, sel, 0)
    svf = cv.obj(X, Y + 620, "s lg_xp_vfield")
    srf = cv.obj(X + 120, Y + 620, "s lg_xp_rfield")
    svo = cv.obj(X + 240, Y + 620, "s lg_xp_vok")
    sro = cv.obj(X + 340, Y + 620, "s lg_xp_rok")
    sget = cv.obj(X + 440, Y + 620, "s lg_engine")
    idx = 0
    for page in L.XPNDR_PAGES:
        for (vf, rf) in L.XPNDR_FIELDS[page]:
            xx = X + (idx % 8) * 150
            yyq = Y + 140 + (idx // 8) * 58
            parts = []
            parts.append(f"symbol {vf if vf else 'none'}")
            m1 = cv.msg(xx, yyq, esc_msg(", ".join([
                f"symbol {vf if vf else 'none'}",
            ])))
            cv.connect(sel, idx, m1, 0)
            cv.connect(m1, 0, svf, 0)
            m2 = cv.msg(xx, yyq + 18, f"symbol {rf if rf else 'none'}")
            cv.connect(sel, idx, m2, 0)
            cv.connect(m2, 0, srf, 0)
            m3 = cv.msg(xx + 70, yyq, f"{1 if vf else 0}")
            cv.connect(sel, idx, m3, 0)
            cv.connect(m3, 0, svo, 0)
            m4 = cv.msg(xx + 70, yyq + 18, f"{1 if rf else 0}")
            cv.connect(sel, idx, m4, 0)
            cv.connect(m4, 0, sro, 0)
            if vf:
                m5 = cv.msg(xx + 100, yyq, f"snapbuf_get {vf}")
                cv.connect(sel, idx, m5, 0)
                cv.connect(m5, 0, sget, 0)
            idx += 1

    Y2 = Y + 660
    seng = cv.obj(X, Y2 + 260, "s lg_engine")
    # slot ops
    fj = cv.obj(X, Y2, "f 0")
    rslot = cv.obj(X + 120, Y2 - 25, f"r {snd('xp_slot')}")
    cv.connect(rslot, 0, fj, 1)
    rl = cv.obj(X, Y2 - 25, f"r {snd('xp_load')}")
    cv.connect(rl, 0, fj, 0)
    tld = cv.obj(X, Y2 + 25, "t f f")
    mld = cv.msg(X, Y2 + 50, "snapbuf_load \\$1")
    sled = cv.obj(X + 120, Y2 + 50, f"s {rcv('xp_led')}")
    cv.connect(fj, 0, tld, 0)
    cv.connect(tld, 0, mld, 0)
    cv.connect(tld, 1, sled, 0)
    cv.connect(mld, 0, seng, 0)
    rfl = cv.obj(X + 260, Y2 - 25, f"r {snd('xp_fromlive')}")
    mfl = cv.msg(X + 260, Y2, "snapbuf_from_live")
    cv.connect(rfl, 0, mfl, 0)
    cv.connect(mfl, 0, seng, 0)
    rst = cv.obj(X + 400, Y2 - 25, f"r {snd('xp_store')}")
    fst = cv.obj(X + 400, Y2, "f 0")
    cv.connect(rslot, 0, fst, 1)
    mst = cv.msg(X + 400, Y2 + 25, "snapbuf_store \\$1")
    cv.connect(rst, 0, fst, 0)
    cv.connect(fst, 0, mst, 0)
    cv.connect(mst, 0, seng, 0)
    ras = cv.obj(X + 560, Y2 - 25, f"r {snd('xp_assign')}")
    mas = cv.msg(X + 560, Y2, "snapbuf_apply")
    cv.connect(ras, 0, mas, 0)
    cv.connect(mas, 0, seng, 0)
    rau = cv.obj(X + 680, Y2 - 25, f"r {snd('xp_audition')}")
    mau = cv.msg(X + 680, Y2, "snapbuf_audition \\$1")
    cv.connect(rau, 0, mau, 0)
    cv.connect(mau, 0, seng, 0)
    rab = cv.obj(X + 840, Y2 - 25, f"r {snd('xp_compare')}")
    mab = cv.msg(X + 840, Y2, "snapbuf_compare")
    cv.connect(rab, 0, mab, 0)
    cv.connect(mab, 0, seng, 0)

    # VALUE -> snapbuf_set <field> <v>   (gated by vok)
    Y3 = Y2 + 320
    rv = cv.obj(X, Y3, f"r {snd('xp_value')}")
    gv = cv.obj(X, Y3 + 25, "spigot")
    rvo = cv.obj(X + 100, Y3, "r lg_xp_vok")
    pkv = cv.obj(X, Y3 + 50, "pack f s")
    rvf = cv.obj(X + 200, Y3 + 25, "r lg_xp_vfield")
    mv = cv.msg(X, Y3 + 75, "snapbuf_set \\$2 \\$1")
    cv.connect(rv, 0, gv, 0)
    cv.connect(rvo, 0, gv, 1)
    cv.connect(gv, 0, pkv, 0)
    cv.connect(rvf, 0, pkv, 1)
    cv.connect(pkv, 0, mv, 0)
    cv.connect(mv, 0, seng, 0)

    # outlet-9 snapbuf reports populate VALUE (field must match the address)
    Y4 = Y3 + 120
    r9 = cv.obj(X, Y4, "r lg_state9")
    rt9 = cv.obj(X, Y4 + 25, "route snapbuf")
    ls = cv.obj(X, Y4 + 50, "list split 1")
    fv = cv.obj(X + 260, Y4 + 100, "f 0")
    ltr = cv.obj(X, Y4 + 75, "symbol")
    slm = cv.obj(X, Y4 + 100, "select dummy")
    rvf2 = cv.obj(X + 140, Y4 + 75, "r lg_xp_vfield")
    sv2 = cv.obj(X + 260, Y4 + 130, f"s {rcv('xp_value')}")
    up = cv.obj(X + 260, Y4 + 75, "unpack f")   # whole-band reports carry 8 floats; keep the first
    cv.connect(r9, 0, rt9, 0)
    cv.connect(rt9, 0, ls, 0)
    cv.connect(ls, 1, up, 0)
    cv.connect(up, 0, fv, 1)          # store value (fires before the match bang)
    cv.connect(ls, 0, ltr, 0)
    cv.connect(ltr, 0, slm, 0)
    cv.connect(rvf2, 0, slm, 1)       # match symbol = current field
    cv.connect(slm, 0, fv, 0)
    cv.connect(fv, 0, sv2, 0)

    # BAND cluster -> snapbuf_set <range_field> <sub> <v>   (gated by rok)
    Y5 = Y4 + 180
    subs = [("xp_min", "min", None), ("xp_max", "max", None), ("xp_slew", "slew", None),
            ("xp_enabled", "enabled", None), ("xp_invert", "invert", None),
            ("xp_inst", "rand_instance", None)]
    for i, (cid, subname, _) in enumerate(subs):
        xx = X + (i % 3) * 320
        yyq = Y5 + (i // 3) * 130
        r = cv.obj(xx, yyq, f"r {snd(cid)}")
        g = cv.obj(xx, yyq + 25, "spigot")
        rro = cv.obj(xx + 90, yyq, "r lg_xp_rok")
        pkb = cv.obj(xx, yyq + 50, "pack f s")
        rrf = cv.obj(xx + 180, yyq + 25, "r lg_xp_rfield")
        m = cv.msg(xx, yyq + 75, f"snapbuf_set \\$2 {subname} \\$1")
        cv.connect(r, 0, g, 0)
        cv.connect(rro, 0, g, 1)
        cv.connect(g, 0, pkb, 0)
        cv.connect(rrf, 0, pkb, 1)
        cv.connect(pkb, 0, m, 0)
        cv.connect(m, 0, seng, 0)
    # SOURCE radio -> rand_type code
    Y6 = Y5 + 280
    r = cv.obj(X, Y6, f"r {snd('xp_source')}")
    slc = cv.obj(X, Y6 + 25, "sel " + " ".join(str(i) for i in range(len(L.XPNDR_SOURCE_CODES))))
    g = cv.obj(X, Y6 + 75, "spigot")
    rro = cv.obj(X + 120, Y6 + 50, "r lg_xp_rok")
    pkb = cv.obj(X, Y6 + 100, "pack f s")
    rrf = cv.obj(X + 220, Y6 + 75, "r lg_xp_rfield")
    m = cv.msg(X, Y6 + 125, "snapbuf_set \\$2 rand_type \\$1")
    cv.connect(r, 0, slc, 0)
    for i, code in enumerate(L.XPNDR_SOURCE_CODES):
        mm = cv.msg(X + i * 60, Y6 + 50, str(code))
        cv.connect(slc, i, mm, 0)
        cv.connect(mm, 0, g, 0)
    cv.connect(rro, 0, g, 1)
    cv.connect(g, 0, pkb, 0)
    cv.connect(rrf, 0, pkb, 1)
    cv.connect(pkb, 0, m, 0)
    cv.connect(m, 0, seng, 0)

    # init the address decode on load (page/param default 0 0 -> field symbols valid)
    lb = cv.obj(X + 300, Y, "loadbang")
    mi = cv.msg(X + 300, Y + 25, f"\\; {rcv('xp_page')} 0 \\; {rcv('xp_param')} 0")
    cv.connect(lb, 0, mi, 0)
    return cv


# ======================================================================
# SEQ / SCALE canvas body (shared by the [pd seq] subpatch and the standalone file)
#
# The harmonic + notation surface (Plans/seq_scale_sidecar.md). Speaks only the
# lg_engine / lg_state9 buses like the XPNDR. Three regions:
#   TONE CIRCLE  12 ring [tgl] = pitch classes -> a complementary-spigot prepend
#                CASCADE composes the ascending degree list (COLD); ROOT/MODE/PRESET;
#                APPLY commits pitch_scale/scale_root/scale_rotate routed by DEST.
#   SLOTS/SEQ    A-P select (pitch_scale_slot); AXIS->SLOTS writes the shape at each
#                axis rotation (pitch_scale_to) AND arms `pattern pitch_scale_slot [..]`.
#   TIME/GRID    euclid `<v>(k,n)` composer (live); 8x16 pin grid -> `pattern <field> [..]`
#                at the VALUE level (the matrix DEPTH-at-pin rule); XPNDR PAGE x PARAM
#                addressing picks the row target.
# String building uses the [list]-family idiom (list prepend / list trim) already shipping
# in the matrix depth-appender; every control keeps its lgR_<id> receive symbol.
# ======================================================================
def build_seq_body(cv, controls, standalone=False):
    C = {c["id"]: c for c in controls}
    cv.cnv(20, 8, 900, 26, "SEQ_/_SCALE_-_tone_circle_·_time_circle_·_pattern_grid", 16)
    if standalone:
        cv.comment(20, 40, "standalone variant: speaks only the lg_engine / lg_state9 buses "
                           "published by ligase_panel.pd. TONE circle edits COLD (APPLY commits); "
                           "SLOTS/AXIS->SLOTS + the euclid arm + the grid pins are live.")

    seng = cv.obj(1250, 60, "s lg_engine")     # shared engine send for this canvas

    def eng(node, outlet=0):
        cv.connect(node, outlet, seng, 0)

    # ---------------- DEST routing state (0 GRAIN / 1 SMEAR / 2 BOTH) ----------------
    rdest = cv.obj(60, 470, f"r {snd('seq_dest')}")
    destf = cv.obj(60, 495, "f 2")
    rdgo = cv.obj(200, 470, "r lg_seq_destpoll")   # poll DEST at commit time
    cv.connect(rdest, 0, destf, 1)
    cv.connect(rdgo, 0, destf, 0)
    gon = cv.obj(60, 520, "expr $f1!=1")           # grain path on unless SMEAR-only
    son = cv.obj(160, 520, "expr $f1!=0")          # smear path on unless GRAIN-only
    cv.connect(destf, 0, gon, 0)
    cv.connect(destf, 0, son, 0)
    sgon = cv.obj(60, 545, "s lg_seq_gon")
    sson = cv.obj(160, 545, "s lg_seq_son")
    cv.connect(gon, 0, sgon, 0)
    cv.connect(son, 0, sson, 0)
    # DEST hradio GUI
    cv.hradio(60, 440, 3, snd("seq_dest"), rcv("seq_dest"), "DEST_GRAIN.SMEAR.BOTH", 2)

    # ================= TONE CIRCLE GUI =================
    ring = {}
    for i in range(12):
        ring[i] = cv.tgl(40 + i * 26, 70, snd(f"seq_ring_{i}"), rcv(f"seq_ring_{i}"),
                         f"pc{i}", C[f"seq_ring_{i}"]["default"], size=18)
    cv.comment(40, 44, "TONE CIRCLE — 12 pitch-class toggles (default = whole tone)")
    cv.hradio(40, 110, 3, snd("seq_ring_order"), rcv("seq_ring_order"), "RING_ORDER_(display)", 0)
    cv.hsl(180, 112, 120, -24, 24, snd("seq_root"), rcv("seq_root"), "ROOT_scale_root", 0)
    cv.hsl(330, 112, 80, 0, 11, snd("seq_mode"), rcv("seq_mode"), "MODE_scale_rotate", 0)
    cv.hsl(430, 112, 80, 0, 4, snd("seq_axis"), rcv("seq_axis"), "AXIS_1.2.3.4.6", 2)
    cv.hsl(530, 112, 80, 0, 5, snd("seq_preset"), rcv("seq_preset"), "PRESET_MAJ..AUG", 0)

    # ---------------- RING CASCADE: complementary-spigot prepend -> ascending degrees ----------------
    # (validated idiom, see scratchpad/gen_cascade_test.py). stage i output feeds stage i-1.
    spA = {}; spB = {}; lp = {}
    cy = 170
    for i in range(11, -1, -1):
        spA[i] = cv.obj(40 + (11 - i) * 60, cy, "spigot")
        spB[i] = cv.obj(70 + (11 - i) * 60, cy + 22, "spigot")
        lp[i] = cv.obj(40 + (11 - i) * 60, cy + 44, f"list prepend {i}")
    scale_out = cv.obj(40, cy + 80, "list")        # the assembled ascending degree list
    for i in range(11, -1, -1):
        cv.connect(spA[i], 0, lp[i], 0)
        if i == 0:
            cv.connect(lp[i], 0, scale_out, 0)
            cv.connect(spB[i], 0, scale_out, 0)
        else:
            cv.connect(lp[i], 0, spA[i - 1], 0)
            cv.connect(lp[i], 0, spB[i - 1], 0)
            cv.connect(spB[i], 0, spA[i - 1], 0)
            cv.connect(spB[i], 0, spB[i - 1], 0)
    # gate wiring + per-ring rebuild trigger
    rreb = cv.obj(760, 150, "r lg_seq_rebuild")
    ring_tff = {}
    for i in range(12):
        rr = cv.obj(40 + i * 58, 300, f"r {snd(f'seq_ring_{i}')}")
        tff = cv.obj(40 + i * 58, 325, "t b f")
        ring_tff[i] = tff
        inv = cv.obj(100 + i * 58, 350, "== 0")
        cv.connect(rr, 0, tff, 0)
        cv.connect(tff, 1, spA[i], 1)          # state -> spA gate (fires first)
        cv.connect(tff, 1, inv, 0)
        cv.connect(inv, 0, spB[i], 1)          # not-state -> spB gate
        srb = cv.obj(40 + i * 58, 375, "s lg_seq_rebuild")
        cv.connect(tff, 0, srb, 0)             # then rebuild
    cv.connect(rreb, 0, spA[11], 0)            # inject empty list (bang) at the top stage
    cv.connect(rreb, 0, spB[11], 0)
    # LOAD-TIME GATE INIT: a spigot's default gate is 0 (closed) on BOTH paths, so a ring pin
    # that is never toggled would dead-end the cascade. Fire every stage's tff once at load
    # with its default value -> an untouched (off) pin gets its pass-through (spB) gate OPEN,
    # so composing from ONLY the ON pins works (the realistic "click what you want" case).
    lbinit = cv.obj(560, 240, "loadbang")
    for i in range(12):
        mi0 = cv.msg(560 + i * 26, 265, fnum(C[f"seq_ring_{i}"]["default"]))
        cv.connect(lbinit, 0, mi0, 0)
        cv.connect(mi0, 0, ring_tff[i], 0)

    # ---------------- scale hold + live readout ----------------
    scale_hold = cv.obj(40, 430, "list append")    # HOLD: right inlet stores; bang left re-emits
    cv.connect(scale_out, 0, scale_hold, 1)
    haslen = cv.obj(300, 430, "list length")
    hasf = cv.obj(300, 455, "> 0")
    shas = cv.obj(300, 480, "s lg_seq_haslen")
    cv.connect(scale_out, 0, haslen, 0)
    cv.connect(haslen, 0, hasf, 0)
    cv.connect(hasf, 0, shas, 0)
    prn = cv.obj(460, 430, "print SEQ_SCALE")       # headless witness of the composed list
    cv.connect(scale_out, 0, prn, 0)
    rdo = cv.obj(560, 405, "list prepend pitch_scale")
    rdset = cv.obj(560, 430, "list prepend set")
    rdmsg = cv.msg(560, 455, "pitch_scale (cold)")
    cv.connect(scale_out, 0, rdo, 0)
    cv.connect(rdo, 0, rdset, 0)
    cv.connect(rdset, 0, rdmsg, 0)

    # ---------------- ROOT / MODE cold holds ----------------
    rroot = cv.obj(180, 160, f"r {snd('seq_root')}")
    rootf = cv.obj(180, 185, "f 0")
    cv.connect(rroot, 0, rootf, 1)
    rmode = cv.obj(300, 160, f"r {snd('seq_mode')}")
    modef = cv.obj(300, 185, "f 0")
    cv.connect(rmode, 0, modef, 1)

    # ---------------- APPLY: commit scale + root + mode, DEST-routed ----------------
    rap = cv.obj(60, 600, f"r {snd('seq_apply')}")
    cv.bng(60, 570, snd("seq_apply"), rcv("seq_apply"), "APPLY")
    # poll DEST first (sets lg_seq_gon/son), THEN emit the three commits
    apt_seq = cv.obj(60, 620, "t b b")           # outlet1 poll (first), outlet0 commit
    mpoll = cv.msg(60, 645, "bang")
    spoll = cv.obj(200, 645, "s lg_seq_destpoll")
    apt = cv.obj(60, 670, "t b b b b")
    cv.connect(rap, 0, apt_seq, 0)
    cv.connect(apt_seq, 1, mpoll, 0)
    cv.connect(mpoll, 0, spoll, 0)
    cv.connect(apt_seq, 0, apt, 0)
    # scale commit (gated by haslen AND dest)
    rhas = cv.obj(360, 650, "r lg_seq_haslen")
    hgate = cv.obj(320, 700, "spigot")           # only commit a non-empty scale
    cv.connect(rhas, 0, hgate, 1)
    cv.connect(apt, 3, scale_hold, 0)            # bang hold -> stored degree list
    cv.connect(scale_hold, 0, hgate, 0)
    # grain scale
    gsg = cv.obj(320, 730, "spigot")
    rgon1 = cv.obj(430, 705, "r lg_seq_gon")
    gsp = cv.obj(320, 755, "list prepend pitch_scale")
    gst = cv.obj(320, 780, "list trim")
    cv.connect(hgate, 0, gsg, 0)
    cv.connect(rgon1, 0, gsg, 1)
    cv.connect(gsg, 0, gsp, 0)
    cv.connect(gsp, 0, gst, 0)
    eng(gst)
    # smear scale
    ssg = cv.obj(560, 730, "spigot")
    rson1 = cv.obj(670, 705, "r lg_seq_son")
    ssp = cv.obj(560, 755, "list prepend smear_pitch_scale")
    sst = cv.obj(560, 780, "list trim")
    cv.connect(hgate, 0, ssg, 0)
    cv.connect(rson1, 0, ssg, 1)
    cv.connect(ssg, 0, ssp, 0)
    cv.connect(ssp, 0, sst, 0)
    eng(sst)
    # root commit
    cv.connect(apt, 2, rootf, 0)
    grg = cv.obj(60, 720, "spigot")
    rgon2 = cv.obj(170, 700, "r lg_seq_gon")
    grp = cv.obj(60, 745, "list prepend scale_root")
    grt = cv.obj(60, 770, "list trim")
    cv.connect(rootf, 0, grg, 0)
    cv.connect(rgon2, 0, grg, 1)
    cv.connect(grg, 0, grp, 0)
    cv.connect(grp, 0, grt, 0)
    eng(grt)
    srg = cv.obj(60, 800, "spigot")
    rson2 = cv.obj(170, 800, "r lg_seq_son")
    srp = cv.obj(60, 825, "list prepend smear_scale_root")
    srt = cv.obj(60, 850, "list trim")
    cv.connect(rootf, 0, srg, 0)
    cv.connect(rson2, 0, srg, 1)
    cv.connect(srg, 0, srp, 0)
    cv.connect(srp, 0, srt, 0)
    eng(srt)
    # mode (scale_rotate) commit
    cv.connect(apt, 1, modef, 0)
    gmg = cv.obj(60, 900, "spigot")
    rgon3 = cv.obj(170, 880, "r lg_seq_gon")
    gmp = cv.obj(60, 925, "list prepend scale_rotate")
    gmt = cv.obj(60, 950, "list trim")
    cv.connect(modef, 0, gmg, 0)
    cv.connect(rgon3, 0, gmg, 1)
    cv.connect(gmg, 0, gmp, 0)
    cv.connect(gmp, 0, gmt, 0)
    eng(gmt)
    smg = cv.obj(260, 900, "spigot")
    rson3 = cv.obj(370, 880, "r lg_seq_son")
    smp = cv.obj(260, 925, "list prepend smear_scale_rotate")
    smt = cv.obj(260, 950, "list trim")
    cv.connect(modef, 0, smg, 0)
    cv.connect(rson3, 0, smg, 1)
    cv.connect(smg, 0, smp, 0)
    cv.connect(smp, 0, smt, 0)
    eng(smt)

    # ---------------- POLYGON PRESET -> light the ring ----------------
    rpre = cv.obj(700, 300, f"r {snd('seq_preset')}")
    prx = cv.obj(700, 325, "expr int($f1+0.5)")
    prsel = cv.obj(700, 350, "sel " + " ".join(str(k) for k in sorted(L.SEQ_PRESET_PCS)))
    cv.connect(rpre, 0, prx, 0)
    cv.connect(prx, 0, prsel, 0)
    for i, p in enumerate(sorted(L.SEQ_PRESET_PCS)):
        pcs = set(L.SEQ_PRESET_PCS[p])
        body = " \\; ".join(f"{rcv(f'seq_ring_{k}')} {1 if k in pcs else 0}" for k in range(12))
        mm = cv.msg(700 + i * 12, 380, body)
        cv.connect(prsel, i, mm, 0)

    # ================= SLOTS / COMMIT =================
    slot_bng = {}
    for i in range(16):
        slot_bng[i] = cv.bng(40 + i * 34, 1010, snd(f"seq_slot_{chr(65+i)}"),
                             rcv(f"seq_slot_{chr(65+i)}"), chr(65 + i))
    cv.comment(40, 990, "SCALE SLOTS A-P: select the live slot (pitch_scale_slot, DEST-routed)")
    # slot select -> pitch_scale_slot i / smear_pitch_scale_slot i, DEST-routed
    spoll2 = cv.obj(600, 1010, "s lg_seq_destpoll")
    for i in range(16):
        mpo = cv.msg(40 + i * 34, 1035, "bang")
        cv.connect(slot_bng[i], 0, mpo, 0)
        cv.connect(mpo, 0, spoll2, 0)
        gm = cv.msg(40 + i * 34, 1060, f"pitch_scale_slot {i}")
        gg = cv.obj(40 + i * 34, 1085, "spigot")
        rg = cv.obj(120 + i * 34, 1085, "r lg_seq_gon")
        cv.connect(slot_bng[i], 0, gm, 0)
        cv.connect(gm, 0, gg, 0)
        cv.connect(rg, 0, gg, 1)
        eng(gg)
        sm = cv.msg(40 + i * 34, 1110, f"smear_pitch_scale_slot {i}")
        sg = cv.obj(40 + i * 34, 1135, "spigot")
        rs = cv.obj(120 + i * 34, 1135, "r lg_seq_son")
        cv.connect(slot_bng[i], 0, sm, 0)
        cv.connect(sm, 0, sg, 0)
        cv.connect(rs, 0, sg, 1)
        eng(sg)

    # ---------------- AXIS -> SLOTS (Coltrane generator) + SEQ arm ----------------
    # vexpr is not compiled in the target vanilla Pd, so element-wise degree transposition
    # is impractical. The axis rotations are instead SEQUENCED THROUGH scale_root -- the
    # engine's own harmonic model ("the polygon spins with scale_root; slot switches reshape
    # it", ligase~.c ~L2223), which IS the Coltrane axis: `pattern scale_root [ 0 4 8 ]` is
    # the Giant Steps three-key tonic cycle. AXIS->SLOTS also writes the composed shape into
    # slot 0 (pitch_scale_to) so the slot bank + pitch_scale_slot audition path stay live.
    cv.bng(40, 1180, snd("seq_axis_slots"), rcv("seq_axis_slots"), "AXIS_->_SLOTS")
    cv.tgl(560, 1180, snd("seq_rev"), rcv("seq_rev"), "REV", False)
    cv.hsl(620, 1182, 70, 0, 8, snd("seq_alt"), rcv("seq_alt"), "ALT_<>_depth", 0)
    raxs = cv.obj(40, 1210, f"r {snd('seq_axis_slots')}")
    raxi = cv.obj(240, 1210, f"r {snd('seq_axis')}")
    axif = cv.obj(240, 1235, "f 2")
    cv.connect(raxi, 0, axif, 1)
    rrevf = cv.obj(360, 1210, f"r {snd('seq_rev')}")
    revf = cv.obj(360, 1235, "f 0")
    cv.connect(rrevf, 0, revf, 1)
    raltf = cv.obj(480, 1210, f"r {snd('seq_alt')}")
    altf = cv.obj(480, 1235, "f 0")
    cv.connect(raltf, 0, altf, 1)
    # AXIS->SLOTS: (2) poll dest first, (1) write shape to slot 0, (0) arm the root sequence
    axt = cv.obj(40, 1260, "t b b b")
    mpo = cv.msg(220, 1245, "bang")
    spo = cv.obj(220, 1270, "s lg_seq_destpoll")
    cv.connect(raxs, 0, axt, 0)
    cv.connect(axt, 2, mpo, 0)
    cv.connect(mpo, 0, spo, 0)
    # (1) write composed shape into slot 0 (pitch_scale_to 0 <deg...>), DEST-routed
    cv.connect(axt, 1, scale_hold, 0)
    gsl = cv.obj(40, 1290, "list prepend 0")
    gslp = cv.obj(40, 1315, "list prepend pitch_scale_to")
    gslt = cv.obj(40, 1340, "list trim")
    gslg = cv.obj(40, 1365, "spigot")
    rgw = cv.obj(210, 1340, "r lg_seq_gon")
    cv.connect(scale_hold, 0, gsl, 0)
    cv.connect(gsl, 0, gslp, 0)
    cv.connect(gslp, 0, gslt, 0)
    cv.connect(gslt, 0, gslg, 0)
    cv.connect(rgw, 0, gslg, 1)
    eng(gslg)
    ssl = cv.obj(280, 1290, "list prepend 0")
    sslp = cv.obj(280, 1315, "list prepend smear_pitch_scale_to")
    sslt = cv.obj(280, 1340, "list trim")
    sslg = cv.obj(280, 1365, "spigot")
    rsw = cv.obj(450, 1340, "r lg_seq_son")
    cv.connect(scale_hold, 0, ssl, 0)
    cv.connect(ssl, 0, sslp, 0)
    cv.connect(sslp, 0, sslt, 0)
    cv.connect(sslt, 0, sslg, 0)
    cv.connect(rsw, 0, sslg, 1)
    eng(sslg)
    # (0) arm the root sequence: broadcast axis index + form (0 plain/1 rev/2 alt) + a bang
    armt = cv.obj(600, 1260, "t b b b")
    cv.connect(axt, 0, armt, 0)
    aix = cv.obj(600, 1290, "expr int($f1+0.5)")
    saix = cv.obj(600, 1315, "s lg_seq_axidx")
    cv.connect(armt, 2, axif, 0)                 # read axis index (fires first)
    cv.connect(axif, 0, aix, 0)
    cv.connect(aix, 0, saix, 0)
    # form = alt>0 ? 2 : (rev!=0 ? 1 : 0)
    fpk = cv.obj(760, 1290, "pack f f")          # inlet0 = alt (hot), inlet1 = rev (cold)
    ftb = cv.obj(700, 1290, "t b b")
    cv.connect(armt, 1, ftb, 0)
    cv.connect(ftb, 1, revf, 0)                  # store rev into pack inlet1 first
    cv.connect(revf, 0, fpk, 1)
    cv.connect(ftb, 0, altf, 0)                  # then alt -> pack (hot)
    cv.connect(altf, 0, fpk, 0)
    fex = cv.obj(760, 1315, "expr ($f1>0)*2+($f1<=0)*($f2!=0)")
    sform = cv.obj(760, 1340, "s lg_seq_form")
    cv.connect(fpk, 0, fex, 0)
    cv.connect(fex, 0, sform, 0)
    sarm = cv.obj(600, 1345, "s lg_seq_arm")
    cv.connect(armt, 0, sarm, 0)                 # trigger the arm (fires last)
    # per-axis arm group: gate by axis==ai, pick grain/smear plain/rev/alt by form + DEST
    seq_read = cv.msg(700, 1270, "pattern scale_root [ 0 4 8 ]")   # seq_readout (frozen LED)
    ax_y = 1400
    for ai, a in enumerate(L.SEQ_AXES):
        shifts = L.SEQ_AXIS_SHIFTS[a]
        base = " ".join(str(s) for s in shifts)
        revs = " ".join(str(s) for s in reversed(shifts))
        rarm = cv.obj(40, ax_y + ai * 150, "r lg_seq_arm")
        raxx = cv.obj(200, ax_y + ai * 150, "r lg_seq_axidx")
        aeq = cv.obj(200, ax_y + ai * 150 + 25, f"expr $f1=={ai}")
        agate = cv.obj(40, ax_y + ai * 150 + 25, "spigot")
        cv.connect(rarm, 0, agate, 0)
        cv.connect(raxx, 0, aeq, 0)
        cv.connect(aeq, 0, agate, 1)
        rfrm = cv.obj(320, ax_y + ai * 150, "r lg_seq_form")
        frmf = cv.obj(320, ax_y + ai * 150 + 25, "f 0")
        cv.connect(rfrm, 0, frmf, 1)
        fsel = cv.obj(40, ax_y + ai * 150 + 50, "sel 0 1 2")
        cv.connect(agate, 0, frmf, 0)            # gated arm bang -> read form
        cv.connect(frmf, 0, fsel, 0)
        forms = {0: ("[", base, "]"), 1: ("[", revs, "]"), 2: ("<", base, ">")}
        for fi in range(3):
            ob, body, cb = forms[fi]
            gm = cv.msg(40 + fi * 300, ax_y + ai * 150 + 80, f"pattern scale_root {ob} {body} {cb}")
            gg = cv.obj(40 + fi * 300, ax_y + ai * 150 + 105, "spigot")
            rgo = cv.obj(160 + fi * 300, ax_y + ai * 150 + 105, "r lg_seq_gon")
            cv.connect(fsel, fi, gm, 0)
            cv.connect(gm, 0, gg, 0)
            cv.connect(rgo, 0, gg, 1)
            eng(gg)
            sm = cv.msg(200 + fi * 300, ax_y + ai * 150 + 80, f"pattern smear_scale_root {ob} {body} {cb}")
            sg = cv.obj(200 + fi * 300, ax_y + ai * 150 + 105, "spigot")
            rso = cv.obj(320 + fi * 300, ax_y + ai * 150 + 105, "r lg_seq_son")
            cv.connect(fsel, fi, sm, 0)
            cv.connect(sm, 0, sg, 0)
            cv.connect(rso, 0, sg, 1)
            eng(sg)
            if fi == 0:   # update the SEQ readout (a msg box) to the plain armed string
                rdset = cv.msg(40 + fi * 300, ax_y + ai * 150 + 130,
                               f"set pattern scale_root [ {base} ]")
                cv.connect(fsel, fi, rdset, 0)
                cv.connect(rdset, 0, seq_read, 0)
    # SEQ readout (seq_readout) — the armed progression string (plugdata-visible + witness)
    cv.comment(700, 1250, "SEQ readout (seq_readout) — armed root/slot progression")
    seqrd_prn = cv.obj(900, 1345, "print SEQ_ARM")
    rarmp = cv.obj(900, 1315, "r lg_seq_arm")
    cv.connect(rarmp, 0, seqrd_prn, 0)

    # ================= TIME CIRCLE — euclid composer (live) =================
    kf_y = 1560
    cv.comment(40, kf_y - 20, "TIME CIRCLE — euclid <v>(k,n) composer (live on K/N/ROT/TARGET/SLOT)")
    cv.hradio(40, kf_y, 4, snd("seq_time_target"), rcv("seq_time_target"), "TARGET_EVNT.MOD.PTCH.SMR", 0)
    cv.hsl(180, kf_y, 90, 0, 16, snd("seq_k"), rcv("seq_k"), "K_pulses", 3)
    cv.hsl(290, kf_y, 90, 1, 16, snd("seq_n"), rcv("seq_n"), "N_steps", 8)
    cv.hsl(400, kf_y, 90, 0, 15, snd("seq_rot"), rcv("seq_rot"), "ROT_(display)", 0)
    cv.hsl(510, kf_y, 90, 1, 8, snd("seq_time_slot"), rcv("seq_time_slot"), "SLOT_1-8", 1)
    # Euclid <v>(k,n) via STATIC preset tokens (the DIST_PRESETS idiom): vanilla Pd cannot
    # build a comma-bearing atom dynamically, so K x N select a static `<v>(k\,n)` message box
    # (the engine's Bjorklund suffix). The token is then routed to the TARGET's pattern.
    # ROT is display-only (the compact suffix carries no rotation; an engine `(k,n,rot)` token
    # would be a B-item) -- ROT still updates the ring visual + keeps its lgR_ hook.
    rk = cv.obj(40, kf_y + 40, f"r {snd('seq_k')}")
    kf = cv.obj(40, kf_y + 65, "f 3")
    rn = cv.obj(160, kf_y + 40, f"r {snd('seq_n')}")
    nf = cv.obj(160, kf_y + 65, "f 8")
    # store K/N (fires first) THEN recompute -- avoids reading a stale value
    tk = cv.obj(40, kf_y + 88, "t b f")
    tn = cv.obj(160, kf_y + 88, "t b f")
    cv.connect(rk, 0, tk, 0)
    cv.connect(rn, 0, tn, 0)
    cv.connect(tk, 1, kf, 1)
    cv.connect(tn, 1, nf, 1)
    # any K/N change -> recompute code = round(k)*100 + round(n) -> select the preset token
    eucreb = cv.obj(40, kf_y + 112, "t b b")
    cv.connect(tk, 0, eucreb, 0)
    cv.connect(tn, 0, eucreb, 0)
    krx = cv.obj(40, kf_y + 130, "expr int($f1+0.5)*100")
    nrx = cv.obj(160, kf_y + 130, "expr int($f1+0.5)")
    cv.connect(eucreb, 1, nf, 0)                  # read n FIRST (cold pack inlet)
    cv.connect(eucreb, 0, kf, 0)                  # then read k (hot pack inlet -> fires the pack)
    cv.connect(kf, 0, krx, 0)
    cv.connect(nf, 0, nrx, 0)
    cpk = cv.obj(40, kf_y + 160, "pack f f")      # inlet0 = k*100 (hot), inlet1 = n (cold)
    cv.connect(nrx, 0, cpk, 1)
    cv.connect(krx, 0, cpk, 0)
    cadd = cv.obj(40, kf_y + 185, "expr $f1+$f2")
    codes = [k * 100 + n for (k, n) in L.SEQ_EUCLID_PRESETS]
    csel = cv.obj(40, kf_y + 210, "sel " + " ".join(str(c) for c in codes))
    cv.connect(cpk, 0, cadd, 0)
    cv.connect(cadd, 0, csel, 0)
    # the token holder + a default for the no-match outlet. etok_in STORES the token then
    # fires lg_seq_euroute (routing) -- NO feedback into the target read (avoids recursion).
    etok_hold = cv.obj(40, kf_y + 320, "list append")   # HOLD the current token atom
    etok_in = cv.obj(40, kf_y + 292, "t b a")           # a: store token (1st); b: route
    sroute = cv.obj(220, kf_y + 320, "s lg_seq_euroute")
    cv.connect(etok_in, 1, etok_hold, 1)                 # store the token as suffix
    cv.connect(etok_in, 0, sroute, 0)                    # then trigger routing
    dk, dn = L.SEQ_EUCLID_DEFAULT
    for pi, (k, n) in enumerate(L.SEQ_EUCLID_PRESETS):
        tok = cv.msg(40 + (pi % 6) * 130, kf_y + 245 + (pi // 6) * 34, f"1({k}\\,{n})")
        cv.connect(csel, pi, tok, 0)
        cv.connect(tok, 0, etok_in, 0)
    dflt = cv.msg(820, kf_y + 245, f"1({dk}\\,{dn})")
    cv.connect(csel, len(codes), dflt, 0)               # no-match -> default token
    cv.connect(dflt, 0, etok_in, 0)
    # ROUTE: recompute the target spigot gates from seq_time_target, THEN emit the token.
    rtt = cv.obj(300, kf_y + 360, f"r {snd('seq_time_target')}")
    ttf = cv.obj(300, kf_y + 385, "f 0")
    cv.connect(rtt, 0, ttf, 1)                           # store target (cold)
    rroute = cv.obj(40, kf_y + 355, "r lg_seq_euroute")
    rtrig2 = cv.obj(40, kf_y + 380, "t b b")             # outlet1 gates (1st), outlet0 emit
    cv.connect(rroute, 0, rtrig2, 0)
    ttx = cv.obj(300, kf_y + 410, "expr int($f1+0.5)")
    cv.connect(rtrig2, 1, ttf, 0)                        # bang target -> value
    cv.connect(ttf, 0, ttx, 0)
    tgate = {}
    for ti in range(4):
        eqi = cv.obj(300 + ti * 60, kf_y + 435, f"expr $f1=={ti}")
        cv.connect(ttx, 0, eqi, 0)
        tgate[ti] = eqi
    cv.connect(rtrig2, 0, etok_hold, 0)                  # then emit the stored token
    # re-arm on TARGET/ROT/SLOT change (re-route, no recursion)
    for i, cid in enumerate(("seq_time_target", "seq_rot", "seq_time_slot")):
        rc = cv.obj(560 + i * 70, kf_y + 385, f"r {snd(cid)}")
        cv.connect(rc, 0, sroute, 0)
    etfilt = cv.obj(40, kf_y + 455, "route bang")       # drop an empty (bang) token
    cv.connect(etok_hold, 0, etfilt, 0)
    for ti, tgt in enumerate(L.SEQ_TIME_TARGETS):
        g = cv.obj(40 + ti * 160, kf_y + 470, "spigot")
        cv.connect(tgate[ti], 0, g, 1)                  # gate = (target==ti)
        cv.connect(etfilt, 1, g, 0)                     # token data (bang filtered out)
        lpp = cv.obj(40 + ti * 160, kf_y + 495, f"list prepend {tgt}")
        lpt2 = cv.obj(40 + ti * 160, kf_y + 520, "list prepend pattern")
        ltt = cv.obj(40 + ti * 160, kf_y + 545, "list trim")
        cv.connect(g, 0, lpp, 0)
        cv.connect(lpp, 0, lpt2, 0)
        cv.connect(lpt2, 0, ltt, 0)
        eng(ltt)
    # euclid readout witness
    epr = cv.obj(760, kf_y + 470, "print SEQ_EUCLID")
    cv.connect(etok_hold, 0, epr, 0)

    # ================= PATTERN GRID (8 x 16) =================
    gg_y = 2000
    cv.comment(40, gg_y - 20, "PATTERN GRID — 8 slots x 16 steps; pin = step at VALUE (DEPTH-at-pin). "
                              "Row target = XPNDR PAGE x PARAM field.")
    cv.hsl(40, gg_y, 90, 0, 1, snd("seq_grid_value"), rcv("seq_grid_value"), "VALUE", 0.5)
    cv.hradio(160, gg_y, 8, snd("seq_grid_page"), rcv("seq_grid_page"), "PAGE", 3)
    cv.hradio(340, gg_y, 8, snd("seq_grid_param"), rcv("seq_grid_param"), "PARAM", 0)
    # VALUE store, fanned to all step multipliers
    rgv = cv.obj(40, gg_y + 30, f"r {snd('seq_grid_value')}")
    gvf = cv.obj(40, gg_y + 55, "f 0.5")
    cv.connect(rgv, 0, gvf, 0)          # hot: a VALUE change outputs -> broadcasts to the muls
    sgv = cv.obj(40, gg_y + 80, "s lg_seq_gridval")
    cv.connect(gvf, 0, sgv, 0)
    # PAGE x PARAM -> field symbol (XPNDR addressing), fanned to all rows' prepend
    rgpg = cv.obj(200, gg_y + 30, f"r {snd('seq_grid_page')}")
    rgpr = cv.obj(340, gg_y + 30, f"r {snd('seq_grid_param')}")
    tgpr = cv.obj(340, gg_y + 55, "t b f")
    gpk = cv.obj(200, gg_y + 80, "pack f f")
    gex = cv.obj(200, gg_y + 105, "expr $f1*8+$f2")
    gsel = cv.obj(200, gg_y + 130, "sel " + " ".join(str(i) for i in range(64)))
    cv.connect(rgpg, 0, gpk, 0)
    cv.connect(rgpr, 0, tgpr, 0)
    cv.connect(tgpr, 1, gpk, 1)
    cv.connect(tgpr, 0, gpk, 0)
    cv.connect(gpk, 0, gex, 0)
    cv.connect(gex, 0, gsel, 0)
    sfield = cv.obj(200, gg_y + 260, "s lg_seq_gridfield")
    idx = 0
    for page in L.XPNDR_PAGES:
        for (vf, rf) in L.XPNDR_FIELDS[page]:
            m = cv.msg(200 + (idx % 8) * 60, gg_y + 155 + (idx // 8) * 24,
                       f"symbol {vf if vf else 'none'}")
            cv.connect(gsel, idx, m, 0)
            cv.connect(m, 0, sfield, 0)
            idx += 1
    # 8 rows x 16 pins; each row rebuilds a 16-float pattern on any pin/VALUE change
    for r in range(8):
        base_y = gg_y + 300 + r * 90
        pinf = {}
        packargs = "f " * 16
        rowpack = cv.obj(40, base_y + 40, f"pack {packargs.strip()}")
        # rebuild trigger: 16-outlet bang, fires inlet0 last (right-to-left)
        rtrig = cv.obj(40, base_y + 15, "t" + " b" * 16)
        for cc in range(16):
            tgl = cv.tgl(40 + cc * 22, base_y - 12, snd(f"seq_grid_{r}_{cc}"),
                         rcv(f"seq_grid_{r}_{cc}"), f"g{r}_{cc}", 0, size=15)
            rp = cv.obj(400 + cc * 4, base_y + 65, f"r {snd(f'seq_grid_{r}_{cc}')}")
            tfp = cv.obj(400 + cc * 4, base_y + 40, "t b f")   # store state (1st) then rebuild
            pinf[cc] = cv.obj(40 + cc * 30, base_y + 90, "f 0")   # pin state store
            mul = cv.obj(40 + cc * 30, base_y + 115, "*")
            rgv2 = cv.obj(200 + cc * 4, base_y + 90, "r lg_seq_gridval")
            cv.connect(rp, 0, tfp, 0)
            cv.connect(tfp, 1, pinf[cc], 1)                # store state (fires first)
            cv.connect(tfp, 0, rtrig, 0)                   # then rebuild
            cv.connect(rtrig, cc, pinf[cc], 0)             # outlet cc -> pinf[cc]; outlet 0 fires last (pack hot inlet)
            cv.connect(pinf[cc], 0, mul, 0)
            cv.connect(rgv2, 0, mul, 1)
            cv.connect(mul, 0, rowpack, cc)
        # any VALUE change also rebuilds this row
        rgvr = cv.obj(360, base_y + 15, "r lg_seq_gridval")
        cv.connect(rgvr, 0, rtrig, 0)
        # rowpack -> prepend field -> pattern -> engine
        lppf = cv.obj(40, base_y + 65, "list prepend none")
        rfld = cv.obj(220, base_y + 40, "r lg_seq_gridfield")
        cv.connect(rfld, 0, lppf, 1)                       # set the field to prepend
        cv.connect(rowpack, 0, lppf, 0)
        lppat = cv.obj(40, base_y + 90, "list prepend pattern")
        ltri = cv.obj(40, base_y + 115, "list trim")
        cv.connect(lppf, 0, lppat, 0)
        cv.connect(lppat, 0, ltri, 0)
        eng(ltri)
        if r == 0:
            gpr = cv.obj(300, base_y + 115, "print SEQ_GRID")
            cv.connect(ltri, 0, gpr, 0)

    # loadbang: init the DEST gates (lg_seq_gon/son only update on a poll) + default the
    # grid field address so the decodes resolve on load
    lbb = cv.obj(1100, 40, "loadbang")
    dlb = cv.obj(1100, 65, "del 20")
    mib = cv.msg(1100, 90, f"\\; lg_seq_destpoll bang \\; {rcv('seq_grid_page')} 3 \\; {rcv('seq_grid_param')} 0")
    cv.connect(lbb, 0, dlb, 0)
    cv.connect(dlb, 0, mib, 0)
    return cv


# ======================================================================
# Main patch
# ======================================================================
def build_main():
    controls = L.CONTROLS
    cv = Canvas(0, 23, 1920, 1120, font=10)
    cv.cnv(20, 8, 420, 30, "ligase~_SYNTHI_CONTROL_SURFACE", 18)
    cv.comment(460, 10, "generated by docs/ui/emit_pd.py from panel_layout.py - do not hand-edit. "
                        "every control: receive lgR_<id> (script/MIDI hook).")

    place_controls(cv, controls)

    # joystick sliders (IN 23/24)
    jx = cv.hsl(812, 986, 216, 0, 1, snd("joy_x"), rcv("joy_x"), "JOY_X_IN23", 0.55)
    jy = cv.vsl(1040, 760, 216, 0, 1, snd("joy_y"), rcv("joy_y"), "JOY_Y_IN24", 0.45)

    # matrix pin grid (positions mirror the silkscreen)
    gx0, gy0 = 864, 184
    CELL = 23
    cv.cnv(gx0 - 6, gy0 - 40, 22 * CELL + 12, 30, "PRESTO-PATCH_MATRIX_(rows=sources_cols=dests)", 12)
    for i, (sdisp, seng) in enumerate(L.MATRIX_SRCS):
        cv.comment(gx0 - 62, gy0 + i * CELL, sdisp)
        for j in range(len(L.MATRIX_DSTS)):
            cv.obj(gx0 + j * CELL, gy0 + i * CELL,
                   f"tgl 15 0 {snd(f'mx_{i}_{j}')} {rcv(f'mx_{i}_{j}')} empty 17 7 0 10 {BG_MX} {FG_MX} {LB} 0 1")
    cv.comment(gx0, gy0 + 16 * CELL + 8,
               "cols: " + " / ".join(d[0] for d in L.MATRIX_DSTS))
    # depth policy controls
    dep = L.CONTROL_BY_ID["mx_depth"]
    cv.nbx(gx0 + 520, gy0 - 40, 5, dep["lo"], dep["hi"], snd("mx_depth"), rcv("mx_depth"), "PIN_DEPTH", dep["default"])
    cv.tgl(gx0 + 610, gy0 - 40, snd("mx_pol"), rcv("mx_pol"), "POL_-_(green_pin)", False)

    # VU + poll displays (MONITOR)
    cv.nbx(1560, 800, 6, -1e+37, 1e+37, "empty", rcv("vu_l"), "VU_L_dB", 0)
    cv.nbx(1640, 800, 6, -1e+37, 1e+37, "empty", rcv("vu_r"), "VU_R_dB", 0)
    for i, nm in enumerate(("disp_grainsize", "disp_speed", "disp_amplitude", "disp_pan", "disp_bpm")):
        cv.nbx(1560 + (i % 3) * 90, 850 + (i // 3) * 40, 6, -1e+37, 1e+37, "empty", rcv(nm),
               nm.replace("disp_", ""), 0)
    cv.comment(1560, 930, "10 Hz poll readouts (get_params -> outlet 9)")
    cv.comment(1150, 740, "SCOPE: outs 10/11 - connect [oscilloscope~] inside [pd engine] in plugdata")

    # subpatches
    eng = cv.sub(build_engine(controls), 60, 1040, "engine")
    wir = cv.sub(build_wiring(controls), 160, 1040, "wiring")
    shp = cv.sub(build_shape(controls), 260, 1040, "shape_logic")
    mxl = cv.sub(build_matrix_logic(), 380, 1040, "matrix_logic")
    dsp = cv.sub(build_displays(), 500, 1040, "displays")
    xp = cv.sub(build_xpndr_body(Canvas(300, 60, 1700, 900, subname="xpndr"), controls), 620, 1040, "xpndr")
    cv.comment(700, 1040, "<- XPNDR window (Snapshot Expander; also emitted standalone as ligase_xpndr.pd)")
    sq = cv.sub(build_seq_body(Canvas(60, 60, 1400, 2600, subname="seq"), controls), 760, 1040, "seq")
    cv.comment(840, 1040, "<- SEQ / SCALE window (tone/time circles + pattern grid; also standalone ligase_seq.pd)")

    # ---- loadbang init sequence (GATE A.5/A.6 contract) ----
    lb = cv.obj(20, 60, "loadbang")
    tt = cv.obj(20, 85, "t b b b b b")
    # fired first (rightmost): DSP on
    m_dsp = cv.msg(420, 110, "\\; pd dsp 1")
    cv.connect(lb, 0, tt, 0)
    cv.connect(tt, 4, m_dsp, 0)
    # engine contract: headless 0 + scope default + CV morph cursor
    m_ctr = cv.msg(320, 140, esc_msg("headless 0, scope_tap lorenz 1, morph_cursor 1"))
    s_e = cv.obj(320, 170, "s lg_engine")
    cv.connect(tt, 3, m_ctr, 0)
    cv.connect(m_ctr, 0, s_e, 0)
    # defaults broadcast 1: routing state first (family/inst gates, matrix depth)
    pre = [("shape_family", L.CONTROL_BY_ID["shape_family"]["default"]),
           ("shape_inst", L.CONTROL_BY_ID["shape_inst"]["default"]),
           ("mx_depth", L.CONTROL_BY_ID["mx_depth"]["default"]),
           ("mx_pol", L.CONTROL_BY_ID["mx_pol"]["default"])]
    m_pre = cv.msg(220, 200, " \\; ".join(f"{rcv(cid)} {fnum(v)}" for cid, v in pre))
    cv.connect(tt, 2, m_pre, 0)
    # defaults broadcast 2: every init_send control (drives ALL signal inlets + modes)
    chunks = []
    for c in L.CONTROLS:
        if not c["init_send"] or not c["bind"]:
            continue
        if c["bind"][0] == "special" and c["bind"][1] not in ():
            continue
        chunks.append(f"{rcv(c['id'])} {fnum(c['default'])}")
    # split into two message boxes to keep them readable
    half = (len(chunks) + 1) // 2
    m_d1 = cv.msg(120, 240, " \\; ".join(chunks[:half]))
    m_d2 = cv.msg(120, 300, " \\; ".join(chunks[half:]))
    cv.connect(tt, 1, m_d1, 0)
    cv.connect(tt, 1, m_d2, 0)
    # last: start the 10 Hz display poll
    dl = cv.obj(20, 130, "del 800")
    m_go = cv.msg(20, 160, "1")
    s_go = cv.obj(20, 190, "s lg_poll_on")
    cv.connect(tt, 0, dl, 0)
    cv.connect(dl, 0, m_go, 0)
    cv.connect(m_go, 0, s_go, 0)

    return cv


def build_xpndr_standalone():
    cv = Canvas(300, 60, 1700, 900, font=10)
    return build_xpndr_body(cv, L.CONTROLS, standalone=True)


def build_seq_standalone():
    cv = Canvas(60, 60, 1400, 2600, font=10)
    return build_seq_body(cv, L.CONTROLS, standalone=True)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    main_cv = build_main()
    p1 = os.path.join(OUT_DIR, "ligase_panel.pd")
    open(p1, "w").write(main_cv.render())
    xp = build_xpndr_standalone()
    p2 = os.path.join(OUT_DIR, "ligase_xpndr.pd")
    open(p2, "w").write(xp.render())
    sq = build_seq_standalone()
    p3 = os.path.join(OUT_DIR, "ligase_seq.pd")
    open(p3, "w").write(sq.render())
    n1 = sum(1 for l in open(p1) if l.startswith(("#X obj", "#X msg")))
    n2 = sum(1 for l in open(p2) if l.startswith(("#X obj", "#X msg")))
    n3 = sum(1 for l in open(p3) if l.startswith(("#X obj", "#X msg")))
    print(f"wrote {p1} ({n1} objects), {p2} ({n2} objects) and {p3} ({n3} objects)")


if __name__ == "__main__":
    main()
