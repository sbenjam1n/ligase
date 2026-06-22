# Plan B2: Reel Load/Save on macOS (path resolution + robust WAV I/O)

**Owner:** SLB
**Date:** 2026-06-16
**Status:** ✅ DONE (2026-06-16; archived 2026-06-22). Core (canvas-relative load/save paths) + polish (robust chunk-walking WAV parser, 16-bit PCM accepted, read/write checks, failures routed via `pd_error`) implemented and verified headless — float round-trip w/ cue, 16-bit load, clear errors (QUEUE §6 Seq 11/14). (Header below predates completion; left as the original plan record.)
**Tracked in:** `QUEUE.md` §1 (AGENT lane, B2).
**Related:** Plan B1 (`sample_rate_buffering.md`) — the WAV-header sample-rate fix (§Fix-5) is shared; coordinate edits to `reel_load_wav`/`reel_save_wav`.

## Problem
The `load <file>` and `save <file>` messages (read/write 48 kHz 32-bit-float stereo WAV) do not work on macOS.

## Root cause (3-agent scan, 2026-06-16; file:line verified)
Ranked:
1. **No canvas-relative path resolution; no stored canvas.** `ligase_load`/`ligase_save` pass the raw `s->s_name` straight to `fopen` (`ligase~.c:1700,1708` → `reel.c:246,363`). A repo-wide grep finds **zero** uses of `canvas_makefilename` / `canvas_open` / `open_via_path` / `canvas_getcurrent`; `struct _ligase` (`ligase~.c:99-280`) has **no `t_canvas*` field** and `ligase_new` (`ligase~.c:4329`) never captures one. On macOS, Pd.app launched from Finder has CWD `/` (or the read-only app bundle), so a **relative** `save foo.wav`→ EROFS/EPERM and `load foo.wav`→ ENOENT. On Linux-from-terminal CWD is the project dir (why it "worked"); an **absolute** `/tmp/x.wav` bypasses resolution (why absolute paths work). The `is_path_safe()` guard also **rejects any path containing `..`** (`reel.c:234-239`), blocking relative workarounds. **This is the bug.**
2. **Silent failure on macOS.** Both reel functions report via `fprintf(stderr, …)` (`reel.c:243-366`), invisible under a Finder-launched Pd.app; the wrapper only emits a generic `pd_error` "failed to load/save" (`ligase~.c:1703,1711`) — the real reason is lost.
3. **Hardcoded 48000 in the writer + strict 48000 reject in the reader** (`reel.c:385` writes `SAMPLE_RATE`; `reel.c:270-274` rejects ≠`SAMPLE_RATE`; `types.h:323`). A non-48k session writes a mislabeled file and can't reload it. (Shared with Plan B1.)
4. **Brittle WAV parsing.** Reader is **format-3 (32-bit float) only** (`reel.c:260-264`), assumes a fixed 44-byte header with `data` right after a 16-byte `fmt ` (no chunk scanning), so files with extra chunks (`LIST`/`fact`/`bext`) or `WAVE_FORMAT_EXTENSIBLE` misparse; the sample `fread` return is ignored (`reel.c:288`) → truncated files load garbage tails. Not macOS-specific but real.

## Fix surface
- **F1 Store the canvas.** Add `t_canvas *x_canvas;` to `struct _ligase`; set `x->x_canvas = canvas_getcurrent();` in `ligase_new`.
- **F2 Resolve on load.** `int fd = canvas_open(x->x_canvas, s->s_name, ".wav", dir, &name, MAXPDSTRING, 0);` build the resolved absolute path (or `fdopen`) and pass it to `reel_load_wav` (which keeps its `fopen`). Anchors relative names to the patch dir + honors Pd's search path.
- **F3 Resolve on save.** `char buf[MAXPDSTRING]; canvas_makefilename(x->x_canvas, s->s_name, buf, MAXPDSTRING);` append `.wav` if missing; pass `buf` to `reel_save_wav`.
- **F4 Relax `is_path_safe`** (`reel.c:234`) so it no longer blocks resolved/absolute paths; canvas resolution makes the crude `..` check unnecessary and harmful.
- **F5 Real error reporting** — route the specific failure reason (open failed / wrong rate / wrong format / short read) through `pd_error(x, …)` instead of `stderr`. *(Coordinate header-SR with Plan B1.)*
- **F6 Robust parsing (optional, recommended)** — walk chunk IDs to find `fmt `/`data`; honor non-16 `fmt_size`; check the `fread` return; consider accepting 16-bit PCM (convert) so common WAVs load.

## Steps & gates
- **GATE A (approval)** — confirm scope; decide F6 ambition (path-fix only vs full robust parser); decide whether file I/O stays 48k-canonical (resample) or follows the engine rate (ties to Plan B1's reel-sizing decision). Needs owner.
- **Step 1 → GATE B** — F1–F4 (canvas capture + load/save resolution + relax guard). This alone restores load/save on macOS.
- **Step 2 → GATE C** — F5 error reporting + F6 parser robustness + header-SR (with B1).
- **Step 3 → GATE D (verify)** — on macOS, **launch Pd from Finder** (CWD `/`), open a patch in an arbitrary dir, `save foo.wav` → file lands next to the patch; `load foo.wav` round-trips; a bad path produces a clear Pd-window error. Headless check: `cd / && pd -nogui <patch>` saving a relative name lands it in the patch dir, not `/`.

## Acceptance criteria
Relative `load`/`save` work from a Finder-launched Pd.app (resolved to the patch dir); absolute paths still work; failures show an actionable message in the Pd window; round-trip recordings reload faithfully.
