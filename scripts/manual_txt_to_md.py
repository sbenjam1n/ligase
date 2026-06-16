#!/usr/bin/env python3
"""One-time conversion: src/ligase_manual.txt (Google-Docs export) -> docs/ligase_manual.md

Cleans the export artifacts so the Markdown master is the single source of truth:
  - strips zero-width / BOM / NBSP characters
  - drops the hand-typed dotted-leader Table of Contents (pandoc --toc regenerates it)
  - promotes the known ALL-CAPS section titles to level-1 Markdown headings
  - normalizes bullet glyphs (* / U+25CF / U+25CB) to Markdown list items
  - collapses runs of blank lines

Run from the repo root:  python3 scripts/manual_txt_to_md.py
This is a one-shot migration; after it, edit docs/ligase_manual.md directly.
"""
import re

SRC = "src/ligase_manual.txt"
DST = "docs/ligase_manual.md"

KNOWN_H1 = {
    "OVERVIEW", "INLETS", "OUTLETS", "MESSAGES", "PLAYBACK CONTROL",
    "RECORDING CONFIGURATION", "SPLICE NAVIGATION", "PLAYHEAD MODES",
    "GRAIN PARAMETERS", "TIMING & QUANTIZATION", "PITCH & SPEED", "DELAY",
    "FOG (SPECTRAL EFFECT)", "DISTORTION", "MOOG LADDER FILTER",
    "PARAMETER RANGES & MODULATION", "QUERY STATE",
}

FRONTMATTER = '''---
title: "ligase~ — Reference Manual"
subtitle: "Granular synthesizer / sampler / looper / delay for Pure Data"
author: "Steven Benjamin"
date: "© 2025 · GNU General Public License v2"
---

'''

def main():
    src = open(SRC, encoding="utf-8").read()
    for ch in ("﻿", "​", "‌", "‍"):
        src = src.replace(ch, "")
    src = src.replace(" ", " ")
    lines = [ln.rstrip() for ln in src.split("\n")]

    # drop the standalone copyright line (moves into frontmatter)
    copyright_line = "Copyright (C) 2025 Steven Benjamin - Licensed under GNU General Public License"
    lines = [ln for ln in lines if ln.strip() != copyright_line]

    # drop the dotted-leader TOC: everything from "Contents" up to the first bare "OVERVIEW"
    toc = next((i for i, ln in enumerate(lines) if ln.strip() == "Contents"), None)
    ov = next((i for i, ln in enumerate(lines) if ln.strip() == "OVERVIEW"), None)
    if toc is not None and ov is not None and ov > toc:
        lines = lines[:toc] + lines[ov:]

    out, prev_blank, i = [], False, 0
    while i < len(lines):
        s = lines[i].strip()

        # the export mangled "QUERY STATE" into ".\n STATE"
        if s == "." and i + 1 < len(lines) and lines[i + 1].strip() == "STATE":
            out += ["# QUERY STATE", ""]; i += 2; prev_blank = True; continue
        if s == "STATE":
            out.append("# QUERY STATE"); i += 1; prev_blank = False; continue
        if s in KNOWN_H1:
            out.append("# " + s); i += 1; prev_blank = False; continue

        # bullets
        m = re.match(r"^[●•]\s*(.*)$", s)
        if m:
            s = "- " + m.group(1)
        elif s.startswith("○"):
            s = "  - " + s[1:].strip()
        elif re.match(r"^\*\s+", s):
            s = re.sub(r"^\*\s+", "- ", s)

        if s == "":
            if not prev_blank:
                out.append("")
            prev_blank = True; i += 1; continue
        prev_blank = False
        out.append(s); i += 1

    body = "\n".join(out).strip() + "\n"
    open(DST, "w", encoding="utf-8").write(FRONTMATTER + body)
    print(f"wrote {DST} ({body.count(chr(10))} lines)")

if __name__ == "__main__":
    main()
