#include "Camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

const float Camera::scMaxYawValue = 119.0f;
const float Camera::scMaxPitchValue = 89.0f;
const float Camera::scMaxFieldOfView = 89.0f;
float Camera::sSensitivity = 0.05f;

Camera::Camera(const glm::vec3 &position,
               const float yaw, const float pitch,
               float fovy, float near, float far)
    : mPosition(position), mYaw(yaw), mPitch(pitch), mFieldOfView(fovy), mNear(near), mFar(far)
{
    updateVectors();
}

void Camera::move(const glm::vec3 &velocity, float dt)
{
    mPosition = mPosition + (velocity * dt);
}

void Camera::rotate(float yaw, float pitch, bool holdPitch, bool holdYaw)
{
    mYaw += yaw * sSensitivity;
    mPitch += pitch * sSensitivity;

    if (holdPitch)
    {
        if (mPitch > scMaxPitchValue)
            mPitch = scMaxPitchValue;
        if (mPitch < -scMaxPitchValue)
            mPitch = -scMaxPitchValue;
    }

    if (holdYaw)
    {
        if (mYaw > scMaxYawValue)
            mYaw = -scMaxYawValue;
        if (mYaw < -scMaxYawValue)
            mYaw = scMaxYawValue;
    }

    updateVectors();
}

glm::mat4 Camera::getLookAt() const
{
    if (mMode == CameraMode::THIRD_PERSON)
    {
        return getThirdPersonLookAt();
    }
    return glm::lookAt(mPosition, mPosition + mTarget, mUp);
}

glm::mat4 Camera::getPerspective(const float aspectRatio) const
{
    return glm::perspective(glm::radians(mFieldOfView), aspectRatio, mNear, mFar);
}

glm::mat4 Camera::getInfPerspective(const float aspectRatio) const
{
    return glm::infinitePerspective(glm::radians(mFieldOfView), aspectRatio, mNear);
}

glm::vec3 Camera::getFrustumEyeRay(float ar, int x, int y) const
{
    // Transform from NDC space to world space using inverse view-projection
    glm::vec3 actualPos = getActualPosition();
    glm::mat4 invViewProj = glm::inverse(getPerspective(ar) * getLookAt());
    glm::vec4 eyeVec = invViewProj * glm::vec4(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f);
    eyeVec /= eyeVec.w;

    // Return ray direction from camera position to the frustum corner point
    return glm::normalize(glm::vec3(eyeVec) - actualPos);
}

void Camera::updateFieldOfView(float dy)
{
    if (mFieldOfView >= 1.0f && mFieldOfView <= scMaxFieldOfView)
    {
        mFieldOfView -= dy;

        if (mFieldOfView <= 1.0f)
            mFieldOfView = 1.0f;
        else if (mFieldOfView >= scMaxFieldOfView)
            mFieldOfView = scMaxFieldOfView;
    }
}

glm::vec3 Camera::getPosition() const
{
    return mPosition;
}

void Camera::setPosition(const glm::vec3 &position)
{
    mPosition = position;
}

glm::vec3 Camera::getTarget() const
{
    return mTarget;
}

void Camera::setTarget(const glm::vec3 &target)
{
    mTarget = target;
}

glm::vec3 Camera::getUp() const
{
    return mUp;
}

void Camera::setUp(const glm::vec3 &up)
{
    if (glm::length(up) > 0.0001f)
    {
        mUp = glm::normalize(up);
        mUseCustomUpVector = true;
        updateVectors();
    }
}

glm::vec3 Camera::getRight() const
{
    return mRight;
}

void Camera::setRight(const glm::vec3 &right)
{
    mRight = right;
}

float Camera::getNear() const
{
    return mNear;
}

void Camera::setNear(float near)
{
    mNear = near;
}

float Camera::getFar() const
{
    return mFar;
}

void Camera::setFar(float far)
{
    mFar = far;
}

float Camera::getYaw() const
{
    return mYaw;
}

float Camera::getPitch() const
{
    return mPitch;
}

void Camera::setYawPitch(float yaw, float pitch, bool clampPitch, bool wrapYaw)
{
    mYaw = yaw;
    mPitch = pitch;

    if (clampPitch)
    {
        if (mPitch > scMaxPitchValue)
            mPitch = scMaxPitchValue;
        if (mPitch < -scMaxPitchValue)
            mPitch = -scMaxPitchValue;
    }

    if (wrapYaw)
    {
        if (mYaw > scMaxYawValue)
            mYaw = -scMaxYawValue;
        if (mYaw < -scMaxYawValue)
            mYaw = scMaxYawValue;
    }

    updateVectors();
}

void Camera::updateVectors()
{
    static const glm::vec3 yAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 target;
    target.x = std::cos(glm::radians(mYaw)) * std::cos(glm::radians(mPitch));
    target.y = std::sin(glm::radians(mPitch));
    target.z = std::sin(glm::radians(mYaw)) * std::cos(glm::radians(mPitch));
    mTarget = glm::normalize(target);
    const glm::vec3 referenceUp = mUseCustomUpVector ? mUp : yAxis;
    mRight = glm::normalize(glm::cross(mTarget, referenceUp));
    if (glm::length(mRight) < 0.0001f)
    {
        mRight = glm::normalize(glm::cross(mTarget, yAxis));
    }
    // When using custom up vector (e.g., for spherical planet), preserve it exactly
    // Otherwise, recompute up from right/target for standard Cartesian movement
    if (!mUseCustomUpVector)
    {
        mUp = glm::normalize(glm::cross(mRight, mTarget));
    }
}

// ============================================================================
// Third-person camera support
// ============================================================================

void Camera::updateThirdPersonPosition()
{
    if (mMode != CameraMode::THIRD_PERSON)
    {
        return;
    }

    const glm::vec3 upAxis = glm::normalize(mUp);
    
    // For spherical planets with custom up vector, use the target direction directly
    // Project target onto plane perpendicular to up to get horizontal forward direction
    glm::vec3 forwardFlat = mTarget - upAxis * glm::dot(mTarget, upAxis);
    const float forwardFlatLen = glm::length(forwardFlat);
    
    if (forwardFlatLen < 0.0001f)
    {
        // If looking straight up/down, use right vector to determine backward direction
        glm::vec3 fallbackForward = glm::cross(upAxis, mRight);
        if (glm::length(fallbackForward) > 0.0001f)
        {
            forwardFlat = glm::normalize(fallbackForward);
        }
        else
        {
            forwardFlat = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }
    else
    {
        forwardFlat = forwardFlat / forwardFlatLen;
    }

    mPosition = mFollowTarget - forwardFlat * mThirdPersonDistance + upAxis * mThirdPersonHeight;
}

glm::vec3 Camera::getActualPosition() const
{
    if (mMode == CameraMode::THIRD_PERSON)
    {
        const glm::vec3 upAxis = glm::normalize(mUp);
        glm::vec3 forwardFlat = mTarget - upAxis * glm::dot(mTarget, upAxis);
        const float forwardFlatLen = glm::length(forwardFlat);
        
        if (forwardFlatLen < 0.0001f)
        {
            // Use right vector fallback for degenerate cases
            glm::vec3 fallbackForward = glm::cross(upAxis, mRight);
            if (glm::length(fallbackForward) > 0.0001f)
            {
                forwardFlat = glm::normalize(fallbackForward);
            }
            else
            {
                forwardFlat = glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }
        else
        {
            forwardFlat = forwardFlat / forwardFlatLen;
        }

        return mFollowTarget - forwardFlat * mThirdPersonDistance + upAxis * mThirdPersonHeight;
    }
    return mPosition;
}

glm::mat4 Camera::getThirdPersonLookAt() const
{
    glm::vec3 cameraPos = getActualPosition();
    // Look at the follow target (player position) plus a slight height offset
    glm::vec3 lookAtPoint = mFollowTarget + mUp;

    // Debug log on first call
    static bool firstCall = true;
    if (firstCall)
    {
        firstCall = false;
    }

    return glm::lookAt(cameraPos, lookAtPoint, glm::normalize(mUp));
}
