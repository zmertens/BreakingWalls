#include "Ball.hpp"

#include <utility>

#include <SDL3/SDL.h>

#include <box2d/box2d.h>

#include "Physics.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"

Ball::Ball(Type type, const TextureManager& textureManager)
    : mType{type}, mSprite{textureManager.get(Ball::getTextureID())}, mShapeId{b2_nullShapeId}
{
}

void Ball::createPhysicsBody(b2WorldId worldId, b2Vec2 position) noexcept
{
    // Set the scene node position
    setPosition(position.x, position.y);

    // Define physics properties based on ball type
    // Using smaller radii to match the new physics scale (0.3-0.7 meters = 3-7 pixels)
    float radius = 5.0f;       // pixels (about 0.5 meters in physics)
    float density = 1.0f;
    float restitution = 0.6f;  // bounciness
    float friction = 0.3f;
    float linearDamping = 0.2f;
    float angularDamping = 0.4f;

    switch (mType)
    {
    case Type::NORMAL:
        radius = 5.0f;  // 0.5 meters
        density = 1.0f;
        restitution = 0.6f;
        friction = 0.3f;
        break;
    case Type::HEAVY:
        radius = 7.0f;  // 0.7 meters
        density = 3.0f;
        restitution = 0.4f;
        friction = 0.25f;
        linearDamping = 0.3f;  // Heavier balls slow down faster
        break;
    case Type::LIGHT:
        radius = 4.0f;  // 0.4 meters
        density = 0.6f;
        restitution = 0.7f;  // Lighter balls bounce more
        friction = 0.2f;
        linearDamping = 0.15f;  // Light balls maintain velocity longer
        break;
    case Type::EXPLOSIVE:
        radius = 6.0f;  // 0.6 meters
        density = 1.5f;
        restitution = 0.5f;
        friction = 0.25f;
        break;
    }

    // Create the Box2D dynamic body
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = physics::toMetersVec(position);
    bodyDef.linearDamping = linearDamping;
    bodyDef.angularDamping = angularDamping;
    bodyDef.isBullet = true;  // Enable continuous collision detection for fast-moving projectiles

    // Create the body using Entity's helper method
    createBody(worldId, &bodyDef);
    
    b2BodyId bodyId = getBodyId();
    if (!b2Body_IsValid(bodyId))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create Ball physics body!");
        return;
    }

    // Create a circle shape for the ball
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;

    // Box2D 3.x uses separate circle definition
    b2Circle circle = {{0.0f, 0.0f}, physics::toMeters(radius)};
    mShapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);

    // Set friction and restitution after shape creation (Box2D 3.x API)
    b2Shape_SetFriction(mShapeId, friction);
    b2Shape_SetRestitution(mShapeId, restitution);

    // Ensure the body is awake and ready for simulation
    b2Body_SetAwake(bodyId, true);
}

void Ball::launch(b2Vec2 impulse) noexcept
{
    b2BodyId bodyId = getBodyId();
    if (b2Body_IsValid(bodyId))
    {
        // Apply impulse at the center of mass to launch the ball
        b2Vec2 centerOfMass = b2Body_GetWorldCenterOfMass(bodyId);
        b2Body_ApplyLinearImpulse(bodyId, impulse, centerOfMass, true);
        b2Body_SetAwake(bodyId, true);
    }
}

void Ball::onBeginContact(Entity* other) noexcept
{
    // Log contact for debugging (can expand for game logic)
    if (other != nullptr)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Ball began contact with another entity");
    }
}

void Ball::onPostSolve(Entity* other, float impulse) noexcept
{
    // Handle high-impact collisions
    const float impactThreshold = 5.0f;
    
    if (impulse > impactThreshold)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Ball high-impact collision: %.2f", impulse);
        
        // For explosive balls, could trigger explosion here
        if (mType == Type::EXPLOSIVE)
        {
            // TODO: Trigger explosion effect/damage
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Explosive ball impact!");
        }
    }
}

void Ball::updateCurrent(float dt, CommandQueue& commands) noexcept
{
    // Call base class to sync physics body position to scene node transform
    Entity::updateCurrent(dt, commands);
}


// Draw the ball
void Ball::drawCurrent(RenderStates states) const noexcept
{
    static int drawCount = 0;
    if (drawCount < 5)
    {
        drawCount++;
    }
    mSprite.draw(states);
}

Textures::ID Ball::getTextureID() const noexcept
{
    switch (mType)
    {
    case Type::NORMAL:
        {
            return Textures::ID::BALL_NORMAL;
        }
    case Type::HEAVY:
        {
            return Textures::ID::BALL_NORMAL;
        }
    case Type::LIGHT:
        {
            return Textures::ID::BALL_NORMAL;
        }
    case Type::EXPLOSIVE:
        {
            return Textures::ID::BALL_NORMAL;
        }
    default:
        {
            return Textures::ID::BALL_NORMAL;
        }
    }
}
