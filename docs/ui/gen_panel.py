#!/usr/bin/env python3
# Generate the ligase~ Synthi-style control-surface mockup (SVG).
#
# Thin compatibility wrapper: the layout lives in panel_layout.py (data) and the
# renderer in emit_svg.py. The working-patch emitter (emit_pd.py) consumes the
# same layout data — the SVG and the .pd can never drift.
import emit_svg

emit_svg.main()
