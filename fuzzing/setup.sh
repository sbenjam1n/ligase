#!/bin/bash
# setup.sh - Quick setup script for ligase~ fuzzing

set -e

echo "=== ligase~ Fuzzing Setup ==="
echo ""

# Check dependencies
echo "Checking dependencies..."
MISSING=0

if ! command -v gcc &> /dev/null; then
    echo "  ✗ gcc not found"
    MISSING=1
else
    echo "  ✓ gcc found"
fi

if ! command -v make &> /dev/null; then
    echo "  ✗ make not found"
    MISSING=1
else
    echo "  ✓ make found"
fi

if ! command -v pd &> /dev/null; then
    echo "  ✗ puredata not found"
    MISSING=1
else
    echo "  ✓ puredata found"
fi

if [ ! -d "/usr/include/pd" ]; then
    echo "  ✗ pd headers not found (/usr/include/pd)"
    MISSING=1
else
    echo "  ✓ pd headers found"
fi

echo ""

if [ $MISSING -eq 1 ]; then
    echo "Missing dependencies. Install with:"
    echo "  sudo apt-get install puredata puredata-dev gcc make"
    echo ""
    read -p "Install now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo apt-get update
        sudo apt-get install -y puredata puredata-dev gcc make
    else
        echo "Please install dependencies manually"
        exit 1
    fi
fi

# Build
echo "Building fuzzer..."
make clean
make

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Quick start:"
echo "  ./run_fuzzer.sh"
echo ""
echo "For more options:"
echo "  ./run_fuzzer.sh --help"
echo ""
echo "Read the documentation:"
echo "  cat README.md"
echo ""
