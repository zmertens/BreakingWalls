#include "VertexBufferObject.hpp"

#include <iostream>

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
        std::cerr << "VertexBufferObject::loadFromFile - buffer already allocated (" << mVBO << ")\n";
        return true;
    }
    glGenBuffers(1, &mVBO);
    if (mVBO == 0)
    {
        std::cerr << "VertexBufferObject::loadFromFile - glGenBuffers failed\n";
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
