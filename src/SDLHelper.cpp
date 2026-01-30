#include "SDLHelper.hpp"

#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <dearimgui/imgui.h>
#include <dearimgui/backends/imgui_impl_sdl3.h>
#include <dearimgui/backends/imgui_impl_opengl3.h>

void SDLHelper::init(std::string_view title, int width, int height) noexcept
{
    auto initFunc = [this, title, width, height]()
    {
        if (!SDL_SetAppMetadata("Maze builder with physics", title.data(), "physics;maze;c++;sdl")) {

            return;
        }

        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, title.data());
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "Flips An dAle");
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "MIT License");
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "simulation;game;voxel");
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, title.data());

        // Set OpenGL attributes before window creation
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        this->m_window = SDL_CreateWindow(title.data(), width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);

        if (!this->m_window)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindow failed: %s\n", SDL_GetError());

            return;
        }

        // Create OpenGL context
        this->m_context = SDL_GL_CreateContext(this->m_window);
        if (!this->m_context)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(this->m_window);
            return;
        }

        // Make context current
        SDL_GL_MakeCurrent(this->m_window, this->m_context);

        // Initialize GLAD
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to initialize GLAD\n");
            SDL_GL_DestroyContext(this->m_context);
            SDL_DestroyWindow(this->m_window);
            return;
        }

        SDL_Log("OpenGL Version: %s\n", glGetString(GL_VERSION));
        SDL_Log("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

        // Enable VSync for OpenGL
        SDL_GL_SetSwapInterval(1);

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        // Setup ImGui style
        ImGui::StyleColorsDark();
        
        // Initialize ImGui SDL3 and OpenGL3 backends
        ImGui_ImplSDL3_InitForOpenGL(this->m_window, this->m_context);
        ImGui_ImplOpenGL3_Init("#version 430");
        
        SDL_Log("SDLHelper::init - ImGui initialized successfully");

        SDL_Log("SDLHelper::init - OpenGL and SDL initialized successfully");
    };

    // SDL_Init returns true on SUCCESS (SDL3 behavior)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        std::call_once(sdlInitializedFlag, initFunc);
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s\n", SDL_GetError());
    }
}

void SDLHelper::destroyAndQuit() noexcept
{
    // Prevent double-destruction
    if (!this->m_window && !this->m_context)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Already destroyed, skipping\n");
        return;
    }

    // Shutdown ImGui backends first
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_Log("SDLHelper::destroyAndQuit() - ImGui shutdown complete\n");

    if (m_context)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Destroying OpenGL context\n");
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }

    if (m_window)
    {
#if defined(MAZE_DEBUG)
        SDL_Log("SDLHelper::destroyAndQuit() - Destroying window %p\n", static_cast<void*>(m_window));
#endif
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    // Only call SDL_Quit() if we actually destroyed something
    if (SDL_WasInit(0) != 0)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Calling SDL_Quit()\n");
        SDL_Quit();
    }
}
