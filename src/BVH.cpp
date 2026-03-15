#include "BVH.hpp"

#include <algorithm>
#include <numeric>

// ────────────────────────────────────────────────────────────────────────────
// Public
// ────────────────────────────────────────────────────────────────────────────

void BVHBuilder::build(std::vector<GLTFModel::RayTraceTriangle> &triangles)
{
    const int triCount = static_cast<int>(triangles.size());
    if (triCount == 0)
    {
        mNodes.clear();
        return;
    }

    // Pre-compute per-triangle AABB and centroid
    mTriInfos.resize(triCount);
    mTriIndices.resize(triCount);
    for (int i = 0; i < triCount; ++i)
    {
        mTriIndices[i] = i;
        auto &info = mTriInfos[i];
        info.bounds = AABB{};
        info.bounds.grow(glm::vec3(triangles[i].v0));
        info.bounds.grow(glm::vec3(triangles[i].v1));
        info.bounds.grow(glm::vec3(triangles[i].v2));
        info.centroid = (glm::vec3(triangles[i].v0) +
                         glm::vec3(triangles[i].v1) +
                         glm::vec3(triangles[i].v2)) / 3.0f;
    }

    // Reserve worst-case node count (2*n - 1 for n leaves)
    mNodes.clear();
    mNodes.reserve(2 * triCount);

    // Create root node
    mNodes.push_back(BVHNode{});
    buildRecursive(0, 0, triCount);

    // Reorder triangle array to match BVH leaf order (mTriIndices)
    std::vector<GLTFModel::RayTraceTriangle> sorted(triCount);
    for (int i = 0; i < triCount; ++i)
        sorted[i] = triangles[mTriIndices[i]];
    triangles = std::move(sorted);

}

// ────────────────────────────────────────────────────────────────────────────
// Private helpers
// ────────────────────────────────────────────────────────────────────────────

AABB BVHBuilder::computeBounds(int begin, int end) const
{
    AABB box;
    for (int i = begin; i < end; ++i)
        box.grow(mTriInfos[mTriIndices[i]].bounds);
    return box;
}

AABB BVHBuilder::computeCentroidBounds(int begin, int end) const
{
    AABB box;
    for (int i = begin; i < end; ++i)
        box.grow(mTriInfos[mTriIndices[i]].centroid);
    return box;
}

void BVHBuilder::buildRecursive(int nodeIdx, int begin, int end)
{
    const int count = end - begin;
    AABB nodeBounds = computeBounds(begin, end);
    BVHNode &node = mNodes[nodeIdx];
    node.boundsMin = nodeBounds.bmin;
    node.boundsMax = nodeBounds.bmax;

    // Make a leaf if few enough primitives
    if (count <= kMaxLeafSize)
    {
        node.leftFirst = static_cast<float>(begin);
        node.triCount  = static_cast<float>(count);
        return;
    }

    // ── SAH binned split ────────────────────────────────────────────
    AABB centroidBounds = computeCentroidBounds(begin, end);
    glm::vec3 extent = centroidBounds.bmax - centroidBounds.bmin;

    // Pick longest axis
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    // Degenerate centroid extent → make leaf
    if (extent[axis] < 1e-7f)
    {
        node.leftFirst = static_cast<float>(begin);
        node.triCount  = static_cast<float>(count);
        return;
    }

    // Bin triangles
    struct Bin { AABB bounds; int count{0}; };
    Bin bins[kSAHBins];

    float scale = static_cast<float>(kSAHBins) / extent[axis];
    for (int i = begin; i < end; ++i)
    {
        int idx = mTriIndices[i];
        int b = std::min(kSAHBins - 1,
                         static_cast<int>((mTriInfos[idx].centroid[axis] - centroidBounds.bmin[axis]) * scale));
        bins[b].count++;
        bins[b].bounds.grow(mTriInfos[idx].bounds);
    }

    // Evaluate SAH cost at each split position
    float bestCost = 1e30f;
    int bestSplit = -1;

    // Sweep from left, accumulating area and count
    float leftArea[kSAHBins - 1], rightArea[kSAHBins - 1];
    int   leftCount[kSAHBins - 1], rightCount[kSAHBins - 1];

    {
        AABB leftBox;
        int leftN = 0;
        for (int i = 0; i < kSAHBins - 1; ++i)
        {
            leftBox.grow(bins[i].bounds);
            leftN += bins[i].count;
            leftArea[i] = leftBox.area();
            leftCount[i] = leftN;
        }
    }
    {
        AABB rightBox;
        int rightN = 0;
        for (int i = kSAHBins - 1; i > 0; --i)
        {
            rightBox.grow(bins[i].bounds);
            rightN += bins[i].count;
            rightArea[i - 1] = rightBox.area();
            rightCount[i - 1] = rightN;
        }
    }

    for (int i = 0; i < kSAHBins - 1; ++i)
    {
        float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
        if (cost < bestCost)
        {
            bestCost = cost;
            bestSplit = i;
        }
    }

    // If SAH says a leaf is cheaper, make a leaf
    float leafCost = static_cast<float>(count) * nodeBounds.area();
    if (bestSplit < 0 || bestCost >= leafCost)
    {
        node.leftFirst = static_cast<float>(begin);
        node.triCount  = static_cast<float>(count);
        return;
    }

    // Partition mTriIndices[begin..end) by split plane
    float splitPos = centroidBounds.bmin[axis] +
                     (static_cast<float>(bestSplit + 1) / static_cast<float>(kSAHBins)) * extent[axis];

    auto mid = std::partition(mTriIndices.begin() + begin, mTriIndices.begin() + end,
                              [&](int idx) { return mTriInfos[idx].centroid[axis] < splitPos; });
    int midIdx = static_cast<int>(mid - mTriIndices.begin());

    // Guard against degenerate partitions
    if (midIdx == begin || midIdx == end)
    {
        midIdx = begin + count / 2;
        std::nth_element(mTriIndices.begin() + begin, mTriIndices.begin() + midIdx,
                         mTriIndices.begin() + end,
                         [&](int a, int b) {
                             return mTriInfos[a].centroid[axis] < mTriInfos[b].centroid[axis];
                         });
    }

    // Allocate two child nodes (left, right stored contiguously)
    int leftChild = static_cast<int>(mNodes.size());
    mNodes.push_back(BVHNode{});
    mNodes.push_back(BVHNode{});

    // Update current node (must re-fetch reference after push_back may realloc)
    mNodes[nodeIdx].leftFirst = static_cast<float>(leftChild);
    mNodes[nodeIdx].triCount  = 0.0f; // internal node

    buildRecursive(leftChild, begin, midIdx);
    buildRecursive(leftChild + 1, midIdx, end);
}
