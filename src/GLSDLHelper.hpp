#ifndef GLSDL_HELPER_HPP
#define GLSDL_HELPER_HPP

#include <mutex>
#include <optional>
#include <string_view>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <SFML/Window.hpp>

struct AnimationRect;

class Shader;

class GLSDLHelper
{
public:
    void init(std::string_view title, int width, int height) noexcept;

    void destroyAndQuit() noexcept;

    [[nodiscard]] sf::Window* getWindow() const noexcept { return mWindow.value_or(nullptr); }

    static void enableRenderingFeatures() noexcept;
    static GLuint createAndBindVAO() noexcept;
    static GLuint createAndBindSSBO(GLuint bindingPoint) noexcept;
    static void allocateSSBOBuffer(GLsizeiptr bufferSize, const void* data = nullptr) noexcept;
    static void updateSSBOBuffer(GLintptr offset, GLsizeiptr size, const void* data) noexcept;
    static void deleteVAO(GLuint& vao) noexcept;
    static void deleteBuffer(GLuint& buffer) noexcept;
    static void deleteTexture(GLuint& texture) noexcept;

    static void initializeBillboardRendering() noexcept;
    static void cleanupBillboardRendering() noexcept;

    static void renderBillboardSprite(
        Shader& billboardShader,
        GLuint textureId,
        const AnimationRect& frameRect,
        const glm::vec3& worldPosition,
        float halfSize,
        const glm::mat4& viewMatrix,
        const glm::mat4& projMatrix,
        int sheetWidth,
        int sheetHeight) noexcept;

    static void renderBillboardSpriteUV(
        Shader& billboardShader,
        GLuint textureId,
        const glm::vec4& uvRect,
        const glm::vec3& worldPosition,
        float halfSize,
        const glm::mat4& viewMatrix,
        const glm::mat4& projMatrix,
        const glm::vec4& tintColor,
        bool flipX,
        bool flipY,
        bool useRedAsAlpha,
        const glm::vec2& halfSizeXY = glm::vec2(0.0f),
        bool useWorldAxes = false,
        const glm::vec3& rightAxisWS = glm::vec3(1.0f, 0.0f, 0.0f),
        const glm::vec3& upAxisWS = glm::vec3(0.0f, 1.0f, 0.0f),
        bool doubleSided = false) noexcept;

    [[nodiscard]] static bool isBillboardInitialized() noexcept;
    static void setBillboardOITPass(bool enabled) noexcept;

private:
    std::once_flag mInitializedFlag;
    std::optional<sf::Window*> mWindow;

    static GLuint sBillboardVAO;
    static GLuint sBillboardVBO;
    static bool sBillboardInitialized;
    static bool sBillboardOITPass;
};

#endif // GLSDL_HELPER_HPP
