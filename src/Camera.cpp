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
    mUp = up;
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

void Camera::updateVectors()
{
    static const glm::vec3 yAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 target;
    target.x = std::cos(glm::radians(mYaw)) * std::cos(glm::radians(mPitch));
    target.y = std::sin(glm::radians(mPitch));
    target.z = std::sin(glm::radians(mYaw)) * std::cos(glm::radians(mPitch));
    mTarget = glm::normalize(target);
    mRight = glm::normalize(glm::cross(mTarget, yAxis));
    mUp = glm::normalize(glm::cross(mRight, mTarget));
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

    // Calculate camera position behind and above the follow target
    // Use the camera's yaw to determine the orbit position around the target
    float horizontalDistance = mThirdPersonDistance * std::cos(glm::radians(mPitch));
    float verticalDistance = mThirdPersonDistance * std::sin(glm::radians(mPitch));

    // Calculate offset from target
    float offsetX = horizontalDistance * std::cos(glm::radians(mYaw + 180.0f));
    float offsetZ = horizontalDistance * std::sin(glm::radians(mYaw + 180.0f));
    float offsetY = mThirdPersonHeight + verticalDistance;

    // Update position
    mPosition = mFollowTarget + glm::vec3(offsetX, offsetY, offsetZ);
}

glm::vec3 Camera::getActualPosition() const
{
    if (mMode == CameraMode::THIRD_PERSON)
    {
        // Return the calculated third-person camera position
        float horizontalDistance = mThirdPersonDistance * std::cos(glm::radians(mPitch));
        float verticalDistance = mThirdPersonDistance * std::sin(glm::radians(mPitch));

        float offsetX = horizontalDistance * std::cos(glm::radians(mYaw + 180.0f));
        float offsetZ = horizontalDistance * std::sin(glm::radians(mYaw + 180.0f));
        float offsetY = mThirdPersonHeight + verticalDistance;

        return mFollowTarget + glm::vec3(offsetX, offsetY, offsetZ);
    }
    return mPosition;
}

glm::mat4 Camera::getThirdPersonLookAt() const
{
    glm::vec3 cameraPos = getActualPosition();
    // Look at the follow target (player position) plus a slight height offset
    glm::vec3 lookAtPoint = mFollowTarget + glm::vec3(0.0f, 1.0f, 0.0f);

    // Debug log on first call
    static bool firstCall = true;
    if (firstCall)
    {
        firstCall = false;
    }

    return glm::lookAt(cameraPos, lookAtPoint, glm::vec3(0.0f, 1.0f, 0.0f));
}
