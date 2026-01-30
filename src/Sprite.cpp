#include "Sprite.hpp"

#include "Texture.hpp"

#include <SDL3/SDL.h>

Sprite::Sprite(const Texture& texture)
    : mTexture(&texture), mTextureRect{SDL_Rect{0, 0, texture.getWidth(), texture.getHeight()}}
{
}

Sprite::Sprite(const Texture& texture, const SDL_Rect& rect)
    : mTexture(&texture), mTextureRect{rect}
{
}

void Sprite::draw(RenderStates states) const noexcept
{
    if (!mTexture)
    {
        return;
    }

    // Check if SDL is still initialized
    if (!SDL_WasInit(SDL_INIT_VIDEO))
    {
        return; // SDL already quit, skip drawing
    }

    const auto textureId = mTexture->get();
    if (textureId == 0)
    {
        return; // No valid OpenGL texture
    }

    // TODO: Implement OpenGL sprite rendering
    // The Texture class uses OpenGL textures (mTextureId is a GLuint).
    // This draw method needs to be refactored to use OpenGL calls:
    // 1. Bind the texture: glBindTexture(GL_TEXTURE_2D, textureId);
    // 2. Set up vertex data for a quad
    // 3. Use a sprite shader program
    // 4. Draw the quad with glDrawArrays or glDrawElements
}

/// @brief
/// @param texture
/// @param resetRect false
void Sprite::setTexture(const Texture& texture, bool resetRect)
{
    mTexture = &texture;

    if (resetRect)
    {
        mTextureRect = SDL_Rect{0, 0, texture.getWidth(), texture.getHeight()};
    }
}
