#include "Light.hpp"

Light::Light()
    : mAmbient(glm::vec3(0)), mDiffuse(glm::vec3(0)), mSpecular(glm::vec3(0)), mPosition(glm::vec4(0))
{
}

Light::Light(const glm::vec3 &ambient, const glm::vec3 &diffuse, const glm::vec3 &specular,
             const glm::vec4 &position)
    : mAmbient(ambient), mDiffuse(diffuse), mSpecular(specular), mPosition(position)
{
}

glm::vec3 Light::getAmbient() const
{
    return mAmbient;
}

void Light::setAmbient(const glm::vec3 &ambient)
{
    mAmbient = ambient;
}

glm::vec3 Light::getDiffuse() const
{
    return mDiffuse;
}

void Light::setDiffuse(const glm::vec3 &diffuse)
{
    mDiffuse = diffuse;
}

glm::vec3 Light::getSpecular() const
{
    return mSpecular;
}

void Light::setSpecular(const glm::vec3 &specular)
{
    mSpecular = specular;
}

glm::vec4 Light::getPosition() const
{
    return mPosition;
}

void Light::setPosition(const glm::vec4 &position)
{
    mPosition = position;
}
