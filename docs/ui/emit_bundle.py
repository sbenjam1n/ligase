#!/usr/bin/env python3
# emit_bundle.py — build dist/ligase.plugdata, the drag-and-drop install bundle for
# plugdata STANDALONE (Plans/pd_panel_prototype.md Step 5).
#
# Container contract (matched against plugdata's loader, PluginEditor.cpp
# installPackage(), develop @ 2026-07): a .plugdata file is a ZIP; the installer
# extracts it and moves each TOP-LEVEL DIRECTORY into ~/Documents/plugdata/Patches/.
# Each package directory carries a meta.json (PatchInfo keys: Title, Author,
# Release date, Description, Version, FolderName, ...); "FolderName" overrides the
# default install-folder name (title-slug + author/version hash). __MACOSX litter is
# skipped by the loader. The patch's own directory is on Pd's search path, so the
# compiled external installs alongside the patches and loads with them.
#
# Boundary (per the plan): this is the STANDALONE distribution path only —
# plugdata-as-VST/AU cannot load externals at runtime and needs the compiled-in
# build (Plans/vst_plugin.md v1).
#
# Determinism gate: fixed ZIP timestamps + sorted entries + fixed compression, so
# the same inputs always produce the same bytes (sha256 printed).
#
# Usage:
#   python3 emit_bundle.py            build dist/ligase.plugdata
#   python3 emit_bundle.py --verify   build, then re-open and structurally check
import hashlib
import io
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
DIST = os.path.join(ROOT, "dist")
OUT = os.path.join(DIST, "ligase.plugdata")
PKG = "ligase"                     # the one top-level package directory

META = {
    "Title": "ligase~",
    "Author": "SLB",
    "Release date": "2026-07-06",
    "Description": "Granular tape synthesizer: the ligase~ external with its "
                   "Synthi-style control surface (ligase_panel.pd) and the "
                   "snapshot-expander sidecar (ligase_xpndr.pd). See README.md "
                   "inside the package.",
    "Version": "0.1",
    "FolderName": PKG,             # install as Patches/ligase, not the hashed slug
}

# (source path relative to repo root, name inside the package, required?)
CONTENTS = [
    ("pd/ligase_panel.pd", "ligase_panel.pd", True),
    ("pd/ligase_xpndr.pd", "ligase_xpndr.pd", True),
    ("pd/ligase_seq.pd", "ligase_seq.pd", True),
    ("pd/README.md", "README.md", True),
    ("ligase.conf", "ligase.conf", True),
    # platform externals: Pd loads whichever matches the host. At least one must
    # exist; the darwin binary only exists when the bundle is built on the Mac.
    ("ligase~.pd_linux", "ligase~.pd_linux", False),
    ("ligase~.pd_darwin", "ligase~.pd_darwin", False),
]

ZIP_DATE = (2026, 7, 6, 0, 0, 0)   # fixed timestamp -> deterministic archive


def build():
    files = []                     # (arcname, bytes)
    meta_bytes = (json.dumps(META, indent=2) + "\n").encode()
    files.append((f"{PKG}/meta.json", meta_bytes))
    externals = 0
    for src, name, required in CONTENTS:
        path = os.path.join(ROOT, src)
        if not os.path.exists(path):
            if required:
                sys.exit(f"emit_bundle: REQUIRED input missing: {src}")
            print(f"emit_bundle: note — optional input absent, not bundled: {src}")
            continue
        with open(path, "rb") as f:
            files.append((f"{PKG}/{name}", f.read()))
        if name.startswith("ligase~.pd_"):
            externals += 1
    if externals == 0:
        sys.exit("emit_bundle: no compiled external found (run `make` first; "
                 "build on the Mac to include ligase~.pd_darwin)")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for arcname, data in sorted(files):
            zi = zipfile.ZipInfo(arcname, date_time=ZIP_DATE)
            zi.external_attr = 0o644 << 16
            if arcname.endswith((".pd_linux", ".pd_darwin")):
                zi.external_attr = 0o755 << 16
            z.writestr(zi, data, zipfile.ZIP_DEFLATED, 9)
    blob = buf.getvalue()
    os.makedirs(DIST, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(blob)
    digest = hashlib.sha256(blob).hexdigest()
    names = [a for a, _ in sorted(files)]
    print(f"wrote {OUT} ({len(blob)} bytes, {len(names)} entries)")
    print(f"sha256 {digest}")
    return digest, names


def verify(names):
    """Structural check against the loader contract + patch byte-identity."""
    with zipfile.ZipFile(OUT) as z:
        entries = z.namelist()
        # 1. every entry lives under exactly one top-level package directory
        tops = {e.split("/", 1)[0] for e in entries}
        assert tops == {PKG}, f"top-level dirs {tops} != {{{PKG!r}}}"
        assert all("/" in e for e in entries), "loose file at archive root"
        # 2. meta.json parses and carries the PatchInfo keys the loader reads
        meta = json.loads(z.read(f"{PKG}/meta.json"))
        for key in ("Title", "Author", "Version", "FolderName"):
            assert meta.get(key), f"meta.json missing {key}"
        # 3. contained patch set is byte-identical to the emitted ones
        for src, name, _ in CONTENTS:
            arc = f"{PKG}/{name}"
            if arc not in entries:
                continue
            with open(os.path.join(ROOT, src), "rb") as f:
                assert z.read(arc) == f.read(), f"{arc} differs from {src}"
        # 4. at least one platform external rode along
        assert any(e.endswith((".pd_linux", ".pd_darwin")) for e in entries), \
            "no external in bundle"
    print(f"verify OK: {len(entries)} entries under {PKG}/, meta.json valid, "
          f"patches byte-identical, external present")


if __name__ == "__main__":
    digest1, names = build()
    if "--verify" in sys.argv:
        digest2, _ = build()
        assert digest1 == digest2, "bundle is not deterministic"
        print("determinism OK: rebuild sha256 identical")
        verify(names)
