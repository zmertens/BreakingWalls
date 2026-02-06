#include "SDLHelper.hpp"

#include <SDL3/SDL.h>

#include "GLUtils.hpp"

#include <glad/glad.h>

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
#if defined(BREAKING_WALLS_DEBUG)
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

            this->window = SDL_CreateWindow(title.data(), width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);

            if (!this->window)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindow failed: %s\n", SDL_GetError());

                return;
            }

            // Create OpenGL context
            this->glContext = SDL_GL_CreateContext(this->window);
            if (!this->glContext)
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
                SDL_DestroyWindow(this->window);
                return;
            }

            // Make context current
            SDL_GL_MakeCurrent(this->window, this->glContext);

            // Initialize GLAD
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to initialize GLAD\n");
                SDL_GL_DestroyContext(this->glContext);
                SDL_DestroyWindow(this->window);
                return;
            }

            SDL_Log("OpenGL Version: %s\n", glGetString(GL_VERSION));
            SDL_Log("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

            // Enable VSync for OpenGL
            SDL_GL_SetSwapInterval(1);

#if defined(BREAKING_WALLS_DEBUG)
            // Register debug callback if available (OpenGL 4.3+)
            glDebugMessageCallback(GLUtils::GlDebugCallback, nullptr);
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            SDL_Log("GameState: OpenGL Debug Output enabled");
#endif

            SDL_Log("SDLHelper::init - OpenGL and SDL initialized successfully");
        };

    // SDL_Init returns true on SUCCESS (SDL3 behavior)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        std::call_once(sdlInitializedFlag, initFunc);
    } else
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s\n", SDL_GetError());
    }
}

void SDLHelper::destroyAndQuit() noexcept
{
    // Prevent double-destruction
    if (!this->window && !this->glContext)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Already destroyed, skipping\n");
        return;
    }

    if (glContext)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Destroying OpenGL context\n");
        SDL_GL_DestroyContext(glContext);
        glContext = nullptr;
    }

    if (window)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Destroying window %p\n", static_cast<void*>(window));
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    // Only call SDL_Quit() if we actually destroyed something
    if (SDL_WasInit(0) != 0)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Calling SDL_Quit()\n");
        SDL_Quit();
    }
}
