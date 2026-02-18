#ifndef RENDER_WINDOW_HPP
#define RENDER_WINDOW_HPP

#include "Events.hpp"

struct SDL_Window;

/// @brief SDL-based RenderWindow that mimics SFML's sf::RenderWindow interface
class RenderWindow
{
public:
    explicit RenderWindow(SDL_Window *window);

    ~RenderWindow() = default;

    // Delete copy constructor and copy assignment operator
    // because RenderWindow contains std::unique_ptr which is not copyable
    RenderWindow(const RenderWindow &) = delete;
    RenderWindow &operator=(const RenderWindow &) = delete;

    // Allow move constructor and move assignment operator
    RenderWindow(RenderWindow &&) = default;
    RenderWindow &operator=(RenderWindow &&) = default;

    void beginFrame() const noexcept;

    /// @brief Clear the render target
    void clear() const noexcept;

    /// @brief Present the rendered frame
    void display() const noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    void close() noexcept;

    void setFullscreen(bool fullscreen) const noexcept;

    [[nodiscard]] bool isFullscreen() const noexcept;

    void setVsync(bool enabled) const noexcept;

    /// @brief Get the SDL window for direct access
    [[nodiscard]] SDL_Window *getSDLWindow() const noexcept;

private:
    SDL_Window *mWindow;
};

#endif // RENDER_WINDOW_HPP
