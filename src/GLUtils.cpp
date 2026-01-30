#include "GLUtils.hpp"

#include <cstdio>
#include <glad/glad.h>

bool GLUtils::CheckForOpenGLError(const std::string& file, int line)
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

        printf("glError in file %s @ line %d, error message: %s\n", file.c_str(), line, message.c_str());
        glErr = glGetError();
        error = true;
    }
    return error;
}

void GLUtils::GlDebugCallback(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const GLchar* msg, const void* param)
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
        case GL_DEBUG_SEVERITY_LOW:
            severityStr = "LOW";
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION:
            severityStr = "NOTIFICATION";
            break;
        default:
            severityStr = "UNKNOWN";
    }

    printf("GL DEBUG - Source: %s, Type: %s, Severity: %s, Message: %s\n",
           sourceStr.c_str(), typeStr.c_str(), severityStr.c_str(), msg);
}
