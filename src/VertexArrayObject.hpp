#ifndef VERTEX_ARRAY_OBJECT_HPP
#define VERTEX_ARRAY_OBJECT_HPP

#include <glad/glad.h>
#include <string_view>

/// @brief RAII wrapper for an OpenGL Vertex Array Object (VAO).
/// Manages the lifetime of a single VAO, deleting it on destruction.
class VertexArrayObject
{
public:
    VertexArrayObject();
    ~VertexArrayObject() noexcept;

    VertexArrayObject(const VertexArrayObject &) = delete;
    VertexArrayObject &operator=(const VertexArrayObject &) = delete;

    VertexArrayObject(VertexArrayObject &&other) noexcept;
    VertexArrayObject &operator=(VertexArrayObject &&other) noexcept;

    /// @brief Allocates the VAO via glGenVertexArrays.
    /// Called by ResourceManager::load(id, filename) through the loadFromFile interface.
    /// The filename parameter is unused; the VAO is simply allocated.
    /// @return true if the VAO was created successfully
    bool loadFromFile(std::string_view unused);

    /// @brief Bind this VAO as the current vertex array.
    void bind() const noexcept;

    /// @brief Unbind any currently bound VAO (bind 0).
    static void unbind() noexcept;

    /// @brief Get the raw OpenGL VAO name.
    [[nodiscard]] GLuint get() const noexcept { return mVAO; }

    /// @brief Check if this VAO holds a valid OpenGL name.
    [[nodiscard]] bool isValid() const noexcept { return mVAO != 0; }

    /// @brief Release the VAO, deleting the OpenGL resource.
    void cleanUp() noexcept;

private:
    GLuint mVAO{0};
};

#endif // VERTEX_ARRAY_OBJECT_HPP
