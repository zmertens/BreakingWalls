#ifndef GLSDLHELPER_HPP
#define GLSDLHELPER_HPP

#include <mutex>
#include <string_view>

#include <SDL3/SDL.h>
#include <glad/glad.h>

struct SDL_Window;

class GLSDLHelper
{
public:
    SDL_GLContext mGLContext{};
    SDL_Window* mWindow{ nullptr };

private:
    std::once_flag mInitializedFlag;

public:
    void init(std::string_view title, int width, int height) noexcept;

    void destroyAndQuit() noexcept;

    // Static helper functions for common OpenGL operations

    /// Enable common OpenGL features for rendering (multisample, depth test, cull face)
    static void enableRenderingFeatures() noexcept;

    /// Create a Vertex Array Object and bind it
    /// @return The generated VAO handle
    static GLuint createAndBindVAO() noexcept;

    /// Create a Shader Storage Buffer Object and bind it to the specified binding point
    /// @param bindingPoint The binding point for the SSBO
    /// @return The generated SSBO handle
    static GLuint createAndBindSSBO(GLuint bindingPoint) noexcept;

    /// Allocate SSBO buffer with specified capacity
    /// @param bufferSize Size in bytes to allocate
    /// @param data Optional initial data (can be nullptr)
    static void allocateSSBOBuffer(GLsizeiptr bufferSize, const void* data = nullptr) noexcept;

    /// Update SSBO buffer data
    /// @param offset Offset in bytes
    /// @param size Size in bytes
    /// @param data Pointer to the data
    static void updateSSBOBuffer(GLintptr offset, GLsizeiptr size, const void* data) noexcept;

    /// Create a 2D texture for path tracing with RGBA32F format
    /// @param width Texture width
    /// @param height Texture height
    /// @return The generated texture handle
    static GLuint createPathTracerTexture(GLsizei width, GLsizei height) noexcept;

    /// Delete a Vertex Array Object
    /// @param vao Reference to VAO handle (will be set to 0 after deletion)
    static void deleteVAO(GLuint& vao) noexcept;

    /// Delete a buffer object (SSBO, VBO, etc.)
    /// @param buffer Reference to buffer handle (will be set to 0 after deletion)
    static void deleteBuffer(GLuint& buffer) noexcept;

    /// Delete a texture
    /// @param texture Reference to texture handle (will be set to 0 after deletion)
    static void deleteTexture(GLuint& texture) noexcept;

}; // GLSDLHelper class

#endif // GLSDLHELPER_HPP
