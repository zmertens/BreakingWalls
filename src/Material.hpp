#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

/// @brief Physically-based material class supporting legacy and PBR properties
class Material
{
public:
    /// Material types for physically-based rendering
    enum class MaterialType : std::uint32_t
    {
        // Diffuse/matte materials
        LAMBERTIAN = 0,
        // Reflective metallic materials with optional fuzz
        METAL = 1,
        // Glass-like materials with refraction and reflection
        DIELECTRIC = 2
    };

    typedef std::unique_ptr<Material> Ptr;

public:
    explicit Material();

    /// Legacy constructor for backward compatibility
    explicit Material(const glm::vec3 &ambient, const glm::vec3 &diffuse, const glm::vec3 &specular,
                      float shininess);

    /// Legacy constructor with reflectivity/refractivity
    explicit Material(const glm::vec3 &ambient, const glm::vec3 &diffuse, const glm::vec3 &specular,
                      float shininess, float reflectValue, float refractValue);

    /// PBR constructor for modern physically-based rendering
    explicit Material(const glm::vec3 &albedo, MaterialType type, float fuzz = 0.0f, float refractiveIndex = 1.5f);

    // Legacy getters/setters (for backward compatibility)
    [[nodiscard]] glm::vec3 getAmbient() const;
    void setAmbient(const glm::vec3 &ambient);

    [[nodiscard]] glm::vec3 getDiffuse() const;
    void setDiffuse(const glm::vec3 &diffuse);

    [[nodiscard]] glm::vec3 getSpecular() const;
    void setSpecular(const glm::vec3 &specular);

    [[nodiscard]] float getShininess() const;
    void setShininess(float shininess);

    [[nodiscard]] float getReflectivity() const;
    void setReflectivity(float reflectivity);

    [[nodiscard]] float getRefractivity() const;
    void setRefractivity(float refractivity);

    // PBR getters/setters
    [[nodiscard]] glm::vec3 getAlbedo() const;
    void setAlbedo(const glm::vec3 &albedo);

    [[nodiscard]] MaterialType getType() const;
    void setType(MaterialType type);

    [[nodiscard]] float getFuzz() const;
    void setFuzz(float fuzz);

    [[nodiscard]] float getRefractiveIndex() const;
    void setRefractiveIndex(float index);

private:
    // Legacy properties (for backward compatibility)
    glm::vec3 mAmbient;
    glm::vec3 mDiffuse;
    glm::vec3 mSpecular;
    float mShininess;
    float mReflectivity;
    float mRefractivity;

    // New PBR properties
    glm::vec3 mAlbedo;      ///< Base color for PBR
    MaterialType mType;     ///< Material type
    float mFuzz;            ///< Fuzziness for metals (0-1)
    float mRefractiveIndex; ///< Refractive index for dielectrics (e.g., 1.5 for glass)
};

#endif // MATERIAL_HPP
