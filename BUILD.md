# PixelPaint - SDL3 Build Guide

Modern pixel art painting application built with SDL3, Dear ImGui, and C++20.

## Features

- **Cross-Platform Support**: macOS, iOS, and Web (Emscripten)
- **Modern Architecture**: SDL3 callback architecture for seamless mobile/web lifecycle
- **Metal Backend**: Native Metal rendering on Apple platforms
- **WebGL Support**: Runs in modern browsers via Emscripten
- **No Global Dependencies**: All dependencies fetched automatically via CMake FetchContent

## Architecture

### Tech Stack

- **Language**: C++20
- **UI Framework**: Dear ImGui (docking branch)
- **Plotting**: ImPlot
- **Windowing/Input**: SDL3
- **Graphics API**:
  - macOS/iOS: Metal (via imgui_impl_metal)
  - Web: WebGL/OpenGL ES 3 (via imgui_impl_opengl3)
- **Formatting**: fmt library

### SDL3 Callback Architecture

This project uses SDL3's new callback-based architecture (`SDL_AppIterate`) which provides:
- Automatic browser/mobile lifecycle management
- Better integration with event loops on Web and iOS
- Simplified cross-platform main loop handling

The callbacks implemented are:
- `SDL_AppInit()` - Initialize application
- `SDL_AppEvent()` - Handle events
- `SDL_AppIterate()` - Main frame update loop
- `SDL_AppQuit()` - Cleanup

## Prerequisites

### All Platforms

- CMake 3.22 or higher
- C++20 compatible compiler
- Git

### macOS

- Xcode 12.0 or higher
- macOS 11.0 or higher

### iOS

- Xcode 12.0 or higher
- iOS 14.0 or higher
- Apple Developer account (for device deployment)

### Web (Emscripten)

```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

## Building

We provide a convenient build script (`scripts/dev.sh`) for managing different platform builds.

### Quick Start - macOS

```bash
# Build and run
./scripts/dev.sh run-macos

# Or just build
./scripts/dev.sh macos

# Debug build
./scripts/dev.sh macos -d
```

### iOS Device

```bash
# Build for iOS device
./scripts/dev.sh ios

# The app will be at:
# build/ios-Release/Release-iphoneos/PixelPaint.app

# Deploy using Xcode or:
xcrun devicectl device install app --device <device-id> build/ios-Release/Release-iphoneos/PixelPaint.app
```

### iOS Simulator

```bash
# Build for iOS Simulator
./scripts/dev.sh ios-sim

# Launch in simulator
open -a Simulator
xcrun simctl install booted build/ios-simulator-Release/Release-iphonesimulator/PixelPaint.app
xcrun simctl launch booted com.pixelpaint.app
```

### Web (Emscripten)

```bash
# Build and serve
./scripts/dev.sh run-web

# Or just build
./scripts/dev.sh web

# The web version will be available at:
# http://localhost:8000/PixelPaint.html
```

## Build Script Usage

```bash
./scripts/dev.sh <command> [options]

Commands:
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
    help            Show help message

Options:
    -d, --debug     Build in debug mode (default is Release)
    -c, --clean     Clean before building
    -v, --verbose   Verbose output

Examples:
    ./scripts/dev.sh macos                # Build macOS release version
    ./scripts/dev.sh macos -d             # Build macOS debug version
    ./scripts/dev.sh ios -c               # Clean and build iOS version
    ./scripts/dev.sh web                  # Build web version
```

## Manual Build (Advanced)

### macOS

```bash
mkdir -p build/macos && cd build/macos
cmake ../.. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
    -G "Xcode"
cmake --build . --config Release
```

### iOS

```bash
mkdir -p build/ios && cd build/ios
cmake ../.. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="14.0" \
    -DIOS=TRUE \
    -G "Xcode"
cmake --build . --config Release
```

### Web

```bash
# Make sure Emscripten is activated
source /path/to/emsdk/emsdk_env.sh

mkdir -p build/web && cd build/web
emcmake cmake ../.. -DCMAKE_BUILD_TYPE=Release
emmake make
```

## Project Structure

```
pelpaint/
├── CMakeLists.txt          # Main CMake configuration
├── BUILD.md                # This file
├── scripts/
│   └── dev.sh              # Build automation script
├── src/
│   ├── main.cpp            # OpenGL/Web entry point
│   ├── main.mm             # Metal/Apple entry point
│   ├── PixelPaintView.cpp  # Main application logic
│   ├── PixelPaintView.hpp
│   ├── FileUtils.cpp
│   ├── FileUtils.hpp
│   └── ImGuiFileDialog.*   # File dialog implementation
├── web/
│   └── shell.html          # Emscripten HTML shell
└── build/                  # Build output (gitignored)
    ├── macos-Release/
    ├── ios-Release/
    └── web-Release/
```



## License

See project root for license information.

## Contributing

Contributions welcome! Please ensure:
- Code builds on all platforms
- No compiler warnings
- SDL3 callback architecture is maintained
- Platform-specific code is properly isolated

## Resources

- [SDL3 Documentation](https://wiki.libsdl.org/SDL3/)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImPlot](https://github.com/epezent/implot)
- [Emscripten](https://emscripten.org/)

---

Built with ❤️ using SDL3, Dear ImGui, and modern C++
