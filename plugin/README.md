# plugin/ — ligase~ as a DAW plugin

VST3 · VST2 · CLAP · LV2 · JACK standalone · AU (macOS), built with the [DPF](https://github.com/DISTRHO/DPF)
submodule. The Pure Data engine in `../src` is compiled unmodified through `pdshim.c` and driven by
`ligase_engine.c`; the GUI is the SVG control surface in a web view (shared with the browser build).

```bash
git submodule update --init --recursive
make -C tests            # engine identity gate: the hosted engine == the Pd baseline, byte for byte
make -j8                 # -> bin/  (every format for this OS)
make -C tests clap       # headless DAW-style smoke test of the built CLAP
```

Full documentation: [`../docs/plugin_build.md`](../docs/plugin_build.md). Surface/engine contract:
[`../docs/ui/panel_bridge.md`](../docs/ui/panel_bridge.md) and [`../docs/ui/ui_sections.md`](../docs/ui/ui_sections.md).
