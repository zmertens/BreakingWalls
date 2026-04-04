#include "VertexBufferObject.hpp"

#include <SDL3/SDL.h>

VertexBufferObject::VertexBufferObject() = default;

VertexBufferObject::~VertexBufferObject() noexcept
{
    cleanUp();
}

VertexBufferObject::VertexBufferObject(VertexBufferObject &&other) noexcept
    : mVBO{other.mVBO}
{
    other.mVBO = 0;
}

VertexBufferObject &VertexBufferObject::operator=(VertexBufferObject &&other) noexcept
{
    if (this != &other)
    {
        cleanUp();
        mVBO = other.mVBO;
        other.mVBO = 0;
    }
    return *this;
}

bool VertexBufferObject::loadFromFile(std::string_view /*unused*/)
{
    if (mVBO != 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "VertexBufferObject::loadFromFile - buffer already allocated (%u)", mVBO);
        return true;
    }
    glGenBuffers(1, &mVBO);
    if (mVBO == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "VertexBufferObject::loadFromFile - glGenBuffers failed");
        return false;
    }
    return true;
}

void VertexBufferObject::bind(GLenum target) const noexcept
{
    glBindBuffer(target, mVBO);
}

void VertexBufferObject::unbind(GLenum target) noexcept
{
    glBindBuffer(target, 0);
}

void VertexBufferObject::cleanUp() noexcept
{
    if (mVBO != 0)
    {
        glDeleteBuffers(1, &mVBO);
        mVBO = 0;
    }
}
