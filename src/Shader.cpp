#include "Shader.hpp"

#include <SDL3/SDL_log.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <glm/gtc/type_ptr.hpp>

using ShaderType = Shader::ShaderType;

void Shader::compileAndAttachShader(ShaderType shaderType, const std::string &filename)
{
    createProgram();

    std::string shaderCode = "";
    std::ifstream shaderFileStream;
    shaderFileStream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try
    {
        shaderFileStream.open(filename);
        std::stringstream shaderStrStream;
        shaderStrStream << shaderFileStream.rdbuf();
        shaderFileStream.close();
        shaderCode = shaderStrStream.str();
    }
    catch (const std::ifstream::failure &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: %s", filename.c_str());
        return;
    }

    mFileNames.emplace(shaderType, filename);
    GLuint shaderId = compile(shaderType, shaderCode);

    if (shaderId != 0)
    {
        attach(shaderId);
        deleteShader(shaderId);
    }
}

void Shader::compileAndAttachShader(ShaderType shaderType, const std::string &codeId, const GLchar *code)
{
    createProgram();

    mFileNames.emplace(shaderType, codeId);
    GLuint shaderId = compile(shaderType, code);

    if (shaderId != 0)
    {
        attach(shaderId);
        deleteShader(shaderId);
    }
}

void Shader::linkProgram()
{
    glLinkProgram(mProgram);

    GLint success;
    GLchar infoLog[512];

    glGetProgramiv(mProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(mProgram, 512, nullptr, infoLog);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Program link failed: %s\n", infoLog);
    }
}

void Shader::bind() const
{
    glUseProgram(mProgram);
}

void Shader::release() const
{
    glUseProgram(0);
}

void Shader::cleanUp()
{
    if (mProgram)
    {
        deleteProgram(mProgram);
    }
    mGLSLLocations.clear();
    mFileNames.clear();
}

std::string Shader::getGLSLUniforms() const
{
    GLint numUniforms = 0;
    glGetProgramInterfaceiv(mProgram, GL_UNIFORM, GL_ACTIVE_RESOURCES, &numUniforms);
    GLenum properties[] = {GL_NAME_LENGTH, GL_TYPE, GL_LOCATION, GL_BLOCK_INDEX};

    std::string retString = "\t(Active) GLSL Uniforms:\n";

    for (int i = 0; i != numUniforms; ++i)
    {
        GLint results[4];
        glGetProgramResourceiv(mProgram, GL_UNIFORM, i, 4, properties, 4, nullptr, results);

        if (results[3] != -1)
            continue; // skip block uniforms here

        GLint nameBufSize = results[0] + 1;
        char *name = new char[nameBufSize];

        glGetProgramResourceName(mProgram, GL_UNIFORM, i, nameBufSize, nullptr, name);

        retString += "\tlocation = " + std::to_string(results[2]) + ", name = " + name + ", type = " + getStringFromType(results[1]) + "\n";

        delete[] name;
    }

    return retString;
}

std::string Shader::getGLSLAttribs() const
{
    GLint numAttribs;
    glGetProgramInterfaceiv(mProgram, GL_PROGRAM_INPUT, GL_ACTIVE_RESOURCES, &numAttribs);
    GLenum properties[] = {GL_NAME_LENGTH, GL_TYPE, GL_LOCATION};

    std::string retString = "\t(Active) GLSL Attributes:\n";

    for (int i = 0; i != numAttribs; ++i)
    {
        GLint results[3];
        glGetProgramResourceiv(mProgram, GL_PROGRAM_INPUT, i, 3, properties, 3, nullptr, results);

        GLint nameBufSize = results[0] + 1;
        char *name = new char[nameBufSize];

        glGetProgramResourceName(mProgram, GL_PROGRAM_INPUT, i, nameBufSize, nullptr, name);

        retString += "\tlocation = " + std::to_string(results[2]) + ", name = " + name + ", type = " + getStringFromType(results[1]) + "\n";

        delete[] name;
    }
    return retString;
}

void Shader::setUniform(const std::string &str, const glm::mat3 &matrix)
{
    glUniformMatrix3fv(getUniformLocation(str), 1, GL_FALSE, glm::value_ptr(matrix));
}

void Shader::setUniform(const std::string &str, const glm::mat4 &matrix)
{
    glUniformMatrix4fv(getUniformLocation(str), 1, GL_FALSE, glm::value_ptr(matrix));
}

void Shader::setUniform(const std::string &str, const glm::vec2 &vec)
{
    glUniform2f(getUniformLocation(str), vec.x, vec.y);
}

void Shader::setUniform(const std::string &str, const glm::vec3 &vec)
{
    glUniform3f(getUniformLocation(str), vec.x, vec.y, vec.z);
}

void Shader::setUniform(const std::string &str, const glm::vec4 &vec)
{
    glUniform4f(getUniformLocation(str), vec.x, vec.y, vec.z, vec.w);
}

void Shader::setUniform(const std::string &str, const glm::mat4 *matrices, unsigned int count)
{
    if (!matrices || count == 0)
    {
        return;
    }

    glUniformMatrix4fv(getUniformLocation(str), count, GL_FALSE, glm::value_ptr(matrices[0]));
}

void Shader::setUniform(const std::string &str, GLfloat arr[][2], unsigned int count)
{
    glUniform2fv(getUniformLocation(str), count, arr[0]);
}

void Shader::setUniform(const std::string &str, GLint arr[], const unsigned int count)
{
    glUniform1iv(getUniformLocation(str), count, arr);
}

void Shader::setUniform(const std::string &str, GLfloat arr[], unsigned int count)
{
    glUniform1fv(getUniformLocation(str), count, arr);
}

void Shader::setUniform(const std::string &str, GLfloat value)
{
    glUniform1f(getUniformLocation(str), value);
}

void Shader::setUniform(const std::string &str, GLdouble value)
{
    glUniform1d(getUniformLocation(str), value);
}

void Shader::setUniform(const std::string &str, GLint value)
{
    glUniform1i(getUniformLocation(str), value);
}

void Shader::setUniform(const std::string &str, GLuint value)
{
    glUniform1ui(getUniformLocation(str), value);
}

void Shader::setSubroutine(GLenum shaderType, GLuint count, const std::string &name)
{
    GLuint loc = getSubroutineLocation(shaderType, name);
    glUniformSubroutinesuiv(shaderType, count, &loc);
}

void Shader::setSubroutine(GLenum shaderType, GLuint count, GLuint index)
{
    glUniformSubroutinesuiv(shaderType, count, &index);
}

void Shader::bindFragDataLocation(const std::string &str, GLuint loc)
{
    glBindFragDataLocation(mProgram, loc, str.c_str());
}

void Shader::bindAttribLocation(const std::string &str, GLuint loc)
{
    glBindAttribLocation(mProgram, loc, str.c_str());
}

unsigned int Shader::getProgramHandle() const
{
    return static_cast<unsigned int>(mProgram);
}

GLenum Shader::getShaderType(ShaderType shaderType) const
{
    switch (shaderType)
    {
    case ShaderType::VERTEX:
        return GL_VERTEX_SHADER;
    case ShaderType::FRAGMENT:
        return GL_FRAGMENT_SHADER;
    case ShaderType::GEOMETRY:
        return GL_GEOMETRY_SHADER;
    case ShaderType::TESSELATION_CONTROL:
        return GL_TESS_CONTROL_SHADER;
    case ShaderType::TESSELATION_EVAL:
        return GL_TESS_EVALUATION_SHADER;
    case ShaderType::COMPUTE:
        return GL_COMPUTE_SHADER;
    default:
        return GL_VERTEX_SHADER;
    }
}

std::unordered_map<std::string, GLint> Shader::getGLSLLocations() const
{
    return mGLSLLocations;
}

std::unordered_map<ShaderType, std::string> Shader::getFileNames() const
{
    return mFileNames;
}

GLuint Shader::compile(ShaderType shaderType, const std::string &shaderCode)
{
    GLint length = static_cast<GLint>(shaderCode.length());
    const GLchar *glShaderString = shaderCode.c_str();

    GLenum glShaderType = getShaderType(shaderType);

    GLint success;
    GLchar infoLog[512];

    GLuint shaderId = glCreateShader(glShaderType);

    glShaderSource(shaderId, 1, &glShaderString, &length);
    glCompileShader(shaderId);
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &length);
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        glGetShaderInfoLog(shaderId, 512, nullptr, infoLog);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s -- Shader Compilation Failed: %s", mFileNames.at(shaderType).c_str(), infoLog);
        return 0;
    }

    return shaderId;
}

GLuint Shader::compile(ShaderType shaderType, const GLchar *shaderCode)
{
    GLint length = static_cast<GLint>(std::strlen(shaderCode));

    GLenum glShaderType = getShaderType(shaderType);

    GLint success;
    GLchar infoLog[512];

    GLuint shaderId = glCreateShader(glShaderType);

    glShaderSource(shaderId, 1, &shaderCode, &length);
    glCompileShader(shaderId);
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &length);
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        glGetShaderInfoLog(shaderId, 512, nullptr, infoLog);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s -- Shader Compilation Failed: %s", mFileNames.at(shaderType).c_str(), infoLog);
        return 0;
    }

    return shaderId;
}

void Shader::attach(GLuint shaderId)
{
    glAttachShader(mProgram, shaderId);
}

void Shader::createProgram()
{
    mProgram = (mProgram == 0) ? glCreateProgram() : mProgram;
}

void Shader::deleteShader(GLuint shaderId)
{
    glDeleteShader(shaderId);
}

void Shader::deleteProgram(GLint shaderId)
{
    glDeleteProgram(shaderId);
}

GLint Shader::getUniformLocation(const std::string &str)
{
    auto iter = mGLSLLocations.find(str);
    if (iter == mGLSLLocations.end())
    {
        GLint loc = glGetUniformLocation(mProgram, str.c_str());
        if (loc == -1)
        {
            // Only warn once per uniform name per shader program
            static std::unordered_set<std::string> warnedUniforms;
            std::string key = std::to_string(mProgram) + ":" + str;
            if (warnedUniforms.find(key) == warnedUniforms.end())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s does not exist in the shader (program %d)", str.c_str(), mProgram);
                warnedUniforms.insert(key);
            }
        }
        else
        {
            mGLSLLocations.emplace(str, loc);
        }

        return loc;
    }
    else
    {
        return mGLSLLocations.at(str);
    }
}

GLint Shader::getAttribLocation(const std::string &str)
{
    return glGetAttribLocation(mProgram, str.c_str());
}

GLuint Shader::getSubroutineLocation(GLenum shaderType, const std::string &name)
{
    return glGetSubroutineIndex(mProgram, shaderType, name.c_str());
}

std::string Shader::getStringFromType(GLenum type) const
{
    switch (type)
    {
    case GL_FLOAT:
        return "float";
    case GL_FLOAT_VEC2:
        return "vec2";
    case GL_FLOAT_VEC3:
        return "vec3";
    case GL_FLOAT_VEC4:
        return "vec4";
    case GL_DOUBLE:
        return "double";
    case GL_INT:
        return "int";
    case GL_UNSIGNED_INT:
        return "unsigned int";
    case GL_BOOL:
        return "bool";
    case GL_FLOAT_MAT2:
        return "mat2";
    case GL_FLOAT_MAT3:
        return "mat3";
    case GL_FLOAT_MAT4:
        return "mat4";
    default:
        return "Unknown GLenum type.";
    }
}
