#!/usr/bin/env python3
# emit_web.py — the FOURTH emitter from panel_layout.py (Plans/web_build.md Arc A, Step 4).
#
# Consumed alongside emit_svg.py (silkscreen), emit_pd.py (the working instrument), and
# gen_panel.py. Where emit_pd builds the Pd patch, emit_web builds an HTML/JS control surface
# that drives the SAME engine running in WASM (web/ligase_wasm.*) over the EXISTING lgR_/lgS_
# receive-symbol bus — no new engine entry points. Layout parity with the SVG (uses the SVG
# x/y coordinates to place controls), NOT pixel parity; the SVG stays the visual spec.
#
# Rule (per the plan): every CONTROL whose receive symbol lgR_<id> actually exists in the
# built panel patch becomes a web control. Controls with no lgR_ receive (pure display /
# not-yet-bound / bespoke SVG-only) are skipped; the special transport/reel/mic controls are
# handled by the hand-written index.html shell, not here.
#
#   web value  --engine.setFloat('lgR_<id>', v)-->  [r lgR_<id>] (iemgui) --> [s lgS_<id>] --> ligase
#   readback   <--engine.watch('lgS_<id>', cb)----  the iemgui echoes its value on lgS_<id>
#
# Usage:
#   python3 docs/ui/emit_web.py [--panel pd/ligase_panel.pd] [--out web/ligase_controls.js]
#
# Output: web/ligase_controls.js  (ES module: `export const CONTROLS`, `export function
# buildControls(engine, container)`, and an injected stylesheet). index.html imports it.

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import panel_layout as PL  # noqa: E402


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
    """Map a panel-layout control to a web widget kind, or None to skip."""
    kind = ctrl["kind"]
    bind = ctrl.get("bind")
    sel = bind[0] if isinstance(bind, (tuple, list)) and bind else None
    if kind in ("led", "jack"):
        return None
    if sel == "special":
        name = bind[1] if len(bind) > 1 else ""
        if name in _SPECIAL_SKIP:
            return None
        # bespoke matrix/joystick names may vary; skip anything mentioning them
        if any(t in name for t in ("matrix", "joystick", "xy")):
            return None
    if kind == "toggle":
        return "toggle"
    if kind == "switch":
        return "switch"
    if kind == "button":
        return "button"       # momentary -> bang to lgR_<id>
    # knob / number / anything numeric with a range
    return "slider"


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


def parse_sections():
    """Map control id -> section title, from panel_layout.py's own `# ---- TITLE ----`
    markers (so the web grouping tracks the single source, no per-control metadata)."""
    src = open(os.path.join(ROOT, "docs", "ui", "panel_layout.py"), encoding="utf-8").read()
    id2sec, cur = {}, "PANEL"
    # only scan the CONTROLS list region (before the trailing data tables)
    region = src.split("CONTROL_BY_ID", 1)[0]
    for line in region.splitlines():
        m = re.search(r"#\s*-+\s*(.+?)\s*-+\s*$", line)
        if m:
            title = re.sub(r"\s*\(.*$", "", m.group(1)).strip()   # drop parentheticals
            title = re.sub(r"^[A-H]\.\s*", "", title)             # drop "A. " prefixes
            cur = title or cur
            continue
        cm = re.search(r'_c\(\s*(?:f?)"([a-z0-9_]+)"', line)
        if cm:
            id2sec[cm.group(1)] = cur
    return id2sec


def _generated_section(cid):
    """Section for controls emitted by f-string loops in panel_layout.py, which
    parse_sections() cannot see (the id isn't a literal). Mirror the `# ---- TITLE ----`
    marker each loop lives under so its widgets group with their hand-written siblings."""
    if re.match(r"^snap\d+$", cid):
        return "PRESETS / SNAPSHOTS"
    if re.match(r"^seq_ring_\d+$", cid):
        return "SEQ / SCALE"
    if re.match(r"^seq_slot_[A-Z]$", cid):
        return "SLOTS / COMMIT"
    if re.match(r"^seq_grid_\d+_\d+$", cid):
        return "PATTERN GRID"
    return "PANEL"


def build_descriptors(panel_path):
    recv = panel_receives(panel_path)
    id2sec = parse_sections()
    out = []
    skipped = []
    for c in PL.CONTROLS:
        cid = c["id"]
        wk = web_kind(c)
        if wk is None:
            skipped.append((cid, "kind/bind not a web control"))
            continue
        if ("lgR_" + cid) not in recv:
            skipped.append((cid, "no lgR_%s receive in panel" % cid))
            continue
        svg = c.get("svg") or {}
        d = {
            "id": cid,
            "recv": "lgR_" + cid,
            "send": "lgS_" + cid,
            "kind": wk,
            "label": control_label(c),
            "lo": c.get("lo", 0.0),
            "hi": c.get("hi", 1.0),
            "default": c.get("default", 0.0),
            "initSend": bool(c.get("init_send", True)),
            "section": id2sec.get(cid) or _generated_section(cid),
        }
        if wk == "switch":
            d["labels"] = switch_labels(c)
        out.append(d)
    return out, skipped


JS_TEMPLATE = r"""/* AUTO-GENERATED by docs/ui/emit_web.py from docs/ui/panel_layout.py — DO NOT EDIT.
 * Web control surface for ligase~ (Plans/web_build.md Arc A, Step 4). Layout parity with the
 * SVG (SVG x/y coordinates), driving the engine over the lgR_/lgS_ bus. Regenerate with:
 *   python3 docs/ui/emit_web.py
 */
export const PANEL_W = %(W)d, PANEL_H = %(H)d;
export const CONTROLS = %(CONTROLS)s;

const CSS = `
.lg-panel { background:#1c1e20; color:#ece8da;
  font:11px/1.3 ui-monospace,Menlo,Consolas,monospace;
  display:flex; flex-direction:column; gap:14px; padding:14px;
  box-sizing:border-box; }
.lg-sec { border:1px solid #34373b; border-radius:6px; background:#26282b;
  box-sizing:border-box; }
.lg-sec-h { font-size:9.5px; letter-spacing:.18em; font-weight:700; color:#9fd0a0;
  padding:6px 10px; border-bottom:1px solid #34373b; text-transform:uppercase; }
.lg-sec-b { display:flex; flex-wrap:wrap; gap:8px; padding:10px; }
.lg-ctl { box-sizing:border-box; display:flex; flex-direction:column; gap:3px;
  min-width:104px; max-width:160px; padding:6px 8px; border:1px solid #34373b;
  border-radius:4px; background:#2b2e31; }
.lg-ctl label { font-size:8.5px; letter-spacing:.05em; color:#c8cacd; white-space:nowrap;
  overflow:hidden; text-overflow:ellipsis; }
.lg-ctl .lg-val { font-size:9px; color:#9fd0a0; align-self:flex-end; }
.lg-ctl input[type=range]{ width:100%%; accent-color:#9fd0a0; margin:0; }
.lg-ctl input[type=checkbox]{ align-self:flex-start; accent-color:#9fd0a0; width:16px; height:16px; }
.lg-ctl.lg-btnctl { min-width:0; padding:0; border:0; background:none; justify-content:center; }
.lg-btn { width:100%%; background:#3a3d40; color:#ece8da; border:1px solid #55585c;
  border-radius:4px; padding:6px 10px; cursor:pointer; font:inherit; white-space:nowrap; }
.lg-btn:hover { background:#45484c; } .lg-btn:active { background:#9fd0a0; color:#1c1e20; }
select.lg-sw { width:100%%; background:#1c1e20; color:#ece8da; border:1px solid #55585c;
  border-radius:3px; font:inherit; padding:2px; }
`;

/* buildControls(engine, container) — create DOM controls grouped by panel section and wire
 * them to the engine. Responsive flow layout (works at any viewport width; the wide SVG
 * panel is the visual spec, this is layout parity per Plans/web_build.md). Returns a map
 * id -> {set(v), el}. (opts kept for back-compat; layout is always responsive flow.) */
export function buildControls(engine, container, opts = {}) {
  if (!document.getElementById('lg-panel-style')) {
    const s = document.createElement('style'); s.id = 'lg-panel-style';
    s.textContent = CSS; document.head.appendChild(s);
  }
  container.classList.add('lg-panel');

  // group controls into their panel sections, preserving first-seen order
  const order = [], byS = {};
  for (const c of CONTROLS) {
    const sec = c.section || 'PANEL';
    if (!byS[sec]) { byS[sec] = []; order.push(sec); }
    byS[sec].push(c);
  }

  const handles = {};
  for (const sec of order) {
    const box = document.createElement('div'); box.className = 'lg-sec';
    const h = document.createElement('div'); h.className = 'lg-sec-h'; h.textContent = sec;
    const body = document.createElement('div'); body.className = 'lg-sec-b';
    box.appendChild(h); box.appendChild(body); container.appendChild(box);
  for (const c of byS[sec]) {
    const wrap = document.createElement('div');
    wrap.className = 'lg-ctl' + (c.kind === 'button' ? ' lg-btnctl' : '');
    wrap.dataset.id = c.id;

    const lab = document.createElement('label');
    lab.textContent = c.label; wrap.appendChild(lab);

    let setter = () => {};
    if (c.kind === 'slider') {
      const inp = document.createElement('input');
      inp.type = 'range'; inp.min = c.lo; inp.max = c.hi;
      inp.step = (c.hi - c.lo) / 1000 || 0.001; inp.value = c.default;
      const val = document.createElement('span'); val.className = 'lg-val'; val.textContent = fmt(c.default);
      inp.addEventListener('input', () => { engine.setFloat(c.recv, +inp.value); val.textContent = fmt(+inp.value); });
      wrap.appendChild(inp); wrap.appendChild(val);
      engine.watch(c.send, (v) => { inp.value = v; val.textContent = fmt(v); });
      setter = (v) => { inp.value = v; engine.setFloat(c.recv, +v); val.textContent = fmt(+v); };
    } else if (c.kind === 'toggle') {
      const inp = document.createElement('input'); inp.type = 'checkbox'; inp.checked = !!c.default;
      inp.addEventListener('change', () => engine.setFloat(c.recv, inp.checked ? 1 : 0));
      wrap.appendChild(inp);
      engine.watch(c.send, (v) => { inp.checked = !!v; });
      setter = (v) => { inp.checked = !!v; engine.setFloat(c.recv, v ? 1 : 0); };
    } else if (c.kind === 'switch') {
      const seln = document.createElement('select'); seln.className = 'lg-sw';
      (c.labels || []).forEach((t, i) => { const o = document.createElement('option'); o.value = c.lo + i; o.textContent = t; seln.appendChild(o); });
      seln.value = c.default;
      seln.addEventListener('change', () => engine.setFloat(c.recv, +seln.value));
      wrap.appendChild(seln);
      engine.watch(c.send, (v) => { seln.value = v; });
      setter = (v) => { seln.value = v; engine.setFloat(c.recv, +v); };
    } else if (c.kind === 'button') {
      const b = document.createElement('button'); b.className = 'lg-btn'; b.textContent = c.label; lab.remove();
      b.addEventListener('click', () => engine.sendBang(c.recv));
      wrap.appendChild(b);
      setter = () => engine.sendBang(c.recv);
    }
    body.appendChild(wrap);
    handles[c.id] = { set: setter, el: wrap };
  }
  }
  return handles;
}

/* Push every init_send default into the engine (mirrors the patch loadbang broadcast). */
export function sendDefaults(engine) {
  for (const c of CONTROLS) {
    if (!c.initSend) continue;
    if (c.kind === 'button') continue;
    engine.setFloat(c.recv, +c.default);
  }
}

function fmt(v) { v = +v; return Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(3); }
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--panel", default=os.path.join(ROOT, "pd", "ligase_panel.pd"))
    ap.add_argument("--out", default=os.path.join(ROOT, "web", "ligase_controls.js"))
    args = ap.parse_args()

    if not os.path.exists(args.panel):
        sys.exit("panel patch not found: %s (run emit_pd.py first)" % args.panel)

    descriptors, skipped = build_descriptors(args.panel)
    js = JS_TEMPLATE % {
        "W": getattr(PL, "MAINW", getattr(PL, "W", 1520)),
        "H": getattr(PL, "H", 1096),
        "CONTROLS": json.dumps(descriptors, indent=2),
    }
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(js)
    print("[emit_web] wrote %s: %d web controls (%d skipped)"
          % (os.path.relpath(args.out, ROOT), len(descriptors), len(skipped)))
    # Surface a few skip reasons for auditability (not an error).
    kinds = {}
    for _, why in skipped:
        key = why.split(":")[0]
        kinds[key] = kinds.get(key, 0) + 1
    for k, n in sorted(kinds.items()):
        print("           skipped %3d: %s" % (n, k))


if __name__ == "__main__":
    main()
