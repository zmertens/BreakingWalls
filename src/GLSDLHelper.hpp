#ifndef GLSDLHELPER_HPP
#define GLSDLHELPER_HPP

#include <mutex>
#include <string_view>

#include <SDL3/SDL.h>

struct SDL_Window;

class GLSDLHelper
{
public:
    SDL_GLContext mGLContext{};
    SDL_Window* mWindow{ nullptr };

private:
    std::once_flag mInitializedFlag;

public:
    void init(std::string_view title, int width, int height) noexcept;

    void destroyAndQuit() noexcept;
}; // GLSDLHelper class

#endif // GLSDLHELPER_HPP
