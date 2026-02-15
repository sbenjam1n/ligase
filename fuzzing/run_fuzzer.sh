#!/bin/bash
# run_fuzzer.sh - Run ligase~ fuzzer with AddressSanitizer and UBSan

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== ligase~ Fuzzer ===${NC}"
echo ""

# Check if fuzzer is built
if [ ! -f "ligase_fuzzer~.pd_linux" ]; then
    echo -e "${RED}Error: ligase_fuzzer~.pd_linux not found${NC}"
    echo "Run 'make' to build the fuzzer first"
    exit 1
fi

# Check if Pure Data is installed
if ! command -v pd &> /dev/null; then
    echo -e "${RED}Error: Pure Data (pd) not found${NC}"
    echo "Install with: sudo apt-get install puredata"
    exit 1
fi

# Create crashes directory
mkdir -p crashes

# Set AddressSanitizer options
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:symbolize=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1"

# Set UndefinedBehaviorSanitizer options
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

# Set log paths
LOG_DIR="crashes"
ASAN_LOG="$LOG_DIR/asan_$(date +%Y%m%d_%H%M%S).log"
PD_LOG="$LOG_DIR/pd_output_$(date +%Y%m%d_%H%M%S).log"

echo -e "${YELLOW}Sanitizer options:${NC}"
echo "  ASAN_OPTIONS=$ASAN_OPTIONS"
echo "  UBSAN_OPTIONS=$UBSAN_OPTIONS"
echo ""
echo -e "${YELLOW}Logs will be saved to:${NC}"
echo "  $ASAN_LOG"
echo "  $PD_LOG"
echo ""

# Parse command line arguments
ITERATIONS=10000
PATCH="patches/fuzzer_test.pd"
NOGUI="-nogui"
BATCH=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -i|--iterations)
            ITERATIONS="$2"
            shift 2
            ;;
        -p|--patch)
            PATCH="$2"
            shift 2
            ;;
        -g|--gui)
            NOGUI=""
            shift
            ;;
        -b|--batch)
            BATCH="-batch"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  -i, --iterations N    Number of fuzz iterations (default: 10000)"
            echo "  -p, --patch FILE      Pure Data patch to run (default: patches/fuzzer_test.pd)"
            echo "  -g, --gui             Run with GUI (default: nogui)"
            echo "  -b, --batch           Run in batch mode (non-interactive)"
            echo "  -h, --help            Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Run with defaults"
            echo "  $0 -i 100000          # Run 100k iterations"
            echo "  $0 -g                 # Run with GUI"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use -h for help"
            exit 1
            ;;
    esac
done

# Check if patch exists
if [ ! -f "$PATCH" ]; then
    echo -e "${RED}Error: Patch file not found: $PATCH${NC}"
    exit 1
fi

echo -e "${GREEN}Starting fuzzer...${NC}"
echo "  Iterations: $ITERATIONS"
echo "  Patch: $PATCH"
echo ""
echo -e "${YELLOW}Monitor for crashes in: $LOG_DIR/${NC}"
echo ""

# Run Pure Data with fuzzer
# Redirect both stdout and stderr to log file
pd $NOGUI $BATCH "$PATCH" 2>&1 | tee "$PD_LOG"

# Check for crashes
echo ""
echo -e "${GREEN}=== Fuzzing Complete ===${NC}"
echo ""

if grep -q "ERROR:" "$PD_LOG" || grep -q "Sanitizer" "$PD_LOG"; then
    echo -e "${RED}!!! ISSUES DETECTED !!!${NC}"
    echo "Check logs: $PD_LOG"
    echo ""
    echo "Summary of errors:"
    grep -E "(ERROR:|Sanitizer)" "$PD_LOG" | head -20
    exit 1
else
    echo -e "${GREEN}No sanitizer errors detected${NC}"
    echo "Fuzzer completed successfully"
    exit 0
fi
