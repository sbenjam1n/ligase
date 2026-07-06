#!/usr/bin/env python3
# Generate the ligase~ Synthi-style control-surface mockup (SVG).
import math

W, H = 1914, 1096
MAINW = 1520   # right edge of the main panel case
parts = []

# ---------- palette (minimalist-luxury chromecore Buchla: brushed aluminum, chrome
# domes on colored skirts, graphite type, bronze accents) ----------
PANEL   = "url(#brushed)"
CASE    = "url(#caseEdge)"
PANEL_FLAT = "#d3d5d8"          # solid stand-in where a flat panel color is needed
LINE    = "#a4a9ae"             # hairlines
LEGEND  = "#26282b"             # graphite type
MUTED   = "#71767c"
HOLE    = "#111316"
BRONZE  = "#8a6d3b"             # the accent voice (was amber-on-dark)
INSET   = "url(#insetPlate)"    # recessed dark beds (matrix, pads, meters)
CAP = {
    "white":  "#ece8da",
    "green":  "#4f9860",
    "blue":   "#3b66b5",
    "yellow": "#d9a23a",
    "red":    "#c04a38",
    "grey":   "#787f88",
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

def text(x, y, s, size=10, fill=LEGEND, anchor="middle", weight="normal", ls=None, rotate=None, family="Helvetica, Arial, sans-serif"):
    t = f'font-family="{family}" font-size="{size}" fill="{fill}" text-anchor="{anchor}"'
    if weight != "normal": t += f' font-weight="{weight}"'
    if ls: t += f' letter-spacing="{ls}"'
    tr = f' transform="rotate({rotate} {x} {y})"' if rotate is not None else ""
    parts.append(f'<text x="{x}" y="{y}" {t}{tr}>{esc(s)}</text>')

def badge(x, y, label, kind="in"):
    """Small inlet/message badge centered at x,y."""
    w = 8 + 5.4 * len(label)
    fill = "#2b2e32" if kind == "in" else "#e9e2cd"
    stroke = "#585d63" if kind == "in" else "#b5a878"
    col = "#eceade" if kind == "in" else "#6b5f35"
    parts.append(f'<rect x="{x-w/2:.1f}" y="{y-7}" width="{w:.1f}" height="12" rx="3" fill="{fill}" stroke="{stroke}" stroke-width="0.7"/>')
    text(x, y + 2.5, label, size=7.5, fill=col, weight="bold")

def knob(x, y, name, rng, inlet=None, cap="white", pos=0.5, small=False, name2=None):
    """Buchla-luxe: colored SKIRT disc + chrome dome cap + skirt pointer."""
    r  = 12 if small else 17
    # tick marks (hairline)
    for f in (0.0, 0.5, 1.0):
        a = math.radians(-225 + 270 * f)
        x1, y1 = x + (r+3)*math.cos(a), y + (r+3)*math.sin(a)
        x2, y2 = x + (r+6)*math.cos(a), y + (r+6)*math.sin(a)
        parts.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{MUTED}" stroke-width="0.8"/>')
    col = CAP[cap]
    parts.append(f'<circle cx="{x}" cy="{y+0.8}" r="{r}" fill="#00000022"/>')  # soft drop shadow
    parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{col}" stroke="#4d5157" stroke-width="1.1"/>')
    parts.append(f'<circle cx="{x}" cy="{y}" r="{r*0.60:.1f}" fill="url(#chromeDome)" stroke="#7e8388" stroke-width="0.6"/>')
    a = math.radians(-225 + 270 * pos)
    px, py = x + (r-1.2)*math.cos(a), y + (r-1.2)*math.sin(a)
    ix, iy = x + (r*0.66)*math.cos(a), y + (r*0.66)*math.sin(a)
    ptr = "#f4f1e8" if cap in ("green","blue","red","grey") else "#26282b"
    parts.append(f'<line x1="{ix:.1f}" y1="{iy:.1f}" x2="{px:.1f}" y2="{py:.1f}" stroke="{ptr}" stroke-width="1.8" stroke-linecap="round"/>')
    ty = y + r + 13
    text(x, ty, name, size=8.5 if not small else 8, weight="bold", ls="0.4")
    if name2:
        ty += 9
        text(x, ty, name2, size=8.5 if not small else 8, weight="bold", ls="0.4")
    text(x, ty + 9, rng, size=6.8, fill=MUTED)
    if inlet:
        kind = "in" if inlet.startswith("IN") else "msg"
        badge(x, y - r - 10, inlet, kind)

def switch(x, y, w, labels, sel=0, title=None, inlet=None):
    """Horizontal n-pos slide switch centered at x,y (w total width)."""
    n = len(labels)
    parts.append(f'<rect x="{x-w/2}" y="{y-5}" width="{w}" height="10" rx="5" fill="{INSET}" stroke="#8d9297" stroke-width="0.8"/>')
    step = w / n
    for i, lab in enumerate(labels):
        cx = x - w/2 + step*(i+0.5)
        if i == sel:
            parts.append(f'<circle cx="{cx:.1f}" cy="{y}" r="6.5" fill="url(#chromeDome)" stroke="#6f7479" stroke-width="0.8"/>')
        text(cx, y + 17, lab, size=6.8, fill=MUTED)
    if title:
        text(x, y - 12, title, size=8, weight="bold")
    if inlet:
        badge(x + w/2 + 18, y, inlet, "in" if inlet.startswith("IN") else "msg")

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

def jack(x, y, label, sub=None, col="blue"):
    parts.append(f'<circle cx="{x}" cy="{y}" r="8.5" fill="url(#chromeBezel)" stroke="#84888c" stroke-width="0.8"/>')
    parts.append(f'<circle cx="{x}" cy="{y}" r="5.5" fill="{CAP[col]}"/>')
    parts.append(f'<circle cx="{x}" cy="{y}" r="2.2" fill="{HOLE}"/>')
    text(x, y + 20, label, size=7.5, weight="bold")
    if sub: text(x, y + 29, sub, size=6.5, fill=MUTED)

def strip(x, y, w, h, title, color):
    """Minimalist: one hairline rule + a colored chip + tracked-out title. No box."""
    parts.append(f'<circle cx="{x+8}" cy="{y}" r="3" fill="{CAP[color]}" stroke="#4d5157" stroke-width="0.8"/>')
    text(x + 16, y + 3.5, title, size=8.5, anchor="start", weight="bold", ls="2.2")
    tw = 24 + 7.4 * len(title)
    parts.append(f'<line x1="{x+tw:.0f}" y1="{y}" x2="{x+w}" y2="{y}" stroke="{LINE}" stroke-width="0.7"/>')

def led_display(x, y, digits, label=None, w=64, h=32, ghost="88", fs=26):
    """Amber LED numeric display (segment-ghost style), centered at x,y."""
    parts.append(f'<rect x="{x-w/2-2}" y="{y-h/2-2}" width="{w+4}" height="{h+4}" rx="5" fill="url(#chromeBezel)" stroke="#84888c" stroke-width="0.8"/>')
    parts.append(f'<rect x="{x-w/2}" y="{y-h/2}" width="{w}" height="{h}" rx="3" fill="#0a0705"/>')
    dy = fs * 0.35
    parts.append(f'<text x="{x}" y="{y+dy:.0f}" font-family="Courier New, monospace" font-size="{fs}" font-weight="bold" fill="#31201a" text-anchor="middle" letter-spacing="3">{esc(ghost)}</text>')
    parts.append(f'<text x="{x}" y="{y+dy:.0f}" font-family="Courier New, monospace" font-size="{fs}" font-weight="bold" fill="#f08a4b" text-anchor="middle" letter-spacing="3">{esc(digits)}</text>')
    if label:
        text(x, y + h/2 + 11, label, size=7, fill=MUTED)

def quant_group(x, y, title, badge_note, badge_amt, note_pos=0.55, amt_pos=0.6):
    """The shared quantization control block (playhead & delay look identical)."""
    text(x + 28, y - 30, title, size=8, weight="bold")
    knob(x, y, "GRID", "1/1 – 1/128", badge_note, "grey", note_pos, small=True)
    knob(x + 56, y, "AMOUNT", "0 – 1", badge_amt, "grey", amt_pos, small=True)

def screw(x, y):
    parts.append(f'<circle cx="{x}" cy="{y}" r="5.5" fill="url(#chromeDome)" stroke="#7d8186" stroke-width="0.8"/>')
    parts.append(f'<line x1="{x-3}" y1="{y-3}" x2="{x+3}" y2="{y+3}" stroke="#6b7075" stroke-width="1.2"/>')

# ---------- case + panel ----------
parts.append(DEFS)
parts.append(f'<rect width="{W}" height="{H}" fill="{CASE}"/>')
parts.append(f'<rect x="14" y="14" width="{W-28}" height="{H-28}" rx="14" fill="{PANEL}" stroke="#9ba0a5" stroke-width="1.2"/>')
for sx in (34, W-34):
    for sy in (34, H-34):
        screw(sx, sy)

# ---------- header ----------
text(52, 52, "ligase~", size=25, anchor="start", weight="normal", ls="5")
text(214, 52, "GRANULAR TAPE SYNTHESIZER", size=10.5, anchor="start", fill=MUTED, ls="3")
text(214, 66, "SYNTHI-STYLE CONTROL SURFACE", size=8, anchor="start", fill=MUTED, ls="2")
jack(560, 50, "IN L", "IN 1 · bang = clock", col="blue")
jack(625, 50, "IN R", "IN 2", col="blue")
jack(692, 50, "OUT L", col="red")
jack(748, 50, "OUT R", col="red")

LX, LW = 44, 726   # left column

# ---------- A. GRANULAR ENGINE ----------
sy = 92; sh = 104
strip(LX, sy, LW, sh, "GRANULAR ENGINE", "white")
xs = [LX+70 + i*118 for i in range(6)]
ky = sy + 40
knob(xs[0], ky, "GRAIN SIZE", "1 ms – 10 s",  "IN 3",  "white", 0.35)
knob(xs[1], ky, "START",      "0 – 1",        "IN 4",  "white", 0.5)
knob(xs[2], ky, "SPEED",      "−4 – +4",      "IN 5",  "white", 0.62)
knob(xs[3], ky, "DENSITY",    "IOT 1 ms – 2 s","IN 9", "white", 0.3)
knob(xs[4], ky, "VOICES",     "1 – 200",      "IN 10", "white", 0.15)
knob(xs[5], ky, "LEVEL",      "0 – 2",        "IN 21", "white", 0.55)

# ---------- B. TAPE / REEL ----------
sy += sh + 12
strip(LX, sy, LW, sh, "TAPE REEL / RECORD", "green")
ky = sy + 40
knob(LX+70,  ky, "ORGANIZE",  "0 – 1",  "IN 6", "green", 0.4)
knob(LX+188, ky, "S.O.S.",    "0 – 1",  "IN 8", "green", 0.7)
switch(LX+316, ky-8, 96, ["INPUT", "SPLICE", "OVRDUB"], 1, "REC MODE")
button(LX+418, ky-10, "RECORD", lit=True)
button(LX+418, ky+14, "PLAY")
toggle(LX+486, ky-8, "LOOP / 1-SHOT", on=True)
button(LX+586, ky-10, "SELECT REEL", 84)
button(LX+586, ky+14, "EXPORT REEL", 84)
text(LX+662, ky-7, "load ←[openpanel]", 6.5, MUTED, "start")
text(LX+662, ky+17, "save →[savepanel]", 6.5, MUTED, "start")

# ---------- C. PLAYHEAD + SPLICE SELECT ----------
sy += sh + 12
strip(LX, sy, 452, sh, "PLAYHEAD", "green")
ky = sy + 40
switch(LX+82, ky-8, 118, ["STATIC", "SCAN", "CLOCK"], 1, "MODE · playhead 1/2/3")
knob(LX+192, ky, "SCAN", "0 – 8", "IN 7", "green", 0.45)
quant_group(LX+252, ky, "QUANTIZE", "MSG", "MSG")
text(LX+280, ky+46, "quantize · quant", 6.5, MUTED)
toggle(LX+382, ky-10, "CLK-ADV QUANT", on=False)
text(LX+382, ky+26, "clock: EXTERNAL", 7, BRONZE, weight="bold")
text(LX+382, ky+36, "bang → IN 1", 6.5, MUTED)

SPX = LX + 466
strip(SPX, sy, LW-466, sh, "SPLICE SELECT", "green")
led_display(SPX+52, ky-4, "03", "current splice")
knob(SPX+112, ky, "DATA", "0 – 63", "MSG", "grey", 0.2, small=True)
button(SPX+170, ky-10, "ENTER", 46, lit=True)
button(SPX+224, ky-10, "◀", 22)
button(SPX+250, ky-10, "▶", 22)
text(SPX+237, ky+12, "shift ±1", 6.5, MUTED)
text(SPX+150, ky+46, "ENTER → splice_finish_nav <n>", 6.5, MUTED)

# ---------- D. DELAY ----------
sy += sh + 12
strip(LX, sy, LW, sh, "GRAIN DELAY", "blue")
ky = sy + 40
switch(LX+76, ky-8, 108, ["DD-4", "BENCINA", "STUT"], 0, "MODE")
knob(LX+186, ky, "TIME · REPS",   "0–10 s · 1–16",   "IN 11", "blue", 0.4)
knob(LX+290, ky, "REGEN · DECAY", "0 – 1",           "IN 12", "blue", 0.5)
knob(LX+394, ky, "TONE · SPACE",  "0–1 · 1–5000 ms", "IN 13", "blue", 0.6)
knob(LX+490, ky, "MIX",           "0 – 1",           "IN 14", "blue", 0.35)
quant_group(LX+556, ky, "QUANTIZE", "MSG", "MSG")
text(LX+584, ky+46, "delay_quantize · delay_quant", 6.5, MUTED)
button(LX+678, ky-10, "STUT !", 42, lit=True)
knob(LX+678, ky+26, "GLIDE", "0–5000 ms", "MSG", "grey", 0.2, small=True)

# ---------- D. LADDER FILTER + E. SMEAR/RESONATOR ----------
sy += sh + 12
strip(LX, sy, 330, sh, "LADDER FILTER", "yellow")
ky = sy + 40
knob(LX+60,  ky, "CUTOFF",    "20 – 20 kHz", "IN 16", "yellow", 0.75)
knob(LX+170, ky, "RESONANCE", "0 – 4",       "IN 17", "yellow", 0.3)
knob(LX+272, ky, "MIX",       "0 – 1",       "IN 18", "yellow", 0.4)

EX = LX + 344
strip(EX, sy, LW-344, sh, "SMEAR / RESONATOR BANK", "red")
knob(EX+46, ky, "MIX", "0 – 1", "IN 15", "red", 0.35)
knob(EX+112, ky, "FREQ",   "20Hz–0.45sr", "MSG", "grey", 0.5, small=True)
knob(EX+170, ky, "RESON",  "0 – 0.999",   "MSG", "grey", 0.8, small=True)
knob(EX+228, ky, "STAGES", "0 – 48",      "MSG", "grey", 0.25, small=True)
knob(EX+286, ky, "FDBK",   "±0.99",       "MSG", "grey", 0.5, small=True)
switch(EX+341, ky-2, 64, ["SNGL", "BANK"], 0, "MODE")
text(EX+341, ky+34, "bank tuned by", 6.5, MUTED)
text(EX+341, ky+43, "smear_pitch_scale", 6.5, MUTED)

# ---------- F. ENVELOPE + G. PITCH/MIDI ----------
sy += sh + 12
strip(LX, sy, 356, sh, "GRAIN ENVELOPE", "white")
ky = sy + 40
switch(LX+96, ky-8, 140, ["PARA", "TRAP", "COS", "GAUS", "EXP"], 2, "TYPE")
knob(LX+218, ky, "SKEW", "0 – 1", "IN 20", "white", 0.5)
knob(LX+278, ky, "SAW CYC", "0 – 64", "MSG", "grey", 0.1, small=True)
knob(LX+330, ky, "SAW DEP", "0 – 1",  "MSG", "grey", 0.0, small=True)

GX = LX + 370
strip(GX, sy, LW-370, sh, "PITCH / MIDI", "blue")
knob(GX+52, ky, "MIDI NOTE", "1 – 127", "IN 19", "blue", 0.5)
switch(GX+180, ky-8, 150, ["OFF", "SEMI", "SCALE", "MIDI", "PATRN"], 3, "PITCH MODE")
knob(GX+292, ky, "FINE", "±50 ¢", "MSG", "grey", 0.5, small=True)
toggle(GX+130, ky+32, "POLY ×8", on=True)
button(GX+205, ky+34, "CHORD", 48)

# ---------- H. DISTORTION (presets) + I. OUTPUT ----------
sy += sh + 12
strip(LX, sy, 330, sh, "DISTORTION — PRESET SET", "red")
ky = sy + 40
toggle(LX+52, ky-6, "ON / OFF", on=True)
switch(LX+134, ky-8, 66, ["HP", "LP"], 1, "EMPHASIS")
knob(LX+228, ky, "PRESET", "1 – 8", "MSG", "grey", 0.42)
text(LX+140, ky+52, "character & bands come from the preset", 6.8, MUTED)

IX = LX + 344
strip(IX, sy, LW-344, sh, "OUTPUT / SPACE", "red")
knob(IX+56, ky, "PAN", "0 – 1", "IN 22", "red", 0.5)
switch(IX+178, ky-8, 116, ["MONO", "STEREO", "SPATIAL"], 2, "PAN MODE")
knob(IX+282, ky, "WIDTH", "0 – 1 · spatial", "MSG", "grey", 1.0, small=True)
switch(IX+178, ky+34, 96, ["SPHERE", "NBODY"], 0)
text(IX+120, ky+38, "SOURCE", 7, MUTED, "middle", "bold")

# ---------- PRESETS row (bottom-left) ----------
sy += sh + 12
strip(LX, sy, LW, 74, "PRESETS / SNAPSHOTS", "yellow")
for i in range(8):
    button(LX+56 + i*62, sy+30, str(i+1), 44, lit=(i == 0))
button(LX+56 + 8*62 + 14, sy+30, "STORE", 54)
button(LX+56 + 8*62 + 76, sy+30, "RECALL", 54)
text(LX+LW/2, sy+58, "a preset = a message bundle (distortion char, delay/bencina/stut voicings, scales, matrix routings) — [text]/[msg] objects in Pd", 7, MUTED)

# ---------- RIGHT: PIN MATRIX ----------
MX, MY = 806, 130
CELL = 23
NSRC, NDST = 16, 22
srcs = ["SIN 1","SAW 1","SQR 1","PERL 1","PERL 2","LRNZ 1","NBDY 1","SPHR 1",
        "RAND 1","PAT 0","PAT 1","PAT 2","PAT 3","ENV L","ENV R","ENV M"]
dsts = ["DLY TIME","DLY FEED","DLY TONE","DLY MIX","CUTOFF","RESON","FLT MIX",
        "SMR FRQ","SMR RES","SMR STG","SMR FB","SCAN","ORGANIZE","S.O.S.","IOT","SKEW",
        "SPEED","SIZE","START","AMP","PAN","FINE"]
MW = NDST * CELL
MHh = NSRC * CELL

text(MX + 58 + MW/2, 96, "PRESTO-PATCH  ·  MODULATION MATRIX", size=12, weight="bold", ls="2")
text(MX + 58 + MW/2, 110, "pin = matrix_connect <source> <dest> <depth>   ·   WHITE PIN depth +   ·   GREEN PIN depth −", size=7.5, fill=MUTED)

gx = MX + 58
# column labels (rotated)
for j, d in enumerate(dsts):
    text(gx + j*CELL + CELL/2 + 2, MY + 46, d, size=7, fill=LEGEND, anchor="start", rotate=-62)
gy = MY + 54
parts.append(f'<rect x="{gx-4}" y="{gy-4}" width="{MW+8}" height="{MHh+8}" rx="4" fill="{INSET}" stroke="#84888c" stroke-width="1"/>')
pins = {(3,0):"white", (15,4):"white", (8,7):"white", (9,13):"green", (5,15):"white", (13,3):"green", (0,11):"white", (8,17):"white", (0,21):"green"}
for i, s in enumerate(srcs):
    text(gx - 10, gy + i*CELL + CELL/2 + 2.5, s, size=7, fill=LEGEND, anchor="end")
    for j in range(NDST):
        cx, cy = gx + j*CELL + CELL/2, gy + i*CELL + CELL/2
        if (i, j) in pins:
            col = CAP["white"] if pins[(i,j)] == "white" else CAP["green"]
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="6.5" fill="{col}" stroke="#0d0e10" stroke-width="1.2"/>')
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="2" fill="#0d0e10"/>')
        else:
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="3.4" fill="{HOLE}" stroke="#3e4247" stroke-width="0.8"/>')
text(gx + MW/2, gy + MHh + 20, "16 × 22 panel subset — full 44 sources × 26 destinations (incl. modout 1-4) reachable by message", size=7, fill=MUTED)
parts.append(f'<line x1="{gx + 16*CELL}" y1="{gy-4}" x2="{gx + 16*CELL}" y2="{gy+MHh+4}" stroke="#4a4f55" stroke-width="1" stroke-dasharray="3,3"/>')
text(gx + 16*CELL + 3*CELL, gy - 66, "PER-GRAIN (v1.5)", size=7, fill=BRONZE, weight="bold")
text(gx + MW/2, gy + MHh + 31, "sources: LFO ×4 · perlin ×4 · lorenz ×4 · nbody ×4 · sphere ×4 · rand ×4 · pattern 0-7 · input env follower · per-grain cols apply at trigger, never write the knobs", size=7, fill=MUTED)

# ---------- RIGHT-MIDDLE: MOD SOURCES (shared generators = the matrix rows) ----------
MSX, MSY = 798, 598
strip(MSX, MSY, 662, 118, "SOURCE SHAPE — MULTI-ENGINE EDIT (THE MATRIX ROWS)", "yellow")
mky = MSY + 36
switch(MSX+146, mky-2, 252, ["SIN","SAW","SQR","PERL","LRNZ","NBDY","SPHR","RAND","FOLW"], 5, "FAMILY")
switch(MSX+300, mky-2, 60, ["1","2","3","4"], 0, "INST")
knob(MSX+356, mky, "RATE", "0.01-100 x", None, "yellow", 0.4, small=True)
knob(MSX+410, mky, "A", "", None, "yellow", 0.55, small=True)
knob(MSX+462, mky, "B", "", None, "yellow", 0.3, small=True)
knob(MSX+514, mky, "C", "", None, "yellow", 0.5, small=True)
knob(MSX+566, mky, "D", "", None, "yellow", 0.65, small=True)
button(MSX+622, mky-14, "KICK !", 46, lit=True)
button(MSX+622, mky+10, "RESET", 46)
switch(MSX+616, mky+38, 46, ["0","1","2"], 1)
text(MSX+644, mky+41, "MODE", 6.3, MUTED, "start")
# printed per-family legend (Synthi/Plaits idiom: same knobs, per-family meaning)
lx = MSX+26; ly = MSY+80
text(lx, ly,    "A    SIN/SAW/SQR: PHASE   PERL: FREQ   LRNZ: SIGMA   NBDY: G   SPHR: DAMP   FOLW: RELEASE ms", 6.4, MUTED, "start")
text(lx, ly+10, "B    SAW: SKEW   SQR: PULSE WIDTH   LRNZ: RHO (chaos)   NBDY: DAMP   SPHR: ELAST", 6.4, MUTED, "start")
text(lx, ly+20, "C    LRNZ: BETA   NBDY: EPSILON   SPHR: SPIN        D    NBDY: PUMP amt (hold = interval)   SPHR: KICK strength", 6.4, MUTED, "start")
text(lx, ly+30, "RATE = IOT x scale (sources breathe with grain density; GLOBAL per instance)  ·  KICK = sphere_kick impulse  ·  RESET per family", 6.4, MUTED, "start")
text(MSX+636, ly+30, "all captured (v4) · FAMILY×INST feeds the SCOPE →", 6.4, BRONZE, "end")

# ---------- RIGHT-BOTTOM: JOYSTICK (morph) ----------
JX, JY, JS = 812, 760, 216
strip(JX-14, JY-36, JS+220, 268, "MORPH METASURFACE — JOYSTICK", "green")
parts.append(f'<rect x="{JX}" y="{JY}" width="{JS}" height="{JS}" rx="6" fill="{INSET}" stroke="#84888c" stroke-width="1"/>')
for f in (0.25, 0.5, 0.75):
    parts.append(f'<line x1="{JX+JS*f}" y1="{JY}" x2="{JX+JS*f}" y2="{JY+JS}" stroke="#33363b" stroke-width="0.8"/>')
    parts.append(f'<line x1="{JX}" y1="{JY+JS*f}" x2="{JX+JS}" y2="{JY+JS*f}" stroke="#33363b" stroke-width="0.8"/>')
snaps = [(0.18,0.2,"A"),(0.82,0.24,"B"),(0.25,0.8,"C"),(0.74,0.72,"D")]
for fx, fy, lab in snaps:
    sxp, syp = JX+JS*fx, JY+JS*fy
    parts.append(f'<circle cx="{sxp}" cy="{syp}" r="7" fill="#2b2e32" stroke="{CAP["yellow"]}" stroke-width="1.5"/>')
    text(sxp, syp+3, lab, size=7.5, weight="bold", fill=CAP["yellow"])
cxp, cyp = JX+JS*0.55, JY+JS*0.45
parts.append(f'<line x1="{cxp-11}" y1="{cyp}" x2="{cxp+11}" y2="{cyp}" stroke="#f2efe6" stroke-width="1.4"/>')
parts.append(f'<line x1="{cxp}" y1="{cyp-11}" x2="{cxp}" y2="{cyp+11}" stroke="#f2efe6" stroke-width="1.4"/>')
parts.append(f'<circle cx="{cxp}" cy="{cyp}" r="5" fill="none" stroke="#f2efe6" stroke-width="1.4"/>')
badge(JX+40, JY+JS+14, "IN 23", "in"); text(JX+76, JY+JS+17, "X", 8, LEGEND, "start", "bold")
badge(JX+124, JY+JS+14, "IN 24", "in"); text(JX+160, JY+JS+17, "Y", 8, LEGEND, "start", "bold")
bx = JX + JS + 58
button(bx, JY+16, "SNAP", 58)
button(bx, JY+44, "ROUTE RUN", 74, lit=True)
button(bx, JY+72, "STOP", 58)
button(bx, JY+100, "PAUSE", 58)
switch(bx, JY+136, 72, ["IDW", "NN"], 0, "KERNEL")
knob(bx, JY+184, "POWER", "IDW sharp · MSG", None, "grey", 0.4, small=True)
text(bx, JY+226, "morph_cursor 1 = CV", 6.8, MUTED)

# ---------- RIGHT-BOTTOM: SCOPE (mod-source / grain-state display) ----------
MQX = 1246
strip(MQX, 724, 214, 268, "SCOPE", "green")
parts.append(f'<rect x="{MQX+6}" y="742" width="202" height="148" rx="4" fill="{INSET}" stroke="#84888c" stroke-width="0.8"/>')
for gfrac in (0.25, 0.5, 0.75):
    parts.append(f'<line x1="{MQX+6+202*gfrac:.0f}" y1="742" x2="{MQX+6+202*gfrac:.0f}" y2="890" stroke="#33363b" stroke-width="0.6"/>')
    parts.append(f'<line x1="{MQX+6}" y1="{742+148*gfrac:.0f}" x2="{MQX+208}" y2="{742+148*gfrac:.0f}" stroke="#33363b" stroke-width="0.6"/>')
# a REAL Lorenz trace (sigma 10, rho 28, beta 8/3), x-vs-z projection -- the FOLW/LRNZ view
_lx, _ly, _lz = 0.1, 0.0, 20.0
_pts = []
for _i in range(6000):
    _dx = 10.0*(_ly-_lx); _dy = _lx*(28.0-_lz)-_ly; _dz = _lx*_ly-(8.0/3.0)*_lz
    _lx += _dx*0.004; _ly += _dy*0.004; _lz += _dz*0.004
    if _i % 12 == 0:
        _px = MQX+6 + (_lx+22.0)/44.0*202.0
        _py = 742 + (1.0-(_lz-2.0)/48.0)*148.0
        _pts.append(f"{_px:.1f},{_py:.1f}")
parts.append(f'<polyline points="{" ".join(_pts)}" fill="none" stroke="#79c98b" stroke-width="0.9" stroke-opacity="0.85"/>')
switch(MQX+58, 912, 90, ["FOLW","GRAIN"], 0, "TAP")
switch(MQX+165, 912, 66, ["XY","SWP"], 0, "VIEW")
text(MQX+10, 946, "FOLW = the SOURCE SHAPE selection:", 6.6, MUTED, "start")
text(MQX+10, 956, "waveforms / perlin / rand \u2192 time sweep", 6.6, MUTED, "start")
text(MQX+10, 966, "lorenz / nbody / sphere \u2192 XY orbit", 6.6, MUTED, "start")
text(MQX+10, 976, "GRAIN \u2192 constellation: X = splice pos, Y = env", 6.6, MUTED, "start")
text(MQX+10, 988, "scope_x~ / scope_y~ = OUT 10/11 \u00b7 Plans/scope_taps.md (GATE A)", 6.3, BRONZE, "start")

# ---------- SNAPSHOT EXPANDER column (integrated into the main panel) ----------
XL = 1508                # expander column left
XW = 1886 - XL           # column width (to the panel's inner right edge)
XC = XL + XW/2           # column center x
parts.append(f'<line x1="{XL-12}" y1="42" x2="{XL-12}" y2="{H-56}" stroke="{LINE}" stroke-width="0.7"/>')
text(XL+24, 56, "SNAPSHOT EXPANDER", size=13, anchor="start", weight="normal", ls="4")
text(XL+24, 70, "EDIT BUFFER — cold; ASSIGN is the only realtime touchpoint", size=7.5, anchor="start", fill=MUTED, ls="0.5")

xy = 92; xh = 104
strip(XL+24, xy, XW-48, xh, "SNAPSHOT", "yellow")
kxy = xy + 42
led_display(XL+80, kxy-4, "02", "buffer holds snap")
knob(XL+150, kxy, "DATA", "slot 0 – 63", "MSG", "grey", 0.25, small=True)
button(XL+248, kxy-12, "LOAD", 64)
button(XL+248, kxy+14, "FROM LIVE", 78)
text(XL+248, kxy+40, "snapbuf_load · snapbuf_from_live", 6.5, MUTED)

xy += xh + 12
strip(XL+24, xy, XW-48, 118, "ADDRESS — PAGE × PARAM", "white")
switch(XC, xy+38, 312, ["GRAIN","TAPE","DELAY","FILTR","SMEAR","ENV","PITCH","SPACE"], 3, "PAGE")
switch(XC, xy+86, 312, ["1","2","3","4","5","6","7","8"], 1, "PARAM")

xy += 118 + 12
strip(XL+24, xy, XW-48, 92, "VALUE", "white")
led_display(XL+96, xy+42, "0.42", "stored value", w=96, h=30, ghost="8.88", fs=22)
knob(XL+196, xy+42, "VALUE", "scalar · detent = discrete", "MSG", "white", 0.42)
text(XL+290, xy+38, "snapbuf_get", 6.5, MUTED, "start")
text(XL+290, xy+48, "snapbuf_set", 6.5, MUTED, "start")

xy += 92 + 12
strip(XL+24, xy, XW-48, 168, "MODULATION BAND (of the addressed param)", "blue")
bky = xy + 42
knob(XL+78,  bky, "MIN",  "band low",  None, "blue", 0.3, small=True)
knob(XL+142, bky, "MAX",  "band high", None, "blue", 0.7, small=True)
knob(XL+206, bky, "SLEW", "smooth",    None, "blue", 0.2, small=True)
toggle(XL+262, bky-6, "ENABLED", on=True)
toggle(XL+318, bky-6, "INVERT", on=False)
switch(XL+136, xy+120, 210, ["OFF","PERL","LRNZ","NBDY","SPHR","RAND","PAT"], 1, "SOURCE")
switch(XL+296, xy+120, 64, ["1","2","3","4"], 0, "INST")

xy += 168 + 12
strip(XL+24, xy, XW-48, 100, "COMMIT / AUDITION", "red")
cky = xy + 38
button(XL+72, cky, "STORE", 62)
button(XL+150, cky, "ASSIGN", 62, lit=True)
button(XL+232, cky, "AUDITION", 66)
button(XL+308, cky, "A/B", 40)
text(XL+72, cky+22, "snapbuf_store", 6.5, MUTED)
text(XL+150, cky+22, "snapbuf_apply", 6.5, MUTED)
text(XL+232, cky+22, "hold = momentary", 6.5, MUTED)
text(XL+308, cky+22, "compare", 6.5, MUTED)
text(XC, cky+42, "edits are COLD — ASSIGN commits; AUDITION borrows the live voice and reverts exactly", 7.2, BRONZE, weight="bold")

# MONITOR moves under COMMIT (fills the column); see the relocated block below.
VX2, VY2 = XL + 38, 782
strip(VX2-14, VY2-36, XW-48, 268, "MONITOR", "white")
for ch, off in (("L", 0), ("R", 34)):
    parts.append(f'<rect x="{VX2+off}" y="{VY2}" width="20" height="170" rx="3" fill="{INSET}" stroke="#84888c" stroke-width="0.8"/>')
    segs = 12
    lit = 8 if ch == "L" else 7
    for i in range(segs):
        yy = VY2 + 166 - i*13.5
        col = "#2c2f34"
        if i < lit: col = CAP["green"] if i < 9 else (CAP["yellow"] if i < 11 else CAP["red"])
        parts.append(f'<rect x="{VX2+off+3}" y="{yy-9}" width="14" height="9" fill="{col}"/>')
    text(VX2+off+10, VY2+184, ch, 8, LEGEND, weight="bold")
text(VX2+27, VY2-8, "OUTPUT", 8, MUTED)
knob(VX2+150, VY2+40, "MASTER", "amplitude · IN 21", None, "red", 0.6)
knob(VX2+235, VY2+40, "BANK MIX", "0 – 1", "MSG", "grey", 0.0, small=True)
text(VX2+185, VY2+120, "OUTLETS", 8, MUTED, weight="bold")
text(VX2+185, VY2+134, "1/2 audio · 3 note-change", 7, MUTED)
text(VX2+185, VY2+146, "4 grain bang · 5 splice end", 7, MUTED)
text(VX2+185, VY2+158, "9 state / param reports", 7, MUTED)
text(VX2+185, VY2+186, "pattern events: message-driven", 7, MUTED)
text(VX2+185, VY2+198, "pattern event grain [ 1(3,8) ]", 7, BRONZE)

# ---------- footer ----------
fy = H - 44
parts.append(f'<line x1="40" y1="{fy-14}" x2="{W-40}" y2="{fy-14}" stroke="{LINE}" stroke-width="0.8"/>')
text(46, fy, "Pd PROTOTYPE KEY:  knob = [knb]/[hsl] → signal inlet  ·  pin matrix = [tgl] grid → matrix_connect  ·  joystick = [grid]/2×[hsl] → IN 23/24  ·  switch = [radio]  ·  button = [bng]  ·  splice display = [nbx]+[knb]+ENTER[bng]  ·  reel = [openpanel]/[savepanel] → load/save", size=8, fill=MUTED, anchor="start")
text(46, fy+14, "BADGES:  IN n = signal inlet n (CV-drivable, headless 0/1 conventions apply)  ·  MSG = message/preset-set (no dedicated inlet; automatable via the modulation matrix & param_range)", size=8, fill=MUTED, anchor="start")
text(W-46, fy+14, "ligase~ — QUEUE Seq 70 feature set", size=8, fill=MUTED, anchor="end")

svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n' + "\n".join(p for p in parts if p) + "\n</svg>\n"
import os
os.makedirs("/home/user/ligase/docs/ui", exist_ok=True)
open("/home/user/ligase/docs/ui/ligase_synthi_panel.svg", "w").write(svg)
print("wrote", len(svg), "bytes")
