#include "Plane.hpp"

Plane::Plane(const glm::vec3 &point, const glm::vec3 &normal, const Material &material)
    : mPoint(point), mNormal(normal), mMaterial(material) {}

const glm::vec3 &Plane::getPoint() const noexcept
{
    return mPoint;
}

const glm::vec3 &Plane::getNormal() const noexcept
{
    return mNormal;
}

const Material &Plane::getMaterial() const noexcept
{
    return mMaterial;
}
