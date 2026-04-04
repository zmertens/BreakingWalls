#include "RenderWindow.hpp"

#include <glad/glad.h>

#include <SDL3/SDL.h>

RenderWindow::RenderWindow(SDL_Window *window)
    : mWindow(window)
{

}

void RenderWindow::clear() const noexcept
{
    if (!mWindow)
    {
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderWindow::display() const noexcept
{
    if (!mWindow)
    {
        return;
    }

    SDL_GL_SwapWindow(mWindow);
}

bool RenderWindow::isOpen() const noexcept
{
    return mWindow != nullptr;
}

void RenderWindow::close() noexcept
{
    mWindow = nullptr;
}

void RenderWindow::setFullscreen(bool fullscreen) const noexcept
{
    if (const auto flags = fullscreen ? SDL_WINDOW_FULLSCREEN : 0; !isFullscreen() && flags != 0)
    {
        SDL_SetWindowFullscreen(mWindow, flags);
    }
    else if (isFullscreen() && !fullscreen)
    {
        SDL_SetWindowFullscreen(mWindow, false);
    }
}

bool RenderWindow::isFullscreen() const noexcept
{
    const auto flags = SDL_GetWindowFlags(mWindow);
    return (flags & SDL_WINDOW_FULLSCREEN) != 0;
}

void RenderWindow::setVsync(bool enabled) const noexcept
{
    if (!mWindow)
    {
        return;
    }
    // SDL3: 1 = vsync enabled, 0 = vsync disabled
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

SDL_Window *RenderWindow::getSDLWindow() const noexcept
{
    return mWindow;
}
