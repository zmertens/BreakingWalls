#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include <cstdint>
#include <functional>
#include <string_view>

struct SDL_Window;

/// @file Texture.hpp
/// @brief Texture class for OpenGL 3 textures
/// @details This class wraps OpenGL texture operations
class Texture
{
public:
    enum class RenderTargetFormat
    {
        RGBA32F,
        RGBA16F,
        R16F
    };

    Texture() = default;

    ~Texture() noexcept;

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    Texture(Texture &&other) noexcept;

    Texture &operator=(Texture &&other) noexcept;

    void free() noexcept;

    /// Get the OpenGL texture ID
    [[nodiscard]] std::uint32_t get() const noexcept;

    [[nodiscard]] std::uint8_t *getPixelData() const noexcept;

    [[nodiscard]] int getWidth() const noexcept;

    [[nodiscard]] int getHeight() const noexcept;

    /// Create an empty texture for use as a render target with explicit format
    bool loadRenderTarget(int width, int height,
                          RenderTargetFormat format,
                          std::uint32_t channelOffset = 0) noexcept;

    /// Create an empty texture for use as a render target
    bool loadRGBA32F(int width, int height, std::uint32_t channelOffset = 0) noexcept;

    /// Load texture from file using stb_image
    bool loadFromFile(std::string_view filepath, std::uint32_t channelOffset = 0) noexcept;

    bool loadProceduralTextures(int width, int height,
        const std::function<void(std::vector<std::uint8_t>&, int, int)> &generator,
        std::uint32_t channelOffset = 0) noexcept;

    /// Load texture from raw RGBA memory data
    bool loadFromMemory(const std::uint8_t *data, int width, int height,
                        std::uint32_t channelOffset = 0, bool rotate_180 = false) noexcept;

    /// Update existing texture from raw RGBA memory data efficiently
    bool updateFromMemory(const std::uint8_t *data, int width, int height,
                          std::uint32_t channelOffset = 0, bool rotate_180 = false) noexcept;

    static constexpr int MAX_TEXTURE_WIDTH = 8192;
    static constexpr int MAX_TEXTURE_HEIGHT = 8192;

private:
    std::uint32_t mTextureId{0};
    int mWidth{0};
    int mHeight{0};
    std::uint8_t *mBytes{nullptr};
}; // Texture class

#endif // TEXTURE_HPP
