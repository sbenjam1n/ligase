# Makefile for ligase~ fuzzing
# Compiles both ligase~ and ligase_fuzzer~ with sanitizer instrumentation

CC = gcc
PD_INCLUDE = /usr/include/pd

# Sanitizer flags for fuzzing build
SANITIZER_FLAGS = -fsanitize=address,undefined \
                  -fno-omit-frame-pointer \
                  -fno-sanitize-recover=all \
                  -g -O1

# Coverage-guided fuzzing (optional, for future libFuzzer integration)
# COVERAGE_FLAGS = -fsanitize-coverage=trace-pc-guard

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

# Ligase sources (to be compiled with sanitizers)
LIGASE_SRCS = $(LIGASE_SRC_DIR)/ligase.c

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

# Build instrumented ligase~ (copy from main build if it exists, else warn)
$(LIGASE_TARGET): | $(BUILD_DIR)
	@if [ -f ../ligase~.pd_linux ]; then \
		echo "Copying and rebuilding ligase~ with sanitizers..."; \
		$(MAKE) -C .. ligase~.pd_linux CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"; \
		cp ../ligase~.pd_linux $@; \
		echo "Built instrumented ligase~: $@"; \
	else \
		echo "WARNING: ../ligase~.pd_linux not found. Build ligase~ first."; \
		echo "Run: make -C .."; \
		exit 1; \
	fi

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
