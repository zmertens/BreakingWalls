#ifndef GLUTILS_HPP
#define GLUTILS_HPP

#include <string>
#include <glad/glad.h>

/// @brief OpenGL utility functions for error checking and debugging
class GLUtils
{
public:
    /// Check for OpenGL errors and print debugging information
    static bool CheckForOpenGLError(const std::string& file, int line);
    
    /// OpenGL debug callback for ARB_debug_output
    static void GlDebugCallback(GLenum source, GLenum type, GLuint id,
        GLenum severity, GLsizei length, const GLchar* msg, const void* param);
};

#endif // GLUTILS_HPP
