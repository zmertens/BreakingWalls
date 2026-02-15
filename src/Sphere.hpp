#ifndef SPHERE_HPP
#define SPHERE_HPP

#include <glm/glm.hpp>

#include <cstdint>

#include "Material.hpp"

/// @brief GPU-friendly sphere data structure for compute shader raytracing
/// @details Layout maintains 16-byte alignment for proper SSBO storage
class Sphere
{
    
public:
    /// PBR constructor for physically-based rendering
    explicit Sphere(const glm::vec3 &cent, const float rad,
           const glm::vec3 &color, Material::MaterialType matType,
           float fuzziness = 0.0f, float refractIdx = 1.5f);

    void setCenter(const glm::vec3 &cent) noexcept;
    glm::vec3 getCenter() const noexcept;
    void setMaterialType(Material::MaterialType type) noexcept;
    Material::MaterialType getMaterialType() const noexcept;
    void setRadius(float rad) noexcept;
    float getRadius() const noexcept;
    void setRefractiveIndex(float index) noexcept;
    float getRefractiveIndex() const noexcept;
    void setFuzz(float fuzziness) noexcept;
    float getFuzz() const noexcept;

private:
    glm::vec4 mCenter;
    glm::vec4 mAmbient;       // For shader compatibility (not used in PBR)
    glm::vec4 mDiffuse;       // For shader compatibility (not used in PBR)
    glm::vec4 mSpecular;      // For shader compatibility (not used in PBR)
    float mRadius;
    float mRadius2;
    float mShininess;         // For shader compatibility (not used in PBR)
    float mReflectivity;      // For shader compatibility (not used in PBR)

    // New PBR properties (maintained at 16-byte alignment)
    ///< RGB color + padding
    glm::vec4 mAlbedo; 
    ///< 0=Lambertian, 1=Metal, 2=Dielectric
    std::uint32_t mMaterialType;
    // Fuzziness for metals
    float mFuzz;
    // Refractive index for dielectrics
    float mRefractiveIndex;
    // Alignment padding
    std::uint32_t mPadding;
};

#endif // SPHERE_HPP
