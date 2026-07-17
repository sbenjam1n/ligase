#!/usr/bin/env bash
# assemble_site.sh — gather the deployable static site into web/site/ (Plans/web_build.md Step 5).
# Everything the browser needs, flat, so it serves off plain GitHub Pages with no COOP/COEP:
#   index.html, ligase-host.js, ligase-processor.js, ligase_controls.js (emit_web output),
#   ligase_wasm.js/.wasm (TARGET=worklet build), the engine patch(es), ligase.conf.
# web/site/ is a build output (gitignored). Run after build_wasm.sh TARGET=worklet + emit_web.py.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SITE="$HERE/site"
rm -rf "$SITE"; mkdir -p "$SITE"

need() { [ -f "$1" ] || { echo "assemble_site: missing $1 — run the build first" >&2; exit 1; }; }
for f in index.html ligase-host.js ligase-processor.js ligase_controls.js ligase_wasm.js ligase_wasm.wasm; do
  need "$HERE/$f"; cp "$HERE/$f" "$SITE/"
done
# Engine patch + optional grain cap. index.html fetches ./ligase_panel.pd (flat).
need "$ROOT/pd/ligase_panel.pd"; cp "$ROOT/pd/ligase_panel.pd" "$SITE/"
# The panel references its paired sub-patches; ship them if present (harmless if unused).
for p in ligase_xpndr.pd ligase_seq.pd; do [ -f "$ROOT/pd/$p" ] && cp "$ROOT/pd/$p" "$SITE/"; done
[ -f "$ROOT/ligase.conf" ] && cp "$ROOT/ligase.conf" "$SITE/"
# A no-jekyll marker so GitHub Pages serves files/dirs starting with underscores verbatim.
touch "$SITE/.nojekyll"

echo "[assemble_site] wrote $(realpath --relative-to="$ROOT" "$SITE")/ ($(ls -1 "$SITE" | wc -l) files)"
ls -1 "$SITE"
