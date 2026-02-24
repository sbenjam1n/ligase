# Makefile for ligase~ fuzzing
# Compiles both ligase~ and ligase_fuzzer~ with sanitizer instrumentation

CC = gcc
PD_INCLUDE = /usr/include/pd

# Sanitizer flags for fuzzing build
SANITIZER_FLAGS = -fsanitize=address,undefined \
                  -fno-omit-frame-pointer \
                  -fno-sanitize-recover=all \
                  -g -O1

# Standard Pure Data external flags
PD_CFLAGS = -DPD -I$(PD_INCLUDE) -Wall -W -Wstrict-prototypes \
            -Wno-unused -Wno-parentheses -Wno-switch -fPIC

# Combined flags
CFLAGS = $(PD_CFLAGS) $(SANITIZER_FLAGS)
LDFLAGS = $(SANITIZER_FLAGS) -shared -lm -fPIC

# Source directories
SRC_DIR = src
LIGASE_SRC_DIR = ../src
BUILD_DIR = build

# Fuzzer sources
FUZZER_SRCS = $(SRC_DIR)/ligase_fuzzer.c \
              $(SRC_DIR)/corpus_seeds.c \
              $(SRC_DIR)/corpus_selection.c

# All ligase~ source files (compiled directly with sanitizers)
LIGASE_SRCS = $(LIGASE_SRC_DIR)/ligase~.c \
              $(LIGASE_SRC_DIR)/envelope.c \
              $(LIGASE_SRC_DIR)/grain.c \
              $(LIGASE_SRC_DIR)/grain_delay.c \
              $(LIGASE_SRC_DIR)/grain_delay_stut.c \
              $(LIGASE_SRC_DIR)/grain_delay_bencina.c \
              $(LIGASE_SRC_DIR)/grain_distortion.c \
              $(LIGASE_SRC_DIR)/grain_moogladder.c \
              $(LIGASE_SRC_DIR)/grain_fog.c \
              $(LIGASE_SRC_DIR)/kiss_fft.c \
              $(LIGASE_SRC_DIR)/kiss_fftr.c \
              $(LIGASE_SRC_DIR)/reel.c \
              $(LIGASE_SRC_DIR)/splice.c \
              $(LIGASE_SRC_DIR)/perlin.c \
              $(LIGASE_SRC_DIR)/sphere.c

LIGASE_OBJS = $(addprefix $(BUILD_DIR)/,$(notdir $(LIGASE_SRCS:.c=.o)))

# Output
FUZZER_TARGET = ligase_fuzzer~.pd_linux
LIGASE_TARGET = ligase~_instrumented.pd_linux

.PHONY: all clean test help

all: $(FUZZER_TARGET) $(LIGASE_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build fuzzer external
$(FUZZER_TARGET): $(FUZZER_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FUZZER_SRCS)
	@echo "Built fuzzer: $@"

# Compile each ligase~ source with sanitizers into build/
$(BUILD_DIR)/%.o: $(LIGASE_SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I/usr/local/include/pd -c $< -o $@

# Link instrumented ligase~ from sanitized object files
$(LIGASE_TARGET): $(LIGASE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(LIGASE_OBJS)
	@echo "Built instrumented ligase~: $@"

clean:
	rm -f $(FUZZER_TARGET) $(LIGASE_TARGET)
	rm -rf $(BUILD_DIR)
	rm -f crashes/*.bin
	@echo "Cleaned fuzzing build artifacts"

test: all
	@echo "Running basic fuzzer test..."
	@echo "Make sure Pure Data is installed: sudo apt-get install puredata"
	@echo "Run: pd -nogui patches/fuzzer_test.pd"

help:
	@echo "ligase~ Fuzzing Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all             - Build fuzzer and instrumented ligase~ (default)"
	@echo "  clean           - Remove build artifacts"
	@echo "  test            - Run basic fuzzer test"
	@echo "  help            - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  1. Build: make"
	@echo "  2. Run: ./run_fuzzer.sh"
	@echo "  3. Check crashes in crashes/ directory"
	@echo ""
	@echo "Sanitizers enabled:"
	@echo "  - AddressSanitizer (detects memory errors)"
	@echo "  - UndefinedBehaviorSanitizer (detects UB)"
	@echo ""
	@echo "Environment variables:"
	@echo "  CC              - C compiler (default: gcc)"
	@echo "  PD_INCLUDE      - Pure Data include path (default: /usr/include/pd)"
