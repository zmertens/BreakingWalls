#include "RenderWindow.hpp"

#include <glad/glad.h>

#include <SFML/Window.hpp>

RenderWindow::RenderWindow(sf::Window* window)
    : mWindow(window)
{
}

void RenderWindow::clear() const noexcept
{
    if (!mWindow)
        return;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderWindow::display() const noexcept
{
    if (!mWindow)
        return;
    mWindow->display();
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
    if (!mWindow || mFullscreen == fullscreen)
        return;
    mFullscreen = fullscreen;
}

bool RenderWindow::isFullscreen() const noexcept
{
    return mFullscreen;
}

void RenderWindow::setVsync(bool enabled) const noexcept
{
    if (!mWindow)
        return;
    mWindow->setVerticalSyncEnabled(enabled);
}

sf::Window* RenderWindow::getSFMLWindow() const noexcept
{
    return mWindow;
}
