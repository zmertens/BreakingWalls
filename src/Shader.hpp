#ifndef SHADER_HPP
#define SHADER_HPP

#include <memory>
#include <string>
#include <unordered_map>

#include <glad/glad.h>
#include <glm/glm.hpp>

/// @brief Shader type enumeration
enum class ShaderType
{
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    TESSELATION_CONTROL,
    TESSELATION_EVAL,
    COMPUTE
};

/// @brief Manages OpenGL shader programs with automatic uniform location caching
/// @details Supports vertex, fragment, geometry, tessellation, and compute shaders
/// Provides type-safe uniform setters and shader introspection capabilities
class Shader final
{
public:
    typedef std::unique_ptr<Shader> Ptr;
public:
    explicit Shader();
    virtual ~Shader();

    /// Compile and attach a shader from file
    void compileAndAttachShader(ShaderType shaderType, const std::string& filename);
    
    /// Compile and attach a shader from memory
    void compileAndAttachShader(ShaderType shaderType, const std::string& codeId, const GLchar* code);
    
    /// Link the shader program
    void linkProgram();
    
    /// Bind this shader program for use
    void bind() const;
    
    /// Release the current shader program
    void release() const;

    /// Clean up shader resources
    void cleanUp();

    /// Get introspection info about active uniforms
    std::string getGlslUniforms() const;
    
    /// Get introspection info about active attributes
    std::string getGlslAttribs() const;

    // Type-safe uniform setters
    void setUniform(const std::string& str, const glm::mat3& matrix);
    void setUniform(const std::string& str, const glm::mat4& matrix);
    void setUniform(const std::string& str, const glm::vec2& vec);
    void setUniform(const std::string& str, const glm::vec3& vec);
    void setUniform(const std::string& str, const glm::vec4& vec);
    void setUniform(const std::string& str, GLfloat arr[][2], unsigned int count);
    void setUniform(const std::string& str, GLint arr[], unsigned int count);
    void setUniform(const std::string& str, GLfloat arr[], unsigned int count);
    void setUniform(const std::string& str, GLfloat value);
    void setUniform(const std::string& str, GLdouble value);
    void setUniform(const std::string& str, GLint value);
    void setUniform(const std::string& str, GLuint value);

    void setSubroutine(GLenum shaderType, GLuint count, const std::string& name);
    void setSubroutine(GLenum shaderType, GLuint count, GLuint index);

    void bindFragDataLocation(const std::string& str, GLuint loc);
    void bindAttribLocation(const std::string& str, GLuint loc);

    [[nodiscard]] unsigned int getProgramHandle() const;
    [[nodiscard]] GLenum getShaderType(ShaderType shaderType) const;
    [[nodiscard]] std::unordered_map<std::string, GLint> getGlslLocations() const;
    [[nodiscard]] std::unordered_map<ShaderType, std::string> getFileNames() const;

private:
    GLint mProgram{0};
    std::unordered_map<std::string, GLint> mGlslLocations;
    std::unordered_map<ShaderType, std::string> mFileNames;

private:
    Shader(const Shader& other) = delete;
    Shader& operator=(const Shader& other) = delete;
    
    GLuint compile(ShaderType shaderType, const std::string& shaderCode);
    GLuint compile(ShaderType shaderType, const GLchar* shaderCode);
    void attach(GLuint shaderId);
    void createProgram();
    void deleteShader(GLuint shaderId);
    void deleteProgram(GLint shaderId);
    GLint getUniformLocation(const std::string& str);
    GLint getAttribLocation(const std::string& str);
    GLuint getSubroutineLocation(GLenum shaderType, const std::string& name);
    std::string getStringFromType(GLenum shaderType) const;
};

#endif // SHADER_HPP
