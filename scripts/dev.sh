#!/bin/bash

# PixelPaint Build Script
# Supports: macOS, iOS, iOS Simulator, and Web (Emscripten)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project root directory
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# Print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Print usage information
usage() {
    cat << EOF
Usage: $0 <command> [options]

Commands:
    dev             Build and Run
    build-run       Build and Run
    run             Only cmake --build and Run
    macos           Build for macOS (native)
    ios             Build for iOS device
    ios-sim         Build for iOS Simulator
    web             Build for Web (Emscripten)
    clean           Clean all build directories
    clean-macos     Clean macOS build directory
    clean-ios       Clean iOS build directory
    clean-web       Clean Web build directory
    run-macos       Build and run macOS version
    run-web         Build and serve Web version
    help            Show this help message

Options:
    -d, --debug     Build in debug mode (default is Release)
    -c, --clean     Clean before building
    -v, --verbose   Verbose output

Examples:
    $0 macos                # Build macOS release version
    $0 macos -d             # Build macOS debug version
    $0 ios -c               # Clean and build iOS version
    $0 web                  # Build web version
    $0 run-macos            # Build and run macOS version

EOF
}

# Parse command line arguments
COMMAND=""
BUILD_TYPE="Release"
CLEAN_BEFORE_BUILD=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        macos|ios|ios-sim|web|clean|clean-*|run-*|help)
            COMMAND="$1"
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN_BEFORE_BUILD=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Check if command is provided
if [ -z "$COMMAND" ]; then
    print_error "No command provided"
    usage
    exit 1
fi

# Verbose flag for CMake and Make
VERBOSE_FLAG=""
if [ "$VERBOSE" = true ]; then
    VERBOSE_FLAG="--verbose"
fi

# Build for macOS
build_macos() {
    print_info "Building for macOS (${BUILD_TYPE})..."

    local BUILD_PATH="${BUILD_DIR}/macos-${BUILD_TYPE}"

    if [ "$CLEAN_BEFORE_BUILD" = true ] || [ ! -d "$BUILD_PATH" ]; then
        print_info "Creating build directory: ${BUILD_PATH}"
        rm -rf "$BUILD_PATH"
        mkdir -p "$BUILD_PATH"
    fi

    cd "$BUILD_PATH"

    print_info "Running CMake configuration..."
    cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
        -G "Xcode" \
        ${VERBOSE_FLAG}

    print_info "Building project..."
    cmake --build . --config "${BUILD_TYPE}" ${VERBOSE_FLAG}

    print_success "macOS build completed successfully!"
    print_info "Executable location: ${BUILD_PATH}/${BUILD_TYPE}/PixelPaint.app"
}

# Build for iOS
build_ios() {
    print_info "Building for iOS Device (${BUILD_TYPE})..."

    local BUILD_PATH="${BUILD_DIR}/ios-${BUILD_TYPE}"

    if [ "$CLEAN_BEFORE_BUILD" = true ] || [ ! -d "$BUILD_PATH" ]; then
        print_info "Creating build directory: ${BUILD_PATH}"
        rm -rf "$BUILD_PATH"
        mkdir -p "$BUILD_PATH"
    fi

    cd "$BUILD_PATH"

    print_info "Running CMake configuration for iOS (No Signing)..."
    cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="arm64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="14.0" \
        -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="" \
        -DCMAKE_XCODE_ATTRIBUTE_AD_HOC_CODE_SIGNING_ALLOWED=YES \
        -DIOS=TRUE \
        -G "Xcode" \
        ${VERBOSE_FLAG}

    print_info "Building project..."
    cmake --build . --config "${BUILD_TYPE}" -- -allowProvisioningUpdates ${VERBOSE_FLAG}

    print_success "iOS build completed successfully!"
    print_info "App location: ${BUILD_PATH}/${BUILD_TYPE}-iphoneos/PixelPaint.app"
    print_warning "Deploy to device using Xcode or: xcrun devicectl device install app --device <device-id> <path-to-app>"
}

# Build for iOS Simulator
build_ios_simulator() {
    print_info "Building for iOS Simulator (${BUILD_TYPE})..."

    local BUILD_PATH="${BUILD_DIR}/ios-simulator-${BUILD_TYPE}"

    if [ "$CLEAN_BEFORE_BUILD" = true ] || [ ! -d "$BUILD_PATH" ]; then
        print_info "Creating build directory: ${BUILD_PATH}"
        rm -rf "$BUILD_PATH"
        mkdir -p "$BUILD_PATH"
    fi

    cd "$BUILD_PATH"

    print_info "Running CMake configuration for iOS Simulator..."
    cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="14.0" \
        -DCMAKE_OSX_SYSROOT=iphonesimulator \
        -DIOS=TRUE \
        -G "Xcode" \
        ${VERBOSE_FLAG}

    print_info "Building project..."
    cmake --build . --config "${BUILD_TYPE}" ${VERBOSE_FLAG}

    print_success "iOS Simulator build completed successfully!"
    print_info "App location: ${BUILD_PATH}/${BUILD_TYPE}-iphonesimulator/PixelPaint.app"
}

# Build for Web (Emscripten)
build_web() {
    print_info "Building for Web with Emscripten (${BUILD_TYPE})..."

    # Check if Emscripten is installed
    if ! command -v emcc &> /dev/null; then
        print_error "Emscripten not found!"
        print_info "Please install Emscripten: https://emscripten.org/docs/getting_started/downloads.html"
        print_info "Or run: find \$HOME -name emsdk_env.sh 2> /dev/null || git clone https://github.com/emscripten-core/emsdk.git && cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh"
        exit 1
    fi

    local BUILD_PATH="${BUILD_DIR}/web-${BUILD_TYPE}"

    if [ "$CLEAN_BEFORE_BUILD" = true ] || [ ! -d "$BUILD_PATH" ]; then
        print_info "Creating build directory: ${BUILD_PATH}"
        rm -rf "$BUILD_PATH"
        mkdir -p "$BUILD_PATH"
    fi

    cd "$BUILD_PATH"

    print_info "Running CMake configuration for Web..."
    emcmake cmake "${PROJECT_ROOT}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_CROSSCOMPILING_EMULATOR="${EMSDK_NODE}" \
        ${VERBOSE_FLAG}

    print_info "Building project..."
    emmake make ${VERBOSE_FLAG}

    print_success "Web build completed successfully!"
    print_info "Output files: ${BUILD_PATH}/PixelPaint.html, PixelPaint.js, PixelPaint.wasm"
}

# Run macOS build
run_macos() {
    build_macos

    local APP_PATH="${BUILD_DIR}/macos-${BUILD_TYPE}/${BUILD_TYPE}/PixelPaint.app"

    if [ -d "$APP_PATH" ]; then
        print_info "Running PixelPaint..."
        open "$APP_PATH"
    else
        print_error "Application not found at: ${APP_PATH}"
        exit 1
    fi
}

# Run web build (with local server)
run_web() {
    build_web

    local WEB_PATH="${BUILD_DIR}/web-${BUILD_TYPE}"

    if [ -f "${WEB_PATH}/PixelPaint.html" ]; then
        print_info "Starting local web server..."
        print_info "Open http://localhost:8000/PixelPaint.html in your browser"
        print_info "Press Ctrl+C to stop the server"
        cd "$WEB_PATH"
        python3 -m http.server 8000
    else
        print_error "Web build not found at: ${WEB_PATH}"
        exit 1
    fi
}

# Clean functions
clean_all() {
    print_info "Cleaning all build directories..."
    rm -rf "${BUILD_DIR}"
    print_success "All build directories cleaned!"
}

clean_macos() {
    print_info "Cleaning macOS build directories..."
    rm -rf "${BUILD_DIR}/macos-"*
    print_success "macOS build directories cleaned!"
}

clean_ios() {
    print_info "Cleaning iOS build directories..."
    rm -rf "${BUILD_DIR}/ios-"*
    print_success "iOS build directories cleaned!"
}

clean_web() {
    print_info "Cleaning Web build directories..."
    rm -rf "${BUILD_DIR}/web-"*
    print_success "Web build directories cleaned!"
}

# Execute command
case $COMMAND in
    macos)
        build_macos
        ;;
    ios)
        build_ios
        ;;
    ios-sim)
        build_ios_simulator
        ;;
    web)
        build_web
        ;;
    run-macos)
        run_macos
        ;;
    run-web)
        run_web
        ;;
    clean)
        clean_all
        ;;
    clean-macos)
        clean_macos
        ;;
    clean-ios)
        clean_ios
        ;;
    clean-web)
        clean_web
        ;;
    build-run)
        print_info "Building and running the project..."
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
        cmake ..
        cmake --build .
        ./PixelPaint
        ;;
    run)
        print_info "Running the project..."
        cd "$BUILD_DIR"
        ./PixelPaint
        ;;
    help)
        usage
        ;;
    *)
        print_error "Unknown command: $COMMAND"
        usage
        exit 1
        ;;
esac

exit 0
