# plugin/tests/Makefile — standalone engine tests for the hosted (Pd-shim) build of ligase~.
# No DPF, no Pd: compiles src/*.c + plugin/pdshim.c + plugin/ligase_engine.c into a test binary.
#   make            build + run the identity gate (reproduces AUTOMATED_TEST_PROCEDURE.md)
#   make NATIVE=/path/to/native_reel.wav   additionally require byte-identity with a native reel
CC      ?= gcc
ROOT    := $(abspath ../..)
SRC     := $(ROOT)/src
PLUGIN  := $(ROOT)/plugin
BUILD   := build
CFLAGS  ?= -O2 -fPIC -Wall -Wno-unused-function
CFLAGS  += -I$(SRC) -I$(PLUGIN) -std=gnu11
ENGINE_SRCS := $(wildcard $(SRC)/*.c)
SHIM_SRCS   := $(PLUGIN)/pdshim.c $(PLUGIN)/ligase_engine.c
OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/src_%.o,$(ENGINE_SRCS)) $(patsubst $(PLUGIN)/%.c,$(BUILD)/plugin_%.o,$(SHIM_SRCS))
NATIVE ?=

all: run

$(BUILD)/src_%.o: $(SRC)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/plugin_%.o: $(PLUGIN)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/identity_test: identity_test.c $(OBJS)
	$(CC) $(CFLAGS) identity_test.c $(OBJS) -o $@ -lm -lpthread

run: $(BUILD)/identity_test
	./$(BUILD)/identity_test $(abspath $(BUILD)) $(NATIVE)

clean:
	rm -rf $(BUILD)
.PHONY: all run clean

# ---- CLAP host smoke test against the built plugin binary ----
CLAP_BIN ?= $(abspath ../bin/ligase.clap/ligase.clap)
$(BUILD)/clap_host_test: clap_host_test.c
	@mkdir -p $(BUILD)
	$(CC) -O1 -Wall -std=gnu11 -I$(PLUGIN)/dpf/distrho/src clap_host_test.c -o $@ -ldl -lm
clap: $(BUILD)/clap_host_test
	./$(BUILD)/clap_host_test $(CLAP_BIN)
.PHONY: clap
