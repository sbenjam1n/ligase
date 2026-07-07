#!/usr/bin/env python3
# gen_seq_mockup.py — CONCEPT MOCKUP for the SEQ/SCALE sidecar (tone circle + time
# circle + pattern grid). Renders docs/ui/ligase_seq_panel.svg.
#
# STATUS: plan-stage design artifact for Plans/seq_scale_sidecar.md. At build time
# this layout folds into panel_layout.py (SEQ_* records) and emit_svg/emit_pd emit
# it like every other surface; this standalone generator is then retired.
#
# Drawing idiom is copied VERBATIM from emit_svg.py (palette, knob/switch/button/
# strip/led/badge/screw) so the sidecar is provably the same visual language.
# Geometry echoes the main panel's twin-square motif: the TONE and TIME displays
# are 216x216 beds — the same size as the joystick pad and the scope display —
# top-aligned and mirrored about the panel centerline.
import math

W, H = 1040, 872
parts = []

PANEL = "url(#brushed)"
CASE = "url(#caseEdge)"
LINE = "#a4a9ae"
LEGEND = "#26282b"
MUTED = "#71767c"
HOLE = "#111316"
BRONZE = "#8a6d3b"
INSET = "url(#insetPlate)"
PHOS = "#79c98b"
AMBER = "#f08a4b"
CAP = {
    "white": "#ece8da", "green": "#4f9860", "blue": "#3b66b5",
    "yellow": "#d9a23a", "red": "#c04a38", "grey": "#787f88",
}
DEFS = """<defs>
<linearGradient id="brushed" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#dfe1e3"/><stop offset="0.35" stop-color="#cfd1d4"/>
  <stop offset="0.7" stop-color="#d6d8da"/><stop offset="1" stop-color="#c6c8cb"/>
</linearGradient>
<linearGradient id="caseEdge" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#b8bbbf"/><stop offset="0.5" stop-color="#8e9296"/>
  <stop offset="1" stop-color="#a7aaae"/>
</linearGradient>
<radialGradient id="chromeDome" cx="0.35" cy="0.3" r="0.85">
  <stop offset="0" stop-color="#fbfcfd"/><stop offset="0.45" stop-color="#d4d7da"/>
  <stop offset="0.8" stop-color="#8f9498"/><stop offset="1" stop-color="#b7babd"/>
</radialGradient>
<linearGradient id="chromeBezel" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#f2f3f4"/><stop offset="1" stop-color="#a6aaae"/>
</linearGradient>
<linearGradient id="insetPlate" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#15161a"/><stop offset="1" stop-color="#232529"/>
</linearGradient>
</defs>"""


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y, s, size=10, fill=LEGEND, anchor="middle", weight="normal", ls=None, family="Helvetica, Arial, sans-serif"):
    t = f'font-family="{family}" font-size="{size}" fill="{fill}" text-anchor="{anchor}"'
    if weight != "normal":
        t += f' font-weight="{weight}"'
    if ls:
        t += f' letter-spacing="{ls}"'
    parts.append(f'<text x="{x}" y="{y}" {t}>{esc(s)}</text>')


def badge(x, y, label, kind="in"):
    w = 8 + 5.4 * len(label)
    fill = "#2b2e32" if kind == "in" else "#e9e2cd"
    stroke = "#585d63" if kind == "in" else "#b5a878"
    col = "#eceade" if kind == "in" else "#6b5f35"
    parts.append(f'<rect x="{x-w/2:.1f}" y="{y-7}" width="{w:.1f}" height="12" rx="3" fill="{fill}" stroke="{stroke}" stroke-width="0.7"/>')
    text(x, y + 2.5, label, size=7.5, fill=col, weight="bold")


def knob(x, y, name, rng, cap="white", pos=0.5, small=False):
    r = 12 if small else 17
    for f in (0.0, 0.5, 1.0):
        a = math.radians(-225 + 270 * f)
        parts.append(f'<line x1="{x+(r+3)*math.cos(a):.1f}" y1="{y+(r+3)*math.sin(a):.1f}" x2="{x+(r+6)*math.cos(a):.1f}" y2="{y+(r+6)*math.sin(a):.1f}" stroke="{MUTED}" stroke-width="0.8"/>')
    col = CAP[cap]
    parts.append(f'<circle cx="{x}" cy="{y+0.8}" r="{r}" fill="#00000022"/>')
    parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{col}" stroke="#4d5157" stroke-width="1.1"/>')
    parts.append(f'<circle cx="{x}" cy="{y}" r="{r*0.60:.1f}" fill="url(#chromeDome)" stroke="#7e8388" stroke-width="0.6"/>')
    a = math.radians(-225 + 270 * pos)
    px, py = x + (r - 1.2) * math.cos(a), y + (r - 1.2) * math.sin(a)
    ix, iy = x + (r * 0.66) * math.cos(a), y + (r * 0.66) * math.sin(a)
    ptr = "#f4f1e8" if cap in ("green", "blue", "red", "grey") else "#26282b"
    parts.append(f'<line x1="{ix:.1f}" y1="{iy:.1f}" x2="{px:.1f}" y2="{py:.1f}" stroke="{ptr}" stroke-width="1.8" stroke-linecap="round"/>')
    ty = y + r + 13
    text(x, ty, name, size=8.5 if not small else 8, weight="bold", ls="0.4")
    text(x, ty + 9, rng, size=6.8, fill=MUTED)


def switch(x, y, w, labels, sel=0, title=None):
    n = len(labels)
    parts.append(f'<rect x="{x-w/2}" y="{y-5}" width="{w}" height="10" rx="5" fill="{INSET}" stroke="#8d9297" stroke-width="0.8"/>')
    step = w / n
    for i, lab in enumerate(labels):
        cx = x - w / 2 + step * (i + 0.5)
        if i == sel:
            parts.append(f'<circle cx="{cx:.1f}" cy="{y}" r="6.5" fill="url(#chromeDome)" stroke="#6f7479" stroke-width="0.8"/>')
        text(cx, y + 17, lab, size=6.8, fill=MUTED)
    if title:
        text(x, y - 12, title, size=8, weight="bold")


def button(x, y, label, w=52, lit=False):
    fill = "#e8c47c" if lit else "url(#chromeBezel)"
    stroke = "#a8863c" if lit else "#8d9195"
    parts.append(f'<rect x="{x-w/2}" y="{y-8.5}" width="{w}" height="17" rx="8.5" fill="{fill}" stroke="{stroke}" stroke-width="0.9"/>')
    text(x, y + 3, label, size=7.5, weight="bold", fill="#26282b")


def toggle(x, y, label, on=False):
    parts.append(f'<rect x="{x-13}" y="{y-8}" width="26" height="16" rx="8" fill="{INSET}" stroke="#8d9297" stroke-width="0.8"/>')
    cx = x + 6 if on else x - 6
    parts.append(f'<circle cx="{cx}" cy="{y}" r="6" fill="{CAP["green"] if on else "url(#chromeDome)"}" stroke="#6f7479" stroke-width="0.6"/>')
    text(x, y + 20, label, size=7, fill=MUTED)


def strip(x, y, w, title, color):
    parts.append(f'<circle cx="{x+8}" cy="{y}" r="3" fill="{CAP[color]}" stroke="#4d5157" stroke-width="0.8"/>')
    text(x + 16, y + 3.5, title, size=8.5, anchor="start", weight="bold", ls="2.2")
    tw = 24 + 7.4 * len(title)
    parts.append(f'<line x1="{x+tw:.0f}" y1="{y}" x2="{x+w}" y2="{y}" stroke="{LINE}" stroke-width="0.7"/>')


def led_line(x, y, w, s, fs=8.5):
    """LED message readout (bezel + amber Courier), centered at x."""
    parts.append(f'<rect x="{x-w/2-2}" y="{y-11}" width="{w+4}" height="22" rx="5" fill="url(#chromeBezel)" stroke="#84888c" stroke-width="0.8"/>')
    parts.append(f'<rect x="{x-w/2}" y="{y-9}" width="{w}" height="18" rx="3" fill="#0a0705"/>')
    parts.append(f'<text x="{x}" y="{y+3.5}" font-family="Courier New, monospace" font-size="{fs}" font-weight="bold" fill="{AMBER}" text-anchor="middle">{esc(s)}</text>')


def screw(x, y):
    parts.append(f'<circle cx="{x}" cy="{y}" r="5.5" fill="url(#chromeDome)" stroke="#7d8186" stroke-width="0.8"/>')
    parts.append(f'<line x1="{x-3}" y1="{y-3}" x2="{x+3}" y2="{y+3}" stroke="#6b7075" stroke-width="1.2"/>')


def pin(x, y, cap="white"):
    parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="6.5" fill="{CAP[cap]}" stroke="#0d0e10" stroke-width="1.2"/>')
    parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="2" fill="#0d0e10"/>')


def hole(x, y):
    parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.4" fill="{HOLE}" stroke="#3e4247" stroke-width="0.8"/>')


# ---------- case + header ----------
parts.append(DEFS)
parts.append(f'<rect width="{W}" height="{H}" fill="{CASE}"/>')
parts.append(f'<rect x="14" y="14" width="{W-28}" height="{H-28}" rx="14" fill="{PANEL}" stroke="#9ba0a5" stroke-width="1.2"/>')
for sx in (34, W - 34):
    for sy in (34, H - 34):
        screw(sx, sy)
text(52, 52, "ligase~", size=25, anchor="start", weight="normal", ls="5")
text(214, 52, "SEQ / SCALE EXPANDER", size=10.5, anchor="start", fill=MUTED, ls="3")
text(214, 66, "TONE CIRCLE · TIME CIRCLE · PATTERN GRID — a scale is a polygon; a rhythm is the same polygon in time", size=8, anchor="start", fill=MUTED, ls="0.5")

CTR = W / 2                      # 520 — the mirror line
BEDW = 216                       # the panel's twin-square module (joystick/scope)
BY = 130                         # bed top (shared)
LBX, RBX = 64, W - 64 - BEDW     # 64 and 760 — mirrored beds
LCX, RCX = LBX + BEDW / 2, RBX + BEDW / 2   # 172 / 868 column centers

# ---------- strips (three columns, mirrored widths) ----------
strip(48, 104, 320, "TONE CIRCLE — SCALE", "yellow")
strip(672, 104, 320, "TIME CIRCLE — PATTERN", "green")
strip(404, 104, 232, "SLOTS / COMMIT", "white")

# ---------- TONE CIRCLE bed (216x216, matrix-pin idiom) ----------
parts.append(f'<rect x="{LBX}" y="{BY}" width="{BEDW}" height="{BEDW}" rx="6" fill="{INSET}" stroke="#84888c" stroke-width="1"/>')
tcx, tcy, TR = LCX, BY + BEDW / 2, 78
parts.append(f'<circle cx="{tcx}" cy="{tcy}" r="{TR}" fill="none" stroke="#33363b" stroke-width="0.8"/>')
order = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
wt = {"C", "D", "E", "F#", "G#", "A#"}
axis3 = ["C", "E", "G#"]
pts = {}
for i, name in enumerate(order):
    a = math.radians(-90 + i * 30)
    pts[name] = (tcx + TR * math.cos(a), tcy + TR * math.sin(a))
poly = " ".join(f"{pts[n][0]:.1f},{pts[n][1]:.1f}" for n in order if n in wt)
parts.append(f'<polygon points="{poly}" fill="none" stroke="{PHOS}" stroke-width="1.4" stroke-opacity="0.9"/>')
tri = " ".join(f"{pts[n][0]:.1f},{pts[n][1]:.1f}" for n in axis3)
parts.append(f'<polygon points="{tri}" fill="none" stroke="{BRONZE}" stroke-width="1.2" stroke-dasharray="4,3" stroke-opacity="0.95"/>')
for i, name in enumerate(order):
    px, py = pts[name]
    if name in wt:
        pin(px, py, "green" if name in axis3 else "white")
    else:
        hole(px, py)
    la = math.radians(-90 + i * 30)
    text(tcx + (TR + 16) * math.cos(la), tcy + (TR + 16) * math.sin(la) + 2.5, name, 7,
         "#b9bec4" if name in wt else "#5b6066", "middle", "bold" if name in wt else "normal")
text(LCX, BY + BEDW + 14, "pin = pitch class · WHITE = scale · GREEN = axis tonic", 6.5, MUTED)
text(LCX, BY + BEDW + 24, "polygon stays in key under rotation", 6.5, BRONZE)

# ---------- TIME CIRCLE bed (216x216 twin) ----------
parts.append(f'<rect x="{RBX}" y="{BY}" width="{BEDW}" height="{BEDW}" rx="6" fill="{INSET}" stroke="#84888c" stroke-width="1"/>')
ucx, ucy = RCX, BY + BEDW / 2
parts.append(f'<circle cx="{ucx}" cy="{ucy}" r="{TR}" fill="none" stroke="#33363b" stroke-width="0.8"/>')
n_steps, k = 8, 3
onsets = set()
acc = 0
for i in range(n_steps):
    acc += k
    if acc >= n_steps:
        acc -= n_steps
        onsets.add(i)
ep = []
for i in range(n_steps):
    a = math.radians(-90 + i * 360 / n_steps)
    ep.append((ucx + TR * math.cos(a), ucy + TR * math.sin(a), i in onsets))
opoly = " ".join(f"{p[0]:.1f},{p[1]:.1f}" for p in ep if p[2])
parts.append(f'<polygon points="{opoly}" fill="none" stroke="{PHOS}" stroke-width="1.4" stroke-opacity="0.9"/>')
pa = math.radians(-90 + 135)
parts.append(f'<line x1="{ucx}" y1="{ucy}" x2="{ucx+TR*math.cos(pa):.1f}" y2="{ucy+TR*math.sin(pa):.1f}" stroke="{AMBER}" stroke-width="1.3"/>')
for i, (px, py, on) in enumerate(ep):
    if on:
        pin(px, py, "white")
    else:
        hole(px, py)
    la = math.radians(-90 + i * 360 / n_steps)
    text(ucx + (TR + 16) * math.cos(la), ucy + (TR + 16) * math.sin(la) + 2.5, str(i + 1), 7,
         "#b9bec4" if on else "#5b6066")
text(RCX, BY + BEDW + 14, "pin = onset · euclid (3,8) on the cycle clock", 6.5, MUTED)
text(RCX, BY + BEDW + 24, "playhead sweeps once per pattern_cycle", 6.5, BRONZE)

# ---------- CENTER column: slots / commit (mirror line = CTR) ----------
cy0 = 140
text(CTR, cy0, "SCALE SLOTS", 8, LEGEND, "middle", "bold", "1.5")
for i, lab in enumerate("ABCD"):
    button(CTR - 78 + i * 52, cy0 + 22, lab, 40, lit=(i == 0))
button(CTR, cy0 + 50, "AXIS → SLOTS", 96)
text(CTR, cy0 + 68, "shape rotated by the AXIS interval → A/B/C", 6.5, BRONZE)
text(CTR, cy0 + 78, "(AXIS 3 = the Giant Steps tonic cycle)", 6.5, BRONZE)
led_line(CTR, cy0 + 100, 208, "pattern scale_slot [ 0 1 2 ]")
text(CTR, cy0 + 120, "SEQ — slot progression on the cycle clock", 6.5, MUTED)
switch(CTR, cy0 + 152, 140, ["GRAIN", "SMEAR", "BOTH"], 2, "DEST")
button(CTR, cy0 + 196, "APPLY", 56, lit=True)
text(CTR, cy0 + 214, "circles edit COLD; APPLY commits (expander rule)", 6.5, MUTED)
text(CTR, cy0 + 240, "SENDS", 8, LEGEND, "middle", "bold", "1.5")
led_line(CTR, cy0 + 258, 224, "pitch_scale 0 2 4 6 8 10")
led_line(CTR, cy0 + 284, 224, "pattern event grain [ 1(3,8) ]")
text(CTR, cy0 + 304, "the knobs write the code — readouts show the truth", 6.5, BRONZE)

# ---------- mirrored control rows under the beds ----------
ry1, ry2, ry3 = 396, 452, 516
switch(LCX, ry1, 128, ["CHRO", "5THS", "W-T"], 0, "RING ORDER")
switch(RCX, ry1, 128, ["EVNT", "MOD", "PTCH", "SMR"], 0, "TARGET")
knob(LCX - 70, ry2, "ROOT", "rotate = transpose", "white", 0.0)
knob(LCX, ry2, "MODE", "home degree", "grey", 0.0, small=True)
knob(LCX + 70, ry2, "AXIS", "1·2·3·4·6", "yellow", 0.62, small=True)
knob(RCX - 70, ry2, "K", "pulses", "white", 0.4)
knob(RCX, ry2, "N", "steps", "grey", 0.5, small=True)
knob(RCX + 70, ry2, "ROT", "rotate", "grey", 0.2, small=True)
switch(LCX, ry3, 176, ["MAJ", "MIN", "PENT", "W-T", "OCTA", "AUG"], 3, "POLYGON PRESET")
switch(RCX, ry3, 176, ["1", "2", "3", "4", "5", "6", "7", "8"], 0, "SLOT")
toggle(CTR - 38, ry3 - 4, "REV", on=False)
knob(CTR + 38, ry3 - 8, "ALT", "<> depth", "grey", 0.15, small=True)
text(CTR, ry3 + 36, "SEQ modifiers — retrograde · <> alternation of the slot progression", 6.5, MUTED)

# ---------- PATTERN GRID strip (full width; matrix idiom) ----------
GY = 576
strip(48, GY, W - 96, "PATTERN GRID — 8 SLOTS × 16 STEPS (plain sequences; nesting/alternation via notation or ALT)", "blue")
CELL = 23
gx, gy = 120, GY + 22
GW, GH = 16 * CELL, 8 * CELL
parts.append(f'<rect x="{gx-4}" y="{gy-4}" width="{GW+8}" height="{GH+8}" rx="4" fill="{INSET}" stroke="#84888c" stroke-width="1"/>')
demo = {(0, 0), (0, 4), (0, 8), (0, 12), (2, 0), (2, 3), (2, 6), (2, 10), (2, 13), (5, 2), (5, 9)}
for r in range(8):
    text(gx - 12, gy + r * CELL + CELL / 2 + 2.5, str(r), 7, "#26282b", "end")
    for c in range(16):
        cx, cyy = gx + c * CELL + CELL / 2, gy + r * CELL + CELL / 2
        if (r, c) in demo:
            pin(cx, cyy, "white")
        else:
            hole(cx, cyy)
for c in range(0, 16, 4):
    parts.append(f'<line x1="{gx+c*CELL}" y1="{gy-4}" x2="{gx+c*CELL}" y2="{gy+GH+4}" stroke="#4a4f55" stroke-width="0.8" stroke-dasharray="3,3"/>')
text(gx + GW / 2, gy + GH + 18, "pin = step at the VALUE knob's level (the matrix DEPTH-at-pin rule) · row = slot · quarters marked", 6.8, MUTED)

# right of grid: slot addressing (XPNDR idiom) + value + readout
AX = 620
text(AX, gy + 6, "SLOT TARGET — XPNDR ADDRESSING", 8, LEGEND, "start", "bold", "1.2")
switch(AX + 170, gy + 34, 312, ["GRAIN", "TAPE", "DELAY", "FILTR", "SMEAR", "ENV", "PITCH", "SPACE"], 3, None)
text(AX + 170, gy + 16, "PAGE", 7.5, LEGEND, "middle", "bold")
switch(AX + 170, gy + 78, 312, ["1", "2", "3", "4", "5", "6", "7", "8"], 0, None)
text(AX + 170, gy + 62, "PARAM", 7.5, LEGEND, "middle", "bold")
knob(AX + 40, gy + 128, "VALUE", "step level", "white", 0.42)
badge(AX + 120, gy + 124, "MSG")
led_line(AX + 236, gy + 128, 200, "pattern moog_cutoff [ ... ]", 8)
text(AX + 170, gy + 160, "row target = any snapbuf-addressable field, or EVNT kinds via TARGET", 6.5, MUTED)

# footer
parts.append(f'<line x1="48" y1="{H-56}" x2="{W-48}" y2="{H-56}" stroke="{LINE}" stroke-width="0.7"/>')
text(52, H - 40, "Pd PROTOTYPE KEY: circles = 12/8 [tgl] on a ring · knobs = [knb] · readouts = generated message strings (truth in labeling) · grid = [tgl] bank", 6.8, MUTED, "start")
text(W - 52, H - 40, "ligase~ — Plans/seq_scale_sidecar.md", 6.8, MUTED, "end")

svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(parts) + "</svg>"
import os
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ligase_seq_panel.svg")
open(out, "w").write(svg)
print("wrote", out, len(svg), "bytes")
