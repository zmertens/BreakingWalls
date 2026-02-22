#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <memory>

#include <glm/glm.hpp>

/// @brief Camera perspective mode (matches Options enum)
enum class CameraMode : unsigned int
{
    FIRST_PERSON = 0,
    THIRD_PERSON = 1
};

/// @brief 3D camera with spherical coordinate system (yaw/pitch)
/// @details Provides perspective matrix, look-at matrix, and frustum ray generation for raytracing
/// Supports both first-person and third-person camera modes
class Camera
{
public:
    typedef std::unique_ptr<Camera> Ptr;

public:
    /// Constructor with position and spherical coordinates
    explicit Camera(const glm::vec3 &position = glm::vec3(0.f, 50.f, 200.f),
                    const float yaw = -10.0f, const float pitch = 0.0f,
                    float fovy = 65.0f, float near = 0.1f, float far = 100.0f);

    /// Move camera by velocity vector
    void move(const glm::vec3 &velocity, float dt);

    /// Rotate camera using yaw and pitch
    void rotate(float yaw, float pitch, bool holdPitch = true, bool holdYaw = true);

    /// Get the look-at matrix
    [[nodiscard]] glm::mat4 getLookAt() const;

    /// Get the perspective projection matrix
    [[nodiscard]] glm::mat4 getPerspective(const float aspectRatio) const;

    /// Get infinite perspective projection matrix
    [[nodiscard]] glm::mat4 getInfPerspective(const float aspectRatio) const;

    /// Get a frustum ray from camera through a frustum corner for raytracing
    [[nodiscard]] glm::vec3 getFrustumEyeRay(float ar, int x, int y) const;

    /// Update field of view with mouse wheel input
    void updateFieldOfView(float dy);

    [[nodiscard]] glm::vec3 getPosition() const;
    void setPosition(const glm::vec3 &position);

    [[nodiscard]] glm::vec3 getTarget() const;
    void setTarget(const glm::vec3 &target);

    [[nodiscard]] glm::vec3 getUp() const;
    void setUp(const glm::vec3 &up);

    [[nodiscard]] glm::vec3 getRight() const;
    void setRight(const glm::vec3 &right);

    [[nodiscard]] float getNear() const;
    void setNear(float near);

    [[nodiscard]] float getFar() const;
    void setFar(float far);

    [[nodiscard]] float getYaw() const;
    [[nodiscard]] float getPitch() const;

    /// Force camera yaw/pitch and refresh basis vectors
    void setYawPitch(float yaw, float pitch, bool clampPitch = true, bool wrapYaw = true);

    // ========================================================================
    // Third-person camera support
    // ========================================================================

    /// Set camera mode (first person or third person)
    void setMode(CameraMode mode) noexcept { mMode = mode; }

    /// Get current camera mode
    [[nodiscard]] CameraMode getMode() const noexcept { return mMode; }

    /// Set the target position to follow (for third person)
    void setFollowTarget(const glm::vec3 &targetPos) noexcept { mFollowTarget = targetPos; }

    /// Get the follow target position
    [[nodiscard]] glm::vec3 getFollowTarget() const noexcept { return mFollowTarget; }

    /// Set third person camera distance
    void setThirdPersonDistance(float distance) noexcept { mThirdPersonDistance = distance; }

    /// Get third person camera distance
    [[nodiscard]] float getThirdPersonDistance() const noexcept { return mThirdPersonDistance; }

    /// Set third person camera height offset
    void setThirdPersonHeight(float height) noexcept { mThirdPersonHeight = height; }

    /// Get third person camera height offset
    [[nodiscard]] float getThirdPersonHeight() const noexcept { return mThirdPersonHeight; }

    /// Update third person camera position based on follow target
    void updateThirdPersonPosition();

    /// Get the actual camera position (accounts for third person offset)
    [[nodiscard]] glm::vec3 getActualPosition() const;

    /// Get the look-at matrix for third person (looks at follow target)
    [[nodiscard]] glm::mat4 getThirdPersonLookAt() const;

private:
    static const float scMaxYawValue;
    static const float scMaxPitchValue;
    static const float scMaxFieldOfView;
    static float sSensitivity;

    glm::vec3 mPosition;
    glm::vec3 mTarget; ///< Direction vector
    glm::vec3 mUp;
    glm::vec3 mRight;
    float mYaw;
    float mPitch;
    float mFieldOfView;
    float mNear;
    float mFar;

    // Third-person camera support
    CameraMode mMode{CameraMode::THIRD_PERSON};
    glm::vec3 mFollowTarget{0.0f};     ///< Position to follow in third person
    float mThirdPersonDistance{10.0f}; ///< Distance behind target
    float mThirdPersonHeight{5.0f};    ///< Height above target

private:
    /// Update target, right, and up vectors based on yaw/pitch Euler angles
    void updateVectors();
};

#endif // CAMERA_HPP
