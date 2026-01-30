#ifndef BALL_HPP
#define BALL_HPP

#include "Entity.hpp"
#include "ResourceIdentifiers.hpp"
#include "Sprite.hpp"

/// @file Ball.hpp
/// @class Ball
/// @brief Data class for a ball with physics properties
class Ball : public Entity
{
public:
    enum class Type
    {
        NORMAL,
        HEAVY,
        LIGHT,
        EXPLOSIVE
    };

    explicit Ball(Type type, const TextureManager& textureManager);

    // Create physics body for the ball
    void createPhysicsBody(b2WorldId worldId, b2Vec2 position) noexcept;

    // Launch the ball with an impulse
    void launch(b2Vec2 impulse) noexcept;
    
    // Get ball type
    [[nodiscard]] Type getType() const noexcept { return mType; }

    // Contact callbacks
    void onBeginContact(Entity* other) noexcept override;
    void onPostSolve(Entity* other, float impulse) noexcept override;

private:
    void drawCurrent(RenderStates states) const noexcept override;

    [[nodiscard]] Textures::ID getTextureID() const noexcept override;

    void updateCurrent(float dt, CommandQueue&) noexcept override;

    Type mType;
    Sprite mSprite;
    b2ShapeId mShapeId;
};

#endif // BALL_HPP
