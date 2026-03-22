#ifndef VERTEX_BUFFER_OBJECT_HPP
#define VERTEX_BUFFER_OBJECT_HPP

#include <glad/glad.h>
#include <string_view>

/// @brief RAII wrapper for an OpenGL Buffer Object (VBO, SSBO, etc.).
/// Manages the lifetime of a single buffer, deleting it on destruction.
class VertexBufferObject
{
public:
    VertexBufferObject();
    ~VertexBufferObject() noexcept;

    VertexBufferObject(const VertexBufferObject &) = delete;
    VertexBufferObject &operator=(const VertexBufferObject &) = delete;

    VertexBufferObject(VertexBufferObject &&other) noexcept;
    VertexBufferObject &operator=(VertexBufferObject &&other) noexcept;

    /// @brief Allocates the buffer via glGenBuffers.
    /// Called by ResourceManager::load(id, filename) through the loadFromFile interface.
    /// @return true if the buffer was created successfully
    bool loadFromFile(std::string_view unused);

    /// @brief Bind this buffer to the given target.
    void bind(GLenum target = GL_ARRAY_BUFFER) const noexcept;

    /// @brief Unbind the given target (bind 0).
    static void unbind(GLenum target = GL_ARRAY_BUFFER) noexcept;

    /// @brief Get the raw OpenGL buffer name.
    [[nodiscard]] GLuint get() const noexcept { return mVBO; }

    /// @brief Check if this buffer holds a valid OpenGL name.
    [[nodiscard]] bool isValid() const noexcept { return mVBO != 0; }

    /// @brief Release the buffer, deleting the OpenGL resource.
    void cleanUp() noexcept;

private:
    GLuint mVBO{0};
};

#endif // VERTEX_BUFFER_OBJECT_HPP
