#include "GLSDLHelper.hpp"

#include "Animation.hpp"
#include "Shader.hpp"

#include <SDL3/SDL.h>

#include <string>

#include <glad/glad.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Static member initialization for billboard rendering
GLuint GLSDLHelper::sBillboardVAO = 0;
GLuint GLSDLHelper::sBillboardVBO = 0;
bool GLSDLHelper::sBillboardInitialized = false;
bool GLSDLHelper::sBillboardOITPass = false;

namespace
{
    bool checkForOpenGLError(const std::string &file, int line)
    {
        bool error = false;
        GLenum glErr;
        glErr = glGetError();
        while (glErr != GL_NO_ERROR)
        {
            std::string message;
            switch (glErr)
            {
            case GL_INVALID_ENUM:
                message = "Invalid enum";
                break;
            case GL_INVALID_VALUE:
                message = "Invalid value";
                break;
            case GL_INVALID_OPERATION:
                message = "Invalid operation";
                break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                message = "Invalid framebuffer operation";
                break;
            case GL_OUT_OF_MEMORY:
                message = "Out of memory";
                break;
            default:
                message = "Unknown error";
            }

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "OpenGL error in file %s at line %d, error message: %s\n",
                         file.c_str(), line, message.c_str());
            glErr = glGetError();
            error = true;
        }
        return error;
    }

    void debugCallbackForOpenGL(GLenum source, GLenum type, GLuint id,
                                GLenum severity, GLsizei length, const GLchar *message, const void *param)
    {
        std::string sourceStr;
        switch (source)
        {
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
            sourceStr = "WindowSys";
            break;
        case GL_DEBUG_SOURCE_APPLICATION:
            sourceStr = "App";
            break;
        case GL_DEBUG_SOURCE_API:
            sourceStr = "OpenGL";
            break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            sourceStr = "ShaderCompiler";
            break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:
            sourceStr = "3rdParty";
            break;
        case GL_DEBUG_SOURCE_OTHER:
            sourceStr = "Other";
            break;
        default:
            sourceStr = "Unknown";
        }

        std::string typeStr;
        switch (type)
        {
        case GL_DEBUG_TYPE_ERROR:
            typeStr = "Error";
            break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            typeStr = "Deprecated";
            break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            typeStr = "Undefined";
            break;
        case GL_DEBUG_TYPE_PORTABILITY:
            typeStr = "Portability";
            break;
        case GL_DEBUG_TYPE_PERFORMANCE:
            typeStr = "Performance";
            break;
        case GL_DEBUG_TYPE_OTHER:
            typeStr = "Other";
            break;
        default:
            typeStr = "Unknown";
        }

        std::string severityStr;
        switch (severity)
        {
        case GL_DEBUG_SEVERITY_HIGH:
            severityStr = "HIGH";
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            severityStr = "MEDIUM";
            break;
        case GL_DEBUG_SEVERITY_LOW: [[fallthrough]];
        case GL_DEBUG_SEVERITY_NOTIFICATION: [[fallthrough]];
        default:
            // Ignore low severity messages to reduce log spam
            return;
        }

        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "OpenGL - Source: %s, Type: %s, Severity: %s, Message: %s\n",
                     sourceStr.c_str(), typeStr.c_str(), severityStr.c_str(), message);
    }

} // anonymous namespace

void GLSDLHelper::init(std::string_view title, int width, int height) noexcept
{
    auto initFunc = [this, title, width, height]()
    {
        if (!SDL_SetAppMetadata("Breaking Walls with physics", title.data(),
                                "c++;cozy;game;simulation;physics"))
        {
            return;
        }

        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, title.data());
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "Flips And Ale");
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "MIT License");
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "csv");
        SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, title.data());

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

#if defined(BREAKING_WALLS_DEBUG)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

        this->mWindow = SDL_CreateWindow(title.data(), width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);

        if (!this->mWindow)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return;
        }

        this->mGLContext = SDL_GL_CreateContext(this->mWindow);
        if (!this->mGLContext)
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(this->mWindow);
            return;
        }

        SDL_GL_MakeCurrent(this->mWindow, this->mGLContext);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to initialize GLAD\n");
            SDL_GL_DestroyContext(this->mGLContext);
            SDL_DestroyWindow(this->mWindow);
            return;
        }

        SDL_GL_SetSwapInterval(1);

#if defined(BREAKING_WALLS_DEBUG)
        glDebugMessageCallback(debugCallbackForOpenGL, nullptr);
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        SDL_Log("OpenGL and SDL initialized successfully.");
        SDL_Log("OpenGL Version: %s\n", glGetString(GL_VERSION));
        SDL_Log("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));
#endif
    };

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        std::call_once(mInitializedFlag, initFunc);

        // Verify audio subsystem initialized successfully
        if (SDL_WasInit(SDL_INIT_AUDIO))
        {
            SDL_Log("SDL Audio subsystem initialized successfully");

            // Log available audio drivers
            int numDrivers = SDL_GetNumAudioDrivers();
            SDL_Log("Available audio drivers: %d", numDrivers);
            for (int i = 0; i < numDrivers; ++i)
            {
                SDL_Log("  [%d] %s", i, SDL_GetAudioDriver(i));
            }

            // Log current audio driver
            if (const auto *currentDriver = SDL_GetCurrentAudioDriver())
            {
                SDL_Log("Current audio driver: %s", currentDriver);
            }
            else
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "No audio driver initialized");
            }
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDL Audio subsystem failed to initialize");
        }
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s\n", SDL_GetError());
    }
}

void GLSDLHelper::destroyAndQuit() noexcept
{
    // Cleanup billboard rendering resources
    cleanupBillboardRendering();

    if (!this->mWindow && !this->mGLContext)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Already destroyed, skipping\n");
        return;
    }

    if (mGLContext)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Destroying OpenGL context\n");
        SDL_GL_DestroyContext(mGLContext);
        mGLContext = nullptr;
    }

    if (mWindow)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Destroying window %p\n", static_cast<void *>(mWindow));
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }

    if (SDL_WasInit(0) != 0)
    {
        SDL_Log("SDLHelper::destroyAndQuit() - Calling SDL_Quit()\n");
        SDL_Quit();
    }
}

void GLSDLHelper::enableRenderingFeatures() noexcept
{
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

GLuint GLSDLHelper::createAndBindVAO() noexcept
{
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    return vao;
}

GLuint GLSDLHelper::createAndBindSSBO(GLuint bindingPoint) noexcept
{
    GLuint ssbo = 0;
    glGenBuffers(1, &ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, ssbo);
    return ssbo;
}

void GLSDLHelper::allocateSSBOBuffer(GLsizeiptr bufferSize, const void *data) noexcept
{
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, data, GL_DYNAMIC_DRAW);
}

void GLSDLHelper::updateSSBOBuffer(GLintptr offset, GLsizeiptr size, const void *data) noexcept
{
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
}

void GLSDLHelper::deleteVAO(GLuint &vao) noexcept
{
    if (vao != 0)
    {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

void GLSDLHelper::deleteBuffer(GLuint &buffer) noexcept
{
    if (buffer != 0)
    {
        glDeleteBuffers(1, &buffer);
        buffer = 0;
    }
}

void GLSDLHelper::deleteTexture(GLuint &texture) noexcept
{
    if (texture != 0)
    {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
}

// ============================================================================
// Billboard sprite rendering (geometry shader point sprites)
// ============================================================================

void GLSDLHelper::initializeBillboardRendering() noexcept
{
    if (sBillboardInitialized)
    {
        return;
    }

    // Create VAO for point sprite (single point per character)
    glGenVertexArrays(1, &sBillboardVAO);
    glGenBuffers(1, &sBillboardVBO);

    glBindVertexArray(sBillboardVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sBillboardVBO);

    // Single point at origin - the model matrix will position it
    float pointData[3] = {0.0f, 0.0f, 0.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(pointData), pointData, GL_STATIC_DRAW);

    // Position attribute (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    sBillboardInitialized = true;
}

void GLSDLHelper::cleanupBillboardRendering() noexcept
{
    if (!sBillboardInitialized)
    {
        return;
    }

    if (sBillboardVAO != 0)
    {
        glDeleteVertexArrays(1, &sBillboardVAO);
        sBillboardVAO = 0;
    }

    if (sBillboardVBO != 0)
    {
        glDeleteBuffers(1, &sBillboardVBO);
        sBillboardVBO = 0;
    }

    sBillboardInitialized = false;
}

void GLSDLHelper::renderBillboardSprite(
    Shader &billboardShader,
    GLuint textureId,
    const AnimationRect &frameRect,
    const glm::vec3 &worldPosition,
    float halfSize,
    const glm::mat4 &viewMatrix,
    const glm::mat4 &projMatrix,
    int sheetWidth,
    int sheetHeight) noexcept
{
    if (!sBillboardInitialized)
    {
        initializeBillboardRendering();
    }

    if (textureId == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Billboard: Invalid texture ID");
        return;
    }

    // Save current OpenGL state
    GLboolean depthTestEnabled;
    GLboolean blendEnabled;
    GLboolean cullFaceEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    glGetBooleanv(GL_BLEND, &blendEnabled);
    glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    if (!sBillboardOITPass)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Disable face culling for billboards
    glDisable(GL_CULL_FACE);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Bind and use the billboard shader
    billboardShader.bind();

    // Calculate ModelView matrix - position the point in world space
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), worldPosition);
    glm::mat4 modelViewMatrix = viewMatrix * modelMatrix;

    // Calculate UV rect for sprite sheet (normalized 0-1 coordinates)
    float u = static_cast<float>(frameRect.left) / static_cast<float>(sheetWidth);
    float v = static_cast<float>(frameRect.top) / static_cast<float>(sheetHeight);
    float uWidth = static_cast<float>(frameRect.width) / static_cast<float>(sheetWidth);
    float vHeight = static_cast<float>(frameRect.height) / static_cast<float>(sheetHeight);

    const glm::vec4 uvRect(u, v, u + uWidth, v + vHeight);

    renderBillboardSpriteUV(
        billboardShader,
        textureId,
        uvRect,
        worldPosition,
        halfSize,
        viewMatrix,
        projMatrix,
        glm::vec4(1.0f),
        true,
        true,
        false);

    checkForOpenGLError(__FILE__, __LINE__);

    // Restore previous OpenGL state
    if (!blendEnabled)
    {
        glDisable(GL_BLEND);
    }
    if (cullFaceEnabled)
    {
        glEnable(GL_CULL_FACE);
    }
    if (!depthTestEnabled)
    {
        glDisable(GL_DEPTH_TEST);
    }
}

void GLSDLHelper::renderBillboardSpriteUV(
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
    const glm::vec2 &halfSizeXY) noexcept
{
    if (!sBillboardInitialized)
    {
        initializeBillboardRendering();
    }

    if (textureId == 0)
    {
        return;
    }

    billboardShader.bind();

    const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), worldPosition);
    const glm::mat4 modelViewMatrix = viewMatrix * modelMatrix;

    const float uMin = std::min(uvRect.x, uvRect.z);
    const float vMin = std::min(uvRect.y, uvRect.w);
    const float uMax = std::max(uvRect.x, uvRect.z);
    const float vMax = std::max(uvRect.y, uvRect.w);

    billboardShader.setUniform("ModelViewMatrix", modelViewMatrix);
    billboardShader.setUniform("ProjectionMatrix", projMatrix);
    billboardShader.setUniform("Size2", halfSize);
    billboardShader.setUniform("SizeXY", halfSizeXY);
    billboardShader.setUniform("TexRect", glm::vec4(uMin, vMin, uMax, vMax));
    billboardShader.setUniform("SpriteTex", static_cast<GLint>(0));
    billboardShader.setUniform("TintColor", tintColor);
    billboardShader.setUniform("FlipX", static_cast<GLint>(flipX ? 1 : 0));
    billboardShader.setUniform("FlipY", static_cast<GLint>(flipY ? 1 : 0));
    billboardShader.setUniform("UseRedAsAlpha", static_cast<GLint>(useRedAsAlpha ? 1 : 0));
    billboardShader.setUniform("uOITPass", static_cast<GLint>(sBillboardOITPass ? 1 : 0));
    billboardShader.setUniform("uOITWeightScale", 6.0f);

    GLint prevActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glBindVertexArray(sBillboardVAO);
    glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);

    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
}

bool GLSDLHelper::isBillboardInitialized() noexcept
{ 
    return sBillboardInitialized;
}

void GLSDLHelper::setBillboardOITPass(bool enabled) noexcept
{
    sBillboardOITPass = enabled;
}
