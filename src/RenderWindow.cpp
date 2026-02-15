#include "RenderWindow.hpp"

#include "View.hpp"

#include <dearimgui/imgui.h>
#include <dearimgui/backends/imgui_impl_sdl3.h>
#include <dearimgui/backends/imgui_impl_opengl3.h>

#include <glad/glad.h>
#include <SDL3/SDL.h>

RenderWindow::RenderWindow(SDL_Window *window)
    : mWindow(window), mCurrentView()
{
    // Initialize view with window dimensions
    if (mWindow)
    {
        int width = 0, height = 0;
        SDL_GetWindowSize(mWindow, &width, &height);
        mCurrentView.setSize(static_cast<float>(width), static_cast<float>(height));
        mCurrentView.setCenter(static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f);
    }
}

void RenderWindow::setView(const View &view)
{
    mCurrentView = view;
}

View RenderWindow::getView() const noexcept
{
    return mCurrentView;
}

void RenderWindow::beginFrame() const noexcept
{
    if (!mWindow)
    {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
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

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(mWindow);
}

bool RenderWindow::isOpen() const noexcept
{
    return mWindow != nullptr;
}

void RenderWindow::close() noexcept
{
    // Just null out the pointers to signal the window is closed
    // Don't destroy the actual SDL resources - that's SDLHelper's job
    // during proper cleanup in its destructor
    SDL_Log("RenderWindow::close() - Marking window as closed\n");

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
