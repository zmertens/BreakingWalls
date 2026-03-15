#include "Player.hpp"

#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <algorithm>

namespace
{
    constexpr float kGroundPlaneY = 1.0f;
    constexpr float kPlayerGravity = 40.0f;
    constexpr float kPlayerJumpVelocity = 14.0f;
    constexpr float kMouseStrafeUnitsPerPixel = 0.09f;
    constexpr float kMouseStrafeDeadzonePixels = 0.5f;
}

Player::Player() : mIsActive(false), mIsOnGround(false)
{
    mKeyBinding[SDL_SCANCODE_W] = Action::MOVE_FORWARD;
    mKeyBinding[SDL_SCANCODE_S] = Action::MOVE_BACKWARD;
    mKeyBinding[SDL_SCANCODE_A] = Action::MOVE_LEFT;
    mKeyBinding[SDL_SCANCODE_D] = Action::MOVE_RIGHT;
    mKeyBinding[SDL_SCANCODE_SPACE] = Action::JUMP;

    // Camera rotation controls (Arrow keys)
    mKeyBinding[SDL_SCANCODE_LEFT] = Action::ROTATE_LEFT;
    mKeyBinding[SDL_SCANCODE_RIGHT] = Action::ROTATE_RIGHT;
    mKeyBinding[SDL_SCANCODE_UP] = Action::ROTATE_UP;
    mKeyBinding[SDL_SCANCODE_DOWN] = Action::ROTATE_DOWN;

    // Special actions (discrete events)
    mKeyBinding[SDL_SCANCODE_R] = Action::RESET_CAMERA;
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
            if (auto actionIt =  mCameraActions.find(found->second); actionIt != mCameraActions.cend())
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

    // In Cornell/comparison view the player position is locked; skip all input.
    if (mFrozen)
    {
        mMovingForward = false;
        mMovingBackward = false;
        mMovingLeft = false;
        mMovingRight = false;
        mIsMoving = false;
        mIsJumping = false;
        updateAnimationState();
        return;
    }

    int numKeys = 0;
    const auto *keyState = SDL_GetKeyboardState(&numKeys);

    if (!keyState)
        return;

    // Store previous position to calculate movement direction
    mPreviousPosition = mPosition;

    // Reset movement flags
    mMovingForward = false;
    mMovingBackward = false;
    mMovingLeft = false;
    mMovingRight = false;
    mIsMoving = false;
    mIsJumping = false;

    for (auto &pair : mKeyBinding)
    {
        if (isRealtimeAction(pair.second))
        {
            if (pair.first < static_cast<std::uint32_t>(numKeys) && keyState[pair.first])
            {
                auto actionIt = mCameraActions.find(pair.second);
                if (actionIt != mCameraActions.cend())
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

    // Relative mouse strafing (cursor is hidden/center-locked by GameState)
    // Only apply mouse strafing when right mouse button is NOT held (to avoid conflict with camera rotation)
    std::uint32_t mouseButtonState = SDL_GetMouseState(nullptr, nullptr);
    bool rightMouseHeld = (mouseButtonState & SDL_BUTTON_RMASK) != 0;
    
    float relMouseX = 0.0f;
    float relMouseY = 0.0f;
    SDL_GetRelativeMouseState(&relMouseX, &relMouseY);
    
    if (!rightMouseHeld && std::abs(relMouseX) >= kMouseStrafeDeadzonePixels)
    {
        // Project camera right onto the sphere surface tangent plane so mouse
        // strafing stays on the sphere at every latitude (not just the equator).
        glm::vec3 surfaceUp = mUseSphericalGravity
            ? glm::normalize(mPosition - mPlanetCenter)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 rightDir = camera.getRight();
        glm::vec3 tangentRight = rightDir - glm::dot(rightDir, surfaceUp) * surfaceUp;
        glm::vec3 horizontalRight = (glm::length(tangentRight) > 0.01f)
            ? glm::normalize(tangentRight)
            : glm::vec3(1.0f, 0.0f, 0.0f);

        const glm::vec3 movement = horizontalRight * (relMouseX * kMouseStrafeUnitsPerPixel);
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

        if (relMouseX < 0.0f)
        {
            mMovingLeft = true;
        }
        else
        {
            mMovingRight = true;
        }
        mIsMoving = true;
    }

    // Apply gravity and vertical motion
    if (mUseSphericalGravity)
    {
        glm::vec3 radialDir = mPosition - mPlanetCenter;
        float radialLen = glm::length(radialDir);
        if (radialLen < 0.0001f)
        {
            radialDir = glm::vec3(0.0f, 1.0f, 0.0f);
            radialLen = 1.0f;
        }
        else
        {
            radialDir /= radialLen;
        }

        float altitude = radialLen - mPlanetRadius;
        mVerticalVelocity -= kPlayerGravity * dt;
        altitude += mVerticalVelocity * dt;

        if (altitude <= kGroundPlaneY)
        {
            altitude = kGroundPlaneY;
            mVerticalVelocity = 0.0f;
            mIsOnGround = true;
        }
        else
        {
            mIsOnGround = false;
        }

        mPosition = mPlanetCenter + radialDir * (mPlanetRadius + altitude);

        // Keep camera up aligned with the sphere surface so third-person positioning
        // and movement tangents remain correct at every latitude.
        camera.setUp(glm::normalize(mPosition - mPlanetCenter));
    }
    else
    {
        mVerticalVelocity -= kPlayerGravity * dt;
        mPosition.y += mVerticalVelocity * dt;

        if (mPosition.y <= kGroundPlaneY)
        {
            mPosition.y = kGroundPlaneY;
            mVerticalVelocity = 0.0f;
            mIsOnGround = true;
        }
        else
        {
            mIsOnGround = false;
        }
    }

    // Update animator with new position after physics
    mAnimator.setPosition(mPosition);

    // Update player position to match camera (for third person, player IS the focus)
    if (camera.getMode() == CameraMode::FIRST_PERSON)
    {
        glm::vec3 cameraPos = camera.getPosition();
        cameraPos.y = mPosition.y;
        camera.setPosition(cameraPos);
        mPosition = cameraPos;
        // In first person, player faces camera direction
        mFacingDirection = camera.getYaw();
    }
    else
    {
        camera.setFollowTarget(mPosition);
        camera.updateThirdPersonPosition();
        
        // In third person, calculate facing direction from movement
        // The sprite system is 2D side-view: facing < 90° shows right-facing sprite, >= 90° shows left-facing
        glm::vec3 movementDelta = mPosition - mPreviousPosition;
        movementDelta.y = 0.0f; // Ignore vertical movement for facing direction
        
        if (glm::length(movementDelta) > 0.001f)
        {
            // Simple 2D facing logic: check primary movement direction
            // In arcade mode, +X is forward (right), -X is backward (left)
            // Use atan2 to get angle from movement vector
            float movementAngle = glm::degrees(std::atan2(movementDelta.z, movementDelta.x));
            
            // Normalize to 0-360 range
            if (movementAngle < 0.0f)
            {
                movementAngle += 360.0f;
            }
            
            // For 2D side-view sprites:
            // 0-180° (right hemisphere) = face right (facing = 0)
            // 180-360° (left hemisphere) = face left (facing = 180)
            if (movementAngle >= 0.0f && movementAngle < 180.0f)
            {
                mTargetFacingDirection = 0.0f; // Face right
            }
            else
            {
                mTargetFacingDirection = 180.0f; // Face left (will trigger sprite flip)
            }
            
            // Smooth transition between left and right
            float angleDiff = mTargetFacingDirection - mFacingDirection;
            
            // Normalize angle difference to [-180, 180]
            while (angleDiff > 180.0f) angleDiff -= 360.0f;
            while (angleDiff < -180.0f) angleDiff += 360.0f;
            
            // Quick snap for 2D facing (no smooth rotation needed)
            mFacingDirection = mTargetFacingDirection;
        }
        // If not moving, maintain current facing direction
    }
    
    mAnimator.setRotation(mFacingDirection);

    // Update animation state
    updateAnimationState();

    if (mCollisionAnimTimer > 0.0f)
    {
        mCollisionAnimTimer = std::max(0.0f, mCollisionAnimTimer - dt);
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
        glm::vec3 surfaceUp = mUseSphericalGravity
            ? glm::normalize(mPosition - mPlanetCenter)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 camFwd = camera.getTarget();
        glm::vec3 tangentFwd = camFwd - glm::dot(camFwd, surfaceUp) * surfaceUp;
        if (glm::length(tangentFwd) < 0.01f)
            tangentFwd = glm::normalize(camera.getRight() - glm::dot(camera.getRight(), surfaceUp) * surfaceUp);
        else
            tangentFwd = glm::normalize(tangentFwd);
        glm::vec3 movement = tangentFwd * cameraMoveSpeed * dt;

        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition += movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            camera.setPosition(camera.getPosition() + movement);
        }
    };

    mCameraActions[Action::MOVE_BACKWARD] = [this](Camera &camera, float dt)
    {
        glm::vec3 surfaceUp = mUseSphericalGravity
            ? glm::normalize(mPosition - mPlanetCenter)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 camFwd = camera.getTarget();
        glm::vec3 tangentFwd = camFwd - glm::dot(camFwd, surfaceUp) * surfaceUp;
        if (glm::length(tangentFwd) < 0.01f)
            tangentFwd = glm::normalize(camera.getRight() - glm::dot(camera.getRight(), surfaceUp) * surfaceUp);
        else
            tangentFwd = glm::normalize(tangentFwd);
        glm::vec3 movement = tangentFwd * cameraMoveSpeed * dt;

        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition -= movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            camera.setPosition(camera.getPosition() - movement);
        }
    };

    mCameraActions[Action::MOVE_LEFT] = [this](Camera &camera, float dt)
    {
        glm::vec3 surfaceUp = mUseSphericalGravity
            ? glm::normalize(mPosition - mPlanetCenter)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 camRight = camera.getRight();
        glm::vec3 tangentRight = camRight - glm::dot(camRight, surfaceUp) * surfaceUp;
        if (glm::length(tangentRight) < 0.01f)
            tangentRight = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            tangentRight = glm::normalize(tangentRight);
        glm::vec3 movement = tangentRight * cameraMoveSpeed * dt;

        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition -= movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            camera.setPosition(camera.getPosition() - movement);
        }
    };

    mCameraActions[Action::MOVE_RIGHT] = [this](Camera &camera, float dt)
    {
        glm::vec3 surfaceUp = mUseSphericalGravity
            ? glm::normalize(mPosition - mPlanetCenter)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 camRight = camera.getRight();
        glm::vec3 tangentRight = camRight - glm::dot(camRight, surfaceUp) * surfaceUp;
        if (glm::length(tangentRight) < 0.01f)
            tangentRight = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            tangentRight = glm::normalize(tangentRight);
        glm::vec3 movement = tangentRight * cameraMoveSpeed * dt;

        if (camera.getMode() == CameraMode::THIRD_PERSON)
        {
            mPosition += movement;
            mAnimator.setPosition(mPosition);
            camera.setFollowTarget(mPosition);
            camera.updateThirdPersonPosition();
        }
        else
        {
            camera.setPosition(camera.getPosition() + movement);
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
    // In spherical gravity mode, rotate around the surface normal / camera-right so
    // the heading stays surface-relative at every latitude.  Flat mode keeps the
    // existing world-space yaw/pitch behaviour.
    mCameraActions[Action::ROTATE_LEFT] = [this](Camera &camera, float dt)
    {
        if (mUseSphericalGravity && glm::length(mPosition - mPlanetCenter) > 0.01f)
            camera.rotateAroundAxis(glm::normalize(mPosition - mPlanetCenter), -cameraRotateSpeed * dt);
        else
            camera.rotate(-cameraRotateSpeed * dt, 0.0f);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
            camera.updateThirdPersonPosition();
    };

    mCameraActions[Action::ROTATE_RIGHT] = [this](Camera &camera, float dt)
    {
        if (mUseSphericalGravity && glm::length(mPosition - mPlanetCenter) > 0.01f)
            camera.rotateAroundAxis(glm::normalize(mPosition - mPlanetCenter), cameraRotateSpeed * dt);
        else
            camera.rotate(cameraRotateSpeed * dt, 0.0f);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
            camera.updateThirdPersonPosition();
    };

    mCameraActions[Action::ROTATE_UP] = [this](Camera &camera, float dt)
    {
        if (mUseSphericalGravity)
            camera.rotateAroundAxis(camera.getRight(), cameraRotateSpeed * dt);
        else
            camera.rotate(0.0f, cameraRotateSpeed * dt);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
            camera.updateThirdPersonPosition();
    };

    mCameraActions[Action::ROTATE_DOWN] = [this](Camera &camera, float dt)
    {
        if (mUseSphericalGravity)
            camera.rotateAroundAxis(camera.getRight(), -cameraRotateSpeed * dt);
        else
            camera.rotate(0.0f, -cameraRotateSpeed * dt);
        if (camera.getMode() == CameraMode::THIRD_PERSON)
            camera.updateThirdPersonPosition();
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
        }
        else
        {
            camera.setMode(CameraMode::FIRST_PERSON);
            camera.setPosition(mPosition);
        }
    };

    mCameraActions[Action::JUMP] = [this](Camera &camera, float)
    {
        jump();
    };
}

void Player::jump() noexcept
{
    if (!mIsActive)
    {
        return;
    }

    if (mIsOnGround)
    {
        mVerticalVelocity = kPlayerJumpVelocity;
        mIsOnGround = false;
    }
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
    if (!mUseSphericalGravity && mPosition.y <= kGroundPlaneY)
    {
        mPosition.y = kGroundPlaneY;
        mVerticalVelocity = 0.0f;
        mIsOnGround = true;
    }
    else
    {
        mIsOnGround = false;
    }
    mAnimator.setPosition(mPosition);
}

void Player::setPositionUnconstrained(const glm::vec3 &position) noexcept
{
    mPosition = position;
    mVerticalVelocity = 0.0f;
    mIsOnGround = true;
    mAnimator.setPosition(mPosition);
}

void Player::configureSphericalGravity(bool enabled, const glm::vec3 &planetCenter, float planetRadius) noexcept
{
    mUseSphericalGravity = enabled;
    mPlanetCenter = planetCenter;
    mPlanetRadius = std::max(1.0f, planetRadius);
    if (enabled)
    {
        glm::vec3 radial = mPosition - mPlanetCenter;
        float radialLen = glm::length(radial);
        if (radialLen < 0.0001f)
        {
            radial = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        else
        {
            radial = radial / radialLen;
        }
        mPosition = mPlanetCenter + radial * (mPlanetRadius + kGroundPlaneY);
        mVerticalVelocity = 0.0f;
        mIsOnGround = true;
        mAnimator.setPosition(mPosition);
    }
}

void Player::triggerCollisionAnimation(bool positiveCollision) noexcept
{
    mCollisionAnimState = positiveCollision ? CharacterAnimState::ATTACK : CharacterAnimState::DEATH;
    mCollisionAnimTimer = positiveCollision ? 0.22f : 0.32f;
    mAnimator.setState(mCollisionAnimState, true);
}

void Player::updateAnimationState()
{
    if (mCollisionAnimTimer > 0.0f)
    {
        mAnimator.setState(mCollisionAnimState);
        return;
    }

    CharacterAnimState newState = CharacterAnimState::IDLE;

    if (!mIsOnGround)
    {
        newState = CharacterAnimState::JUMP;
    }
    else if (mIsMoving)
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
        else if (mIsJumping)
        {
            newState = CharacterAnimState::JUMP;
        }
    }

    mAnimator.setState(newState);
}
