#ifndef PLAYER_HPP
#define PLAYER_HPP

#include <cstdint>
#include <functional>
#include <map>

#include "Animation.hpp"

union SDL_Event;

class Camera;
class World;

class Player
{
    // Allow World to access player's animator for rendering
    friend class World;

public:
    enum class Action
    {
        // Camera movement actions
        MOVE_FORWARD,
        MOVE_BACKWARD,
        MOVE_LEFT,
        MOVE_RIGHT,
        MOVE_UP,
        MOVE_DOWN,
        JUMP,
        // Camera rotation actions
        ROTATE_LEFT,
        ROTATE_RIGHT,
        ROTATE_UP,
        ROTATE_DOWN,
        // Special actions
        RESET_CAMERA,
        RESET_ACCUMULATION,
        TOGGLE_PERSPECTIVE,
        ACTION_COUNT
    };

    explicit Player();

    void handleEvent(const SDL_Event &event, Camera &camera);
    void handleRealtimeInput(Camera &camera, float dt);

    void assignKey(Action action, std::uint32_t key);
    [[nodiscard]] std::uint32_t getAssignedKey(Action action) const;

    void setGroundContact(bool contact);
    bool hasGroundContact() const;

    bool isActive() const noexcept;
    void setActive(bool active) noexcept;

    // ========================================================================
    // Animation system
    // ========================================================================

    /// Initialize animator for a specific character in the sprite sheet
    void initializeAnimator(int characterIndex);

    /// Update animation state based on movement
    void updateAnimation(float dt);

    /// Get the current animation frame rectangle
    [[nodiscard]] AnimationRect getCurrentAnimationFrame() const;

    /// Get the character animator
    [[nodiscard]] const CharacterAnimator &getAnimator() const noexcept { return mAnimator; }
    [[nodiscard]] CharacterAnimator &getAnimator() noexcept { return mAnimator; }

    /// Get player's 3D position
    [[nodiscard]] glm::vec3 getPosition() const noexcept { return mPosition; }

    /// Set player's 3D position
    void setPosition(const glm::vec3 &position) noexcept;

    /// Set player's 3D position without ground-plane clamping.
    /// Used by special scene framing modes (e.g., Cornell preview).
    void setPositionUnconstrained(const glm::vec3 &position) noexcept;

    /// Configure spherical gravity and surface anchoring for planet gameplay
    void configureSphericalGravity(bool enabled, const glm::vec3 &planetCenter, float planetRadius) noexcept;

    /// Trigger a jump if grounded
    void jump() noexcept;

    /// Get player's facing direction (yaw in degrees)
    [[nodiscard]] float getFacingDirection() const noexcept { return mFacingDirection; }
    
    /// Set player's facing direction (yaw in degrees) and update animator
    void setFacingDirection(float yawDegrees) noexcept { mFacingDirection = yawDegrees; mAnimator.setRotation(mFacingDirection); }

    /// Get surface normal (up direction) where player is standing
    /// Freeze player in place - handleRealtimeInput becomes a no-op.
    void setFrozen(bool frozen) noexcept { mFrozen = frozen; }
    [[nodiscard]] bool isFrozen() const noexcept { return mFrozen; }

    [[nodiscard]] glm::vec3 getSurfaceNormal() const noexcept { return mSurfaceNormal; }
    
    /// Set surface normal (up direction) for spherical surface alignment
    void setSurfaceNormal(const glm::vec3 &normal) noexcept { mSurfaceNormal = glm::normalize(normal); }

    /// Get forward tangent direction on sphere surface
    [[nodiscard]] glm::vec3 getForwardTangent() const noexcept { return mForwardTangent; }
    
    /// Set forward tangent direction (along sphere surface)
    void setForwardTangent(const glm::vec3 &tangent) noexcept { mForwardTangent = glm::normalize(tangent); }

    /// Check if player is moving
    [[nodiscard]] bool isMoving() const noexcept { return mIsMoving; }

    /// Trigger transient animation override from runner collision events
    void triggerCollisionAnimation(bool positiveCollision) noexcept;

private:
    void initializeActions();
    static bool isRealtimeAction(Action action);

    // Update animation state based on current movement flags
    void updateAnimationState();

    std::map<std::uint32_t, Action> mKeyBinding;
    std::map<Action, std::function<void(Camera &, float)>> mCameraActions;
    bool mIsActive;
    bool mIsOnGround;
    bool mFrozen{false};  ///< When true, handleRealtimeInput is a no-op (Cornell view mode).

    // Animation system
    CharacterAnimator mAnimator;
    glm::vec3 mPosition{0.0f};
    glm::vec3 mPreviousPosition{0.0f};  // For calculating movement direction
    glm::vec3 mSurfaceNormal{0.0f, 1.0f, 0.0f};  // Up direction on spherical surface
    glm::vec3 mForwardTangent{0.0f, 0.0f, 1.0f}; // Forward direction on spherical surface
    float mFacingDirection{0.0f};
    float mTargetFacingDirection{0.0f};  // Target direction for smooth rotation
    bool mIsMoving{false};

    // Movement tracking for animation state
    bool mMovingForward{false};
    bool mMovingBackward{false};
    bool mMovingLeft{false};
    bool mMovingRight{false};
    bool mIsJumping{false};
    float mCollisionAnimTimer{0.0f};
    CharacterAnimState mCollisionAnimState{CharacterAnimState::IDLE};

    // Vertical locomotion
    float mVerticalVelocity{0.0f};
    bool mUseSphericalGravity{false};
    glm::vec3 mPlanetCenter{0.0f};
    float mPlanetRadius{50.0f};
};

#endif // PLAYER_HPP
