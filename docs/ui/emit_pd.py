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


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    main_cv = build_main()
    p1 = os.path.join(OUT_DIR, "ligase_panel.pd")
    open(p1, "w").write(main_cv.render())
    xp = build_xpndr_standalone()
    p2 = os.path.join(OUT_DIR, "ligase_xpndr.pd")
    open(p2, "w").write(xp.render())
    n1 = sum(1 for l in open(p1) if l.startswith(("#X obj", "#X msg")))
    n2 = sum(1 for l in open(p2) if l.startswith(("#X obj", "#X msg")))
    print(f"wrote {p1} ({n1} objects) and {p2} ({n2} objects)")


if __name__ == "__main__":
    main()
