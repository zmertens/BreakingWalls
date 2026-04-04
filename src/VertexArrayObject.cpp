#include "VertexArrayObject.hpp"

#include <iostream>

VertexArrayObject::VertexArrayObject() = default;

VertexArrayObject::~VertexArrayObject() noexcept
{
    cleanUp();
}

VertexArrayObject::VertexArrayObject(VertexArrayObject &&other) noexcept
    : mVAO{other.mVAO}
{
    other.mVAO = 0;
}

VertexArrayObject &VertexArrayObject::operator=(VertexArrayObject &&other) noexcept
{
    if (this != &other)
    {
        cleanUp();
        mVAO = other.mVAO;
        other.mVAO = 0;
    }
    return *this;
}

bool VertexArrayObject::loadFromFile(std::string_view /*unused*/)
{
    if (mVAO != 0)
    {
        std::cerr << "VertexArrayObject::loadFromFile - VAO already allocated (" << mVAO << ")\n";
        return true;
    }
    glGenVertexArrays(1, &mVAO);
    if (mVAO == 0)
    {
        std::cerr << "VertexArrayObject::loadFromFile - glGenVertexArrays failed\n";
        return false;
    }
    return true;
}

void VertexArrayObject::bind() const noexcept
{
    glBindVertexArray(mVAO);
}

void VertexArrayObject::unbind() noexcept
{
    glBindVertexArray(0);
}

void VertexArrayObject::cleanUp() noexcept
{
    if (mVAO != 0)
    {
        glDeleteVertexArrays(1, &mVAO);
        mVAO = 0;
    }
}
