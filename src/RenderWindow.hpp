#ifndef RENDER_WINDOW_HPP
#define RENDER_WINDOW_HPP

namespace sf { class Window; }

/// @brief SFML-based RenderWindow wrapping sf::Window with OpenGL rendering
class RenderWindow
{
public:
    explicit RenderWindow(sf::Window* window);

    ~RenderWindow() = default;

    RenderWindow(const RenderWindow&) = delete;
    RenderWindow& operator=(const RenderWindow&) = delete;

    RenderWindow(RenderWindow&&) = default;
    RenderWindow& operator=(RenderWindow&&) = default;

    /// @brief Clear the render target
    void clear() const noexcept;

    /// @brief Present the rendered frame
    void display() const noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    void close() noexcept;

    void setFullscreen(bool fullscreen) const noexcept;

    [[nodiscard]] bool isFullscreen() const noexcept;

    void setVsync(bool enabled) const noexcept;

    [[nodiscard]] sf::Window* getSFMLWindow() const noexcept;

private:
    sf::Window* mWindow;
    mutable bool mFullscreen{false};
};

#endif // RENDER_WINDOW_HPP
