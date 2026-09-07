# Panel bridge contract — one control surface, two hosts (browser + plugin)

_The SVG panel (`docs/ui/panel_layout.py` → `emit_svg.py`) is rendered by ONE JavaScript
surface (`docs/ui/emit_web.py` → `web/ligase_controls.js`) whose behaviour lives in ONE
hand-written brain (`web/ligase_panel_logic.js`). The brain talks to an engine through a tiny
**bridge** object. Two bridges exist: the browser (`web/ligase-host.js`, libpd/WASM) and the
plugin (`plugin/ui/bridge.js`, DPF web view). Nothing panel-shaped lives in either host._

## Layers

| Layer | File | Owner | Role |
|---|---|---|---|
| widgets | `web/ligase_controls.js` (GENERATED — never edit) | `emit_web.py` | draws the SVG backdrop + live overlay; every gesture calls `surface.set(id, value)` / `surface.bang(id)`; returns `handles[id].set(value)` (visual update, NO send) |
| brain | `web/ligase_panel_logic.js` | hand-written | `createPanel(bridge, CONTROLS, tables)` → `surface`; implements every `bind` kind of `panel_layout.py` incl. the `special` wiring ported from `emit_pd.py` |
| bridge | `web/ligase-host.js` (`webBridge(engine)`) · `plugin/ui/bridge.js` | per host | moves control data to/from the engine |

## The bridge object (what the brain sees)

```js
bridge.msg(text)                 // engine message(s) in text form; ';' separates several
                                 //   "grainsize 0.25"  "matrix_connect lorenz1 moog_cutoff 500"  "pattern pitch [ 0 4 7 ]"
bridge.control(id, value, text)  // a BOUND panel control changed: id = panel_layout id, value = ENGINE units,
                                 //   text = the message the brain would send (null for inlet binds).
                                 //   web:    inlet binds -> lgR_<id> (the patch's line~ chain); others -> bridge.msg(text)
                                 //   plugin: controls that are host parameters -> setParameterValue; others -> msg(text)
bridge.note(ch, note, vel)       // optional: MIDI note (on-screen keyboard / computer keys)
bridge.on(event, cb)             // subscribe; events:
//  'status'  cb(st)   ~10-30 Hz  st = { splice, splices, spliceStart, spliceEnd, playing, recording, recMode,
//                                     bpm, reelLen, reelSr, voices, activeGrains, snapshotMask (array of 64 0/1),
//                                     snapbufHas, morph:{x,y,points,route,running} }   (fields may be missing on the web)
//  'out9'    cb(selector, args)  outlet-9 replies: "snapbuf" [field, sub?, values...], "morph_state" lines,
//                                get_params lines ("grainsize" [v], "splice" [cur, count], "playing" [0|1] ...)
//  'vu'      cb(l, r)            output peaks 0..1+
//  'scope'   cb(x, y)            Float32Arrays, one XY window
//  'control' cb(id, value)       the HOST changed a control (automation / preset) -> brain updates the widget only
//  'print'   cb(text, isError)   console
```

## Value conventions
* Inlet-bound and msg-bound knobs carry **engine units** end to end (`lo/hi` in `CONTROLS`).
  The brain never rescales them; the SOURCE SHAPE A–D knobs are the one place the brain maps a
  0..1 knob into per-family engine ranges (`SHAPE_MEANINGS`).
* `msgmap` switches send the mapped message for the selected index (`None` = send nothing).
* `toggle` → `<sel> 0|1`; `bang` → the literal message.
* Snapshot slot ids: panel slots 1-32 → engine `snapshot 0-31`. **Slot 63 is reserved** for the
  plugin's live-voice capture (state save) — never expose it on the panel.

## Special binds (ported from `docs/ui/emit_pd.py`; the brain MUST match these)
* `recmode` (stored) + `record` (toggle): ON → `recinput` | `recsplice` | `record 1` by mode 0/1/2; OFF → `record 0`.
* `play` toggle → `play 1` / `play 0` (bare `play` STOPS — never send it).
* `splice_prev/next` → `shift -1` / `shift 1`; `splice_enter` → jump to splice `splice_data`
  (`shift (N - current)` using the live `status.splice`, falling back to `splice_finish_nav` semantics
  only if no status is available); `splice_led` shows `status.splice` (0-based, printed 1-based? NO — print
  the engine's 0-based index exactly like the Pd panel's counter did: the LED shows the index).
* `snapN` selects slot N-1; `snap_store` → `snapshot <slot>`; `snap_recall` → `snapshot_recall <slot>`;
  `morph_snap` (SNAP) = `snapshot <slot>` + place on the metasurface (`morph_point <slot> <x> <y>`).
* `dist_preset` (1-8) → the `DIST_PRESETS` message bundle.
* SOURCE SHAPE: `shape_family` × `shape_inst` route RATE/A/B/C/D per `SHAPE_MEANINGS`
  (`inst2` = `<sel> <inst> <v>`, `suffix` = `<sel>_<inst> <v>`, `global` = `<sel> <v>`; a knob with no
  entry for the family sends nothing); KICK (sphere only) → `sphere_kick <inst> <s> <s> <s>`, s = D×5;
  RESET → `SHAPE_RESET[family] <inst>`; MODE → `SHAPE_MODE[family] <inst> <mode>`.
  NEW (was a documented gap): while `scope_tap` = FOLW, a FAMILY/INST change also sends
  `scope_tap <family-token> <inst>` (sine/saw/square/perlin/lorenz/nbody/sphere/rand/folw).
* MATRIX pins `mx_<i>_<j>`: ON → `matrix_connect <src> <dst> <depth×(1-2×pol)>`; OFF → `matrix_disconnect <src> <dst>`;
  `mx_depth` (0..4, default 1) and `mx_pol` (0=+ / 1=−) are panel-side policy values.
* XPNDR: `xp_load` → `snapbuf_load <slot>`; `xp_fromlive` → `snapbuf_from_live`; PAGE×PARAM → address
  `XPNDR_FIELDS[page][param]` = (value_field, band_field) and query `snapbuf_get <value_field>` (+ band);
  `xp_value` → `snapbuf_set <value_field> <v>`; MIN/MAX/SLEW/ENABLED/INVERT/INST → `snapbuf_set <band_field> <sub> <v>`
  (subs: min max slew enabled invert rand_instance); SOURCE → `snapbuf_set <band_field> rand_type <code>` with
  `XPNDR_SOURCE_CODES`; STORE → `snapbuf_store <slot>`; ASSIGN → `snapbuf_apply`; AUDITION → `snapbuf_audition 0|1`;
  A/B → `snapbuf_compare`. NEW: MIN/MAX are scaled into the band field's natural range (`XPNDR_BAND_RANGES`)
  instead of raw 0..1 (the documented XPNDR seam); the VALUE LED shows the `snapbuf` reply for the addressed field.
* SEQ/SCALE: tone ring toggles compose the ascending pitch-class list; APPLY → `pitch_scale <deg…>` /
  `smear_pitch_scale …` + `scale_root <v>` / `smear_scale_root …` + `scale_rotate <v>` / `smear_scale_rotate …`
  routed by `seq_dest` (GRAIN/SMEAR/BOTH); PRESET lights the ring per `SEQ_PRESET_PCS`; slots A–P →
  `pitch_scale_slot <i>` (+ smear twin); AXIS→SLOTS → `pitch_scale_to 0 <deg…>` + `pattern scale_root [ shifts ]`
  (REV reverses, ALT>0 uses `< … >`), + smear twins; TIME circle: K×N → euclid token from `SEQ_EUCLID_PRESETS`
  (`1(k,n)`, default `1(3,8)`), TARGET → `pattern <SEQ_TIME_TARGETS[t]> <token>`; GRID rows → `pattern <field> <16 values>`
  (pin × VALUE) where field = `XPNDR_FIELDS[grid_page][grid_param].value_field`.
* MORPH: the metasurface canvas (already in emit_web) owns SNAP/RUN/STOP/PAUSE, points, waypoints, base rate.
* MASTER: display-side gain (the plugin implements it as an output-gain parameter; the web applies it in the host).
* SCOPE VIEW: XY vs SWP is a pure display mode of the scope canvas (SWP = time sweep of Y).

## Readbacks the brain renders
`splice_led` ← status.splice; `xp_led` ← the loaded slot; `xp_value_led` ← last `snapbuf` reply for the
addressed value field; `seq_readout` / `seq_grid_readout` ← the last message the section sent; VU ← 'vu';
SCOPE ← 'scope'; transport lamps (RECORD/PLAY lit) ← status; preset slot lamps ← status.snapshotMask.
