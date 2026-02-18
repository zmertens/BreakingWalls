#include "Player.hpp"

#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <algorithm>

namespace
{
    constexpr float kGroundPlaneY = 0.0f;
}

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
    mKeyBinding[SDL_SCANCODE_V] = Action::TOGGLE_PERSPECTIVE;

    initializeActions();

    // Initialize animator with default character (index 0)
    initializeAnimator(0);
}

void Player::handleEvent(const SDL_Event &event, Camera &camera)
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

void Player::handleRealtimeInput(Camera &camera, float dt)
{
    if (!mIsActive)
        return;

    int numKeys = 0;
    const auto *keyState = SDL_GetKeyboardState(&numKeys);

    if (!keyState)
        return;

    // Reset movement flags
    mMovingForward = false;
    mMovingBackward = false;
    mMovingLeft = false;
    mMovingRight = false;
    mIsMoving = false;

    for (auto &pair : mKeyBinding)
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

                // Track movement for animation
                switch (pair.second)
                {
                case Action::MOVE_FORWARD:
                    mMovingForward = true;
                    mIsMoving = true;
                    break;
                case Action::MOVE_BACKWARD:
                    mMovingBackward = true;
                    mIsMoving = true;
                    break;
                case Action::MOVE_LEFT:
                    mMovingLeft = true;
                    mIsMoving = true;
                    break;
                case Action::MOVE_RIGHT:
                    mMovingRight = true;
                    mIsMoving = true;
                    break;
                default:
                    break;
                }
            }
        }
    }

    // Keep player/camera above the infinite ground plane
    if (camera.getMode() == CameraMode::THIRD_PERSON)
    {
        float clampedY = std::max(mPosition.y, kGroundPlaneY);
        if (clampedY != mPosition.y)
        {
            mPosition.y = clampedY;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
    }
    else
    {
        glm::vec3 cameraPos = camera.getPosition();
        float clampedY = std::max(cameraPos.y, kGroundPlaneY);
        if (clampedY != cameraPos.y)
        {
            cameraPos.y = clampedY;
            camera.setPosition(cameraPos);
        }
    }

    // Update player position to match camera (for third person, player IS the focus)
    if (camera.getMode() == CameraMode::FIRST_PERSON)
    {
        mPosition = camera.getPosition();
    }
    // In third person, player position is updated separately
    // and camera follows the player

    // Update facing direction based on camera yaw
    mFacingDirection = camera.getYaw();
    mAnimator.setRotation(mFacingDirection);

    // Update animation state
    updateAnimationState();
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
    static constexpr float cameraMoveSpeed = 50.0f;    // Units per second
    static constexpr float cameraRotateSpeed = 180.0f; // Degrees per second

    // Movement actions (continuous)
    mCameraActions[Action::MOVE_FORWARD] = [this](Camera &camera, float dt)
    {
        glm::vec3 movement = camera.getTarget() * cameraMoveSpeed * dt;
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            // Move player, camera follows
            mPosition += movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            glm::vec3 newPos = camera.getPosition() + movement;
            camera.setPosition(newPos);
        }
    };

    mCameraActions[Action::MOVE_BACKWARD] = [this](Camera &camera, float dt)
    {
        glm::vec3 movement = camera.getTarget() * cameraMoveSpeed * dt;
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition -= movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            glm::vec3 newPos = camera.getPosition() - movement;
            camera.setPosition(newPos);
        }
    };

    mCameraActions[Action::MOVE_LEFT] = [this](Camera &camera, float dt)
    {
        glm::vec3 movement = camera.getRight() * cameraMoveSpeed * dt;
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition -= movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            glm::vec3 newPos = camera.getPosition() - movement;
            camera.setPosition(newPos);
        }
    };

    mCameraActions[Action::MOVE_RIGHT] = [this](Camera &camera, float dt)
    {
        glm::vec3 movement = camera.getRight() * cameraMoveSpeed * dt;
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition += movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            glm::vec3 newPos = camera.getPosition() + movement;
            camera.setPosition(newPos);
        }
    };

    mCameraActions[Action::MOVE_UP] = [this](Camera &camera, float dt)
    {
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition.y += cameraMoveSpeed * dt;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            glm::vec3 newPos = camera.getPosition();
            newPos.y += cameraMoveSpeed * dt;
            camera.setPosition(newPos);
        }
    };

    mCameraActions[Action::MOVE_DOWN] = [this](Camera &camera, float dt)
    {
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition.y -= cameraMoveSpeed * dt;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            glm::vec3 newPos = camera.getPosition();
            newPos.y -= cameraMoveSpeed * dt;
            camera.setPosition(newPos);
        }
    };

    // Rotation actions (continuous)
    mCameraActions[Action::ROTATE_LEFT] = [](Camera &camera, float dt)
    {
        camera.rotate(-cameraRotateSpeed * dt, 0.0f);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            camera.updateThirdPersonPosition();
        }
    };

    mCameraActions[Action::ROTATE_RIGHT] = [](Camera &camera, float dt)
    {
        camera.rotate(cameraRotateSpeed * dt, 0.0f);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            camera.updateThirdPersonPosition();
        }
    };

    mCameraActions[Action::ROTATE_UP] = [](Camera &camera, float dt)
    {
        camera.rotate(0.0f, cameraRotateSpeed * dt);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            camera.updateThirdPersonPosition();
        }
    };

    mCameraActions[Action::ROTATE_DOWN] = [](Camera &camera, float dt)
    {
        camera.rotate(0.0f, -cameraRotateSpeed * dt);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            camera.updateThirdPersonPosition();
        }
    };

    // Special actions (discrete events)
    mCameraActions[Action::RESET_CAMERA] = [this](Camera &camera, float)
    {
        glm::vec3 resetPos = glm::vec3(0.0f, 50.0f, 200.0f);
        camera.setPosition(resetPos);
        camera.rotate(0.0f, 0.0f); // Reset yaw/pitch to defaults

        // Also reset player position
        mPosition = resetPos;
        mAnimator.setPosition(mPosition);

        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
    };

    mCameraActions[Action::RESET_ACCUMULATION] = [](Camera &, float)
    {
        // This action needs to be handled in GameState (resetting mCurrentBatch)
        // We'll just mark it as a valid action here
        // GameState will need to detect when this key is pressed
    };

    mCameraActions[Action::TOGGLE_PERSPECTIVE] = [this](Camera &camera, float)
    {
        // Toggle between first and third person
        if (camera.getMode() == CameraMode::FIRST_PERSON)
        {
            camera.setMode(CameraMode::THIRD_PERSON);
            camera.setFollowTarget(mPosition);
            camera.setThirdPersonDistance(15.0f); // Set distance behind player
            camera.setThirdPersonHeight(8.0f);    // Set height above player
            camera.updateThirdPersonPosition();
            SDL_Log("Player: Switched to THIRD-PERSON camera (pos: %.1f, %.1f, %.1f)",
                    mPosition.x, mPosition.y, mPosition.z);
        }
        else
        {
            camera.setMode(CameraMode::FIRST_PERSON);
            camera.setPosition(mPosition);
            SDL_Log("Player: Switched to FIRST-PERSON camera");
        }
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
    for (auto &pair : mKeyBinding)
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

// ============================================================================
// Animation system
// ============================================================================

void Player::initializeAnimator(int characterIndex)
{
    mAnimator.initialize(characterIndex);
    mAnimator.setPosition(mPosition);
}

void Player::updateAnimation(float dt)
{
    // Update the animation (advances frames based on timing)
    mAnimator.update();
}

AnimationRect Player::getCurrentAnimationFrame() const
{
    return mAnimator.getCurrentFrame();
}

void Player::setPosition(const glm::vec3 &position) noexcept
{
    mPosition = position;
    mPosition.y = std::max(mPosition.y, kGroundPlaneY);
    mAnimator.setPosition(mPosition);
}

void Player::updateAnimationState()
{
    CharacterAnimState newState = CharacterAnimState::IDLE;

    if (mIsMoving)
    {
        if (mMovingForward)
        {
            newState = CharacterAnimState::WALK_FORWARD;
        }
        else if (mMovingBackward)
        {
            newState = CharacterAnimState::WALK_BACKWARD;
        }
        else if (mMovingLeft)
        {
            newState = CharacterAnimState::WALK_LEFT;
        }
        else if (mMovingRight)
        {
            newState = CharacterAnimState::WALK_RIGHT;
        }
    }

    mAnimator.setState(newState);
}
