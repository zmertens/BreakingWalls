#include "Sphere.hpp"

Sphere::Sphere(const glm::vec3 &cent, const float rad,
               const glm::vec3 &color, Material::MaterialType matType,
               float fuzziness, float refractIdx)
    : mCenter(glm::vec4(cent, 0.0f)), mRadius(rad), mRadius2(rad * rad), mAlbedo(glm::vec4(color, 0.0f)), mMaterialType(static_cast<std::uint32_t>(matType)), mFuzz(fuzziness), mRefractiveIndex(refractIdx), mPadding(0)
{
}

void Sphere::setCenter(const glm::vec3 &cent) noexcept
{
    mCenter = glm::vec4(cent, 0.0f);
}
glm::vec3 Sphere::getCenter() const noexcept
{
    return glm::vec3(mCenter);
}

void Sphere::setMaterialType(Material::MaterialType type) noexcept
{
    mMaterialType = static_cast<std::uint32_t>(type);
}

Material::MaterialType Sphere::getMaterialType() const noexcept
{
    return static_cast<Material::MaterialType>(mMaterialType);
}

void Sphere::setRadius(float rad) noexcept
{
    mRadius = rad;
    mRadius2 = rad * rad;
}

float Sphere::getRadius() const noexcept
{
    return mRadius;
}

void Sphere::setRefractiveIndex(float index) noexcept
{
    mRefractiveIndex = index;
}

float Sphere::getRefractiveIndex() const noexcept
{
    return mRefractiveIndex;
}

void Sphere::setFuzz(float fuzziness) noexcept
{
    mFuzz = fuzziness;
}

float Sphere::getFuzz() const noexcept
{
    return mFuzz;
}


