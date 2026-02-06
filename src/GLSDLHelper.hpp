#ifndef GLSDLHELPER_HPP
#define GLSDLHELPER_HPP

#include <mutex>
#include <string_view>

#include <SDL3/SDL.h>

#include "State.hpp"

struct SDL_Window;

class GLSDLHelper
{
public:
    SDL_Window* window{nullptr};
    SDL_GLContext glContext{};

private:
    std::once_flag sdlInitializedFlag;

public:
    void init(std::string_view title, int width, int height) noexcept;

    void destroyAndQuit() noexcept;
}; // GLSDLHelper class

#endif // GLSDLHELPER_HPP
