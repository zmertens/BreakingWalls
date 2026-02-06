#include "Player.hpp"

#include "CommandQueue.hpp"
#include "Entity.hpp"
#include "Pathfinder.hpp"

#include <box2d/box2d.h>

#include <SDL3/SDL.h>

Player::Player() : mIsActive(true), mIsOnGround(false)
{
    // WASD controls for free-flying movement
    mKeyBinding[SDL_SCANCODE_A] = Action::MOVE_LEFT;
    mKeyBinding[SDL_SCANCODE_D] = Action::MOVE_RIGHT;
    mKeyBinding[SDL_SCANCODE_W] = Action::MOVE_UP;
    mKeyBinding[SDL_SCANCODE_S] = Action::MOVE_DOWN;
    mKeyBinding[SDL_SCANCODE_SPACE] = Action::JUMP;

    initializeActions();

    for (auto& pair : mActionBinding)
    {
        pair.second.category = Category::Type::PLAYER;
    }
}

void Player::handleEvent(const SDL_Event& event, CommandQueue& commands)
{
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        auto found = mKeyBinding.find(event.key.scancode);

        if (found != mKeyBinding.cend() && !isRealtimeAction(found->second))
        {
            if (found->second == Action::JUMP && !mIsOnGround)
            {
                return; // do not jump if not on ground
            }
            commands.push(mActionBinding[found->second]);
        }
    }
}

// Handle continuous input for realtime actions
void Player::handleRealtimeInput(CommandQueue& commands)
{
    for (auto& pair : mKeyBinding)
    {
        if (isRealtimeAction(pair.second))
        {
            int numKeys = 0;
            const auto* keyState = SDL_GetKeyboardState(&numKeys);

            if (keyState && pair.first < static_cast<std::uint32_t>(numKeys) && keyState[pair.first])
            {
                commands.push(mActionBinding[pair.second]);
            }
        }
    }
}

bool Player::isRealtimeAction(Action action)
{
    switch (action)
    {
    case Action::MOVE_LEFT:
    case Action::MOVE_RIGHT:
    case Action::MOVE_UP:
    case Action::MOVE_DOWN:
        return true;
    default:
        return false;
    }
}

void Player::initializeActions()
{
    // For free-flying kinematic body, use velocity control instead of forces
    static constexpr auto flySpeed = 3.0f;  // Meters per second for kinematic body
    static constexpr auto jumpForce = -500.f;

    mActionBinding[Action::MOVE_LEFT].action = derivedAction<Pathfinder>(
        [](Pathfinder& pathfinder, float)
        {
            b2BodyId bodyId = pathfinder.getBodyId();
            if (b2Body_IsValid(bodyId))
            {
                b2Vec2 velocity = b2Body_GetLinearVelocity(bodyId);
                velocity.x = -flySpeed;
                b2Body_SetLinearVelocity(bodyId, velocity);
            }
        }
    );

    mActionBinding[Action::MOVE_RIGHT].action = derivedAction<Pathfinder>(
        [](Pathfinder& pathfinder, float)
        {
            b2BodyId bodyId = pathfinder.getBodyId();
            if (b2Body_IsValid(bodyId))
            {
                b2Vec2 velocity = b2Body_GetLinearVelocity(bodyId);
                velocity.x = flySpeed;
                b2Body_SetLinearVelocity(bodyId, velocity);
            }
        }
    );

    mActionBinding[Action::MOVE_UP].action = derivedAction<Pathfinder>(
        [](Pathfinder& pathfinder, float)
        {
            b2BodyId bodyId = pathfinder.getBodyId();
            if (b2Body_IsValid(bodyId))
            {
                b2Vec2 velocity = b2Body_GetLinearVelocity(bodyId);
                velocity.y = -flySpeed;  // Negative Y is up
                b2Body_SetLinearVelocity(bodyId, velocity);
            }
        }
    );

    mActionBinding[Action::MOVE_DOWN].action = derivedAction<Pathfinder>(
        [](Pathfinder& pathfinder, float)
        {
            b2BodyId bodyId = pathfinder.getBodyId();
            if (b2Body_IsValid(bodyId))
            {
                b2Vec2 velocity = b2Body_GetLinearVelocity(bodyId);
                velocity.y = flySpeed;  // Positive Y is down
                b2Body_SetLinearVelocity(bodyId, velocity);
            }
        }
    );

    mActionBinding[Action::JUMP].action = derivedAction<Pathfinder>(
        [this](Pathfinder& pathfinder, float)
        {
            if (mIsOnGround)
            {
                b2Body_ApplyLinearImpulseToCenter(pathfinder.getBodyId(), {0.f, jumpForce}, true);
                setGroundContact(false);
            }
        }
    );
}

void Player::assignKey(Action action, std::uint32_t key)
{
    // Remove all keys that already map to action
    for (auto it = mKeyBinding.begin(); it != mKeyBinding.end();)
    {
        if (it->second == action)
            it = mKeyBinding.erase(it);
        else
            ++it;
    }

    // Insert new binding
    mKeyBinding[key] = action;
}

std::uint32_t Player::getAssignedKey(Action action) const
{
    for (auto& pair : mKeyBinding)
    {
        if (pair.second == action)
            return pair.first;
    }

    return SDL_SCANCODE_UNKNOWN;
}

void Player::setGroundContact(bool contact)
{
    mIsOnGround = contact;
}

bool Player::hasGroundContact() const
{
    return mIsOnGround;
}

bool Player::isActive() const noexcept
{
    return mIsActive;
}

void Player::setActive(bool active) noexcept
{
    mIsActive = active;
}
