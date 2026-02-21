#ifndef GLSDL_HELPER_HPP
#define GLSDL_HELPER_HPP

#include <mutex>
#include <string_view>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

struct AnimationRect;
struct SDL_Window;

class Shader;

class GLSDLHelper
{
public:
    SDL_GLContext mGLContext{};
    SDL_Window *mWindow{nullptr};

private:
    std::once_flag mInitializedFlag;

    // Billboard sprite rendering resources
    static GLuint sBillboardVAO;
    static GLuint sBillboardVBO;
    static bool sBillboardInitialized;

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
    static void allocateSSBOBuffer(GLsizeiptr bufferSize, const void *data = nullptr) noexcept;

    /// Update SSBO buffer data
    /// @param offset Offset in bytes
    /// @param size Size in bytes
    /// @param data Pointer to the data
    static void updateSSBOBuffer(GLintptr offset, GLsizeiptr size, const void *data) noexcept;
    
    /// Delete a Vertex Array Object
    /// @param vao Reference to VAO handle (will be set to 0 after deletion)
    static void deleteVAO(GLuint &vao) noexcept;

    /// Delete a buffer object (SSBO, VBO, etc.)
    /// @param buffer Reference to buffer handle (will be set to 0 after deletion)
    static void deleteBuffer(GLuint &buffer) noexcept;

    /// Delete a texture
    /// @param texture Reference to texture handle (will be set to 0 after deletion)
    static void deleteTexture(GLuint &texture) noexcept;

    // ========================================================================
    // Billboard sprite rendering for characters (uses geometry shader)
    // ========================================================================

    /// Initialize billboard rendering resources (VAO/VBO for point sprites)
    static void initializeBillboardRendering() noexcept;

    /// Cleanup billboard rendering resources
    static void cleanupBillboardRendering() noexcept;

    /// Render a sprite from a sprite sheet as a 3D billboard using geometry shader
    /// @param billboardShader The billboard shader program to use
    /// @param textureId OpenGL texture ID of the sprite sheet
    /// @param frameRect Current animation frame rectangle
    /// @param worldPosition 3D position in world space
    /// @param size Half-size of the billboard
    /// @param viewMatrix Camera view matrix
    /// @param projMatrix Camera projection matrix
    /// @param sheetWidth Total width of sprite sheet in pixels
    /// @param sheetHeight Total height of sprite sheet in pixels
    static void renderBillboardSprite(
        Shader &billboardShader,
        GLuint textureId,
        const AnimationRect &frameRect,
        const glm::vec3 &worldPosition,
        float halfSize,
        const glm::mat4 &viewMatrix,
        const glm::mat4 &projMatrix,
        int sheetWidth,
        int sheetHeight) noexcept;

    /// Render billboard using explicit UV rect (u0, v0, u1, v1) with tint and optional alpha-mask sampling
    static void renderBillboardSpriteUV(
        Shader &billboardShader,
        GLuint textureId,
        const glm::vec4 &uvRect,
        const glm::vec3 &worldPosition,
        float halfSize,
        const glm::mat4 &viewMatrix,
        const glm::mat4 &projMatrix,
        const glm::vec4 &tintColor,
        bool flipX,
        bool flipY,
        bool useRedAsAlpha,
        const glm::vec2 &halfSizeXY = glm::vec2(0.0f)) noexcept;

    /// Check if billboard rendering is initialized
    [[nodiscard]] static bool isBillboardInitialized() noexcept { return sBillboardInitialized; }
};

#endif // GLSDL_HELPER_HPP
