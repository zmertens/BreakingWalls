#ifndef BVH_HPP
#define BVH_HPP

#include <glm/glm.hpp>
#include <vector>
#include "GLTFModel.hpp"

/// Flat BVH node for GPU upload (2 x vec4 = 32 bytes, std430-friendly).
///
/// Internal node: triCount == 0, leftFirst = index of left child (right = left+1)
/// Leaf node:     triCount  > 0, leftFirst = index of first triangle in reordered array
struct alignas(16) BVHNode
{
    glm::vec3 boundsMin;
    float leftFirst;  ///< left child idx (internal) or first tri idx (leaf)
    glm::vec3 boundsMax;
    float triCount;   ///< 0 = internal node, >0 = leaf with this many tris
};

/// Axis-aligned bounding box helper used during construction.
struct AABB
{
    glm::vec3 bmin{1e30f};
    glm::vec3 bmax{-1e30f};

    void grow(const glm::vec3 &p)
    {
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
    }

    void grow(const AABB &other)
    {
        bmin = glm::min(bmin, other.bmin);
        bmax = glm::max(bmax, other.bmax);
    }

    float area() const
    {
        glm::vec3 e = bmax - bmin;
        return e.x * e.y + e.y * e.z + e.z * e.x; // half surface area
    }
};

/// Builds a flat BVH over RayTraceTriangle data using the Surface Area Heuristic.
/// After build(), the triangles vector is reordered to match BVH leaf order and
/// the nodes vector is ready for GPU upload as an SSBO.
class BVHBuilder
{
public:
    /// Build BVH from a triangle list.  The triangle vector is reordered in-place.
    void build(std::vector<GLTFModel::RayTraceTriangle> &triangles);

    const std::vector<BVHNode> &getNodes() const { return mNodes; }

private:
    static constexpr int kMaxLeafSize = 4; ///< max triangles per leaf
    static constexpr int kSAHBins = 16;    ///< SAH evaluation bins

    struct TriInfo
    {
        AABB bounds;
        glm::vec3 centroid;
    };

    std::vector<BVHNode> mNodes;
    std::vector<TriInfo> mTriInfos;
    std::vector<int>     mTriIndices;  ///< indirection: mTriIndices[i] -> original tri index

    void buildRecursive(int nodeIdx, int begin, int end);
    AABB computeBounds(int begin, int end) const;
    AABB computeCentroidBounds(int begin, int end) const;
};

#endif // BVH_HPP
