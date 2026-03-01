#ifndef PLANE_HPP
#define PLANE_HPP

#include <glm/glm.hpp>

#include "Material.hpp"

/// @brief GPU-friendly plane data structure for raytracing
class Plane
{
public:
    explicit Plane(const glm::vec3 &point, const glm::vec3 &normal, const Material &material);

    const glm::vec3 &getPoint() const noexcept;
    const glm::vec3 &getNormal() const noexcept;
    const Material &getMaterial() const noexcept;

private:
    Material mMaterial;
    glm::vec3 mPoint;
    glm::vec3 mNormal;
};

#endif // PLANE_HPP
