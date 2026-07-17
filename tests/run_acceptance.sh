#!/bin/sh
# run_acceptance.sh — headless acceptance runner for the five backfilled feature suites:
#   polyphony, expander, source_shapes, scope_taps, harmonic.
#
# It drives every DETERMINISTIC test — the self-asserting patches (which print PASS/FAIL
# in-patch), the polyphony grain-increment stderr metrics, and the harmonic schema-v5 md5
# round-trip — and prints a per-test PASS/FAIL line plus a final tally. Exits non-zero if
# any test fails.
#
# The AUDIO-SHAPE patches (Lorenz butterfly / sine ramp / grain constellation / grainsum /
# rho-chaos / sphere-spin / scale polygon) are NOT run here: they assert a captured WAV shape
# that needs sox/python to measure. Each is documented with its expected metric + a one-line
# check in the per-feature README.md. This script depends only on `pd`, `grep`, `md5sum`.
#
# Usage:  sh tests/run_acceptance.sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
PD=${PD:-pd}
TMP=$(mktemp -d)
PASS=0
FAIL=0

# run_pd <extra pd args...> <patch> -> writes stderr to $TMP/out
run_pd() {
    patch=$1; shift
    pkill -9 pd 2>/dev/null
    sleep 0.3
    timeout -s KILL 12s "$PD" -nogui -nosound -stderr -path . "$@" "$patch" >"$TMP/out" 2>&1
}

report() { # <name> <ok?0/1> <detail>
    if [ "$2" = 1 ]; then PASS=$((PASS+1)); printf 'PASS  %-42s %s\n' "$1" "$3";
    else FAIL=$((FAIL+1)); printf 'FAIL  %-42s %s\n' "$1" "$3"; fi
}

# --- self-asserting patches: the patch prints "<LABEL>_PASS: bang" / "<LABEL>_FAIL: ..." ---
selfassert() { # <patch> <expected count of *_PASS lines>
    run_pd "$1"
    got=$(grep -cE '_PASS: bang$|^PASS: bang$' "$TMP/out")
    bad=$(grep -cE '_FAIL: |^FAIL: ' "$TMP/out")
    name=$(basename "$1")
    if [ "$got" -ge "$2" ] && [ "$bad" -eq 0 ]; then report "$name" 1 "($got/$2 asserts PASS)";
    else report "$name" 0 "($got/$2 PASS, $bad FAIL) -- see $TMP/out"; fi
}

echo "== expander (self-asserting) =="
selfassert tests/expander/snapbuf_from_live_get.pd   1
selfassert tests/expander/snapbuf_set_get.pd         1
selfassert tests/expander/snapbuf_cold_edit.pd       1
selfassert tests/expander/snapbuf_store_recall.pd    1
selfassert tests/expander/snapbuf_audition_hear.pd   1
selfassert tests/expander/snapbuf_audition_revert.pd 1

echo "== source_shapes (self-asserting param capture) =="
selfassert tests/source_shapes/shapes_params_capture.pd 5

echo "== scope_taps (self-asserting idle beam) =="
selfassert tests/scope_taps/scope_grain_idle.pd 2

echo "== harmonic (self-asserting capture) =="
selfassert tests/harmonic/harmonic_capture.pd 4

# --- polyphony: no outlet exposes voice count, so assert the per-grain increments the
#     engine logs to stderr ("grain final #N ... inc=<x>"). Presence/absence of transposition
#     increments proves the voice set. ---
echo "== polyphony (grain-increment stderr metric) =="

# chord 60 64 67 -> three simultaneous voices at inc 1.0000 / 1.2599 / 1.4983
run_pd tests/polyphony/poly_chord_voices.pd
inc=$(grep -oE 'inc=[0-9.]+' "$TMP/out" | head -8 | sort -u | tr '\n' ' ')
case "$inc" in *1.0000*) a=1;; *) a=0;; esac
case "$inc" in *1.2599*) b=1;; *) b=0;; esac
case "$inc" in *1.4983*) c=1;; *) c=0;; esac
if [ "$a$b$c" = 111 ]; then report poly_chord_voices 1 "3 voices: $inc"; else report poly_chord_voices 0 "want 1.0000/1.2599/1.4983 got: $inc"; fi

# mono (poly 0) + midi 64 -> single scalar voice: only inc 1.2599, no 1.0000 / 1.4983
run_pd tests/polyphony/poly_mono_scalar.pd
inc=$(grep -oE 'inc=[0-9.]+' "$TMP/out" | head -8 | sort -u | tr '\n' ' ')
case "$inc" in *1.2599*) b=1;; *) b=0;; esac
case "$inc" in *1.0000*|*1.4983*) x=1;; *) x=0;; esac
if [ "$b" = 1 ] && [ "$x" = 0 ]; then report poly_mono_scalar 1 "single voice: $inc"; else report poly_mono_scalar 0 "want only 1.2599 got: $inc"; fi

# steal: chord of 9 (60..68), pool caps at 8, oldest (note 60=inc 1.0000) stolen; 61=1.0595 survives
run_pd tests/polyphony/poly_steal.pd
inc=$(grep -oE 'inc=[0-9.]+' "$TMP/out" | head -8 | sort -u | tr '\n' ' ')
ndist=$(grep -oE 'inc=[0-9.]+' "$TMP/out" | head -8 | sort -u | grep -c .)
case "$inc" in *1.0595*) s=1;; *) s=0;; esac
case "$inc" in *1.0000*) o=1;; *) o=0;; esac
if [ "$s" = 1 ] && [ "$o" = 0 ] && [ "$ndist" -eq 8 ]; then report poly_steal 1 "8 voices, note60 stolen: $inc"; else report poly_steal 0 "want 8 distinct, 1.0595 present, 1.0000 absent got($ndist): $inc"; fi

# release: chord of 62/65/69, release 65 (inc 1.3348) -> only 62=1.1225 and 69=1.6818 remain
run_pd tests/polyphony/poly_release.pd
inc=$(grep -oE 'inc=[0-9.]+' "$TMP/out" | head -8 | sort -u | tr '\n' ' ')
case "$inc" in *1.1225*) p=1;; *) p=0;; esac
case "$inc" in *1.6818*) q=1;; *) q=0;; esac
case "$inc" in *1.3348*) r=1;; *) r=0;; esac
if [ "$p" = 1 ] && [ "$q" = 1 ] && [ "$r" = 0 ]; then report poly_release 1 "note65 released: $inc"; else report poly_release 0 "want 1.1225+1.6818, no 1.3348 got: $inc"; fi

# --- harmonic schema v5: morph_save -> morph_load -> morph_save is byte-stable (md5) ---
echo "== harmonic (schema v5 save/load/save md5) =="
rm -f /tmp/harm_v5_a.morph /tmp/harm_v5_b.morph
run_pd tests/harmonic/harmonic_schema_v5.pd -path /tmp
ma=$(md5sum /tmp/harm_v5_a.morph 2>/dev/null | cut -d' ' -f1)
mb=$(md5sum /tmp/harm_v5_b.morph 2>/dev/null | cut -d' ' -f1)
loaded=$(grep -c 'morph loaded from' "$TMP/out")
if [ -n "$ma" ] && [ "$ma" = "$mb" ] && [ "$loaded" -ge 1 ]; then report harmonic_schema_v5 1 "md5 stable $ma"; else report harmonic_schema_v5 0 "a=$ma b=$mb loaded=$loaded"; fi

# --- primase pairing (Plans/web_build.md Arc B): primase = master rhythm clock driving
#     ligase's stut. primase is a SEPARATE external repo; locate its built binary at the repo
#     root (the bundle/CI drop point), a $PRIMASE_PD override, or a /workspace/primase sibling
#     checkout. SKIP gracefully (does not fail the suite) when primase is not available. ---
echo "== primase pairing (Arc B: primase master clock -> ligase stut) =="
PRIMASE_PATH=""
for d in "$ROOT" "${PRIMASE_PD:-}" /workspace/primase; do
    [ -n "$d" ] || continue
    if [ -f "$d/primase.pd_linux" ] || [ -f "$d/primase.pd_darwin" ]; then PRIMASE_PATH=$d; break; fi
done
if [ -z "$PRIMASE_PATH" ]; then
    printf 'SKIP  %-42s %s\n' primase_pairing "(no primase external; set PRIMASE_PD or drop primase.pd_linux at repo root)"
else
    # 8 one-shot events -> 8 primase bangs -> 8 ligase stut triggers (1:1); clock-lock (delay
    # quantization tracking primase's grid) present after the 2-bang BPM warmup; the patch also
    # self-asserts PRIMASE_N_PASS iff exactly 8 events fired.
    run_pd tests/primase/pair_acceptance.pd -path "$PRIMASE_PATH"
    bangs=$(grep -c 'PRIMASE_EV: bang' "$TMP/out")
    stut=$(grep -c 'stut triggered' "$TMP/out")
    locked=$(grep -c 'quantized spacing' "$TMP/out")
    npass=$(grep -cE 'PRIMASE_N_PASS: bang$' "$TMP/out")
    nfail=$(grep -cE 'PRIMASE_N_FAIL: ' "$TMP/out")
    if [ "$stut" -eq 8 ] && [ "$bangs" -eq 8 ] && [ "$npass" -ge 1 ] && [ "$nfail" -eq 0 ] && [ "$locked" -ge 1 ]; then
        report primase_pairing 1 "8 events -> 8 stut (1:1), $locked clock-locked, N_PASS"
    else
        report primase_pairing 0 "want 8 stut/8 bangs/N_PASS/no-FAIL/locked>0 got stut=$stut bangs=$bangs npass=$npass nfail=$nfail locked=$locked -- see $TMP/out"
    fi
fi

pkill -9 pd 2>/dev/null
rm -rf "$TMP"
echo "--------------------------------------------------------------"
echo "TOTAL: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
