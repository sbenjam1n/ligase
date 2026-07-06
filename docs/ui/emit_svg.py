#!/usr/bin/env python3
# emit_svg.py — render the ligase~ Synthi-style control-surface mockup (SVG) from
# panel_layout.py (the layout AS DATA). Output is byte-identical to the historical
# gen_panel.py renderer; hand-tweaks belong in panel_layout.py, not here.
import math

import panel_layout as L

W, H = L.W, L.H
MAINW = L.MAINW
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

def screw(x, y):
    parts.append(f'<circle cx="{x}" cy="{y}" r="5.5" fill="url(#chromeDome)" stroke="#7d8186" stroke-width="0.8"/>')
    parts.append(f'<line x1="{x-3}" y1="{y-3}" x2="{x+3}" y2="{y+3}" stroke="#6b7075" stroke-width="1.2"/>')

# ---------- layout-data adapters (positions/labels come from panel_layout) ----------
def _svg(cid):
    return L.CONTROL_BY_ID[cid]["svg"]

def draw_knob(cid):
    s = _svg(cid)
    knob(s["x"], s["y"], s["name"], s["rng"], s.get("inlet"), s.get("cap", "white"),
         s.get("pos", 0.5), s.get("small", False), s.get("name2"))

def draw_switch(cid):
    s = _svg(cid)
    switch(s["x"], s["y"], s["w"], s["labels"], s.get("sel", 0), s.get("title"), s.get("inlet"))

def draw_button(cid):
    s = _svg(cid)
    button(s["x"], s["y"], s["label"], s.get("w", 52), s.get("lit", False))

def draw_toggle(cid):
    s = _svg(cid)
    toggle(s["x"], s["y"], s["label"], s.get("on", False))

def draw_jack(cid):
    s = _svg(cid)
    jack(s["x"], s["y"], s["label"], s.get("sub"), s.get("col", "blue"))

def draw_led(cid):
    s = _svg(cid)
    led_display(s["x"], s["y"], s["digits"], s.get("label"), s.get("w", 64), s.get("h", 32),
                s.get("ghost", "88"), s.get("fs", 26))

def draw_quant_group(grid_cid, amt_cid):
    """The shared quantization control block (playhead & delay look identical)."""
    title, x, y = _svg(grid_cid)["group"]
    text(x + 28, y - 30, title, size=8, weight="bold")
    draw_knob(grid_cid)
    draw_knob(amt_cid)

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
draw_jack("jack_inl")
draw_jack("jack_inr")
draw_jack("jack_outl")
draw_jack("jack_outr")

LX, LW = 44, 726   # left column

# ---------- A. GRANULAR ENGINE ----------
sy = 92; sh = 104
strip(LX, sy, LW, sh, "GRANULAR ENGINE", "white")
for cid in ("grainsize", "start", "speed", "density", "voices", "level"):
    draw_knob(cid)

# ---------- B. TAPE / REEL ----------
sy += sh + 12
strip(LX, sy, LW, sh, "TAPE REEL / RECORD", "green")
ky = sy + 40
draw_knob("organize")
draw_knob("sos")
draw_switch("recmode")
draw_button("record")
draw_button("play")
draw_toggle("loop")
draw_button("reel_load")
draw_button("reel_save")
text(LX+662, ky-7, "load ←[openpanel]", 6.5, MUTED, "start")
text(LX+662, ky+17, "save →[savepanel]", 6.5, MUTED, "start")

# ---------- C. PLAYHEAD + SPLICE SELECT ----------
sy += sh + 12
strip(LX, sy, 452, sh, "PLAYHEAD", "green")
ky = sy + 40
draw_switch("playhead")
draw_knob("scan")
draw_quant_group("quantize", "quant")
text(LX+280, ky+46, "quantize · quant", 6.5, MUTED)
draw_toggle("clkadv")
text(LX+382, ky+26, "clock: EXTERNAL", 7, BRONZE, weight="bold")
text(LX+382, ky+36, "bang → IN 1", 6.5, MUTED)

SPX = LX + 466
strip(SPX, sy, LW-466, sh, "SPLICE SELECT", "green")
draw_led("splice_led")
draw_knob("splice_data")
draw_button("splice_enter")
draw_button("splice_prev")
draw_button("splice_next")
text(SPX+237, ky+12, "shift ±1", 6.5, MUTED)
text(SPX+150, ky+46, "ENTER → splice_finish_nav <n>", 6.5, MUTED)

# ---------- D. DELAY ----------
sy += sh + 12
strip(LX, sy, LW, sh, "GRAIN DELAY", "blue")
ky = sy + 40
draw_switch("delay_mode")
draw_knob("dly_time")
draw_knob("dly_feed")
draw_knob("dly_tone")
draw_knob("dly_mix")
draw_quant_group("delay_quantize", "delay_quant")
text(LX+584, ky+46, "delay_quantize · delay_quant", 6.5, MUTED)
draw_button("stut_bang")
draw_knob("delay_glide")

# ---------- D. LADDER FILTER + E. SMEAR/RESONATOR ----------
sy += sh + 12
strip(LX, sy, 330, sh, "LADDER FILTER", "yellow")
ky = sy + 40
draw_knob("cutoff")
draw_knob("resonance")
draw_knob("flt_mix")

EX = LX + 344
strip(EX, sy, LW-344, sh, "SMEAR / RESONATOR BANK", "red")
draw_knob("smr_mix")
draw_knob("smr_freq")
draw_knob("smr_res")
draw_knob("smr_stages")
draw_knob("smr_fdbk")
draw_switch("smr_mode")
text(EX+341, ky+34, "bank tuned by", 6.5, MUTED)
text(EX+341, ky+43, "smear_pitch_scale", 6.5, MUTED)

# ---------- F. ENVELOPE + G. PITCH/MIDI ----------
sy += sh + 12
strip(LX, sy, 356, sh, "GRAIN ENVELOPE", "white")
ky = sy + 40
draw_switch("env_type")
draw_knob("skew")
draw_knob("saw_cycles")
draw_knob("saw_depth")

GX = LX + 370
strip(GX, sy, LW-370, sh, "PITCH / MIDI", "blue")
draw_knob("midi_note")
draw_switch("pitch_mode")
draw_knob("pitch_fine")
draw_toggle("poly")
draw_button("chord")

# ---------- H. DISTORTION (presets) + I. OUTPUT ----------
sy += sh + 12
strip(LX, sy, 330, sh, "DISTORTION — PRESET SET", "red")
ky = sy + 40
draw_toggle("dist_on")
draw_switch("dist_emph")
draw_knob("dist_preset")
text(LX+140, ky+52, "character & bands come from the preset", 6.8, MUTED)

IX = LX + 344
strip(IX, sy, LW-344, sh, "OUTPUT / SPACE", "red")
draw_knob("pan")
draw_switch("pan_mode")
draw_knob("spatial_width")
draw_switch("spatial_src")
text(IX+120, ky+38, "SOURCE", 7, MUTED, "middle", "bold")

# ---------- PRESETS row (bottom-left) ----------
sy += sh + 12
strip(LX, sy, LW, 124, "PRESETS / SNAPSHOTS", "yellow")
for i in range(32):
    draw_button(f"snap{i+1}")
draw_button("snap_store")
draw_button("snap_recall")
ax = LX+56 + 8*62 - 12
text(ax, sy+58, "32 slots → snapshot ids 1–32;", 6.8, MUTED, "start")
text(ax, sy+68, "STORE/RECALL act on the last-", 6.8, MUTED, "start")
text(ax, sy+78, "touched slot. a preset = the", 6.8, MUTED, "start")
text(ax, sy+88, "captured voice (schema v4) —", 6.8, MUTED, "start")
text(ax, sy+98, "place any slot on the surface.", 6.8, MUTED, "start")

# ---------- RIGHT: PIN MATRIX ----------
MX, MY = 806, 130
CELL = 23
NSRC, NDST = len(L.MATRIX_SRCS), len(L.MATRIX_DSTS)
srcs = [s[0] for s in L.MATRIX_SRCS]
dsts = [d[0] for d in L.MATRIX_DSTS]
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
pins = L.MATRIX_PINS
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
draw_switch("shape_family")
draw_switch("shape_inst")
draw_knob("shape_rate")
draw_knob("shape_a")
draw_knob("shape_b")
draw_knob("shape_c")
draw_knob("shape_d")
draw_button("shape_kick")
draw_button("shape_reset")
draw_switch("shape_mode")
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
strip(JX-14, JY-36, 340, 268, "MORPH METASURFACE — JOYSTICK", "green")
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
draw_button("morph_snap")
draw_button("morph_run")
draw_button("morph_stop")
draw_button("morph_pause")
draw_switch("morph_kernel")
draw_knob("morph_power")
text(bx, JY+226, "morph_cursor 1 = CV", 6.8, MUTED)

# ---------- RIGHT-BOTTOM: SCOPE (mod-source / grain-state display) ----------
MQX = 1150
strip(MQX, 724, 310, 268, "SCOPE", "green")
SDX, SDY, SDW, SDH = MQX+8, 760, 216, 216   # exact twin of the joystick pad — symmetry
parts.append(f'<rect x="{SDX}" y="{SDY}" width="{SDW}" height="{SDH}" rx="4" fill="{INSET}" stroke="#84888c" stroke-width="0.8"/>')
for gfrac in (0.25, 0.5, 0.75):
    parts.append(f'<line x1="{SDX+SDW*gfrac:.0f}" y1="{SDY}" x2="{SDX+SDW*gfrac:.0f}" y2="{SDY+SDH}" stroke="#33363b" stroke-width="0.6"/>')
    parts.append(f'<line x1="{SDX}" y1="{SDY+SDH*gfrac:.0f}" x2="{SDX+SDW}" y2="{SDY+SDH*gfrac:.0f}" stroke="#33363b" stroke-width="0.6"/>')
# a REAL Lorenz trace (sigma 10, rho 28, beta 8/3), x-vs-z projection -- the FOLW/LRNZ view
_lx, _ly, _lz = 0.1, 0.0, 20.0
_pts = []
for _i in range(9000):
    _dx = 10.0*(_ly-_lx); _dy = _lx*(28.0-_lz)-_ly; _dz = _lx*_ly-(8.0/3.0)*_lz
    _lx += _dx*0.004; _ly += _dy*0.004; _lz += _dz*0.004
    if _i % 12 == 0:
        _px = SDX + (_lx+22.0)/44.0*SDW
        _py = SDY + (1.0-(_lz-2.0)/48.0)*SDH
        _pts.append(f"{_px:.1f},{_py:.1f}")
parts.append(f'<polyline points="{" ".join(_pts)}" fill="none" stroke="#79c98b" stroke-width="0.9" stroke-opacity="0.85"/>')
SCX = SDX + SDW + 42                        # controls column, right of the display
draw_switch("scope_tap")
draw_switch("scope_view")
badge(SCX, 892, "OUT 10/11", "in")
text(SCX, 910, "scope_x~", 6.3, MUTED)
text(SCX, 919, "scope_y~", 6.3, MUTED)
text(SDX + SDW/2, 990, "FOLW = the SOURCE SHAPE selection · GRAIN = constellation: X = splice pos, Y = env × amp", 6.5, BRONZE)

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
draw_led("xp_led")
draw_knob("xp_slot")
draw_button("xp_load")
draw_button("xp_fromlive")
text(XL+248, kxy+40, "snapbuf_load · snapbuf_from_live", 6.5, MUTED)

xy += xh + 12
strip(XL+24, xy, XW-48, 118, "ADDRESS — PAGE × PARAM", "white")
draw_switch("xp_page")
draw_switch("xp_param")

xy += 118 + 12
strip(XL+24, xy, XW-48, 92, "VALUE", "white")
draw_led("xp_value_led")
draw_knob("xp_value")
text(XL+290, xy+38, "snapbuf_get", 6.5, MUTED, "start")
text(XL+290, xy+48, "snapbuf_set", 6.5, MUTED, "start")

xy += 92 + 12
strip(XL+24, xy, XW-48, 168, "MODULATION BAND (of the addressed param)", "blue")
bky = xy + 42
draw_knob("xp_min")
draw_knob("xp_max")
draw_knob("xp_slew")
draw_toggle("xp_enabled")
draw_toggle("xp_invert")
draw_switch("xp_source")
draw_switch("xp_inst")

xy += 168 + 12
strip(XL+24, xy, XW-48, 100, "COMMIT / AUDITION", "red")
cky = xy + 38
draw_button("xp_store")
draw_button("xp_assign")
draw_button("xp_audition")
draw_button("xp_compare")
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
draw_knob("master")
draw_knob("bank_mix")
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


def render():
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n' + "\n".join(p for p in parts if p) + "\n</svg>\n"


def main(out_path="/home/user/ligase/docs/ui/ligase_synthi_panel.svg"):
    import os
    svg = render()
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    open(out_path, "w").write(svg)
    print("wrote", len(svg), "bytes")


if __name__ == "__main__":
    main()
