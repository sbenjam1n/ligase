#!/usr/bin/env python3
# emit_web.py — the FOURTH emitter from panel_layout.py (Plans/web_build.md Arc A, Step 4).
#
# Consumed alongside emit_svg.py (silkscreen), emit_pd.py (the working instrument), and
# gen_panel.py. Where emit_pd builds the Pd patch, emit_web builds an HTML/JS control surface
# that drives the SAME engine running in WASM (web/ligase_wasm.*) over the EXISTING lgR_/lgS_
# receive-symbol bus — no new engine entry points.
#
# THE PANEL *IS* THE SVG. The web UI renders emit_svg.py's rendered silkscreen
# (ligase_synthi_panel.svg) as its backdrop, then overlays live, exactly-registered
# interactive widgets on top of — and hiding — each control's static twin: knobs turn,
# switches/toggles/buttons/pins respond. Every widget is placed at the SAME (x,y) the SVG
# drew it (panel_layout coordinates, in SVG user units), so the overlay tracks the art at any
# scale. Display-only art (mod matrix pins, the joystick pad, the scope, VU meters) stays as
# the static SVG for now — those are the phased follow-ups.
#
#   web value  --engine.setFloat('lgR_<id>', v)-->  [r lgR_<id>] (iemgui) --> [s lgS_<id>] --> ligase
#   readback   <--engine.watch('lgS_<id>', cb)----  the iemgui echoes its value on lgS_<id>
#
# Usage:
#   python3 docs/ui/emit_web.py [--panel pd/ligase_panel.pd] [--out web/ligase_controls.js]
#
# Output: web/ligase_controls.js  (ES module: `export const CONTROLS`, `export function
# buildControls(engine, container, {svg})`, `export const PANEL_W/PANEL_H`, and an injected
# stylesheet). index.html fetches the SVG, then imports and calls this.

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


def panel_receives(panel_path):
    """Set of lgR_ receive symbols that actually exist in the built patch."""
    txt = open(panel_path, "r", encoding="latin-1").read()
    return set(re.findall(r"lgR_[A-Za-z0-9_]+", txt))


# Special binds the web can NOT sensibly drive over a single lgR_ float/bang: native file
# dialogs (openpanel/savepanel — the index.html shell does reel I/O straight to lg_engine),
# raw audio jacks, and the bespoke 2D controls (matrix, joystick). Everything else with an
# lgR_ receive becomes a web control, keyed off its iemgui primitive.
_SPECIAL_SKIP = {
    "openpanel_load", "savepanel_save", "adc_l", "adc_r", "dac_l", "dac_r",
    "matrix", "joystick", "joystick_xy", "xy",
}


def web_kind(ctrl):
    """Map a panel-layout control to a web widget kind, or None to skip.
    Rendered kinds: knob | switch | toggle | button | pin."""
    cid = ctrl["id"]
    kind = ctrl["kind"]
    bind = ctrl.get("bind")
    sel = bind[0] if isinstance(bind, (tuple, list)) and bind else None
    if kind in ("led", "jack"):
        return None
    if sel == "special":
        name = bind[1] if len(bind) > 1 else ""
        if name in _SPECIAL_SKIP:
            return None
        if any(t in name for t in ("matrix", "joystick", "xy")):
            return None
    # the tone-ring + pattern-grid toggles are drawn as round pins, not slide toggles
    if re.match(r"^seq_ring_\d+$", cid) or re.match(r"^seq_grid_\d+_\d+$", cid):
        return "pin"
    if kind == "toggle":
        return "toggle"
    if kind == "switch":
        return "switch"
    if kind == "button":
        return "button"          # momentary -> bang to lgR_<id>
    return "knob"                # knob / number / anything numeric with a range


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


def control_center(ctrl):
    """(cx, cy) in SVG user units — where emit_svg.py drew this control's center.
    Standard controls carry svg x/y; the tone-ring and pattern-grid pins are generated
    bespoke by emit_svg from the SEQ_TONE / SEQ_GRID tables, so mirror that math."""
    cid = ctrl["id"]
    if re.match(r"^seq_ring_\d+$", cid):
        i = int(cid.rsplit("_", 1)[1])          # ring toggle i == pitch class i (CHRO order)
        t = PL.SEQ_TONE
        a = math.radians(-90 + i * 30)
        return t["cx"] + t["r"] * math.cos(a), t["cy"] + t["r"] * math.sin(a)
    if re.match(r"^seq_grid_\d+_\d+$", cid):
        _, _, r, cc = cid.split("_")
        g = PL.SEQ_GRID
        cell = g["cell"]
        return g["ox"] + int(cc) * cell + cell / 2.0, g["oy"] + int(r) * cell + cell / 2.0
    svg = ctrl.get("svg") or {}
    if "x" in svg and "y" in svg:
        return float(svg["x"]), float(svg["y"])
    return None


def build_descriptors(panel_path):
    recv = panel_receives(panel_path)
    out, skipped = [], []
    for c in PL.CONTROLS:
        cid = c["id"]
        wk = web_kind(c)
        if wk is None:
            skipped.append((cid, "kind/bind not a web control"))
            continue
        if ("lgR_" + cid) not in recv:
            skipped.append((cid, "no lgR_%s receive in panel" % cid))
            continue
        center = control_center(c)
        if center is None:
            skipped.append((cid, "no panel coordinate"))
            continue
        svg = c.get("svg") or {}
        cx, cy = center
        d = {
            "id": cid,
            "recv": "lgR_" + cid,
            "send": "lgS_" + cid,
            "kind": wk,
            "label": control_label(c),
            "cx": round(cx, 1),
            "cy": round(cy, 1),
            "lo": c.get("lo", 0.0),
            "hi": c.get("hi", 1.0),
            "default": c.get("default", 0.0),
            "initSend": bool(c.get("init_send", True)),
        }
        if wk == "knob":
            d["r"] = 12 if svg.get("small") else 17
            d["cap"] = CAP.get(svg.get("cap", "white"), CAP["white"])
        elif wk == "switch":
            d["labels"] = switch_labels(c)
            d["w"] = float(svg.get("w", 104))
        elif wk == "button":
            d["w"] = float(svg.get("w", 52))
            d["btnLabel"] = str(svg.get("label", cid))
        elif wk == "toggle":
            d["tglLabel"] = str(svg.get("label", ""))
        elif wk == "pin":
            d["pinCap"] = CAP["green"] if cid.startswith("seq_ring_") else CAP["white"]
        out.append(d)
    return out, skipped


# ---- the emitted module -------------------------------------------------------------------
# Placeholders (__W__ etc.) are substituted last so the CSS/JS body can contain literal % and
# { } without format-string escaping.
JS_TEMPLATE = r"""/* AUTO-GENERATED by docs/ui/emit_web.py from docs/ui/panel_layout.py — DO NOT EDIT.
 * Web control surface for ligase~ (Plans/web_build.md Arc A, Step 4). The panel IS the SVG
 * silkscreen (ligase_synthi_panel.svg); this overlays live widgets at the SVG's own control
 * coordinates and drives the WASM engine over the lgR_/lgS_ bus. Regenerate with:
 *   python3 docs/ui/emit_web.py
 */
export const PANEL_W = __W__, PANEL_H = __H__;
export const CONTROLS = __CONTROLS__;

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
/* momentary button */
.lg-btn { height:17px; border-radius:8.5px; border:0.9px solid #8d9195;
  background:linear-gradient(#eceef0,#c3c6c9); color:#26282b; font:bold 7.5px/1 ui-monospace,monospace;
  display:flex; align-items:center; justify-content:center; cursor:pointer; letter-spacing:.02em; }
.lg-btn.lit, .lg-btn:active { background:#e8c47c; border-color:#a8863c; }
/* tone-ring / pattern-grid pin — off-state fully covers the silkscreen twin (incl. demo pins) */
.lg-pin { width:14px; height:14px; border-radius:50%; cursor:pointer;
  background:#16181c; border:1px solid #3e4247;
  display:flex; align-items:center; justify-content:center; }
.lg-pin::after { content:''; width:5px; height:5px; border-radius:50%; background:#0d0e10; }
.lg-pin.on { border:1.4px solid #0d0e10; }
.lg-pin.on::after { width:4px; height:4px; background:#0d0e10; }
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

/* buildControls(engine, container, {svg}) — render the SVG panel + live overlay.
 * `svg` is the ligase_synthi_panel.svg markup (index.html fetches it). Returns id -> handle. */
export function buildControls(engine, container, opts = {}) {
  if (!document.getElementById('lg-panel-style')) {
    const s = document.createElement('style'); s.id = 'lg-panel-style';
    s.textContent = CSS; document.head.appendChild(s);
  }
  ensureDefs();
  container.classList.add('lg-root');
  container.innerHTML = '';

  const stage = document.createElement('div'); stage.className = 'lg-stage';
  if (opts.svg) stage.innerHTML = opts.svg;
  const bg = stage.querySelector('svg');
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

  const handles = {};
  for (const c of CONTROLS) {
    const w = document.createElement('div');
    w.className = 'lg-w'; w.dataset.id = c.id;
    w.style.left = c.cx + 'px'; w.style.top = c.cy + 'px';
    let setter = () => {};

    if (c.kind === 'knob') {
      const D = 2 * c.r;
      w.classList.add('lg-w');
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
        if (send) engine.setFloat(c.recv, v);
      };
      let dragging = false, sy = 0, sp = 0;
      const down = (e) => { dragging = true; sy = e.clientY; sp = pos; w.classList.add('live');
        k.setPointerCapture && k.setPointerCapture(e.pointerId); e.preventDefault(); };
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
      engine.watch(c.send, (v) => apply((v - c.lo) / span, false));
      setter = (v) => apply((v - c.lo) / span, true);

    } else if (c.kind === 'switch') {
      const n = c.labels.length;
      const sw = document.createElement('div'); sw.className = 'lg-sw';
      sw.style.width = c.w + 'px';
      const dot = document.createElement('div'); dot.className = 'dot'; sw.appendChild(dot);
      w.style.width = c.w + 'px';
      const place = (sel) => { dot.style.left = (c.w / n) * (sel + 0.5) + 'px'; };
      const apply = (sel, send) => { sel = Math.max(0, Math.min(n - 1, sel)); place(sel);
        if (send) engine.setFloat(c.recv, c.lo + sel); };
      apply(Math.round(c.default - c.lo), false);
      sw.addEventListener('pointerdown', (e) => {
        const rect = sw.getBoundingClientRect();
        apply(Math.floor(((e.clientX - rect.left) / rect.width) * n), true); });
      w.appendChild(sw);
      engine.watch(c.send, (v) => apply(Math.round(v - c.lo), false));
      setter = (v) => apply(Math.round(v - c.lo), true);

    } else if (c.kind === 'toggle') {
      const t = document.createElement('div'); t.className = 'lg-tgl';
      const kn = document.createElement('div'); kn.className = 'knob'; t.appendChild(kn);
      const place = (on) => { kn.style.left = on ? '18px' : '8px'; t.classList.toggle('on', on); };
      let on = !!c.default; place(on);
      const apply = (v, send) => { on = !!v; place(on); if (send) engine.setFloat(c.recv, on ? 1 : 0); };
      t.addEventListener('click', () => apply(!on, true));
      w.appendChild(t);
      engine.watch(c.send, (v) => apply(!!v, false));
      setter = (v) => apply(!!v, true);

    } else if (c.kind === 'button') {
      const b = document.createElement('div'); b.className = 'lg-btn';
      b.style.width = c.w + 'px'; b.textContent = c.btnLabel;
      b.addEventListener('click', () => { engine.sendBang(c.recv);
        b.classList.add('lit'); setTimeout(() => b.classList.remove('lit'), 120); });
      w.appendChild(b);
      setter = () => engine.sendBang(c.recv);

    } else if (c.kind === 'pin') {
      const p = document.createElement('div'); p.className = 'lg-pin';
      p.style.background = '#111316';
      let on = !!c.default;
      const place = () => { p.classList.toggle('on', on);
        p.style.background = on ? c.pinCap : '#111316'; };
      place();
      const apply = (v, send) => { on = !!v; place(); if (send) engine.setFloat(c.recv, on ? 1 : 0); };
      p.addEventListener('click', () => apply(!on, true));
      w.appendChild(p);
      engine.watch(c.send, (v) => apply(!!v, false));
      setter = (v) => apply(!!v, true);
    }

    overlay.appendChild(w);
    handles[c.id] = { set: setter, el: w };
  }
  return handles;
}

/* Push every init_send default into the engine (mirrors the patch loadbang broadcast). */
export function sendDefaults(engine) {
  for (const c of CONTROLS) {
    if (!c.initSend || c.kind === 'button') continue;
    engine.setFloat(c.recv, +c.default);
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
          .replace("__CONTROLS__", json.dumps(controls, indent=2)))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(js)

    from collections import Counter
    reasons = Counter(r for _, r in skipped)
    print(f"[emit_web] wrote {os.path.relpath(args.out, ROOT)}: "
          f"{len(controls)} web controls ({len(skipped)} skipped)")
    for r, n in reasons.most_common():
        print(f"           skipped {n:3d}: {r}")


if __name__ == "__main__":
    main()
