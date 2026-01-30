#ifndef SDLHELPER_HPP
#define SDLHELPER_HPP

#include <mutex>
#include <string_view>

#include <SDL3/SDL.h>

#include "State.hpp"

struct SDL_Window;

class SDLHelper
{
public:
    SDL_Window* m_window{nullptr};
    SDL_GLContext m_context{nullptr};

private:
    std::once_flag sdlInitializedFlag;

public:
    void init(std::string_view title, int width, int height) noexcept;

    void destroyAndQuit() noexcept;
}; // SDLHelper class

#endif // SDLHELPER_HPP
