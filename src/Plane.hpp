#ifndef PLANE_HPP
#define PLANE_HPP

#include <glm/glm.hpp>

#include "Material.hpp"

/// @brief GPU-friendly plane data structure for raytracing
class Plane
{
public:
    Material material;
    glm::vec3 point;        ///< A point on the plane
    glm::vec3 normal;       ///< Plane normal vector
};

#endif // PLANE_HPP
