#include "Plane.hpp"
// The Plane class now represents the ground plane for Voronoi coloring in the pathtracer.
// All Voronoi logic is handled in the shader; this class remains unchanged for compatibility.

Plane::Plane(const glm::vec3 &point, const glm::vec3 &normal, const Material &material)
    : mPoint(point), mNormal(normal), mMaterial(material) {}

const glm::vec3 &Plane::getPoint() const noexcept { return mPoint; }
const glm::vec3 &Plane::getNormal() const noexcept { return mNormal; }
const Material &Plane::getMaterial() const noexcept { return mMaterial; }
