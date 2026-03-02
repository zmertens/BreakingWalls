#include "Material.hpp"

Material::Material()
    : mAmbient(glm::vec3(0)), mDiffuse(glm::vec3(0)), mSpecular(glm::vec3(0)), mShininess(0), mReflectivity(0), mRefractivity(0), mAlbedo(glm::vec3(0.5f)), mType(MaterialType::LAMBERTIAN), mFuzz(0.0f), mRefractiveIndex(1.5f)
{
}

Material::Material(const glm::vec3 &ambient, const glm::vec3 &diffuse, const glm::vec3 &specular,
                   float shininess)
    : mAmbient(ambient), mDiffuse(diffuse), mSpecular(specular), mShininess(shininess), mReflectivity(0), mRefractivity(0), mAlbedo(diffuse), mType(MaterialType::LAMBERTIAN), mFuzz(0.0f), mRefractiveIndex(1.5f)
{
}

Material::Material(const glm::vec3 &ambient, const glm::vec3 &diffuse, const glm::vec3 &specular,
                   float shininess, float reflectValue, float refractValue)
    : mAmbient(ambient), mDiffuse(diffuse), mSpecular(specular), mShininess(shininess), mReflectivity(reflectValue), mRefractivity(refractValue), mAlbedo(diffuse), mType(MaterialType::LAMBERTIAN), mFuzz(0.0f), mRefractiveIndex(refractValue > 0.0f ? refractValue : 1.5f)
{
}

Material::Material(const glm::vec3 &albedo, MaterialType type, float fuzz, float refractiveIndex)
    : mAmbient(albedo), mDiffuse(albedo), mSpecular(glm::vec3(1.0f)), mShininess(32.0f), mReflectivity(type == MaterialType::METAL ? 1.0f : 0.0f), mRefractivity(refractiveIndex), mAlbedo(albedo), mType(type), mFuzz(fuzz), mRefractiveIndex(refractiveIndex)
{
}

glm::vec3 Material::getAmbient() const
{
    return mAmbient;
}

void Material::setAmbient(const glm::vec3 &ambient)
{
    mAmbient = ambient;
}

glm::vec3 Material::getDiffuse() const
{
    return mDiffuse;
}

void Material::setDiffuse(const glm::vec3 &diffuse)
{
    mDiffuse = diffuse;
}

glm::vec3 Material::getSpecular() const
{
    return mSpecular;
}

void Material::setSpecular(const glm::vec3 &specular)
{
    mSpecular = specular;
}

float Material::getShininess() const
{
    return mShininess;
}

void Material::setShininess(float shininess)
{
    mShininess = shininess;
}

float Material::getReflectivity() const
{
    return mReflectivity;
}

void Material::setReflectivity(float reflectivity)
{
    mReflectivity = reflectivity;
}

float Material::getRefractivity() const
{
    return mRefractivity;
}

void Material::setRefractivity(float refractivity)
{
    mRefractivity = refractivity;
}

glm::vec3 Material::getAlbedo() const
{
    return mAlbedo;
}

void Material::setAlbedo(const glm::vec3 &albedo)
{
    mAlbedo = albedo;
}

Material::MaterialType Material::getType() const
{
    return mType;
}

void Material::setType(Material::MaterialType type)
{
    mType = type;
}

float Material::getFuzz() const
{
    return mFuzz;
}

void Material::setFuzz(float fuzz)
{
    mFuzz = fuzz;
}

float Material::getRefractiveIndex() const
{
    return mRefractiveIndex;
}

void Material::setRefractiveIndex(float index)
{
    mRefractiveIndex = index;
}
