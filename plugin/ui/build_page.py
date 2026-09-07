#!/usr/bin/env python3
# build_page.py — assemble the plugin's self-contained web-view page (plugin/resources/index.html)
# from the panel sources: the silkscreen SVG, the generated widget module (web/ligase_controls.js),
# the shared panel brain (web/ligase_panel_logic.js), the plugin bridge and the parameter table.
# ES-module syntax is stripped (export/import) so everything runs as classic inline scripts in a
# file:// web view (no fetch, no module loading, no external files).
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def read(*parts):
    with open(os.path.join(ROOT, *parts), "r", encoding="utf-8") as f:
        return f.read()


def strip_module(js):
    js = re.sub(r"^\s*import\s[^\n]*\n", "", js, flags=re.M)
    js = re.sub(r"^\s*export\s+default\s+", "", js, flags=re.M)
    js = re.sub(r"^\s*export\s+(const|let|var|function|class|async function)\b", r"\1", js, flags=re.M)
    js = re.sub(r"^\s*export\s*\{[^}]*\};?\s*$", "", js, flags=re.M)
    return js


def main():
    svg = read("docs", "ui", "ligase_synthi_panel.svg")
    controls = strip_module(read("web", "ligase_controls.js"))
    logic = strip_module(read("web", "ligase_panel_logic.js"))
    bridge = read("plugin", "ui", "bridge.js")
    params = json.loads(read("plugin", "ui", "params.json"))
    tmpl = read("plugin", "ui", "page_template.html")
    for js in (controls, logic, bridge):
        if "</script" in js.lower():
            sys.exit("build_page: a script contains '</script' — cannot inline")
    page = (tmpl.replace("__SVG__", svg.replace("</script", "<\\/script"))
                .replace("__PARAMS__", json.dumps(params, separators=(",", ":")))
                .replace("__CONTROLS_JS__", controls)
                .replace("__LOGIC_JS__", logic)
                .replace("__BRIDGE_JS__", bridge))
    out = os.path.join(ROOT, "plugin", "resources", "index.html")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(page)
    print("[build_page] wrote plugin/resources/index.html (%d KB)" % (len(page) // 1024))


if __name__ == "__main__":
    main()
