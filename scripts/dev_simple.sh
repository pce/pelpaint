#!/bin/bash

# Simplified PixelPaint Build Script
# Supports: Build and Run, or Run only

set -e

# Project root directory
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# Print usage information
usage() {
    cat << EOF
Usage: $0 <command>

Commands:
    build-run       Build and Run
    run             Only cmake --build and Run

Examples:
    $0 build-run    # Build and run the project
    $0 run          # Run the project without rebuilding

EOF
}

# Check if command is provided
if [ $# -eq 0 ]; then
    usage
    exit 1
fi

COMMAND="$1"

# Build and Run
build_run() {
    echo "[INFO] Building the project..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake ..
    cmake --build .
    run
}

# Run only
run() {
    echo "[INFO] Running the project..."
    cd "$BUILD_DIR"
    ./PixelPaint
}

# Execute command
case $COMMAND in
    build-run)
        build_run
        ;;
    run)
        run
        ;;
    *)
        echo "[ERROR] Unknown command: $COMMAND"
        usage
        exit 1
        ;;
esac

exit 0
