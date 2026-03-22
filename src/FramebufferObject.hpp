#ifndef FRAMEBUFFER_OBJECT_HPP
#define FRAMEBUFFER_OBJECT_HPP

#include <glad/glad.h>
#include <string_view>

/// @brief RAII wrapper for an OpenGL Framebuffer Object (FBO) with optional Renderbuffer (RBO).
/// Manages the lifetime of a single FBO and optionally a depth/stencil RBO.
class FramebufferObject
{
public:
    FramebufferObject();
    ~FramebufferObject() noexcept;

    FramebufferObject(const FramebufferObject &) = delete;
    FramebufferObject &operator=(const FramebufferObject &) = delete;

    FramebufferObject(FramebufferObject &&other) noexcept;
    FramebufferObject &operator=(FramebufferObject &&other) noexcept;

    /// @brief Allocates the FBO via glGenFramebuffers.
    /// Called by ResourceManager::load(id, filename) through the loadFromFile interface.
    /// @return true if the FBO was created successfully
    bool loadFromFile(std::string_view unused);

    /// @brief Bind this FBO as the current framebuffer.
    void bind(GLenum target = GL_FRAMEBUFFER) const noexcept;

    /// @brief Unbind any currently bound FBO (bind 0).
    static void unbind(GLenum target = GL_FRAMEBUFFER) noexcept;

    /// @brief Get the raw OpenGL FBO name.
    [[nodiscard]] GLuint get() const noexcept { return mFBO; }

    /// @brief Check if this FBO holds a valid OpenGL name.
    [[nodiscard]] bool isValid() const noexcept { return mFBO != 0; }

    /// @brief Create the optional renderbuffer (glGenRenderbuffers).
    void createRenderbuffer() noexcept;

    /// @brief Get the raw OpenGL RBO name.
    [[nodiscard]] GLuint getRenderbuffer() const noexcept { return mRBO; }

    /// @brief Bind the associated renderbuffer.
    void bindRenderbuffer() const noexcept;

    /// @brief Unbind any currently bound renderbuffer (bind 0).
    static void unbindRenderbuffer() noexcept;

    /// @brief Release FBO and RBO, deleting the OpenGL resources.
    void cleanUp() noexcept;

private:
    GLuint mFBO{0};
    GLuint mRBO{0};
};

#endif // FRAMEBUFFER_OBJECT_HPP
