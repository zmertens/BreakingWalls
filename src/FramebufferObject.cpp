#include "FramebufferObject.hpp"

#include <SDL3/SDL.h>

FramebufferObject::FramebufferObject() = default;

FramebufferObject::~FramebufferObject() noexcept
{
    cleanUp();
}

FramebufferObject::FramebufferObject(FramebufferObject &&other) noexcept
    : mFBO{other.mFBO}, mRBO{other.mRBO}
{
    other.mFBO = 0;
    other.mRBO = 0;
}

FramebufferObject &FramebufferObject::operator=(FramebufferObject &&other) noexcept
{
    if (this != &other)
    {
        cleanUp();
        mFBO = other.mFBO;
        mRBO = other.mRBO;
        other.mFBO = 0;
        other.mRBO = 0;
    }
    return *this;
}

bool FramebufferObject::loadFromFile(std::string_view /*unused*/)
{
    if (mFBO != 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "FramebufferObject::loadFromFile - FBO already allocated (%u)", mFBO);
        return true;
    }
    glGenFramebuffers(1, &mFBO);
    if (mFBO == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "FramebufferObject::loadFromFile - glGenFramebuffers failed");
        return false;
    }
    return true;
}

void FramebufferObject::bind(GLenum target) const noexcept
{
    glBindFramebuffer(target, mFBO);
}

void FramebufferObject::unbind(GLenum target) noexcept
{
    glBindFramebuffer(target, 0);
}

void FramebufferObject::createRenderbuffer() noexcept
{
    if (mRBO != 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "FramebufferObject::createRenderbuffer - RBO already allocated (%u)", mRBO);
        return;
    }
    glGenRenderbuffers(1, &mRBO);
    if (mRBO == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "FramebufferObject::createRenderbuffer - glGenRenderbuffers failed");
    }
}

void FramebufferObject::bindRenderbuffer() const noexcept
{
    glBindRenderbuffer(GL_RENDERBUFFER, mRBO);
}

void FramebufferObject::unbindRenderbuffer() noexcept
{
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void FramebufferObject::cleanUp() noexcept
{
    if (mRBO != 0)
    {
        glDeleteRenderbuffers(1, &mRBO);
        mRBO = 0;
    }
    if (mFBO != 0)
    {
        glDeleteFramebuffers(1, &mFBO);
        mFBO = 0;
    }
}
