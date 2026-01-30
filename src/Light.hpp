#ifndef LIGHT_HPP
#define LIGHT_HPP

#include <memory>

#include <glm/glm.hpp>

/// @brief Light source for raytracing and illumination
class Light
{
public:
    typedef std::unique_ptr<Light> Ptr;

public:
    Light();
    
    explicit Light(const glm::vec3& ambient, const glm::vec3& diffuse, const glm::vec3& specular,
        const glm::vec4& position);

    [[nodiscard]] glm::vec3 getAmbient() const;
    void setAmbient(const glm::vec3& ambient);

    [[nodiscard]] glm::vec3 getDiffuse() const;
    void setDiffuse(const glm::vec3& diffuse);

    [[nodiscard]] glm::vec3 getSpecular() const;
    void setSpecular(const glm::vec3& specular);

    [[nodiscard]] glm::vec4 getPosition() const;
    void setPosition(const glm::vec4& position);

protected:
    glm::vec3 mAmbient;
    glm::vec3 mDiffuse;
    glm::vec3 mSpecular;
    glm::vec4 mPosition;
};

#endif // LIGHT_HPP
