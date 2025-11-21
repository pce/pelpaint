#include <cstdint>
#include <filesystem>
#include <memory>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <implot.h>
#include "ImGuiFileDialog.h"
#include "ImGuiStyle.hpp"

#if defined(USE_METAL_BACKEND)
    #include "imgui_impl_sdl3.h"
    #include "imgui_impl_metal.h"
    #import <Metal/Metal.h>
    #import <QuartzCore/CAMetalLayer.h>
#else
    #include "imgui_impl_sdl3.h"
    #include "imgui_impl_opengl3.h"
    #if defined(__EMSCRIPTEN__)
        #include <emscripten.h>
        #include <emscripten/html5.h>
    #endif
    #if defined(IMGUI_IMPL_OPENGL_ES2)
        #include <SDL3/SDL_opengles2.h>
    #else
        #include <SDL3/SDL_opengl.h>
    #endif
#endif

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "PixelPaintView.hpp"

constexpr auto WINDOW_WIDTH = std::uint32_t{1280};
constexpr auto WINDOW_HEIGHT = std::uint32_t{720};

namespace fs = std::filesystem;

// Application state structure
struct AppState
{
    SDL_Window* window = nullptr;
    bool quit = false;
    std::unique_ptr<PixelPaintView> pixelPaintView;
    
#if defined(USE_METAL_BACKEND)
    SDL_MetalView metalView = nullptr;
    id<MTLDevice> metalDevice = nil;
    id<MTLCommandQueue> metalCommandQueue = nil;
    CAMetalLayer* metalLayer = nullptr;
#else
    SDL_GLContext glContext = nullptr;
#endif
};

// Global app state (required for SDL3 callback architecture)
static AppState* g_AppState = nullptr;

// Initialize the application
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    // Create application state
    g_AppState = new AppState();
    *appstate = g_AppState;

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        SDL_Log("Error: SDL_Init(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

#if defined(USE_METAL_BACKEND)
    // Create window for Metal
    g_AppState->window = SDL_CreateWindow(
        "PixelPaint",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    
    if (!g_AppState->window)
    {
        SDL_Log("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Setup Metal
    g_AppState->metalView = SDL_Metal_CreateView(g_AppState->window);
    g_AppState->metalDevice = MTLCreateSystemDefaultDevice();
    g_AppState->metalCommandQueue = [g_AppState->metalDevice newCommandQueue];
    g_AppState->metalLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(g_AppState->metalView);
    g_AppState->metalLayer.device = g_AppState->metalDevice;
    g_AppState->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

#else
    // Setup OpenGL attributes
    #if defined(__EMSCRIPTEN__)
        // WebGL 2.0 / GLES 3.0
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    #elif defined(IMGUI_IMPL_OPENGL_ES2)
        // GL ES 2.0
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    #else
        // GL 3.0 + GLSL 130
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    #endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Create window for OpenGL
    g_AppState->window = SDL_CreateWindow(
        "PixelPaint",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!g_AppState->window)
    {
        SDL_Log("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    g_AppState->glContext = SDL_GL_CreateContext(g_AppState->window);
    if (!g_AppState->glContext)
    {
        SDL_Log("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    SDL_GL_MakeCurrent(g_AppState->window, g_AppState->glContext);
    SDL_GL_SetSwapInterval(1); // Enable vsync
#endif

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    // Setup Dear ImGui style - Unreal Engine theme
    ImGuiTheme::SetupUnrealTheme();

    // Setup Platform/Renderer backends
#if defined(USE_METAL_BACKEND)
    ImGui_ImplSDL3_InitForMetal(g_AppState->window);
    ImGui_ImplMetal_Init(g_AppState->metalDevice);
#else
    #if defined(__EMSCRIPTEN__)
        const char* glsl_version = "#version 300 es";
    #elif defined(IMGUI_IMPL_OPENGL_ES2)
        const char* glsl_version = "#version 100";
    #else
        const char* glsl_version = "#version 130";
    #endif
    ImGui_ImplSDL3_InitForOpenGL(g_AppState->window, g_AppState->glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);
#endif

    // Create PixelPaint view
    g_AppState->pixelPaintView = std::make_unique<PixelPaintView>();

    return SDL_APP_CONTINUE;
}

// Handle events
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    AppState* state = static_cast<AppState*>(appstate);
    
    ImGui_ImplSDL3_ProcessEvent(event);
    
    if (event->type == SDL_EVENT_QUIT)
    {
        state->quit = true;
        return SDL_APP_SUCCESS;
    }
    
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event->window.windowID == SDL_GetWindowID(state->window))
    {
        state->quit = true;
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

// Main iteration/frame update
SDL_AppResult SDL_AppIterate(void* appstate)
{
    AppState* state = static_cast<AppState*>(appstate);
    
    if (state->quit)
    {
        return SDL_APP_SUCCESS;
    }

#if defined(USE_METAL_BACKEND)
    // Metal rendering
    @autoreleasepool
    {
        // Start the Dear ImGui frame
        ImGui_ImplMetal_NewFrame((__bridge MTLRenderPassDescriptor*)state->metalLayer);
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Render our UI
        state->pixelPaintView->Draw("PixelPaint");

        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();

        // Get next drawable
        id<CAMetalDrawable> drawable = [state->metalLayer nextDrawable];
        if (!drawable)
        {
            return SDL_APP_CONTINUE;
        }

        // Create render pass descriptor
        MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        renderPassDescriptor.colorAttachments[0].texture = drawable.texture;
        renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(30.0/255.0, 30.0/255.0, 30.0/255.0, 1.0);

        // Create command buffer
        id<MTLCommandBuffer> commandBuffer = [state->metalCommandQueue commandBuffer];
        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

        // Render ImGui
        ImGui_ImplMetal_RenderDrawData(draw_data, commandBuffer, renderEncoder);

        [renderEncoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
#else
    // OpenGL rendering
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Render our UI
    state->pixelPaintView->Draw("PixelPaint");

    // Rendering
    ImGui::Render();
    
    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    
    const ImVec4 clear_color = ImVec4(30.0f/255.0f, 30.0f/255.0f, 30.0f/255.0f, 1.00f);
    glClearColor(clear_color.x * clear_color.w, 
                 clear_color.y * clear_color.w, 
                 clear_color.z * clear_color.w, 
                 clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(state->window);
#endif

    return SDL_APP_CONTINUE;
}

// Cleanup
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    AppState* state = static_cast<AppState*>(appstate);
    
    if (!state)
        return;

    // Cleanup ImGui
#if defined(USE_METAL_BACKEND)
    ImGui_ImplMetal_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    // Cleanup SDL
#if defined(USE_METAL_BACKEND)
    if (state->metalView)
    {
        SDL_Metal_DestroyView(state->metalView);
    }
#else
    if (state->glContext)
    {
        SDL_GL_DestroyContext(state->glContext);
    }
#endif

    if (state->window)
    {
        SDL_DestroyWindow(state->window);
    }

    SDL_Quit();

    delete state;
    g_AppState = nullptr;
}