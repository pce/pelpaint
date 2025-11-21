cmake_minimum_required(VERSION 3.22)

project(
    "PixelPaint"
    VERSION 1.0.0
    LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
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
    set(IMGUI_BACKEND_LIBRARIES
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework QuartzCore"
    )
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

# ImGuiFileDialog sources
set(IMGUI_FILE_DIALOG_SOURCES
    src/ImGuiFileDialog.cpp
)

# Main executable
if(PLATFORM_MACOS OR PLATFORM_IOS)
    # Use .mm extension for Objective-C++ on Apple platforms
    # Copy PixelPaintView.cpp to .mm for Metal support
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/PixelPaintView.cpp
                   ${CMAKE_CURRENT_BINARY_DIR}/PixelPaintView.mm COPYONLY)

    set(APPLE_SOURCES
        ${IMGUI_FILE_DIALOG_SOURCES}
        src/main.mm
        ${CMAKE_CURRENT_BINARY_DIR}/PixelPaintView.mm
        src/FileUtils.cpp
    )

    # Add iOS file manager for iOS/iPadOS builds
    if(PLATFORM_IOS)
        list(APPEND APPLE_SOURCES src/IOSFileManager.mm)
    endif()

    add_executable(${PROJECT_NAME} ${APPLE_SOURCES})
else()
    add_executable(${PROJECT_NAME}
        ${IMGUI_FILE_DIALOG_SOURCES}
        src/main.cpp
        src/PixelPaintView.cpp
        src/FileUtils.cpp
    )
endif()

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)

if(PLATFORM_MACOS OR PLATFORM_IOS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_METAL_BACKEND)
endif()

if(PLATFORM_WEB)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_WEBGL_BACKEND)
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
        RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
elseif(PLATFORM_MACOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.pixelpaint.app
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
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
        "--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )

    target_link_options(${PROJECT_NAME} PRIVATE ${EMSCRIPTEN_LINK_FLAGS})
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
