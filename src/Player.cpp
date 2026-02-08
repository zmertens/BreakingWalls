#include "Player.hpp"

#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

Player::Player() : mIsActive(true), mIsOnGround(false)
{
    // Camera movement controls (WASD + QE for vertical)
    mKeyBinding[SDL_SCANCODE_W] = Action::MOVE_FORWARD;
    mKeyBinding[SDL_SCANCODE_S] = Action::MOVE_BACKWARD;
    mKeyBinding[SDL_SCANCODE_A] = Action::MOVE_LEFT;
    mKeyBinding[SDL_SCANCODE_D] = Action::MOVE_RIGHT;
    mKeyBinding[SDL_SCANCODE_Q] = Action::MOVE_UP;
    mKeyBinding[SDL_SCANCODE_E] = Action::MOVE_DOWN;

    // Camera rotation controls (Arrow keys)
    mKeyBinding[SDL_SCANCODE_LEFT] = Action::ROTATE_LEFT;
    mKeyBinding[SDL_SCANCODE_RIGHT] = Action::ROTATE_RIGHT;
    mKeyBinding[SDL_SCANCODE_UP] = Action::ROTATE_UP;
    mKeyBinding[SDL_SCANCODE_DOWN] = Action::ROTATE_DOWN;

    // Special actions (discrete events)
    mKeyBinding[SDL_SCANCODE_R] = Action::RESET_CAMERA;
    mKeyBinding[SDL_SCANCODE_SPACE] = Action::RESET_ACCUMULATION;

    initializeActions();
}

void Player::handleEvent(const SDL_Event& event, Camera& camera)
{
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        auto found = mKeyBinding.find(event.key.scancode);

        if (found != mKeyBinding.cend() && !isRealtimeAction(found->second))
        {
            // Execute discrete action immediately
            auto actionIt = mCameraActions.find(found->second);
            if (actionIt != mCameraActions.end())
            {
                actionIt->second(camera, 0.0f);
            }
        }
    }
}

void Player::handleRealtimeInput(Camera& camera, float dt)
{
    if (!mIsActive)
        return;

    int numKeys = 0;
    const auto* keyState = SDL_GetKeyboardState(&numKeys);

    if (!keyState)
        return;

    for (auto& pair : mKeyBinding)
    {
        if (isRealtimeAction(pair.second))
        {
            if (pair.first < static_cast<std::uint32_t>(numKeys) && keyState[pair.first])
            {
                auto actionIt = mCameraActions.find(pair.second);
                if (actionIt != mCameraActions.end())
                {
                    actionIt->second(camera, dt);
                }
            }
        }
    }
}

bool Player::isRealtimeAction(Action action)
{
    switch (action)
    {
    case Action::MOVE_FORWARD:
    case Action::MOVE_BACKWARD:
    case Action::MOVE_LEFT:
    case Action::MOVE_RIGHT:
    case Action::MOVE_UP:
    case Action::MOVE_DOWN:
    case Action::ROTATE_LEFT:
    case Action::ROTATE_RIGHT:
    case Action::ROTATE_UP:
    case Action::ROTATE_DOWN:
        return true;
    default:
        return false;
    }
}

void Player::initializeActions()
{
    static constexpr float cameraMoveSpeed = 50.0f;  // Units per second
    static constexpr float cameraRotateSpeed = 180.0f;  // Degrees per second

    // Movement actions (continuous)
    mCameraActions[Action::MOVE_FORWARD] = [](Camera& camera, float dt)
    {
        glm::vec3 movement = camera.getTarget() * cameraMoveSpeed * dt;
        glm::vec3 newPos = camera.getPosition() + movement;
        camera.setPosition(newPos);
    };

    mCameraActions[Action::MOVE_BACKWARD] = [](Camera& camera, float dt)
    {
        glm::vec3 movement = camera.getTarget() * cameraMoveSpeed * dt;
        glm::vec3 newPos = camera.getPosition() - movement;
        camera.setPosition(newPos);
    };

    mCameraActions[Action::MOVE_LEFT] = [](Camera& camera, float dt)
    {
        glm::vec3 movement = camera.getRight() * cameraMoveSpeed * dt;
        glm::vec3 newPos = camera.getPosition() - movement;
        camera.setPosition(newPos);
    };

    mCameraActions[Action::MOVE_RIGHT] = [](Camera& camera, float dt)
    {
        glm::vec3 movement = camera.getRight() * cameraMoveSpeed * dt;
        glm::vec3 newPos = camera.getPosition() + movement;
        camera.setPosition(newPos);
    };

    mCameraActions[Action::MOVE_UP] = [](Camera& camera, float dt)
    {
        glm::vec3 newPos = camera.getPosition();
        newPos.y += cameraMoveSpeed * dt;
        camera.setPosition(newPos);
    };

    mCameraActions[Action::MOVE_DOWN] = [](Camera& camera, float dt)
    {
        glm::vec3 newPos = camera.getPosition();
        newPos.y -= cameraMoveSpeed * dt;
        camera.setPosition(newPos);
    };

    // Rotation actions (continuous)
    mCameraActions[Action::ROTATE_LEFT] = [](Camera& camera, float dt)
    {
        camera.rotate(-cameraRotateSpeed * dt, 0.0f);
    };

    mCameraActions[Action::ROTATE_RIGHT] = [](Camera& camera, float dt)
    {
        camera.rotate(cameraRotateSpeed * dt, 0.0f);
    };

    mCameraActions[Action::ROTATE_UP] = [](Camera& camera, float dt)
    {
        camera.rotate(0.0f, cameraRotateSpeed * dt);
    };

    mCameraActions[Action::ROTATE_DOWN] = [](Camera& camera, float dt)
    {
        camera.rotate(0.0f, -cameraRotateSpeed * dt);
    };

    // Special actions (discrete events)
    mCameraActions[Action::RESET_CAMERA] = [](Camera& camera, float)
    {
        glm::vec3 resetPos = glm::vec3(0.0f, 50.0f, 200.0f);
        camera.setPosition(resetPos);
        camera.rotate(0.0f, 0.0f);  // Reset yaw/pitch to defaults
    };

    mCameraActions[Action::RESET_ACCUMULATION] = [](Camera&, float)
    {
        // This action needs to be handled in GameState (resetting mCurrentBatch)
        // We'll just mark it as a valid action here
        // GameState will need to detect when this key is pressed
    };
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
