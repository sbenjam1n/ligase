#!/usr/bin/env python3
# emit_web.py — the FOURTH emitter from panel_layout.py (Plans/web_build.md Arc A, Step 4).
#
# Consumed alongside emit_svg.py (silkscreen), emit_pd.py (the working instrument), and
# gen_panel.py. Where emit_pd builds the Pd patch, emit_web builds the JavaScript WIDGET layer of
# the control surface: web/ligase_controls.js. The widgets have NO behaviour of their own — every
# gesture is handed to the hand-written panel brain (web/ligase_panel_logic.js) as
# surface.set(id, value) / surface.bang(id), and the brain drives the widgets back through the
# handles this module returns (handles[id].set(value) = VISUAL update only, never a send).
# The brain talks to an engine through the bridge contract in docs/ui/panel_bridge.md — the
# same surface runs in the browser (libpd/WASM) and in the plugin web view.
#
# THE PANEL *IS* THE SVG. The web UI renders emit_svg.py's rendered silkscreen
# (ligase_synthi_panel.svg) as its backdrop, then overlays live, exactly-registered
# interactive widgets on top of — and hiding — each control's static twin: knobs turn,
# switches/toggles/buttons/pins respond, the LED counters / readouts show the brain's readbacks,
# the morph metasurface, VU meters and scope are live canvases.
#
# Usage:
#   python3 docs/ui/emit_web.py [--panel pd/ligase_panel.pd] [--out web/ligase_controls.js]
#
# Output: web/ligase_controls.js (ES module):
#   export const CONTROLS      the widget descriptors (id, kind, geometry, lo/hi/default, initSend,
#                              and the FULL `bind` from panel_layout: ["inlet",3] / ["msg","quantize"]
#                              / ["msgmap",[...]] / ["toggle","play"] / ["bang","stut"] /
#                              ["special","recmode"] / null)
#   export const TABLES        the panel_layout data tables the brain needs (JSON)
#   export const INLET_TO_ID   signal-inlet number -> control id
#   export const PANEL_W/H, CONTROL_BY_ID
#   export function buildControls(surface, container, {svg}) -> handles
#   export function sendDefaults(surface)   push every initSend default through surface.set
# The module's top level is DOM-free (node can import CONTROLS/TABLES for the brain's tests).

import argparse
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import panel_layout as PL  # noqa: E402

# Skirt-cap palette — the exact CAP hexes emit_svg.py uses, so an overlay knob is the same
# color as the one drawn under it.
CAP = {
    "white": "#ece8da", "green": "#4f9860", "blue": "#3b66b5",
    "yellow": "#d9a23a", "red": "#c04a38", "grey": "#787f88",
}

# Panel geometry mirrored from emit_svg.py (the bespoke blocks that are not CONTROLS records).
MX_GX, MX_GY, MX_CELL = 864, 184, 23          # matrix pin grid origin + cell
JX, JY, JS = 812, 760, 216                    # morph joystick bed
SCX, SCY, SCS = 1158, 760, 216                # scope bed (exact twin of the joystick pad)
VUX, VUY = 1988, 932                          # monitor VU bars
# Matrix depth policy controls are pd-only [nbx]/[tgl] with no silkscreen twin; the web draws
# them as a small knob + toggle in the clear space right of the pin grid.
POLICY_POS = {"mx_depth": (1440, 286), "mx_pol": (1440, 346)}
POLICY_LABEL = {"mx_depth": "PIN DEPTH", "mx_pol": "POL −  (green pin)"}

# Audio jacks are wired adc~/dac~ in the patch — nothing to click in a browser.
_SPECIAL_SKIP = {"adc_l", "adc_r", "dac_l", "dac_r"}


def panel_receives(panel_path):
    """Set of lgR_ receive symbols that actually exist in the built patch."""
    txt = open(panel_path, "r", encoding="latin-1").read()
    return set(re.findall(r"lgR_[A-Za-z0-9_]+", txt))


def web_kind(ctrl):
    """Map a panel-layout control to a web widget kind, or None to skip.
    Rendered kinds: knob | switch | toggle | button | pin | led | readout | virtual."""
    cid = ctrl["id"]
    kind = ctrl["kind"]
    bind = ctrl.get("bind")
    sel = bind[0] if isinstance(bind, (tuple, list)) and bind else None
    if kind == "jack":
        return None
    if sel == "special" and (bind[1] if len(bind) > 1 else "") in _SPECIAL_SKIP:
        return None
    if kind == "led":
        return "led"
    if kind == "readout":
        return "readout"
    if cid in ("joy_x", "joy_y"):
        return "virtual"             # driven by the morph joystick pad; value-only (no DOM)
    if cid == "mx_depth":
        return "knob"
    if cid == "mx_pol":
        return "toggle"
    # the tone-ring + pattern-grid toggles are drawn as round pins, not slide toggles
    if re.match(r"^seq_ring_\d+$", cid) or re.match(r"^seq_grid_\d+_\d+$", cid):
        return "pin"
    if kind == "toggle":
        return "toggle"
    if kind == "switch":
        return "switch"
    if kind == "button":
        return "button"
    return "knob"


def control_label(ctrl):
    svg = ctrl.get("svg") or {}
    for key in ("name", "label", "title"):
        if svg.get(key):
            return str(svg[key])
    return ctrl["id"].replace("_", " ").upper()


def switch_labels(ctrl):
    svg = ctrl.get("svg") or {}
    labs = svg.get("labels")
    if labs:
        return list(labs)
    lo, hi = int(ctrl.get("lo", 0)), int(ctrl.get("hi", 1))
    return [str(i) for i in range(lo, hi + 1)]


def ring_position(i):
    t = PL.SEQ_TONE
    a = math.radians(-90 + i * 30)
    return t["cx"] + t["r"] * math.cos(a), t["cy"] + t["r"] * math.sin(a)


def control_center(ctrl):
    """(cx, cy) in SVG user units — where emit_svg.py drew this control's center.
    Standard controls carry svg x/y; the tone-ring and pattern-grid pins are generated
    bespoke by emit_svg from the SEQ_TONE / SEQ_GRID tables, so mirror that math."""
    cid = ctrl["id"]
    if re.match(r"^seq_ring_\d+$", cid):
        return ring_position(int(cid.rsplit("_", 1)[1]))   # ring toggle i == pitch class i (CHRO order)
    if re.match(r"^seq_grid_\d+_\d+$", cid):
        _, _, r, cc = cid.split("_")
        g = PL.SEQ_GRID
        cell = g["cell"]
        return g["ox"] + int(cc) * cell + cell / 2.0, g["oy"] + int(r) * cell + cell / 2.0
    if cid in POLICY_POS:
        return POLICY_POS[cid]
    svg = ctrl.get("svg") or {}
    if "x" in svg and "y" in svg:
        return float(svg["x"]), float(svg["y"])
    return None


def json_bind(bind):
    if not bind:
        return None
    return [bind[0], list(bind[1]) if isinstance(bind[1], (list, tuple)) else bind[1]]


def build_descriptors(panel_path):
    recv = panel_receives(panel_path)
    out, skipped = [], []
    for c in PL.CONTROLS:
        cid = c["id"]
        wk = web_kind(c)
        if wk is None:
            skipped.append((cid, "audio jack (adc~/dac~ in the patch)"))
            continue
        bind = c.get("bind")
        # an inlet-bound control is driven over lgR_<id> -> the patch's line~ chain: it must exist
        if bind and bind[0] == "inlet" and ("lgR_" + cid) not in recv:
            skipped.append((cid, "no lgR_%s receive in panel" % cid))
            continue
        svg = c.get("svg") or {}
        d = {
            "id": cid,
            "recv": "lgR_" + cid,
            "send": "lgS_" + cid,
            "kind": wk,
            "bind": json_bind(bind),
            "label": control_label(c),
            "lo": c.get("lo", 0.0),
            "hi": c.get("hi", 1.0),
            "default": c.get("default", 0.0),
            "initSend": bool(c.get("init_send", True)),
        }
        if bind and bind[0] == "inlet":
            # software-host delivery: the inlet's message twin (None = stays a CV inlet everywhere)
            d["msg"] = PL.INLET_SELECTORS.get(int(bind[1]))
            stut = PL.INLET_STUT_SELECTORS.get(int(bind[1]))
            if stut:
                d["stutMsg"] = stut
        if c.get("note"):
            d["note"] = c["note"]
        if wk != "virtual":
            center = control_center(c)
            if center is None:
                skipped.append((cid, "no panel coordinate"))
                continue
            d["cx"], d["cy"] = round(center[0], 1), round(center[1], 1)
        if wk == "knob":
            small = bool(svg.get("small")) or cid in POLICY_POS
            d["r"] = 12 if small else 17
            d["cap"] = CAP.get(svg.get("cap", "grey" if cid in POLICY_POS else "white"), CAP["white"])
        elif wk == "switch":
            d["labels"] = switch_labels(c)
            d["w"] = float(svg.get("w", 104))
        elif wk == "button":
            d["w"] = float(svg.get("w", 52))
            d["btnLabel"] = str(svg.get("label", cid))
            d["latching"] = bool(bind and (bind[0] == "toggle" or (bind[0] == "special" and bind[1] in ("record", "xp_audition"))))
        elif wk == "toggle":
            d["tglLabel"] = str(svg.get("label", ""))
        elif wk == "pin":
            d["pinCap"] = CAP["green"] if cid.startswith("seq_ring_") else CAP["white"]
            if cid.startswith("seq_ring_"):
                d["ringIndex"] = int(cid.rsplit("_", 1)[1])
        elif wk == "led":
            d.update(w=float(svg.get("w", 64)), h=float(svg.get("h", 32)), fs=float(svg.get("fs", 26)),
                     ghost=str(svg.get("ghost", "88")), digits=str(svg.get("digits", "")))
        elif wk == "readout":
            d.update(w=float(svg.get("w", 188)), fs=float(svg.get("fs", 8.5)), text=str(svg.get("s", "")))
        if cid in POLICY_POS:
            d["overlayLabel"] = POLICY_LABEL[cid]
        out.append(d)

    # --- modulation-matrix pins: generated straight into the pd patch (not CONTROLS),
    # one iemgui toggle mx_<i>_<j> per source×dest. Positions mirror emit_svg.py's matrix
    # geometry (MX+58, MY+54, CELL). The brain's "matrix" special composes matrix_connect/
    # matrix_disconnect from MATRIX_SRCS/MATRIX_DSTS + the depth policy controls.
    for i in range(len(PL.MATRIX_SRCS)):
        for j in range(len(PL.MATRIX_DSTS)):
            cid = "mx_%d_%d" % (i, j)
            out.append({
                "id": cid, "recv": "lgR_" + cid, "send": "lgS_" + cid, "kind": "mxpin",
                "bind": ["special", "matrix"],
                "cx": MX_GX + j * MX_CELL + MX_CELL / 2.0,
                "cy": MX_GY + i * MX_CELL + MX_CELL / 2.0,
                "lo": 0, "hi": 1, "default": 0, "initSend": False,
                "pinCap": CAP["white"],
                "src": PL.MATRIX_SRCS[i][0], "dst": PL.MATRIX_DSTS[j][0],
            })

    # --- morph joystick pad: the 2D metasurface canvas driving joy_x / joy_y (IN 23/24).
    out.append({"id": "joypad", "kind": "joypad", "bind": None, "x": JX, "y": JY, "w": JS, "h": JS,
                "lo": 0, "hi": 1, "default": 0, "initSend": False})
    return out, skipped


def build_tables():
    t = PL.SEQ_TONE
    return {
        "MATRIX_SRCS": [list(p) for p in PL.MATRIX_SRCS],
        "MATRIX_DSTS": [list(p) for p in PL.MATRIX_DSTS],
        "SHAPE_FAMILIES": list(PL.SHAPE_FAMILIES),
        "SHAPE_MEANINGS": {k: {str(f): list(m) for f, m in v.items()} for k, v in PL.SHAPE_MEANINGS.items()},
        "SHAPE_RESET": {str(k): v for k, v in PL.SHAPE_RESET.items()},
        "SHAPE_MODE": {str(k): v for k, v in PL.SHAPE_MODE.items()},
        "SHAPE_KICK_FAMILY": PL.SHAPE_KICK_FAMILY,
        "SHAPE_KICK_SCALE": PL.SHAPE_KICK_SCALE,
        "XPNDR_PAGES": list(PL.XPNDR_PAGES),
        "XPNDR_FIELDS": {p: [list(e) for e in PL.XPNDR_FIELDS[p]] for p in PL.XPNDR_PAGES},
        "XPNDR_SOURCE_CODES": list(PL.XPNDR_SOURCE_CODES),
        "XPNDR_BAND_RANGES": {k: list(v) for k, v in PL.XPNDR_BAND_RANGES.items()},
        "DIST_PRESETS": {str(k): list(v) for k, v in PL.DIST_PRESETS.items()},
        "SEQ_PRESETS": list(PL.SEQ_PRESETS),
        "SEQ_PRESET_PCS": {str(k): list(v) for k, v in PL.SEQ_PRESET_PCS.items()},
        "SEQ_AXES": list(PL.SEQ_AXES),
        "SEQ_AXIS_SHIFTS": {str(k): list(v) for k, v in PL.SEQ_AXIS_SHIFTS.items()},
        "SEQ_TIME_TARGETS": list(PL.SEQ_TIME_TARGETS),
        "SEQ_EUCLID_PRESETS": [list(p) for p in PL.SEQ_EUCLID_PRESETS],
        "SEQ_EUCLID_DEFAULT": list(PL.SEQ_EUCLID_DEFAULT),
        "SEQ_TONE": {"cx": t["cx"], "cy": t["cy"], "r": t["r"], "order": list(t["order"])},
        "SEQ_TIME": {"cx": PL.SEQ_TIME["cx"], "cy": PL.SEQ_TIME["cy"], "r": PL.SEQ_TIME["r"],
                     "steps": PL.SEQ_TIME["steps"]},
        "SEQ_GRID": {k: PL.SEQ_GRID[k] for k in ("ox", "oy", "rows", "cols", "cell")},
    }


def build_inlets():
    return {str(c["bind"][1]): c["id"] for c in PL.CONTROLS if c["bind"] and c["bind"][0] == "inlet"}


# ---- the emitted module -------------------------------------------------------------------
# Placeholders (__W__ etc.) are substituted last so the CSS/JS body can contain literal % and
# { } without format-string escaping.
JS_TEMPLATE = r"""/* AUTO-GENERATED by docs/ui/emit_web.py from docs/ui/panel_layout.py — DO NOT EDIT.
 * Web widget layer for ligase~ (Plans/web_build.md Arc A, Step 4; docs/ui/panel_bridge.md).
 * The panel IS the SVG silkscreen (ligase_synthi_panel.svg); this overlays live widgets at the
 * SVG's own control coordinates. Widgets have no behaviour: every gesture is surface.set(id, v) /
 * surface.bang(id) into the panel brain (web/ligase_panel_logic.js), which renders readbacks
 * through the returned handles[id].set(v) (visual only). Regenerate with:
 *   python3 docs/ui/emit_web.py
 */
export const PANEL_W = __W__, PANEL_H = __H__;
export const CONTROLS = __CONTROLS__;
export const TABLES = __TABLES__;
export const INLET_TO_ID = __INLETS__;
export const CONTROL_BY_ID = Object.fromEntries(CONTROLS.map((c) => [c.id, c]));

const CSS = `
.lg-root { --dome: radial-gradient(circle at 38% 32%, #f6f6f4, #cdd0d3 55%, #9a9ea3 100%);
  background:#c9cbce; display:block; }
.lg-stage { position:relative; width:100%; max-width:1600px; margin:0 auto; }
.lg-stage > svg { width:100%; height:auto; display:block; }
.lg-overlay { position:absolute; top:0; left:0; transform-origin:top left; pointer-events:none; }
.lg-w { position:absolute; pointer-events:auto; box-sizing:border-box;
  transform:translate(-50%,-50%); user-select:none; -webkit-user-select:none; }
/* knob: opaque reproduction of the drawn skirt+dome+pointer, sitting on its static twin */
.lg-knob { border-radius:50%; border:1.1px solid #4d5157; cursor:ns-resize; touch-action:none; }
.lg-knob svg { display:block; width:100%; height:100%; }
.lg-read { position:absolute; left:50%; top:100%; transform:translate(-50%,2px);
  font:9px/1 ui-monospace,monospace; color:#26282b; background:#efe7cf; border:1px solid #b5a878;
  border-radius:3px; padding:1px 4px; white-space:nowrap; opacity:0; transition:opacity .12s;
  pointer-events:none; z-index:5; }
.lg-w.live .lg-read { opacity:1; }
/* overlay-only legend (controls with no silkscreen twin: the matrix depth policy) */
.lg-lbl { position:absolute; left:50%; top:100%; transform:translate(-50%,6px); white-space:nowrap;
  font:7px/1 ui-monospace,monospace; color:#71767c; pointer-events:none; }
/* horizontal slide switch: inset bar + chrome selector dot */
.lg-sw { height:10px; border-radius:5px; background:linear-gradient(#1c1e22,#2a2d31);
  border:0.8px solid #8d9297; cursor:pointer; touch-action:none; }
.lg-sw .dot { position:absolute; top:50%; width:13px; height:13px; border-radius:50%;
  background:var(--dome); border:0.8px solid #6f7479; transform:translate(-50%,-50%);
  transition:left .08s; }
/* toggle pill */
.lg-tgl { width:26px; height:16px; border-radius:8px; background:linear-gradient(#1c1e22,#2a2d31);
  border:0.8px solid #8d9297; cursor:pointer; }
.lg-tgl .knob { position:absolute; top:50%; width:12px; height:12px; border-radius:50%;
  background:var(--dome); border:0.6px solid #6f7479; transform:translate(-50%,-50%);
  transition:left .1s,background .1s; }
.lg-tgl.on .knob { background:#4f9860; }
/* momentary / latching button: lit = selected or armed (amber), held = a captured slot (green) */
.lg-btn { height:17px; border-radius:8.5px; border:0.9px solid #8d9195;
  background:linear-gradient(#eceef0,#c3c6c9); color:#26282b; font:bold 7.5px/1 ui-monospace,monospace;
  display:flex; align-items:center; justify-content:center; cursor:pointer; letter-spacing:.02em;
  transition:background .08s; }
.lg-btn.held { background:linear-gradient(#cfe4d1,#9fc7a4); border-color:#4f9860; }
.lg-btn.lit, .lg-btn.flash, .lg-btn:active { background:#e8c47c; border-color:#a8863c; }
/* tone-ring / pattern-grid / matrix pin — off-state fully covers the silkscreen twin */
.lg-pin { width:14px; height:14px; border-radius:50%; cursor:pointer;
  background:#16181c; border:1px solid #3e4247;
  display:flex; align-items:center; justify-content:center; }
.lg-pin::after { content:''; width:5px; height:5px; border-radius:50%; background:#0d0e10; }
.lg-pin.on { border:1.4px solid #0d0e10; }
.lg-pin.on::after { width:4px; height:4px; background:#0d0e10; }
.lg-pin.mx { width:13px; height:13px; }
/* amber LED counters (segment-ghost style) + the one-line readouts, over their SVG twins */
.lg-led { background:#0a0705; border-radius:3px; position:relative; overflow:hidden;
  font-family:"Courier New",Courier,monospace; font-weight:bold; letter-spacing:3px; color:#f08a4b;
  display:flex; align-items:center; justify-content:center; white-space:nowrap; }
.lg-led .ghost { position:absolute; left:0; right:0; text-align:center; color:#31201a; }
.lg-led .val { position:relative; }
.lg-ro { background:#0a0705; border-radius:3px; height:18px; overflow:hidden; white-space:nowrap;
  font-family:"Courier New",Courier,monospace; font-weight:bold; color:#f08a4b;
  display:flex; align-items:center; justify-content:center; }
/* morph joystick pad — a 2D drag surface driving joy_x/joy_y (bed art is the SVG under it) */
.lg-joypad { transform:none; cursor:crosshair; touch-action:none; }
/* VU meter segment (positioned absolutely; driven live by the 'vu' bridge event) */
.lg-vuseg { position:absolute; width:9px; height:8px; background:#2c2f34; pointer-events:none; }
/* live scope canvas (phosphor XY / sweep trace over the scope bed) */
.lg-scope { position:absolute; pointer-events:none; }
/* morph metasurface canvas — opaque, covers the mockup art, owns snapshot points + cursor + route */
.lg-meta { position:absolute; pointer-events:auto; cursor:crosshair; touch-action:none;
  border-radius:4px; }
`;

function fmt(v) { v = +v; return Math.abs(v) >= 100 ? v.toFixed(0) : (Number.isInteger(v) ? String(v) : v.toFixed(2)); }

// angle (deg) the drawn knob pointer uses: -225 + 270*pos, pos in [0,1]
function knobAngle(pos) { return -225 + 270 * Math.max(0, Math.min(1, pos)); }

function knobSVG(r, cap) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', `${-r - 3} ${-r - 3} ${2 * r + 6} ${2 * r + 6}`);
  const skirt = document.createElementNS(ns, 'circle');
  skirt.setAttribute('r', r); skirt.setAttribute('fill', cap);
  const dome = document.createElementNS(ns, 'circle');
  dome.setAttribute('r', (r * 0.6).toFixed(1)); dome.setAttribute('fill', 'url(#lg-dome)');
  dome.setAttribute('stroke', '#7e8388'); dome.setAttribute('stroke-width', '0.6');
  const ptr = document.createElementNS(ns, 'line');
  const dark = (cap === '#ece8da' || cap === '#d9a23a') ? '#26282b' : '#f4f1e8';
  ptr.setAttribute('stroke', dark); ptr.setAttribute('stroke-width', '1.8');
  ptr.setAttribute('stroke-linecap', 'round');
  svg.appendChild(skirt); svg.appendChild(dome); svg.appendChild(ptr);
  const setPos = (pos) => {
    const a = knobAngle(pos) * Math.PI / 180;
    ptr.setAttribute('x1', ((r * 0.66) * Math.cos(a)).toFixed(1));
    ptr.setAttribute('y1', ((r * 0.66) * Math.sin(a)).toFixed(1));
    ptr.setAttribute('x2', ((r - 1.2) * Math.cos(a)).toFixed(1));
    ptr.setAttribute('y2', ((r - 1.2) * Math.sin(a)).toFixed(1));
  };
  return { svg, setPos };
}

/* createMorphSurface — the Bencina metasurface as a live canvas over the joystick bed
 * (JX,JY,JS = 812,760,216, mirroring emit_svg). Snapshots are placed as labelled points (the
 * label = the PRESETS slot number); the cursor blends between them (drives joy_x/joy_y through
 * the brain = the message cursor `morph <x> <y>` in the software hosts, the CV pair on hardware);
 * routes animate. Engine messages go through surface.msg(): morph_point / morph_rate /
 * morph_route(_clear) / morph_run / morph_stop / morph_pause. SNAP itself is the brain's
 * `morph_snap` special (snapshot <slot> + morph_point <slot> <x> <y>); the canvas listens for it
 * and places the point. REMOVE is the brain's surface.morphRemove(slot) (morph_unplace +
 * snapshot_clear, lamp cleared): double-click, a double-tap (pointer-timed, so it also works
 * under pointer capture / touch / WebKit), alt-click or right-click on a point, or Delete /
 * Backspace with a point selected. */
function createMorphSurface(surface, overlay) {
  const X = __JX__, Y = __JY__, S = __JS__;
  const cv = document.createElement('canvas');
  cv.className = 'lg-meta'; cv.width = S; cv.height = S;
  cv.style.left = X + 'px'; cv.style.top = Y + 'px';
  cv.style.width = S + 'px'; cv.style.height = S + 'px';
  overlay.appendChild(cv);
  const g = cv.getContext('2d');
  const defX = CONTROL_BY_ID.joy_x ? CONTROL_BY_ID.joy_x.default : 0.5;
  const defY = CONTROL_BY_ID.joy_y ? CONTROL_BY_ID.joy_y.default : 0.5;
  const st = { pts: [], wps: [], cx: defX, cy: defY, sel: null, running: false, anim: 0, t0: 0, from: null, baseRate: 1.0 };
  const WP_RATE = 1.0;                       // default per-leg duration (seconds) for a new waypoint
  const clamp = (v) => Math.max(0, Math.min(1, v));
  const px = (v) => v * S;
  // BASE-RATE slider (bottom strip) — the global relative multiplier on every leg (engine's
  // modulatable `morph_rate`). Per-leg rates live on each waypoint (wheel over it to set).
  const R0 = 52, R1 = 150, RY = S - 8, RMIN = 0.1, RMAX = 4.0, RBAND = S - 16;
  const rateToX = (r) => R0 + (r - RMIN) / (RMAX - RMIN) * (R1 - R0);
  const xToRate = (xp) => Math.round((RMIN + clamp((xp - R0) / (R1 - R0)) * (RMAX - RMIN)) / 0.05) * 0.05;
  const wpHit = (x, y) => st.wps.find((w) => Math.hypot(w.x - x, w.y - y) < 0.055);

  function draw() {
    g.fillStyle = '#141619'; g.fillRect(0, 0, S, S);
    g.strokeStyle = '#2a2e33'; g.lineWidth = 1; g.beginPath();
    for (let i = 1; i < 4; i++) { g.moveTo(S * i / 4, 0); g.lineTo(S * i / 4, S); g.moveTo(0, S * i / 4); g.lineTo(S, S * i / 4); }
    g.stroke();
    const path = st.wps.length ? st.wps : st.pts;
    if (path.length > 1) {
      g.strokeStyle = '#8a6d3b'; g.lineWidth = 1.3; g.setLineDash([4, 3]); g.beginPath();
      path.forEach((p, i) => { const x = px(p.x), y = px(p.y); i ? g.lineTo(x, y) : g.moveTo(x, y); });
      g.stroke(); g.setLineDash([]);
    }
    st.wps.forEach((p) => {
      g.fillStyle = (p === st.sel) ? '#e8c47c' : '#8a6d3b';
      g.beginPath(); g.arc(px(p.x), px(p.y), 3.5, 0, 7); g.fill();
      g.fillStyle = '#b79a63'; g.font = '7px ui-monospace,monospace'; g.textAlign = 'left'; g.textBaseline = 'middle';
      g.fillText(p.rate.toFixed(1) + 's', px(p.x) + 5, px(p.y) - 4);
    });
    st.pts.forEach((p) => {
      const x = px(p.x), y = px(p.y);
      g.beginPath(); g.arc(x, y, 7.5, 0, 7);
      g.fillStyle = (p === st.sel) ? '#e8c47c' : '#4f9860'; g.fill();
      g.lineWidth = 1.2; g.strokeStyle = '#0d0e10'; g.stroke();
      g.fillStyle = '#0d0e10'; g.font = 'bold ' + (p.label.length > 1 ? 8 : 9) + 'px ui-monospace,monospace';
      g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText(p.label, x, y + 0.5);
    });
    const cxp = px(st.cx), cyp = px(st.cy);
    g.strokeStyle = '#e8c47c'; g.lineWidth = 1;
    g.beginPath(); g.arc(cxp, cyp, 7, 0, 7); g.stroke();
    g.beginPath(); g.moveTo(cxp - 11, cyp); g.lineTo(cxp + 11, cyp); g.moveTo(cxp, cyp - 11); g.lineTo(cxp, cyp + 11); g.stroke();
    if (!st.pts.length) {
      g.fillStyle = '#6b7075'; g.font = '8px ui-monospace,monospace'; g.textAlign = 'center'; g.textBaseline = 'alphabetic';
      g.fillText('SNAP = snapshot <PRESET slot> here · drag = morph', S / 2, S - 50);
      g.fillText('shift-click = route pt (wheel it = leg rate) · dbl/alt-click, Del = remove', S / 2, S - 38);
    }
    // BASE-RATE strip — the global multiplier (engine morph_rate); modulatable
    g.fillStyle = '#0f1113'; g.fillRect(0, RBAND, S, S - RBAND);
    g.fillStyle = '#8a6d3b'; g.font = 'bold 7px ui-monospace,monospace'; g.textAlign = 'left'; g.textBaseline = 'middle';
    g.fillText('BASE', 6, RY);
    g.strokeStyle = '#3a3d40'; g.lineWidth = 2; g.lineCap = 'round';
    g.beginPath(); g.moveTo(R0, RY); g.lineTo(R1, RY); g.stroke();
    const hx = rateToX(st.baseRate);
    g.strokeStyle = '#8a6d3b'; g.beginPath(); g.moveTo(R0, RY); g.lineTo(hx, RY); g.stroke();
    g.fillStyle = '#e8c47c'; g.beginPath(); g.arc(hx, RY, 4, 0, 7); g.fill();
    g.fillStyle = '#b9bec4'; g.textAlign = 'left';
    g.fillText('×' + st.baseRate.toFixed(2), R1 + 8, RY);
  }

  const rectXY = (e) => { const r = cv.getBoundingClientRect();
    return { x: clamp((e.clientX - r.left) / r.width), y: clamp((e.clientY - r.top) / r.height) }; };
  const hit = (x, y) => st.pts.find((p) => Math.hypot(p.x - x, p.y - y) < 0.06);
  const msg = (...a) => surface.msg(a.map((t) => (typeof t === 'number' ? +t.toFixed(6) : t)).join(' '));
  function setCursor(x, y, send) { st.cx = x; st.cy = y; if (send) { surface.set('joy_x', x); surface.set('joy_y', y); } draw(); }

  const setBase = (v) => { st.baseRate = Math.max(RMIN, Math.min(RMAX, v)); msg('morph_rate', st.baseRate); draw(); };
  // point removal: the brain owns the engine side (morph_unplace + snapshot_clear + lamp); the
  // canvas drops the marker when the brain confirms ('morph_snap:removed') or right away if the
  // brain predates morphRemove
  const dropPt = (id) => { const p = st.pts.find((q) => q.id === id); if (!p) return;
    st.pts = st.pts.filter((q) => q !== p); if (st.sel === p) st.sel = null; draw(); };
  const removePt = (p) => { if (typeof surface.morphRemove === 'function') surface.morphRemove(p.id);
    else { msg('morph_unplace', p.id); msg('snapshot_clear', p.id); } dropPt(p.id); };
  const removeWp = (wp) => { st.wps = st.wps.filter((q) => q !== wp); if (st.sel === wp) st.sel = null; draw(); };
  surface.watch('morph_snap:removed', (ev) => { if (ev && typeof ev === 'object') dropPt(ev.slot); });
  let mode = null, dragPt = null;
  let lastTap = { t: 0, x: -1, y: -1 };                   // pointer-timed double-tap (pointerdown pairs)
  const DBL_MS = 400, DBL_DIST = 0.05;
  cv.tabIndex = 0;                                        // keyboard: Delete / Backspace remove the selection
  cv.addEventListener('contextmenu', (e) => e.preventDefault());
  cv.addEventListener('pointerdown', (e) => {
    const { x, y } = rectXY(e);
    try { cv.focus({ preventScroll: true }); } catch (_) {}
    if (y * S >= RBAND) { mode = 'rate'; setBase(xToRate(x * S));
      try { cv.setPointerCapture(e.pointerId); } catch (_) {} e.preventDefault(); return; }
    if (e.shiftKey) { const w = { x, y, rate: WP_RATE }; st.wps.push(w); st.sel = w; draw(); return; }
    const wp = wpHit(x, y);
    if (wp) {
      if (e.button === 2 || e.altKey) { removeWp(wp); e.preventDefault(); return; }
      st.sel = wp; mode = 'wp'; dragPt = wp; draw();
      try { cv.setPointerCapture(e.pointerId); } catch (_) {} e.preventDefault(); return; }
    const p = hit(x, y);
    const t = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
    const dbl = p && (t - lastTap.t) < DBL_MS && Math.hypot(x - lastTap.x, y - lastTap.y) < DBL_DIST;
    lastTap = { t, x, y };
    if (p && (dbl || e.button === 2 || e.altKey)) { lastTap.t = 0; removePt(p); e.preventDefault(); return; }
    if (p) { st.sel = p; dragPt = p; mode = 'pt'; draw(); }
    else { st.sel = null; mode = 'cur'; setCursor(x, y, true); }
    try { cv.setPointerCapture(e.pointerId); } catch (_) {} e.preventDefault();
  });
  cv.addEventListener('keydown', (e) => {
    if (e.key === 'Delete' || e.key === 'Backspace') {
      if (st.sel && st.pts.includes(st.sel)) { removePt(st.sel); e.preventDefault(); }
      else if (st.sel && st.wps.includes(st.sel)) { removeWp(st.sel); e.preventDefault(); }
    } else if (e.key === 'Escape') { st.sel = null; draw(); }
  });
  cv.addEventListener('pointermove', (e) => { if (!mode) return; const { x, y } = rectXY(e);
    if (mode === 'rate') { setBase(xToRate(x * S)); }
    else if (mode === 'wp' && dragPt) { dragPt.x = x; dragPt.y = y; draw(); }
    else if (mode === 'pt' && dragPt) { dragPt.x = x; dragPt.y = y; msg('morph_point', dragPt.id, x, y); draw(); }
    else if (mode === 'cur') setCursor(x, y, true); });
  const end = () => { mode = null; dragPt = null; };
  cv.addEventListener('pointerup', end); cv.addEventListener('pointercancel', end);
  // wheel over a waypoint tunes ITS per-leg rate; elsewhere it nudges the base rate
  cv.addEventListener('wheel', (e) => { e.preventDefault(); const { x, y } = rectXY(e);
    const wp = wpHit(x, y) || (st.wps.includes(st.sel) ? st.sel : null);
    const d = -Math.sign(e.deltaY) * 0.1;
    if (wp) { wp.rate = Math.max(0.1, Math.min(8, Math.round((wp.rate + d) / 0.1) * 0.1)); }
    else { setBase(Math.round((st.baseRate + d) / 0.05) * 0.05); }
    draw(); }, { passive: false });
  cv.addEventListener('dblclick', (e) => { const { x, y } = rectXY(e);
    const wp = wpHit(x, y);
    if (wp) { removeWp(wp); return; }
    const p = hit(x, y);
    if (p) removePt(p); });

  // the brain's morph_snap special captured `snapshot <slot>` at the cursor: place / move its point
  function upsert(id, x, y, label) {
    let p = st.pts.find((q) => q.id === id);
    if (!p) { p = { id, x, y, label }; st.pts.push(p); } else { p.x = x; p.y = y; p.label = label; }
    st.sel = p; draw();
  }
  surface.watch('morph_snap:placed', (ev) => { if (ev && typeof ev === 'object') upsert(ev.slot, ev.x, ev.y, ev.label || String(ev.slot + 1)); });
  function snap() { surface.bang('morph_snap'); }
  function stop() { msg('morph_stop'); st.running = false; if (st.anim) { cancelAnimationFrame(st.anim); st.anim = 0; } draw(); }
  function run() {
    const legs = st.wps.length ? st.wps.map((w) => ({ x: w.x, y: w.y, rate: w.rate }))
                               : st.pts.map((p) => ({ x: p.x, y: p.y, rate: WP_RATE }));
    if (!legs.length) return;
    msg('morph_rate', st.baseRate);           // global multiplier (engine scales every leg)
    msg('morph_route_clear'); legs.forEach((l) => msg('morph_route', l.x, l.y, l.rate, 3)); msg('morph_run', 1);
    st.running = true; st.t0 = performance.now(); st.from = { x: st.cx, y: st.cy };
    const durs = legs.map((l) => Math.max(0.05, l.rate) / st.baseRate);   // seconds per leg (base-scaled)
    const total = durs.reduce((a, b) => a + b, 0);
    const tick = (now) => { if (!st.running) return;
      let el = ((now - st.t0) / 1000) % total, leg = 0;
      while (leg < durs.length - 1 && el >= durs[leg]) { el -= durs[leg]; leg++; }
      const lt = clamp(el / durs[leg]);
      const a = leg === 0 ? st.from : legs[leg - 1], b = legs[leg], s = lt * lt * (3 - 2 * lt);
      st.cx = a.x + (b.x - a.x) * s; st.cy = a.y + (b.y - a.y) * s; draw();
      st.anim = requestAnimationFrame(tick); };
    st.anim = requestAnimationFrame(tick);
  }
  function pause() { msg('morph_pause'); if (st.anim) { cancelAnimationFrame(st.anim); st.anim = 0; } }
  function clearRoute() { st.wps = []; msg('morph_route_clear'); draw(); }
  // panel-state persistence (surface.getPanelState / setPanelState) — no engine traffic
  function getState() {
    return { pts: st.pts.map((p) => ({ id: p.id, x: p.x, y: p.y, label: p.label })),
             wps: st.wps.map((w) => ({ x: w.x, y: w.y, rate: w.rate })), baseRate: st.baseRate, cx: st.cx, cy: st.cy };
  }
  function setState(s) {
    if (!s || typeof s !== 'object') return;
    if (Array.isArray(s.pts)) st.pts = s.pts.map((p) => ({ id: +p.id, x: clamp(+p.x), y: clamp(+p.y), label: String(p.label || (+p.id + 1)) }));
    if (Array.isArray(s.wps)) st.wps = s.wps.map((w) => ({ x: clamp(+w.x), y: clamp(+w.y), rate: +w.rate || WP_RATE }));
    if (Number.isFinite(+s.baseRate)) st.baseRate = Math.max(RMIN, Math.min(RMAX, +s.baseRate));
    if (Number.isFinite(+s.cx) && Number.isFinite(+s.cy)) { st.cx = clamp(+s.cx); st.cy = clamp(+s.cy); }
    st.sel = null; draw();
  }

  draw();
  return { snap, run, stop, pause, clearRoute, draw, setCursor, getState, setState, el: cv, st };
}

// shared <defs> for the chrome-dome gradient, injected once (mini knob SVGs reference it by id)
function ensureDefs() {
  if (document.getElementById('lg-defs')) return;
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.id = 'lg-defs'; svg.setAttribute('width', 0); svg.setAttribute('height', 0);
  svg.style.position = 'absolute';
  svg.innerHTML = '<defs><radialGradient id="lg-dome" cx="0.38" cy="0.32" r="0.75">' +
    '<stop offset="0" stop-color="#f6f6f4"/><stop offset="0.55" stop-color="#cdd0d3"/>' +
    '<stop offset="1" stop-color="#9a9ea3"/></radialGradient></defs>';
  document.body.appendChild(svg);
}

// tone-ring pin slot for pitch class `pc` under RING ORDER 0 CHRO / 1 5THS / 2 W-T (display only)
export function ringSlot(pc, order) {
  order = Math.round(+order || 0);
  if (order === 1) return (pc * 7) % 12;                       // circle of fifths: slot k holds (7k mod 12)
  if (order === 2) return pc % 2 === 0 ? pc / 2 : 6 + (pc - 1) / 2;   // whole-tone halves
  return pc;
}
export function ringPosition(pc, order) {
  const t = TABLES.SEQ_TONE, a = (-90 + ringSlot(pc, order) * 30) * Math.PI / 180;
  return { x: t.cx + t.r * Math.cos(a), y: t.cy + t.r * Math.sin(a) };
}

/* buildControls(surface, container, {svg}) — render the SVG panel + live overlay.
 * `svg` is the ligase_synthi_panel.svg markup (index.html fetches it). Returns id -> handle:
 *   handles[id] = { set(value), el, kind [, flash()] }   set() is VISUAL ONLY (never re-enters the brain)
 *   buttons: set(0 off | 1 lit | 2 held); leds/readouts: set(text); knobs/switches/toggles/pins: set(value)
 *   handles.morph = the metasurface (getState/setState/snap/run/stop/pause); handles.joy_x/joy_y move the cursor */
export function buildControls(surface, container, opts = {}) {
  if (!document.getElementById('lg-panel-style')) {
    const s = document.createElement('style'); s.id = 'lg-panel-style';
    s.textContent = CSS; document.head.appendChild(s);
  }
  ensureDefs();
  container.classList.add('lg-root');
  container.innerHTML = '';

  const stage = document.createElement('div'); stage.className = 'lg-stage';
  if (opts.svg) stage.innerHTML = opts.svg;
  const overlay = document.createElement('div'); overlay.className = 'lg-overlay';
  overlay.style.width = PANEL_W + 'px'; overlay.style.height = PANEL_H + 'px';
  stage.appendChild(overlay);
  container.appendChild(stage);

  const rescale = () => {
    const s = stage.clientWidth / PANEL_W;
    overlay.style.transform = `scale(${s})`;
  };
  rescale();
  if (window.ResizeObserver) new ResizeObserver(rescale).observe(stage);
  window.addEventListener('resize', rescale);

  // the morph metasurface owns the joystick bed (points/cursor/routes); it also captures the
  // SNAP / ROUTE RUN / STOP / PAUSE buttons below (their engine traffic goes through surface.msg).
  const morph = createMorphSurface(surface, overlay);
  const MORPH_BTN = { morph_snap: () => morph.snap(), morph_run: () => morph.run(),
                      morph_stop: () => morph.stop(), morph_pause: () => morph.pause() };

  const handles = {};
  const ringPins = {};
  for (const c of CONTROLS) {
    if (c.kind === 'joypad') { handles[c.id] = { set: () => {}, el: morph.el, kind: 'joypad' }; continue; }
    if (c.kind === 'virtual') {
      const axis = c.id === 'joy_x' ? 'x' : 'y';
      handles[c.id] = { kind: 'virtual', el: morph.el,
        set: (v) => { const x = axis === 'x' ? +v : morph.st.cx, y = axis === 'y' ? +v : morph.st.cy; morph.setCursor(x, y, false); } };
      continue;
    }
    const w = document.createElement('div');
    w.className = 'lg-w'; w.dataset.id = c.id; w.dataset.kind = c.kind;
    w.style.left = c.cx + 'px'; w.style.top = c.cy + 'px';
    let setter = () => {}, flash = null;

    if (c.kind === 'knob') {
      const D = 2 * c.r;
      const k = document.createElement('div'); k.className = 'lg-knob';
      k.style.width = D + 'px'; k.style.height = D + 'px';
      const { svg, setPos } = knobSVG(c.r, c.cap);
      k.appendChild(svg); w.appendChild(k);
      const read = document.createElement('div'); read.className = 'lg-read'; w.appendChild(read);
      const span = (c.hi - c.lo) || 1;
      let pos = (c.default - c.lo) / span; setPos(pos);
      const apply = (p, send) => {
        pos = Math.max(0, Math.min(1, p)); setPos(pos);
        const v = c.lo + pos * span; read.textContent = fmt(v);
        if (send) surface.set(c.id, v);
      };
      let dragging = false, sy = 0, sp = 0, lastDown = 0;
      const down = (e) => {
        const t = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
        if (t - lastDown < 400) { lastDown = 0; apply((c.default - c.lo) / span, true); e.preventDefault(); return; }   // double-tap = default
        lastDown = t;
        dragging = true; sy = e.clientY; sp = pos; w.classList.add('live');
        try { k.setPointerCapture(e.pointerId); } catch (_) {} e.preventDefault(); };
      const move = (e) => { if (!dragging) return;
        apply(sp + (sy - e.clientY) / 180, true); };
      const up = () => { dragging = false; w.classList.remove('live'); };
      k.addEventListener('pointerdown', down);
      k.addEventListener('pointermove', move);
      k.addEventListener('pointerup', up);
      k.addEventListener('pointercancel', up);
      k.addEventListener('wheel', (e) => { e.preventDefault();
        w.classList.add('live'); apply(pos - Math.sign(e.deltaY) * 0.03, true);
        clearTimeout(k._t); k._t = setTimeout(() => w.classList.remove('live'), 700); },
        { passive: false });
      k.addEventListener('dblclick', () => apply((c.default - c.lo) / span, true));
      setter = (v) => apply((+v - c.lo) / span, false);

    } else if (c.kind === 'switch') {
      const n = c.labels.length;
      const sw = document.createElement('div'); sw.className = 'lg-sw';
      sw.style.width = c.w + 'px';
      const dot = document.createElement('div'); dot.className = 'dot'; sw.appendChild(dot);
      w.style.width = c.w + 'px';
      const place = (sel) => { dot.style.left = (c.w / n) * (sel + 0.5) + 'px'; };
      const apply = (sel, send) => { sel = Math.max(0, Math.min(n - 1, sel)); place(sel);
        if (send) surface.set(c.id, c.lo + sel); };
      apply(Math.round(c.default - c.lo), false);
      sw.addEventListener('pointerdown', (e) => {
        const rect = sw.getBoundingClientRect();
        apply(Math.floor(((e.clientX - rect.left) / rect.width) * n), true); });
      sw.title = c.label + ': ' + c.labels.join(' / ');
      w.appendChild(sw);
      setter = (v) => apply(Math.round(+v - c.lo), false);

    } else if (c.kind === 'toggle') {
      const t = document.createElement('div'); t.className = 'lg-tgl';
      const kn = document.createElement('div'); kn.className = 'knob'; t.appendChild(kn);
      const place = (on) => { kn.style.left = on ? '18px' : '8px'; t.classList.toggle('on', on); };
      let on = !!c.default; place(on);
      const apply = (v, send) => { on = !!v; place(on); if (send) surface.set(c.id, on ? 1 : 0); };
      t.addEventListener('click', () => apply(!on, true));
      w.appendChild(t);
      setter = (v) => apply(!!+v, false);

    } else if (c.kind === 'button') {
      const b = document.createElement('div'); b.className = 'lg-btn';
      b.style.width = c.w + 'px'; b.textContent = c.btnLabel;
      const act = MORPH_BTN[c.id];   // SNAP/RUN/STOP/PAUSE drive the metasurface, not a bare bang
      flash = () => { b.classList.add('flash'); setTimeout(() => b.classList.remove('flash'), 120); };
      b.addEventListener('click', () => { if (act) { act(); flash(); } else surface.bang(c.id); });
      w.appendChild(b);
      setter = (state) => { state = Math.round(+state || 0);
        b.classList.toggle('lit', state === 1); b.classList.toggle('held', state === 2); };

    } else if (c.kind === 'pin' || c.kind === 'mxpin') {
      const p = document.createElement('div'); p.className = 'lg-pin';
      if (c.kind === 'mxpin') p.classList.add('mx');
      let on = !!c.default;
      const place = () => { p.classList.toggle('on', on);
        p.style.background = on ? c.pinCap : '#16181c'; };
      place();
      const apply = (v, send) => { on = !!v; place(); if (send) surface.set(c.id, on ? 1 : 0); };
      p.addEventListener('click', () => apply(!on, true));
      if (c.src) p.title = c.src + ' → ' + c.dst;
      if (c.ringIndex != null) { ringPins[c.ringIndex] = w; p.title = 'pitch class ' + TABLES.SEQ_TONE.order[c.ringIndex]; }
      w.appendChild(p);
      setter = (v) => apply(!!+v, false);

    } else if (c.kind === 'led') {
      const d = document.createElement('div'); d.className = 'lg-led';
      d.style.width = c.w + 'px'; d.style.height = c.h + 'px'; d.style.fontSize = c.fs + 'px';
      const ghost = document.createElement('span'); ghost.className = 'ghost'; ghost.textContent = c.ghost;
      const val = document.createElement('span'); val.className = 'val'; val.textContent = '';
      d.appendChild(ghost); d.appendChild(val); w.appendChild(d);
      const pad = c.ghost.length;
      setter = (v) => { val.textContent = typeof v === 'number' ? String(Math.round(v)).padStart(pad, '0') : String(v == null ? '' : v); };

    } else if (c.kind === 'readout') {
      const d = document.createElement('div'); d.className = 'lg-ro';
      d.style.width = c.w + 'px'; d.style.fontSize = c.fs + 'px';
      w.appendChild(d);
      setter = (v) => { d.textContent = v == null ? '' : String(v); d.title = d.textContent; };
    }

    if (c.overlayLabel) { const l = document.createElement('div'); l.className = 'lg-lbl'; l.textContent = c.overlayLabel; w.appendChild(l); }
    overlay.appendChild(w);
    handles[c.id] = { set: setter, el: w, kind: c.kind };
    if (flash) handles[c.id].flash = flash;
  }
  handles.morph = { set: () => {}, el: morph.el, kind: 'morph', getState: morph.getState, setState: morph.setState,
                    snap: morph.snap, run: morph.run, stop: morph.stop, pause: morph.pause, clearRoute: morph.clearRoute };

  // RING ORDER (CHRO / 5THS / W-T) is a pure display projection: re-seat the 12 ring pins.
  const layoutRing = (order) => { for (const [pc, w] of Object.entries(ringPins)) {
    const p = ringPosition(+pc, order); w.style.left = p.x.toFixed(1) + 'px'; w.style.top = p.y.toFixed(1) + 'px'; } };
  surface.watch('seq_ring_order:layout', layoutRing);
  surface.watch('seq_ring_order', layoutRing);

  // --- live VU meters (MONITOR / OUTPUT) — 12-segment L/R bars over the SVG meter art,
  // driven by the bridge's 'vu' event. Geometry mirrors emit_svg.py's monitor block. ---
  const SEGW = 12, SEGN = 12;
  const meters = [];
  for (const [, off] of [['L', 0], ['R', 22]]) {
    const segs = [];
    for (let i = 0; i < SEGN; i++) {
      const s = document.createElement('div'); s.className = 'lg-vuseg';
      s.style.left = (__VUX__ + 4 + i * SEGW) + 'px'; s.style.top = (__VUY__ + off + 3) + 'px';
      overlay.appendChild(s); segs.push(s);
    }
    meters.push(segs);
  }
  const paintVU = (segs, v) => {
    const nlit = Math.round(Math.min(1, Math.sqrt(Math.max(0, v))) * SEGN);
    for (let i = 0; i < SEGN; i++) {
      const on = i < nlit;
      segs[i].style.background = on ? (i < 9 ? '#4f9860' : i < 11 ? '#d9a23a' : '#c04a38') : '#2c2f34';
    }
  };
  paintVU(meters[0], 0); paintVU(meters[1], 0);
  surface.on('vu', (l, r) => { paintVU(meters[0], l); paintVU(meters[1], r); });

  // --- live SCOPE — a phosphor canvas over the scope bed (SDX,SDY,S mirror emit_svg.py), fed by
  // the bridge's 'scope' event (x/y Float32Array windows). VIEW switch: XY (Lissajous / Lorenz /
  // grain constellation) or SWP (time sweep of Y, left to right). ---
  const cvs = document.createElement('canvas');
  cvs.className = 'lg-scope'; cvs.width = __SCS__; cvs.height = __SCS__;
  cvs.style.left = __SCX__ + 'px'; cvs.style.top = __SCY__ + 'px';
  cvs.style.width = __SCS__ + 'px'; cvs.style.height = __SCS__ + 'px';
  overlay.appendChild(cvs);
  const g = cvs.getContext('2d');
  const SCS = __SCS__;
  g.fillStyle = '#0a0c0e'; g.fillRect(0, 0, SCS, SCS);
  let smax = 0.001;                       // running magnitude for auto-scale (slow decay)
  surface.on('scope', (x, y) => {
    const N = Math.min(x.length, y.length);
    if (!N) return;
    const sweep = Math.round(+surface.getValue('scope_view') || 0) === 1;
    let m = smax * 0.9;
    for (let i = 0; i < N; i++) { const ax = Math.abs(x[i]), ay = Math.abs(y[i]);
      if (!sweep && ax > m) m = ax; if (ay > m) m = ay; }
    smax = Math.max(m, 1e-4);
    g.fillStyle = sweep ? '#0a0c0e' : 'rgba(10,12,14,0.22)'; g.fillRect(0, 0, SCS, SCS);   // phosphor fade / clean sweep
    if (sweep) { g.strokeStyle = '#1f2a22'; g.lineWidth = 1; g.beginPath(); g.moveTo(0, SCS / 2); g.lineTo(SCS, SCS / 2); g.stroke(); }
    g.strokeStyle = '#79c98b'; g.lineWidth = 1; g.beginPath();
    for (let i = 0; i < N; i++) {
      const px = sweep ? (i / (N - 1)) * SCS : (x[i] / smax * 0.46 + 0.5) * SCS;
      const py = (0.5 - y[i] / smax * 0.46) * SCS;
      if (i === 0) g.moveTo(px, py); else g.lineTo(px, py);
    }
    g.stroke();
  });

  return handles;
}

/* Push every init_send default through the brain (mirrors the patch loadbang broadcast). */
export function sendDefaults(surface) {
  for (const c of CONTROLS) {
    if (!c.initSend) continue;
    if (c.kind === 'button' || c.kind === 'led' || c.kind === 'readout' || c.kind === 'joypad') continue;
    surface.set(c.id, +c.default);
  }
}
"""


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--panel", default=os.path.join(ROOT, "pd", "ligase_panel.pd"))
    ap.add_argument("--out", default=os.path.join(ROOT, "web", "ligase_controls.js"))
    args = ap.parse_args(argv)

    controls, skipped = build_descriptors(args.panel)
    js = (JS_TEMPLATE
          .replace("__W__", str(PL.W))
          .replace("__H__", str(PL.H))
          .replace("__CONTROLS__", json.dumps(controls, indent=1))
          .replace("__TABLES__", json.dumps(build_tables(), indent=1))
          .replace("__INLETS__", json.dumps(build_inlets()))
          .replace("__JX__", str(JX)).replace("__JY__", str(JY)).replace("__JS__", str(JS))
          .replace("__SCX__", str(SCX)).replace("__SCY__", str(SCY)).replace("__SCS__", str(SCS))
          .replace("__VUX__", str(VUX)).replace("__VUY__", str(VUY)))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(js)

    from collections import Counter
    reasons = Counter(r for _, r in skipped)
    kinds = Counter(c["kind"] for c in controls)
    print(f"[emit_web] wrote {os.path.relpath(args.out, ROOT)}: "
          f"{len(controls)} web controls ({len(skipped)} skipped)")
    print("           kinds: " + ", ".join(f"{k} {n}" for k, n in sorted(kinds.items())))
    for r, n in reasons.most_common():
        print(f"           skipped {n:3d}: {r}")


if __name__ == "__main__":
    main()
