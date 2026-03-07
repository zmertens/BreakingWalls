#include "PathTraceScene.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>

int PathTraceScene::addMesh(PTMesh mesh)
{
    meshes.push_back(std::move(mesh));
    return static_cast<int>(meshes.size()) - 1;
}

int PathTraceScene::addMaterial(const PTMaterial &material)
{
    materials.push_back(material);
    return static_cast<int>(materials.size()) - 1;
}

int PathTraceScene::addMeshInstance(const PTMeshInstance &instance)
{
    meshInstances.push_back(instance);
    return static_cast<int>(meshInstances.size()) - 1;
}

int PathTraceScene::addLight(const PTLight &light)
{
    lights.push_back(light);
    return static_cast<int>(lights.size()) - 1;
}

void PathTraceScene::setCamera(const PTCamera &cam)
{
    camera = cam;
}

void PathTraceScene::setEnvMap(PTEnvMap map)
{
    envMap = std::move(map);
    envMapModified = true;
}

void PathTraceScene::processScene()
{
    // Flatten per-mesh vertex data into scene-wide arrays.
    // Each mesh instance references a mesh by index; we record the global vertex offset
    // per mesh so that indices are scene-global.

    vertIndices.clear();
    verticesUVX.clear();
    normalsUVY.clear();
    transforms.clear();
    meshInstanceInfos.clear();

    // Track per-mesh vertex offset and triangle offset/count in flattened array
    std::vector<int> meshVertexOffsets(meshes.size(), 0);
    std::vector<int> meshTriangleOffsets(meshes.size(), 0);
    std::vector<int> meshTriangleCounts(meshes.size(), 0);

    int currentVertexOffset = 0;
    int currentTriOffset = 0;
    for (size_t m = 0; m < meshes.size(); ++m)
    {
        meshVertexOffsets[m] = currentVertexOffset;
        meshTriangleOffsets[m] = currentTriOffset;

        const auto &mesh = meshes[m];

        // Append vertices and normals
        verticesUVX.insert(verticesUVX.end(), mesh.verticesUVX.begin(), mesh.verticesUVX.end());
        normalsUVY.insert(normalsUVY.end(), mesh.normalsUVY.begin(), mesh.normalsUVY.end());

        // Build index triplets (each consecutive 3 vertices form a triangle)
        int numTris = static_cast<int>(mesh.verticesUVX.size()) / 3;
        meshTriangleCounts[m] = numTris;
        for (int t = 0; t < numTris; ++t)
        {
            PTIndices idx;
            idx.x = currentVertexOffset + t * 3 + 0;
            idx.y = currentVertexOffset + t * 3 + 1;
            idx.z = currentVertexOffset + t * 3 + 2;
            vertIndices.push_back(idx);
        }

        currentVertexOffset += static_cast<int>(mesh.verticesUVX.size());
        currentTriOffset += numTris;
    }

    // Build transforms array (one per instance)
    transforms.reserve(meshInstances.size());
    for (const auto &inst : meshInstances)
    {
        transforms.push_back(inst.transform);
    }

    // Build mesh instance info (for brute-force intersection without BVH)
    meshInstanceInfos.reserve(meshInstances.size());
    for (size_t i = 0; i < meshInstances.size(); ++i)
    {
        const auto &inst = meshInstances[i];
        PTMeshInstanceInfo info;
        info.triStart      = static_cast<float>(meshTriangleOffsets[inst.meshID]);
        info.triCount      = static_cast<float>(meshTriangleCounts[inst.meshID]);
        info.materialID    = static_cast<float>(inst.materialID);
        info.transformIndex = static_cast<float>(i);
        meshInstanceInfos.push_back(info);
    }

    initialized = true;
    dirty = true;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "PathTraceScene: Processed %zu meshes, %zu instances, %zu triangles, %zu vertices, %zu materials, %zu lights",
                meshes.size(), meshInstances.size(), vertIndices.size(), verticesUVX.size(),
                materials.size(), lights.size());
}
