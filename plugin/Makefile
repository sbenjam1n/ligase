#!/usr/bin/make -f
# plugin/Makefile — ligase~ as a plugin (DPF). Builds VST3 / VST2 / CLAP / LV2 / JACK standalone
# (+ AU on macOS). The engine (src/*.c) is compiled UNMODIFIED through the Pd shim.
#
#   make            everything for this OS
#   make jack       standalone (needs JACK); make vst3 / make clap / make lv2 / make vst2 / make au
#   make ui         (re)generate resources/index.html from the panel sources
#   make test       the engine identity gate (plugin/tests) + the CLAP host smoke test
#
# Requires: the DPF submodule (git submodule update --init), python3, a C/C++ toolchain.
# Linux GUI runtime: webkit2gtk (dlopened at runtime — not a build dependency).

NAME = ligase

ENGINE_SRCS = $(notdir $(wildcard engine/*.c))
FILES_DSP = LigasePlugin.cpp ligase_engine.c pdshim.c $(ENGINE_SRCS:%=engine/%)
FILES_UI  = LigaseUI.cpp

UI_TYPE = webview
# the DGL stub library is built by a sub-make: it must see the same web-view switch
export USE_WEB_VIEW = true
CLAP_BUNDLE = true
VST2_BUNDLE = true

DPF_PATH = dpf
DPF_BUILD_DIR = build
DPF_TARGET_DIR = bin
include dpf/Makefile.plugins.mk

BUILD_C_FLAGS   += -I../src -I. -std=gnu11 -Wno-unused-function -Wno-unused-parameter
BUILD_CXX_FLAGS += -I../src -I. -Wno-unused-parameter -Wno-deprecated-declarations
LINK_FLAGS      += -lm

TARGETS = jack lv2 vst2 vst3 clap
ifeq ($(MACOS),true)
TARGETS += au
endif

# ---- the web-view page: one self-contained index.html per bundle ------------------------------
RESOURCES = resources/index.html
PAGE_DEPS = ui/build_page.py ui/bridge.js ui/page_template.html ../docs/ui/panel_layout.py ../docs/ui/emit_web.py \
            ../docs/ui/emit_plugin.py ../docs/ui/ligase_synthi_panel.svg ../web/ligase_panel_logic.js

$(RESOURCES): $(PAGE_DEPS)
	python3 ../docs/ui/emit_plugin.py > /dev/null
	python3 ../docs/ui/emit_web.py > /dev/null
	python3 ui/build_page.py

ui: $(RESOURCES)

ifeq ($(MACOS_APP_BUNDLE),true)
jackfiles += $(TARGET_DIR)/$(NAME).app/Contents/Resources/index.html
else
jackfiles += $(TARGET_DIR)/resources/index.html
endif
lv2files  += $(TARGET_DIR)/$(NAME).lv2/resources/index.html
clapfiles += $(TARGET_DIR)/$(NAME).clap/resources/index.html
vst2files += $(TARGET_DIR)/$(NAME).vst/resources/index.html
vst3files += $(TARGET_DIR)/$(NAME).vst3/Contents/Resources/index.html
ifeq ($(MACOS),true)
aufiles   += $(TARGET_DIR)/$(NAME).component/Contents/Resources/index.html
endif

all: $(TARGETS) $(jackfiles) $(lv2files) $(clapfiles) $(vst2files) $(vst3files) $(aufiles)

%/index.html: $(RESOURCES)
	-$(SILENT)$(shell mkdir -p "$(shell dirname $(abspath $@))")
	install -m 644 $< $(abspath $@)

test:
	$(MAKE) -C tests
	$(MAKE) -C tests clap NAME=$(NAME) TARGET_DIR=$(abspath $(TARGET_DIR))

.PHONY: ui test
