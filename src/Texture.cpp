#include "Texture.hpp"

#include <glad/glad.h>

#include <SDL3/SDL.h>

#include <MazeBuilder/enums.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <algorithm>
#include <vector>

namespace
{
    /// Create a rotated copy of RGBA texture data (180 degrees)
    std::vector<std::uint8_t> rotate180(const std::uint8_t *data, int width, int height) noexcept
    {
        if (data == nullptr || width <= 0 || height <= 0)
        {
            return {};
        }

        const size_t total_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        std::vector<std::uint8_t> rotated(total_bytes);

        const int total_pixels = width * height;

        // Copy pixels in reverse order
        for (int i = 0; i < total_pixels; ++i)
        {
            const int src_idx = i * 4;
            const int dst_idx = (total_pixels - 1 - i) * 4;
            // RGBA channels
            rotated[dst_idx + 0] = data[src_idx + 0];
            rotated[dst_idx + 1] = data[src_idx + 1];
            rotated[dst_idx + 2] = data[src_idx + 2];
            rotated[dst_idx + 3] = data[src_idx + 3];
        }

        return rotated;
    }
} // anonymous namespace

Texture::Texture(Texture &&other) noexcept
    : mTextureId(other.mTextureId), mWidth(other.mWidth), mHeight(other.mHeight), mBytes(other.mBytes)
{
    other.mTextureId = 0;
    other.mWidth = 0;
    other.mHeight = 0;
    other.mBytes = nullptr;
}

Texture::~Texture() noexcept
{
    free();
}

Texture &Texture::operator=(Texture &&other) noexcept
{
    if (this != &other)
    {
        free();

        mTextureId = other.mTextureId;
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mBytes = other.mBytes;

        other.mTextureId = 0;
        other.mWidth = 0;
        other.mHeight = 0;
        other.mBytes = nullptr;
    }
    return *this;
}

void Texture::free() noexcept
{
    if (mTextureId != 0)
    {
        glDeleteTextures(1, &mTextureId);
        mTextureId = 0;
        mWidth = 0;
        mHeight = 0;
        mBytes = nullptr;
    }
}

std::uint32_t Texture::get() const noexcept
{
    return mTextureId;
}

std::uint8_t *Texture::getPixelData() const noexcept
{
    return mBytes;
}

int Texture::getWidth() const noexcept
{
    return mWidth;
}

int Texture::getHeight() const noexcept
{
    return mHeight;
}

bool Texture::loadRGBA32F(const int width, const int height, const std::uint32_t channelOffset) noexcept
{
    return loadRenderTarget(width, height, RenderTargetFormat::RGBA32F, channelOffset);
}

bool Texture::loadRenderTarget(const int width, const int height,
                               const RenderTargetFormat format,
                               const std::uint32_t channelOffset) noexcept
{
    if (width <= 0 || height <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid render target size %dx%d\n", width, height);
        return false;
    }

    this->free();

    mWidth = width;
    mHeight = height;

    GLenum internalFormat = GL_RGBA32F;
    GLenum uploadFormat = GL_RGBA;

    switch (format)
    {
    case RenderTargetFormat::RGBA32F:
        internalFormat = GL_RGBA32F;
        uploadFormat = GL_RGBA;
        break;
    case RenderTargetFormat::RGBA16F:
        internalFormat = GL_RGBA16F;
        uploadFormat = GL_RGBA;
        break;
    case RenderTargetFormat::R16F:
        internalFormat = GL_R16F;
        uploadFormat = GL_RED;
        break;
    }

    glGenTextures(1, &mTextureId);
    glActiveTexture(GL_TEXTURE0 + channelOffset);
    glBindTexture(GL_TEXTURE_2D, mTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, uploadFormat, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

bool Texture::loadFromFile(const std::string_view filepath, const std::uint32_t channelOffset) noexcept
{
    this->free();

    stbi_set_flip_vertically_on_load(true);

    int width, height;
    int components;

    // Force RGBA (4 components) for consistency
    auto *data = stbi_load(filepath.data(), &width, &height, &components, 4);

    if (data == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "stbi_load %s failed: %s\n",
                     filepath.data(), stbi_failure_reason());
        return false;
    }

    glGenTextures(1, &mTextureId);
    glActiveTexture(GL_TEXTURE0 + channelOffset);
    glBindTexture(GL_TEXTURE_2D, mTextureId);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload texture data - now we know it's always RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);

    // Generate mipmaps for better quality
    glGenerateMipmap(GL_TEXTURE_2D);

    // Check for OpenGL errors
    if (const GLenum error = glGetError(); error != GL_NO_ERROR)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "OpenGL error after loading %s: 0x%x\n",
                     filepath.data(), error);
        stbi_image_free(data);
        return false;
    }

    mWidth = width;
    mHeight = height;
    mBytes = data;
    stbi_image_free(data);

    return true;
}

bool Texture::loadProceduralTextures(int width, int height,
    const std::function<void(std::vector<std::uint8_t>&, int, int)> &generator,
    const std::uint32_t channelOffset) noexcept
{
    this->free();

    // If no generator provided, create an RGBA32F texture for render targets (path tracer)
    if (!generator)
    {
        return loadRGBA32F(width, height, channelOffset);
    }

    // Otherwise, create procedural R8 texture (noise, etc.)
    std::vector<unsigned char> data(static_cast<size_t>(width) * static_cast<size_t>(height));
    generator(data, width, height);

    glGenTextures(1, &mTextureId);
    glActiveTexture(GL_TEXTURE0 + channelOffset);
    glBindTexture(GL_TEXTURE_2D, mTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    mWidth = width;
    mHeight = height;
    mBytes = nullptr; // Do not store pointer to local vector data

    return true;
}

bool Texture::loadFromMemory(const std::uint8_t *data, const int width, const int height,
                             const std::uint32_t channelOffset, const bool rotate_180) noexcept
{
    if (data == nullptr || width <= 0 || height <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid parameters for loadFromMemory\n");
        return false;
    }

    this->free();

    // If rotation requested, create rotated copy
    const std::uint8_t *upload_data = data;
    std::vector<std::uint8_t> rotated_buffer;

    if (rotate_180)
    {
        rotated_buffer = rotate180(data, width, height);
        if (rotated_buffer.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create rotated texture copy\n");
            return false;
        }
        upload_data = rotated_buffer.data();
    }

    mBytes = const_cast<std::uint8_t *>(data);

    glGenTextures(1, &mTextureId);
    glActiveTexture(GL_TEXTURE0 + channelOffset);
    glBindTexture(GL_TEXTURE_2D, mTextureId);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload texture data - RGBA format (potentially rotated)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, upload_data);

    // Generate mipmaps
    glGenerateMipmap(GL_TEXTURE_2D);

    // Check for OpenGL errors
    if (const GLenum error = glGetError(); error != GL_NO_ERROR)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "OpenGL error after loading from memory: 0x%x\n", error);
        return false;
    }

    mWidth = width;
    mHeight = height;

    return true;
}

bool Texture::updateFromMemory(const std::uint8_t *data, const int width, const int height,
                               const std::uint32_t channelOffset, const bool rotate_180) noexcept
{
    if (data == nullptr || width <= 0 || height <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid parameters for updateFromMemory\n");
        return false;
    }

    // Enforce size constraints
    if (width > MAX_TEXTURE_WIDTH || height > MAX_TEXTURE_HEIGHT)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "Texture dimensions %dx%d exceed maximum %dx%d\n",
                     width, height, MAX_TEXTURE_WIDTH, MAX_TEXTURE_HEIGHT);
        return false;
    }

    mBytes = const_cast<std::uint8_t *>(data);

    // If texture doesn't exist or dimensions changed, reallocate
    if (mTextureId == 0 || mWidth != width || mHeight != height)
    {
        return loadFromMemory(data, width, height, channelOffset, rotate_180);
    }

    // If rotation requested, create rotated copy
    const std::uint8_t *upload_data = data;
    std::vector<std::uint8_t> rotated_buffer;

    if (rotate_180)
    {
        rotated_buffer = rotate180(data, width, height);
        if (rotated_buffer.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create rotated texture copy\n");
            return false;
        }
        upload_data = rotated_buffer.data();
    }

    // Efficient update using glTexSubImage2D (reuses existing texture)
    glActiveTexture(GL_TEXTURE0 + channelOffset);
    glBindTexture(GL_TEXTURE_2D, mTextureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                    GL_UNSIGNED_BYTE, upload_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    if (const GLenum error = glGetError(); error != GL_NO_ERROR)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "OpenGL error in updateFromMemory: 0x%x\n", error);
        return false;
    }

    return true;
}
