cmake_minimum_required(VERSION 3.22)

project(
    "PixelPaint"
    VERSION 1.0.0
    LANGUAGES C CXX)

# Enable Objective-C only on Apple platforms
if(APPLE OR IOS)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform detection
if(EMSCRIPTEN)
    set(PLATFORM_WEB TRUE)
elseif(IOS)
    set(PLATFORM_IOS TRUE)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
endif()

# Options
option(ENABLE_WARNINGS "Enable to add warnings to a target." ON)
option(ENABLE_WARNINGS_AS_ERRORS "Enable to treat warnings as errors." OFF)
option(ENABLE_VULKAN_BACKEND "Enable Vulkan backend (non-Apple, non-web)." OFF)

# Module path for custom cmake modules
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/)

# Include custom modules if they exist
if(EXISTS ${CMAKE_MODULE_PATH}/Warnings.cmake)
    include(Warnings)
endif()
if(EXISTS ${CMAKE_MODULE_PATH}/ConfigSafeGuards.cmake)
    include(ConfigSafeGuards)
endif()

# FetchContent for dependencies
include(FetchContent)

# Disable building tests and examples for dependencies
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL3_SHARED_ENABLED_BY_DEFAULT OFF CACHE BOOL "" FORCE)

# SDL3
message(STATUS "Fetching SDL3...")
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui
message(STATUS "Fetching Dear ImGui...")
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# ImPlot
message(STATUS "Fetching ImPlot...")
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(implot)

# nlohmann/json — header-only JSON library
message(STATUS "Fetching nlohmann/json...")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ── Font Awesome 5 Free Solid icon font ──────────────────────────────────────
# License: SIL OFL 1.1 (free to bundle in any application).
# Download once at configure time; subsequent runs skip if the file is present.
set(FA5_FONT_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")
set(FA5_FONT_FILE "${FA5_FONT_DIR}/fa-solid-900.ttf")
file(MAKE_DIRECTORY "${FA5_FONT_DIR}")

if(NOT EXISTS "${FA5_FONT_FILE}")
    message(STATUS "Downloading Font Awesome 5 Free Solid (fa-solid-900.ttf)...")
    file(DOWNLOAD
        "https://github.com/FortAwesome/Font-Awesome/raw/5.15.4/webfonts/fa-solid-900.ttf"
        "${FA5_FONT_FILE}"
        TIMEOUT 60
        STATUS FA5_DL_RESULT
    )
    list(GET FA5_DL_RESULT 0 FA5_DL_STATUS)
    if(NOT FA5_DL_STATUS EQUAL 0)
        message(WARNING
            "Font Awesome 5 download failed (status ${FA5_DL_STATUS}).\n"
            "The toolbar will fall back to text labels.\n"
            "To fix: manually place fa-solid-900.ttf in ${FA5_FONT_DIR}")
        file(REMOVE "${FA5_FONT_FILE}")
    else()
        message(STATUS "Font Awesome 5 Solid downloaded to ${FA5_FONT_FILE}")
    endif()
endif()

# Create ImGui library
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# Platform-specific ImGui backends
if(PLATFORM_MACOS OR PLATFORM_IOS)
    list(APPEND IMGUI_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    if(PLATFORM_IOS)
        set(IMGUI_BACKEND_LIBRARIES
            "-framework Metal"
            "-framework MetalKit"
            "-framework UIKit"
            "-framework CoreGraphics"
            "-framework QuartzCore"
        )
    else()
        set(IMGUI_BACKEND_LIBRARIES
            "-framework Metal"
            "-framework MetalKit"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
            "-framework QuartzCore"
        )
    endif()
elseif(PLATFORM_WEB)
    list(APPEND IMGUI_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
else()
    list(APPEND IMGUI_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
endif()

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3)

if(PLATFORM_MACOS OR PLATFORM_IOS)
    target_link_libraries(imgui_lib PUBLIC ${IMGUI_BACKEND_LIBRARIES})
endif()

# Create ImPlot library
set(IMPLOT_DIR ${implot_SOURCE_DIR})
set(IMPLOT_SOURCES
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
    ${IMPLOT_DIR}/implot_demo.cpp
)
add_library(implot_lib STATIC ${IMPLOT_SOURCES})
target_include_directories(implot_lib PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot_lib PUBLIC imgui_lib)

# ImGuiFileDialog sources (only needed on Windows / Linux desktop)
# macOS uses NSOpenPanel, iOS uses UIDocumentPickerViewController, WASM uses <input type=file>
if(NOT PLATFORM_MACOS AND NOT PLATFORM_IOS AND NOT PLATFORM_WEB)
    set(IMGUI_FILE_DIALOG_SOURCES
        src/ImGuiFileDialog.cpp
    )
else()
    set(IMGUI_FILE_DIALOG_SOURCES "")
endif()

# Catch2 – lightweight unit-test framework (tests only)
message(STATUS "Fetching Catch2...")
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Main executable
if(PLATFORM_MACOS OR PLATFORM_IOS)
    # Use .mm extension for Objective-C++ on Apple platforms.
    # configure_file only runs at cmake-configure time, so edits to the .cpp
    # would not be picked up until the next reconfigure.  We use:
    #   1. execute_process  — seeds the .mm right now so it exists immediately.
    #   2. add_custom_command — tracks the .cpp as a dependency so the build
    #      system re-copies (and recompiles) whenever the source changes,
    #      without requiring a manual cmake reconfigure.
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/src/PixelPaintView.cpp
                ${CMAKE_CURRENT_BINARY_DIR}/PixelPaintView.mm
    )
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/PixelPaintView.mm
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/src/PixelPaintView.cpp
                ${CMAKE_CURRENT_BINARY_DIR}/PixelPaintView.mm
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/PixelPaintView.cpp
        COMMENT "Syncing PixelPaintView.cpp -> PixelPaintView.mm"
        VERBATIM
    )

    set(APPLE_SOURCES
        ${IMGUI_FILE_DIALOG_SOURCES}
        src/main.mm
        src/export/MeshExporter.cpp
        src/export/ProjectSerializer.cpp
        src/core/ImageSurface.cpp
        src/core/Canvas.cpp
        src/core/AnimationTimeline.cpp
        src/tools/DrawingAlgorithms.cpp
        src/tools/ImageSlicer.cpp
        src/tools/ParticleSystem.cpp
        src/tools/PixelPerfectGenerator.cpp
        src/filters/Filters.cpp
        src/filters/colorspace.cpp
        src/filters/dithering.cpp
        src/effects/PaletteCycler.cpp
        src/effects/AnimFxBaker.cpp
        src/operators/FrameOperator.cpp
        src/render/MetalComputeBackend.mm
        src/FileChooser.cpp
        src/settings/AppSettings.cpp
        src/ui/SettingsDialog.cpp
        src/ui/PaletteEditorDialog.cpp
        src/core/PaletteRef.cpp
        src/core/PaletteLibrary.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/PixelPaintView.mm
    )

    # Add iOS file manager for iOS/iPadOS builds
    if(PLATFORM_IOS)
        list(APPEND APPLE_SOURCES src/IOSFileManager.mm)
    endif()

    # Add native macOS file dialogs (NSOpenPanel / NSSavePanel)
    if(PLATFORM_MACOS)
        list(APPEND APPLE_SOURCES src/FileChooser_macOS.mm)
    endif()

    add_executable(${PROJECT_NAME} ${APPLE_SOURCES})
else()
    add_executable(${PROJECT_NAME}
        ${IMGUI_FILE_DIALOG_SOURCES}
        src/main.cpp
        src/PixelPaintView.cpp
        src/export/MeshExporter.cpp
        src/export/VideoExport.cpp
        src/export/ProjectSerializer.cpp
        src/core/ImageSurface.cpp
        src/core/Canvas.cpp
        src/core/AnimationTimeline.cpp
        src/tools/DrawingAlgorithms.cpp
        src/tools/ImageSlicer.cpp
        src/tools/ParticleSystem.cpp
        src/tools/PixelPerfectGenerator.cpp
        src/filters/Filters.cpp
        src/filters/colorspace.cpp
        src/filters/dithering.cpp
        src/effects/PaletteCycler.cpp
        src/effects/AnimFxBaker.cpp
        src/operators/FrameOperator.cpp
        src/render/ComputeBackend_cpu.cpp
        src/FileChooser.cpp
        src/settings/AppSettings.cpp
        src/ui/SettingsDialog.cpp
        src/ui/PaletteEditorDialog.cpp
        src/core/PaletteRef.cpp
        src/core/PaletteLibrary.cpp
    )
endif()

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)

if(PLATFORM_MACOS OR PLATFORM_IOS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_METAL_BACKEND)
endif()

if(PLATFORM_WEB)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_WEBGL_BACKEND)
elseif(ENABLE_VULKAN_BACKEND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_VULKAN_BACKEND)
elseif(NOT PLATFORM_MACOS AND NOT PLATFORM_IOS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENGL_BACKEND)
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3-static
    imgui_lib
    implot_lib
    nlohmann_json::nlohmann_json
)

# Platform-specific settings
if(PLATFORM_IOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.pixelpaint.app
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    )

    # iOS resources
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )

    # Bundle the icon font into the iOS app (Resources/ inside .ipa)
    if(EXISTS "${FA5_FONT_FILE}")
        target_sources(${PROJECT_NAME} PRIVATE "${FA5_FONT_FILE}")
        set_source_files_properties("${FA5_FONT_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "fonts"
        )
    endif()
elseif(PLATFORM_MACOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.pixelpaint.app
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
        # Ad-hoc signing — no provisioning profile required for local dev builds.
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )

    # Bundle the icon font into the macOS app (Contents/Resources/fonts/)
    if(EXISTS "${FA5_FONT_FILE}")
        target_sources(${PROJECT_NAME} PRIVATE "${FA5_FONT_FILE}")
        set_source_files_properties("${FA5_FONT_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/fonts"
        )
    endif()
elseif(PLATFORM_WEB)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".html"
    )

    set(EMSCRIPTEN_LINK_FLAGS
        "-sUSE_SDL=0"
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNO_EXIT_RUNTIME=0"
        "-sASSERTIONS=1"
        "-sFULL_ES3=1"
        "-sUSE_WEBGL2=1"
        # Filesystem support (needed for /tmp MEMFS read/write in FileChooser)
        "-sFORCE_FILESYSTEM=1"
        # Exported C functions callable from JavaScript via Module._xxx()
        # video_export_*  — animation frame access for MediaRecorder capture
        # file_chooser_on_file_selected — file-open/save callback
        # malloc / free — JS allocates frame buffers with Module._malloc
        "-sEXPORTED_FUNCTIONS=_main,_file_chooser_on_file_selected,_free,_malloc,_video_export,_video_export_frame_count,_video_export_frame_width,_video_export_frame_height,_video_export_fps,_video_export_get_frame,_pelpaint_drop_image"
        # Runtime helpers used in JS EM_ASM blocks
        "-sEXPORTED_RUNTIME_METHODS=stringToNewUTF8,UTF8ToString,FS"
        "--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )

    # Preload the icon font into the Emscripten virtual filesystem at /fonts/
    if(EXISTS "${FA5_FONT_FILE}")
        list(APPEND EMSCRIPTEN_LINK_FLAGS
            "--preload-file ${FA5_FONT_FILE}@/fonts/fa-solid-900.ttf"
        )
    endif()

    target_link_options(${PROJECT_NAME} PRIVATE ${EMSCRIPTEN_LINK_FLAGS})
endif()

# Desktop (Linux / Windows): copy icon font next to the executable at build time
if(NOT PLATFORM_MACOS AND NOT PLATFORM_IOS AND NOT PLATFORM_WEB)
    if(EXISTS "${FA5_FONT_FILE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FA5_FONT_FILE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts/fa-solid-900.ttf"
            COMMENT "Copying Font Awesome 5 icon font to build output"
            VERBATIM
        )
    endif()
endif()

# Warnings
if(COMMAND target_set_warnings)
    target_set_warnings(TARGET ${PROJECT_NAME}
        ENABLE ${ENABLE_WARNINGS}
        AS_ERRORS ${ENABLE_WARNINGS_AS_ERRORS})
endif()

# Install
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# ============================================================
# Unit tests  (not built for iOS / Emscripten)
# ============================================================
if(NOT PLATFORM_IOS AND NOT PLATFORM_WEB)
    enable_testing()

    add_executable(pelpaint_tests
        tests/test_filters.cpp
        tests/test_save_pipeline.cpp
        tests/test_export.cpp
        tests/stb_write_impl.cpp
        src/core/ImageSurface.cpp
        src/core/Canvas.cpp
        src/core/AnimationTimeline.cpp
        src/filters/Filters.cpp
        src/filters/colorspace.cpp
        src/filters/dithering.cpp
        src/effects/PaletteCycler.cpp
        src/effects/AnimFxBaker.cpp
        src/operators/FrameOperator.cpp
        src/tools/ParticleSystem.cpp
        src/tools/DrawingAlgorithms.cpp
        src/tools/ImageSlicer.cpp
        src/tools/PixelPerfectGenerator.cpp
        src/settings/AppSettings.cpp
        src/ui/SettingsDialog.cpp
        src/ui/PaletteEditorDialog.cpp
        src/core/PaletteRef.cpp
        src/core/PaletteLibrary.cpp
        src/export/MeshExporter.cpp
    )

    target_include_directories(pelpaint_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(pelpaint_tests PRIVATE
        Catch2::Catch2WithMain
        imgui_lib
        implot_lib
        nlohmann_json::nlohmann_json
    )

    target_compile_definitions(pelpaint_tests PRIVATE NOMINMAX)

    # macOS 15+ kills unsigned executables before they can run.
    # Ad-hoc sign the test binary right after it is linked so that
    # catch_discover_tests (PRE_TEST mode) and manual ctest runs both succeed.
    if(PLATFORM_MACOS)
        add_custom_command(TARGET pelpaint_tests POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_FILE:pelpaint_tests>"
            COMMENT "Ad-hoc signing pelpaint_tests"
            VERBATIM
        )
    endif()

    include(CTest)
    include(Catch)
    # PRE_TEST: discover test cases when CTest first runs them, not at build
    # time.  This avoids the race between the sign step and the Catch2
    # post-build discovery script on Xcode-generator builds.
    catch_discover_tests(pelpaint_tests DISCOVERY_MODE PRE_TEST)
endif()
