#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <memory>

#include <glm/glm.hpp>

/// @brief 3D camera with spherical coordinate system (yaw/pitch)
/// @details Provides perspective matrix, look-at matrix, and frustum ray generation for raytracing
class Camera
{
public:
    typedef std::unique_ptr<Camera> Ptr;

public:
    /// Constructor with position and spherical coordinates
    explicit Camera(const glm::vec3& position = glm::vec3(0),
        const float yaw = -90.0f, const float pitch = 0.0f,
        float fovy = 65.0f, float near = 0.1f, float far = 100.0f);

    /// Move camera by velocity vector
    void move(const glm::vec3& velocity, float dt);
    
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
    void setPosition(const glm::vec3& position);
    
    [[nodiscard]] glm::vec3 getTarget() const;
    void setTarget(const glm::vec3& target);
    
    [[nodiscard]] glm::vec3 getUp() const;
    void setUp(const glm::vec3& up);
    
    [[nodiscard]] glm::vec3 getRight() const;
    void setRight(const glm::vec3& right);
    
    [[nodiscard]] float getNear() const;
    void setNear(float near);

    [[nodiscard]] float getFar() const;
    void setFar(float far);

    [[nodiscard]] float getYaw() const;
    [[nodiscard]] float getPitch() const;

private:
    static const float scMaxYawValue;
    static const float scMaxPitchValue;
    static const float scMaxFieldOfView;
    static float sSensitivity;

    glm::vec3 mPosition;
    glm::vec3 mTarget;      ///< Direction vector
    glm::vec3 mUp;
    glm::vec3 mRight;
    float mYaw;
    float mPitch;
    float mFieldOfView;
    float mNear;
    float mFar;

private:
    /// Update target, right, and up vectors based on yaw/pitch Euler angles
    void updateVectors();
};

#endif // CAMERA_HPP
