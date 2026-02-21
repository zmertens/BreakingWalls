#include "GLTFModel.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <SDL3/SDL_log.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "Shader.hpp"

namespace
{
    void resolveRayTraceMaterial(const aiMaterial *material, glm::vec4 &albedoAndMaterial, glm::vec4 &materialParams) noexcept
    {
        glm::vec3 albedo(0.78f, 0.80f, 0.88f);
        float materialType = 0.0f;
        float fuzz = 0.08f;
        float refractiveIndex = 1.0f;

        if (material)
        {
            aiColor3D diffuse(0.0f, 0.0f, 0.0f);
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            {
                const glm::vec3 sampledDiffuse(diffuse.r, diffuse.g, diffuse.b);
                if (glm::length(sampledDiffuse) > 0.05f)
                {
                    albedo = sampledDiffuse;
                }
            }

            aiColor3D specular(0.0f, 0.0f, 0.0f);
            if (material->Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS)
            {
                const float specAvg = (specular.r + specular.g + specular.b) / 3.0f;
                if (specAvg > 0.25f)
                {
                    materialType = 1.0f;
                }
            }

            float opacity = 1.0f;
            if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
            {
                if (opacity < 0.35f)
                {
                    materialType = 2.0f;
                    refractiveIndex = 1.45f;
                }
            }

            float shininess = 0.0f;
            if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
            {
                if (materialType == 1.0f)
                {
                    const float normalizedShininess = std::clamp(shininess / 128.0f, 0.0f, 1.0f);
                    fuzz = std::max(0.03f, 0.4f - 0.35f * normalizedShininess);
                }
            }

            float reflectivity = 0.0f;
            if (material->Get(AI_MATKEY_REFLECTIVITY, reflectivity) == AI_SUCCESS)
            {
                if (reflectivity > 0.25f)
                {
                    materialType = 1.0f;
                    fuzz = std::max(0.03f, 0.30f - 0.22f * std::clamp(reflectivity, 0.0f, 1.0f));
                }
            }
        }

        if (materialType == 1.0f)
        {
            albedo = glm::clamp(albedo * 1.20f, glm::vec3(0.16f), glm::vec3(1.0f));
        }
        else if (materialType == 2.0f)
        {
            albedo = glm::clamp(albedo * 1.10f, glm::vec3(0.20f), glm::vec3(1.0f));
        }
        else
        {
            albedo = glm::clamp(albedo * 1.15f, glm::vec3(0.22f), glm::vec3(1.0f));
        }

        albedoAndMaterial = glm::vec4(albedo, materialType);
        materialParams = glm::vec4(fuzz, refractiveIndex, 0.0f, 0.0f);
    }

    float normalizedKeyLerp(float animationTime, float keyTime, float nextKeyTime) noexcept
    {
        const float deltaTime = nextKeyTime - keyTime;
        if (deltaTime <= 0.0f)
        {
            return 0.0f;
        }

        const float factor = (animationTime - keyTime) / deltaTime;
        return std::clamp(factor, 0.0f, 1.0f);
    }

    void collectMeshNodeTransforms(const aiNode *node,
                                   const glm::mat4 &parent,
                                   std::vector<glm::mat4> &meshNodeTransforms,
                                   std::vector<bool> &meshTransformAssigned,
                                   std::vector<std::string> &meshNodeNames)
    {
        if (!node)
        {
            return;
        }

        glm::mat4 nodeLocal;
        nodeLocal[0][0] = node->mTransformation.a1;
        nodeLocal[1][0] = node->mTransformation.a2;
        nodeLocal[2][0] = node->mTransformation.a3;
        nodeLocal[3][0] = node->mTransformation.a4;
        nodeLocal[0][1] = node->mTransformation.b1;
        nodeLocal[1][1] = node->mTransformation.b2;
        nodeLocal[2][1] = node->mTransformation.b3;
        nodeLocal[3][1] = node->mTransformation.b4;
        nodeLocal[0][2] = node->mTransformation.c1;
        nodeLocal[1][2] = node->mTransformation.c2;
        nodeLocal[2][2] = node->mTransformation.c3;
        nodeLocal[3][2] = node->mTransformation.c4;
        nodeLocal[0][3] = node->mTransformation.d1;
        nodeLocal[1][3] = node->mTransformation.d2;
        nodeLocal[2][3] = node->mTransformation.d3;
        nodeLocal[3][3] = node->mTransformation.d4;

        const glm::mat4 global = parent * nodeLocal;

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            const unsigned int meshIndex = node->mMeshes[i];
            if (meshIndex < meshNodeTransforms.size() && !meshTransformAssigned[meshIndex])
            {
                meshNodeTransforms[meshIndex] = global;
                meshTransformAssigned[meshIndex] = true;
                meshNodeNames[meshIndex] = node->mName.C_Str();
            }
        }

        for (unsigned int child = 0; child < node->mNumChildren; ++child)
        {
            collectMeshNodeTransforms(node->mChildren[child], global, meshNodeTransforms, meshTransformAssigned, meshNodeNames);
        }
    }

    float findAnimationDurationTicks(const aiAnimation *animation) noexcept
    {
        if (!animation)
        {
            return 0.0f;
        }

        float duration = static_cast<float>(animation->mDuration);
        if (duration > 0.0f)
        {
            return duration;
        }

        float maxKeyTime = 0.0f;
        for (unsigned int i = 0; i < animation->mNumChannels; ++i)
        {
            const aiNodeAnim *channel = animation->mChannels[i];
            if (!channel)
            {
                continue;
            }

            if (channel->mNumPositionKeys > 0)
            {
                maxKeyTime = std::max(maxKeyTime, static_cast<float>(channel->mPositionKeys[channel->mNumPositionKeys - 1].mTime));
            }
            if (channel->mNumRotationKeys > 0)
            {
                maxKeyTime = std::max(maxKeyTime, static_cast<float>(channel->mRotationKeys[channel->mNumRotationKeys - 1].mTime));
            }
            if (channel->mNumScalingKeys > 0)
            {
                maxKeyTime = std::max(maxKeyTime, static_cast<float>(channel->mScalingKeys[channel->mNumScalingKeys - 1].mTime));
            }
        }

        return maxKeyTime;
    }

    float animationActivityScore(const aiAnimation *animation) noexcept
    {
        if (!animation)
        {
            return -1.0f;
        }

        float score = findAnimationDurationTicks(animation);
        for (unsigned int i = 0; i < animation->mNumChannels; ++i)
        {
            const aiNodeAnim *channel = animation->mChannels[i];
            if (!channel)
            {
                continue;
            }

            if (channel->mNumPositionKeys > 1)
            {
                score += 1.0f;
            }
            if (channel->mNumRotationKeys > 1)
            {
                score += 1.0f;
            }
            if (channel->mNumScalingKeys > 1)
            {
                score += 1.0f;
            }
        }

        return score;
    }

    float animationMotionScore(const aiAnimation *animation) noexcept
    {
        if (!animation)
        {
            return -1.0f;
        }

        float score = 0.0f;
        for (unsigned int i = 0; i < animation->mNumChannels; ++i)
        {
            const aiNodeAnim *channel = animation->mChannels[i];
            if (!channel)
            {
                continue;
            }

            if (channel->mNumPositionKeys > 1)
            {
                for (unsigned int key = 0; key + 1 < channel->mNumPositionKeys; ++key)
                {
                    const aiVector3D &a = channel->mPositionKeys[key].mValue;
                    const aiVector3D &b = channel->mPositionKeys[key + 1].mValue;
                    score += (b - a).Length();
                }
            }

            if (channel->mNumRotationKeys > 1)
            {
                for (unsigned int key = 0; key + 1 < channel->mNumRotationKeys; ++key)
                {
                    const aiQuaternion &a = channel->mRotationKeys[key].mValue;
                    const aiQuaternion &b = channel->mRotationKeys[key + 1].mValue;
                    float d = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                    d = std::clamp(d, 0.0f, 1.0f);
                    score += 1.0f - d;
                }
            }

            if (channel->mNumScalingKeys > 1)
            {
                for (unsigned int key = 0; key + 1 < channel->mNumScalingKeys; ++key)
                {
                    const aiVector3D &a = channel->mScalingKeys[key].mValue;
                    const aiVector3D &b = channel->mScalingKeys[key + 1].mValue;
                    score += (b - a).Length() * 0.25f;
                }
            }
        }

        score += animationActivityScore(animation) * 0.01f;
        return score;
    }

    bool animationHasTemporalKeys(const aiAnimation *animation) noexcept
    {
        if (!animation)
        {
            return false;
        }

        for (unsigned int i = 0; i < animation->mNumChannels; ++i)
        {
            const aiNodeAnim *channel = animation->mChannels[i];
            if (!channel)
            {
                continue;
            }

            if (channel->mNumPositionKeys > 1 || channel->mNumRotationKeys > 1 || channel->mNumScalingKeys > 1)
            {
                return true;
            }
        }

        return false;
    }

    std::string canonicalizeNodeName(std::string_view nodeName)
    {
        std::string canonical;
        canonical.reserve(nodeName.size());

        std::size_t start = 0;
        for (std::size_t i = 0; i < nodeName.size(); ++i)
        {
            const char ch = nodeName[i];
            if (ch == '|' || ch == '/' || ch == ':' || ch == '\\')
            {
                start = i + 1;
            }
        }

        for (std::size_t i = start; i < nodeName.size(); ++i)
        {
            canonical.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(nodeName[i]))));
        }

        return canonical;
    }

    float mat4DifferenceScore(const glm::mat4 &a, const glm::mat4 &b) noexcept
    {
        float total = 0.0f;
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                total += std::abs(a[c][r] - b[c][r]);
            }
        }
        return total;
    }
}

GLTFModel::~GLTFModel()
{
    clearGpuBuffers();
}

bool GLTFModel::readFile(std::string_view filename)
{
    clearGpuBuffers();
    mCpuMeshes.clear();
    mBoneMapping.clear();
    mCanonicalBoneMapping.clear();
    mBoneOffsets.clear();
    mBoneMeshNodeTransforms.clear();
    mPreferredAnimationIndex = -1;

    mScene = mImporter.ReadFile(
        filename.data(),
        aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights);

    if (!mScene || !mScene->mRootNode || mScene->mNumMeshes == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GLTFModel: failed to load '%s': %s",
                     filename.data(),
                     mImporter.GetErrorString());
        mScene = nullptr;
        return false;
    }

    const glm::mat4 rootTransform = toGlm(mScene->mRootNode->mTransformation);
    mGlobalInverseTransform = glm::inverse(rootTransform);

    buildMeshesFromScene(mScene);

    if (!mBoneOffsets.empty() && mScene->mRootNode)
    {
        std::unordered_map<std::string, glm::mat4> bindPoseNodeGlobals;
        buildNodeTransformMap(0.0f, mScene->mRootNode, glm::mat4(1.0f), nullptr, bindPoseNodeGlobals);

        auto matrixError = [](const glm::mat4 &a, const glm::mat4 &b) noexcept
        {
            float err = 0.0f;
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    err += std::abs(a[c][r] - b[c][r]);
                }
            }
            return err;
        };

        for (std::size_t boneIndex = 0; boneIndex < mBoneOffsets.size(); ++boneIndex)
        {
            const glm::mat4 &offset = mBoneOffsets[boneIndex];
            const glm::mat4 meshNodeTransform =
                boneIndex < mBoneMeshNodeTransforms.size() ? mBoneMeshNodeTransforms[boneIndex] : glm::mat4(1.0f);
            const glm::mat4 estimatedJointGlobal = meshNodeTransform * glm::inverse(offset);
            float bestErr = std::numeric_limits<float>::infinity();
            std::string bestNodeName;

            for (const auto &entry : bindPoseNodeGlobals)
            {
                const float err = matrixError(entry.second, estimatedJointGlobal);
                if (err < bestErr)
                {
                    bestErr = err;
                    bestNodeName = entry.first;
                }
            }

            if (!bestNodeName.empty())
            {
                const std::string canonical = canonicalizeNodeName(bestNodeName);
                if (!canonical.empty())
                {
                    if (mCanonicalBoneMapping.find(canonical) == mCanonicalBoneMapping.end())
                    {
                        mCanonicalBoneMapping[canonical] = static_cast<std::uint32_t>(boneIndex);
                    }
                }
            }
        }
    }

    if (!mBoneMapping.empty() && mScene->mNumAnimations > 0)
    {
        unsigned int boneLogCount = 0;
        for (const auto &entry : mBoneMapping)
        {
            if (boneLogCount >= 12)
            {
                break;
            }
            ++boneLogCount;
        }

        const aiAnimation *sampleAnim = mScene->mAnimations[0];
        if (sampleAnim)
        {
            const unsigned int sampleChannels = std::min<unsigned int>(sampleAnim->mNumChannels, 20u);
            for (unsigned int channelIdx = 0; channelIdx < sampleChannels; ++channelIdx)
            {
                const aiNodeAnim *channel = sampleAnim->mChannels[channelIdx];
                if (!channel)
                {
                    continue;
                }

                const std::string channelName = channel->mNodeName.C_Str();
                const bool matched = findBoneIndex(channelName) != nullptr;
            }
        }
    }

    // Score and select animation: prioritize temporal motion on mapped skeleton bones.
    float bestScore = -std::numeric_limits<float>::infinity();
    unsigned int bestMappedAnimatedChannels = 0;
    for (unsigned int i = 0; i < mScene->mNumAnimations; ++i)
    {
        const aiAnimation *candidate = mScene->mAnimations[i];
        if (!candidate || candidate->mNumChannels == 0)
        {
            continue;
        }

        float score = 0.0f;
        unsigned int mappedAnimatedChannels = 0;
        unsigned int animatedChannels = 0;

        for (unsigned int channelIndex = 0; channelIndex < candidate->mNumChannels; ++channelIndex)
        {
            const aiNodeAnim *channel = candidate->mChannels[channelIndex];
            if (!channel)
            {
                continue;
            }

            const bool hasTemporalKeys =
                (channel->mNumPositionKeys > 1) ||
                (channel->mNumRotationKeys > 1) ||
                (channel->mNumScalingKeys > 1);

            if (!hasTemporalKeys)
            {
                continue;
            }

            ++animatedChannels;

            const bool mappedToBone = findBoneIndex(channel->mNodeName.C_Str()) != nullptr;
            if (mappedToBone)
            {
                ++mappedAnimatedChannels;
            }

            const float channelWeight = mappedToBone ? 1.0f : 0.05f;

            if (channel->mNumPositionKeys > 1)
            {
                for (unsigned int key = 0; key + 1 < channel->mNumPositionKeys; ++key)
                {
                    const aiVector3D &a = channel->mPositionKeys[key].mValue;
                    const aiVector3D &b = channel->mPositionKeys[key + 1].mValue;
                    score += (b - a).Length() * channelWeight;
                }
            }

            if (channel->mNumRotationKeys > 1)
            {
                for (unsigned int key = 0; key + 1 < channel->mNumRotationKeys; ++key)
                {
                    const aiQuaternion &a = channel->mRotationKeys[key].mValue;
                    const aiQuaternion &b = channel->mRotationKeys[key + 1].mValue;
                    float d = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                    d = std::clamp(d, 0.0f, 1.0f);
                    score += (1.0f - d) * channelWeight;
                }
            }

            if (channel->mNumScalingKeys > 1)
            {
                for (unsigned int key = 0; key + 1 < channel->mNumScalingKeys; ++key)
                {
                    const aiVector3D &a = channel->mScalingKeys[key].mValue;
                    const aiVector3D &b = channel->mScalingKeys[key + 1].mValue;
                    score += (b - a).Length() * 0.25f * channelWeight;
                }
            }
        }

        score += static_cast<float>(mappedAnimatedChannels) * 2.5f;
        score += static_cast<float>(animatedChannels) * 0.02f;

        const float durationTicks = findAnimationDurationTicks(candidate);
        if (durationTicks > 0.0f && !mBoneOffsets.empty())
        {
            std::vector<glm::mat4> bonesAtT0(mBoneOffsets.size(), glm::mat4(1.0f));
            std::vector<glm::mat4> bonesAtT1(mBoneOffsets.size(), glm::mat4(1.0f));
            std::vector<glm::mat4> bonesAtT2(mBoneOffsets.size(), glm::mat4(1.0f));

            readNodeHierarchy(0.0f, mScene->mRootNode, glm::mat4(1.0f), candidate, bonesAtT0);
            readNodeHierarchy(durationTicks * 0.37f, mScene->mRootNode, glm::mat4(1.0f), candidate, bonesAtT1);
            readNodeHierarchy(durationTicks * 0.73f, mScene->mRootNode, glm::mat4(1.0f), candidate, bonesAtT2);

            float deformationScore = 0.0f;
            for (std::size_t boneIdx = 0; boneIdx < mBoneOffsets.size(); ++boneIdx)
            {
                deformationScore += mat4DifferenceScore(bonesAtT0[boneIdx], bonesAtT1[boneIdx]);
                deformationScore += mat4DifferenceScore(bonesAtT1[boneIdx], bonesAtT2[boneIdx]);
            }

            score += deformationScore * 0.01f;
        }

        if (!animationHasTemporalKeys(candidate))
        {
            score -= 1000.0f;
        }

        if (mappedAnimatedChannels == 0)
        {
            score -= 100000.0f;
        }
        
        if (score > bestScore)
        {
            bestScore = score;
            mPreferredAnimationIndex = static_cast<int>(i);
            bestMappedAnimatedChannels = mappedAnimatedChannels;
        }
    }

    if (mPreferredAnimationIndex < 0 && mScene->mNumAnimations > 0)
    {
        mPreferredAnimationIndex = 0;
        bestScore = 0.0f;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GLTFModel: no positive-scored animation clip found, falling back to index 0");
    }

    if (mMeshes.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GLTFModel: no renderable meshes found in '%s'", filename.data());
        mScene = nullptr;
        return false;
    }

    std::uint32_t totalMeshBones = 0;
    for (unsigned int i = 0; i < mScene->mNumMeshes; ++i)
    {
        if (mScene->mMeshes[i])
        {
            totalMeshBones += mScene->mMeshes[i]->mNumBones;
        }
    }

    SDL_Log("GLTFModel: loaded '%s' with %u meshes, %u bones(mapped), %u mesh-bones(raw), %u animations",
            filename.data(),
            static_cast<unsigned int>(mMeshes.size()),
            static_cast<unsigned int>(mBoneOffsets.size()),
            static_cast<unsigned int>(totalMeshBones),
            static_cast<unsigned int>(mScene->mNumAnimations));

    return true;
}

void GLTFModel::render(Shader &shader,
                       const glm::mat4 &model,
                       const glm::mat4 &view,
                       const glm::mat4 &projection,
                       float animationTimeSeconds) const
{
    if (!isLoaded())
    {
        return;
    }

    shader.bind();
    shader.setUniform("uView", view);
    shader.setUniform("uProjection", projection);

    std::vector<glm::mat4> transforms = computeBoneTransforms(animationTimeSeconds);
    std::unordered_map<std::string, glm::mat4> animatedNodeTransforms;
    const aiAnimation *activeAnimation = getActiveAnimation();
    if (activeAnimation && mScene && mScene->mRootNode)
    {
        const float animationTime = computeAnimationTimeTicks(activeAnimation, animationTimeSeconds);
        buildNodeTransformMap(animationTime, mScene->mRootNode, glm::mat4(1.0f), activeAnimation, animatedNodeTransforms);
    }
    if (!transforms.empty())
    {
        const std::size_t clampedBoneCount = std::min<std::size_t>(transforms.size(), kMaxShaderBones);
        if (transforms.size() > kMaxShaderBones)
        {
            static bool loggedBoneClampWarning = false;
            if (!loggedBoneClampWarning)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "GLTFModel: clipping %zu bones to shader limit %u; raster skinning may be incomplete",
                            transforms.size(),
                            static_cast<unsigned int>(kMaxShaderBones));
                loggedBoneClampWarning = true;
            }
        }
        shader.setUniform("uBones[0]", transforms.data(), static_cast<unsigned int>(clampedBoneCount));
        shader.setUniform("uBoneCount", static_cast<GLuint>(clampedBoneCount));
    }
    else
    {
        glm::mat4 identity(1.0f);
        shader.setUniform("uBones[0]", &identity, 1u);
        shader.setUniform("uBoneCount", 1u);
    }

    for (const MeshBuffers &mesh : mMeshes)
    {
        glm::mat4 nodeTransform(1.0f);
        if (!mesh.usesSkinning)
        {
            nodeTransform = mesh.nodeTransform;
            const auto it = animatedNodeTransforms.find(mesh.nodeName);
            if (it != animatedNodeTransforms.end())
            {
                nodeTransform = it->second;
            }
        }

        shader.setUniform("uModel", model * nodeTransform);
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
}

void GLTFModel::extractRayTraceTriangles(std::vector<RayTraceTriangle> &outTriangles,
                                         const glm::mat4 &model,
                                         float animationTimeSeconds,
                                         std::size_t maxTriangles) const
{
    if (!isLoaded() || mCpuMeshes.empty() || maxTriangles == 0)
    {
        return;
    }

    const std::vector<glm::mat4> boneTransforms = computeBoneTransforms(animationTimeSeconds);

    for (const CpuMeshData &mesh : mCpuMeshes)
    {
        const glm::mat4 meshModel = model * (mesh.usesSkinning ? glm::mat4(1.0f) : mesh.nodeTransform);
        const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(meshModel)));
        const std::size_t triangleCount = mesh.indices.size() / 3;
        for (std::size_t triIndex = 0; triIndex < triangleCount; ++triIndex)
        {
            if (outTriangles.size() >= maxTriangles)
            {
                return;
            }

            const std::uint32_t i0 = mesh.indices[triIndex * 3 + 0];
            const std::uint32_t i1 = mesh.indices[triIndex * 3 + 1];
            const std::uint32_t i2 = mesh.indices[triIndex * 3 + 2];

            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
            {
                continue;
            }

            const Vertex &v0 = mesh.vertices[i0];
            const Vertex &v1 = mesh.vertices[i1];
            const Vertex &v2 = mesh.vertices[i2];

            const glm::mat4 skin0 = computeSkinMatrix(v0, boneTransforms);
            const glm::mat4 skin1 = computeSkinMatrix(v1, boneTransforms);
            const glm::mat4 skin2 = computeSkinMatrix(v2, boneTransforms);

            const glm::vec3 localPos0 = glm::vec3(skin0 * glm::vec4(v0.position, 1.0f));
            const glm::vec3 localPos1 = glm::vec3(skin1 * glm::vec4(v1.position, 1.0f));
            const glm::vec3 localPos2 = glm::vec3(skin2 * glm::vec4(v2.position, 1.0f));

            glm::vec3 localN0 = glm::vec3(skin0 * glm::vec4(v0.normal, 0.0f));
            glm::vec3 localN1 = glm::vec3(skin1 * glm::vec4(v1.normal, 0.0f));
            glm::vec3 localN2 = glm::vec3(skin2 * glm::vec4(v2.normal, 0.0f));

            if (glm::length(localN0) < 1e-6f || glm::length(localN1) < 1e-6f || glm::length(localN2) < 1e-6f)
            {
                const glm::vec3 edge1 = localPos1 - localPos0;
                const glm::vec3 edge2 = localPos2 - localPos0;
                const glm::vec3 faceN = glm::normalize(glm::cross(edge1, edge2));
                localN0 = faceN;
                localN1 = faceN;
                localN2 = faceN;
            }

            const glm::vec3 worldPos0 = glm::vec3(meshModel * glm::vec4(localPos0, 1.0f));
            const glm::vec3 worldPos1 = glm::vec3(meshModel * glm::vec4(localPos1, 1.0f));
            const glm::vec3 worldPos2 = glm::vec3(meshModel * glm::vec4(localPos2, 1.0f));

            const glm::vec3 worldN0 = glm::normalize(normalMatrix * glm::normalize(localN0));
            const glm::vec3 worldN1 = glm::normalize(normalMatrix * glm::normalize(localN1));
            const glm::vec3 worldN2 = glm::normalize(normalMatrix * glm::normalize(localN2));

            RayTraceTriangle tri{};
            tri.v0 = glm::vec4(worldPos0, 1.0f);
            tri.v1 = glm::vec4(worldPos1, 1.0f);
            tri.v2 = glm::vec4(worldPos2, 1.0f);
            tri.n0 = glm::vec4(worldN0, 0.0f);
            tri.n1 = glm::vec4(worldN1, 0.0f);
            tri.n2 = glm::vec4(worldN2, 0.0f);
            tri.albedoAndMaterial = mesh.rayTraceAlbedoAndMaterial;
            tri.materialParams = mesh.rayTraceMaterialParams;

            outTriangles.push_back(tri);
        }
    }
}

bool GLTFModel::isLoaded() const noexcept
{
    return mScene != nullptr && !mMeshes.empty();
}

std::size_t GLTFModel::getBoneCount() const noexcept
{
    return mBoneOffsets.size();
}

void GLTFModel::VertexBoneData::addBoneData(std::uint32_t boneId, float weight) noexcept
{
    for (std::uint32_t i = 0; i < kMaxBonesPerVertex; ++i)
    {
        if (weights[i] == 0.0f)
        {
            ids[i] = boneId;
            weights[i] = weight;
            return;
        }
    }
}

void GLTFModel::clearGpuBuffers() noexcept
{
    for (MeshBuffers &mesh : mMeshes)
    {
        if (mesh.ebo != 0)
        {
            glDeleteBuffers(1, &mesh.ebo);
            mesh.ebo = 0;
        }
        if (mesh.vbo != 0)
        {
            glDeleteBuffers(1, &mesh.vbo);
            mesh.vbo = 0;
        }
        if (mesh.vao != 0)
        {
            glDeleteVertexArrays(1, &mesh.vao);
            mesh.vao = 0;
        }
    }
    mMeshes.clear();
    mCpuMeshes.clear();
}

void GLTFModel::buildMeshesFromScene(const aiScene *scene)
{
    mMeshes.reserve(scene->mNumMeshes);
    mCpuMeshes.reserve(scene->mNumMeshes);

    std::vector<glm::mat4> meshNodeTransforms(scene->mNumMeshes, glm::mat4(1.0f));
    std::vector<bool> meshTransformAssigned(scene->mNumMeshes, false);
    std::vector<std::string> meshNodeNames(scene->mNumMeshes);
    collectMeshNodeTransforms(scene->mRootNode, glm::mat4(1.0f), meshNodeTransforms, meshTransformAssigned, meshNodeNames);

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const aiMesh *mesh = scene->mMeshes[meshIndex];
        if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
        {
            continue;
        }

        std::vector<Vertex> vertices(mesh->mNumVertices);
        std::vector<VertexBoneData> vertexBones(mesh->mNumVertices);
        std::vector<std::uint32_t> indices;
        indices.reserve(mesh->mNumFaces * 3);

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            Vertex &vertex = vertices[i];

            const aiVector3D &position = mesh->mVertices[i];
            vertex.position = glm::vec3(position.x, position.y, position.z);

            if (mesh->HasNormals())
            {
                const aiVector3D &normal = mesh->mNormals[i];
                vertex.normal = glm::normalize(glm::vec3(normal.x, normal.y, normal.z));
            }

            if (mesh->HasTextureCoords(0))
            {
                const aiVector3D &uv = mesh->mTextureCoords[0][i];
                vertex.texCoord = glm::vec2(uv.x, uv.y);
            }
        }

        const glm::mat4 meshNodeTransform = meshIndex < meshNodeTransforms.size() ? meshNodeTransforms[meshIndex] : glm::mat4(1.0f);
        loadBones(mesh, vertexBones, meshNodeTransform);

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            const float weightSum =
                vertexBones[i].weights[0] +
                vertexBones[i].weights[1] +
                vertexBones[i].weights[2] +
                vertexBones[i].weights[3];

            if (weightSum > 1e-6f)
            {
                const float invWeightSum = 1.0f / weightSum;
                vertexBones[i].weights[0] *= invWeightSum;
                vertexBones[i].weights[1] *= invWeightSum;
                vertexBones[i].weights[2] *= invWeightSum;
                vertexBones[i].weights[3] *= invWeightSum;
            }

            vertices[i].boneIds = glm::ivec4(
                static_cast<int>(vertexBones[i].ids[0]),
                static_cast<int>(vertexBones[i].ids[1]),
                static_cast<int>(vertexBones[i].ids[2]),
                static_cast<int>(vertexBones[i].ids[3]));
            vertices[i].boneWeights = glm::vec4(
                vertexBones[i].weights[0],
                vertexBones[i].weights[1],
                vertexBones[i].weights[2],
                vertexBones[i].weights[3]);
        }

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
        {
            const aiFace &face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != 3)
            {
                continue;
            }

            indices.push_back(face.mIndices[0]);
            indices.push_back(face.mIndices[1]);
            indices.push_back(face.mIndices[2]);
        }

        if (indices.empty())
        {
            continue;
        }

        MeshBuffers gpuMesh{};
        gpuMesh.indexCount = static_cast<GLsizei>(indices.size());
        gpuMesh.nodeTransform = meshNodeTransform;
        gpuMesh.nodeName = meshIndex < meshNodeNames.size() ? meshNodeNames[meshIndex] : std::string{};

        const bool usesSkinning = std::any_of(vertexBones.begin(), vertexBones.end(), [](const VertexBoneData &boneData)
                                              {
                                                  return boneData.weights[0] > 0.0f ||
                                                         boneData.weights[1] > 0.0f ||
                                                         boneData.weights[2] > 0.0f ||
                                                         boneData.weights[3] > 0.0f;
                                              });
        gpuMesh.usesSkinning = usesSkinning;

        glGenVertexArrays(1, &gpuMesh.vao);
        glGenBuffers(1, &gpuMesh.vbo);
        glGenBuffers(1, &gpuMesh.ebo);

        glBindVertexArray(gpuMesh.vao);

        glBindBuffer(GL_ARRAY_BUFFER, gpuMesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                     vertices.data(),
                     GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuMesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                     indices.data(),
                     GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, position)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, texCoord)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, normal)));

        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_INT, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, boneIds)));

        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, boneWeights)));

        glBindVertexArray(0);
        mMeshes.push_back(gpuMesh);

        CpuMeshData cpuMesh{};
        cpuMesh.vertices = vertices;
        cpuMesh.indices = indices;
        cpuMesh.usesSkinning = usesSkinning;
        cpuMesh.nodeTransform = gpuMesh.nodeTransform;
        cpuMesh.nodeName = gpuMesh.nodeName;

        glm::vec4 rayTraceAlbedoAndMaterial(0.8f, 0.82f, 0.9f, 0.0f);
        glm::vec4 rayTraceMaterialParams(0.0f, 1.0f, 0.0f, 0.0f);
        if (scene->HasMaterials() && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            resolveRayTraceMaterial(scene->mMaterials[mesh->mMaterialIndex], rayTraceAlbedoAndMaterial, rayTraceMaterialParams);
        }

        cpuMesh.rayTraceAlbedoAndMaterial = rayTraceAlbedoAndMaterial;
        cpuMesh.rayTraceMaterialParams = rayTraceMaterialParams;
        mCpuMeshes.push_back(std::move(cpuMesh));
    }
}

void GLTFModel::loadBones(const aiMesh *mesh,
                         std::vector<VertexBoneData> &vertexBones,
                         const glm::mat4 &meshNodeTransform)
{
    for (unsigned int i = 0; i < mesh->mNumBones; ++i)
    {
        const aiBone *bone = mesh->mBones[i];
        const std::string boneName = bone->mName.C_Str();

        std::uint32_t boneIndex = 0;
        const auto found = mBoneMapping.find(boneName);
        if (found == mBoneMapping.end())
        {
            boneIndex = static_cast<std::uint32_t>(mBoneOffsets.size());
            mBoneMapping.emplace(boneName, boneIndex);
            const std::string canonicalBoneName = canonicalizeNodeName(boneName);
            if (!canonicalBoneName.empty() && mCanonicalBoneMapping.find(canonicalBoneName) == mCanonicalBoneMapping.end())
            {
                mCanonicalBoneMapping.emplace(canonicalBoneName, boneIndex);
            }
            mBoneOffsets.push_back(toGlm(bone->mOffsetMatrix));
            mBoneMeshNodeTransforms.push_back(meshNodeTransform);
        }
        else
        {
            boneIndex = found->second;
        }

        for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
        {
            const aiVertexWeight &weight = bone->mWeights[weightIndex];
            if (weight.mVertexId < vertexBones.size())
            {
                vertexBones[weight.mVertexId].addBoneData(boneIndex, weight.mWeight);
            }
        }
    }
}

const std::uint32_t *GLTFModel::findBoneIndex(std::string_view nodeName) const
{
    const auto exact = mBoneMapping.find(std::string(nodeName));
    if (exact != mBoneMapping.end())
    {
        return &exact->second;
    }

    const std::string canonical = canonicalizeNodeName(nodeName);
    if (!canonical.empty())
    {
        const auto alias = mCanonicalBoneMapping.find(canonical);
        if (alias != mCanonicalBoneMapping.end())
        {
            return &alias->second;
        }

        if (canonical.size() >= 3)
        {
            for (const auto &entry : mCanonicalBoneMapping)
            {
                const std::string &boneCanonical = entry.first;
                if (boneCanonical.size() < 3)
                {
                    continue;
                }

                if (boneCanonical.find(canonical) != std::string::npos ||
                    canonical.find(boneCanonical) != std::string::npos)
                {
                    return &entry.second;
                }
            }
        }
    }

    return nullptr;
}

std::vector<glm::mat4> GLTFModel::computeBoneTransforms(float timeSeconds) const
{
    if (!mScene || mBoneOffsets.empty())
    {
        return std::vector<glm::mat4>(std::max<std::size_t>(mBoneOffsets.size(), 1), glm::mat4(1.0f));
    }

    if (!mScene->HasAnimations())
    {
        std::vector<glm::mat4> transforms(mBoneOffsets.size(), glm::mat4(1.0f));
        readNodeHierarchy(0.0f, mScene->mRootNode, glm::mat4(1.0f), nullptr, transforms);
        return transforms;
    }

    const aiAnimation *animation = getActiveAnimation();

    if (!animation)
    {
        std::vector<glm::mat4> transforms(mBoneOffsets.size(), glm::mat4(1.0f));
        readNodeHierarchy(0.0f, mScene->mRootNode, glm::mat4(1.0f), nullptr, transforms);
        return transforms;
    }

    auto countMappedChannels = [this](const aiAnimation *anim, bool requireTemporal) -> unsigned int
    {
        if (!anim)
        {
            return 0;
        }

        unsigned int count = 0;
        for (unsigned int i = 0; i < anim->mNumChannels; ++i)
        {
            const aiNodeAnim *channel = anim->mChannels[i];
            if (!channel)
            {
                continue;
            }

            const bool temporal =
                (channel->mNumPositionKeys > 1) ||
                (channel->mNumRotationKeys > 1) ||
                (channel->mNumScalingKeys > 1);
            if (requireTemporal && !temporal)
            {
                continue;
            }

            if (findBoneIndex(channel->mNodeName.C_Str()) != nullptr)
            {
                ++count;
            }
        }

        return count;
    };

    if (countMappedChannels(animation, true) == 0)
    {
        std::vector<const aiAnimation *> mappedPoseAnimations;
        mappedPoseAnimations.reserve(mScene->mNumAnimations);

        for (unsigned int i = 0; i < mScene->mNumAnimations; ++i)
        {
            const aiAnimation *candidate = mScene->mAnimations[i];
            if (countMappedChannels(candidate, false) > 0)
            {
                mappedPoseAnimations.push_back(candidate);
            }
        }

        if (!mappedPoseAnimations.empty())
        {
            if (mappedPoseAnimations.size() > 1)
            {
                const int poseIndex = static_cast<int>(std::floor(timeSeconds * 4.0f)) % static_cast<int>(mappedPoseAnimations.size());
                animation = mappedPoseAnimations[static_cast<std::size_t>(poseIndex)];
            }
            else
            {
                animation = mappedPoseAnimations.front();
            }
        }
    }

    const float animationTime = computeAnimationTimeTicks(animation, timeSeconds);

    std::vector<glm::mat4> transforms(mBoneOffsets.size(), glm::mat4(1.0f));
    readNodeHierarchy(animationTime, mScene->mRootNode, glm::mat4(1.0f), animation, transforms);

    return transforms;
}

const aiAnimation *GLTFModel::getActiveAnimation() const
{
    if (!mScene || !mScene->HasAnimations())
    {
        return nullptr;
    }

    if (mPreferredAnimationIndex >= 0 && mPreferredAnimationIndex < static_cast<int>(mScene->mNumAnimations))
    {
        return mScene->mAnimations[static_cast<unsigned int>(mPreferredAnimationIndex)];
    }

    return mScene->mAnimations[0];
}

float GLTFModel::computeAnimationTimeTicks(const aiAnimation *animation, float timeSeconds) const
{
    if (!animation)
    {
        return 0.0f;
    }

    const float duration = findAnimationDurationTicks(animation);
    float ticksPerSecond = animation->mTicksPerSecond > 0.0 ? static_cast<float>(animation->mTicksPerSecond) : 0.0f;
    if (ticksPerSecond <= 0.0f)
    {
        ticksPerSecond = 25.0f;
    }

    const float timeInTicks = timeSeconds * ticksPerSecond;
    return (duration > 0.0f) ? std::fmod(timeInTicks, duration) : timeInTicks;
}

void GLTFModel::buildNodeTransformMap(float animationTime,
                                      const aiNode *node,
                                      const glm::mat4 &parentTransform,
                                      const aiAnimation *animation,
                                      std::unordered_map<std::string, glm::mat4> &nodeTransforms) const
{
    if (!node)
    {
        return;
    }

    const std::string nodeName = node->mName.C_Str();
    glm::mat4 nodeTransformation = toGlm(node->mTransformation);

    const aiNodeAnim *nodeAnim = findNodeAnim(animation, nodeName);
    if (nodeAnim)
    {
        const glm::vec3 scaling = interpolateScaling(animationTime, nodeAnim);
        const glm::quat rotation = interpolateRotation(animationTime, nodeAnim);
        const glm::vec3 translation = interpolatePosition(animationTime, nodeAnim);

        const glm::mat4 scalingM = glm::scale(glm::mat4(1.0f), scaling);
        const glm::mat4 rotationM = glm::mat4_cast(rotation);
        const glm::mat4 translationM = glm::translate(glm::mat4(1.0f), translation);

        nodeTransformation = translationM * rotationM * scalingM;
    }

    const glm::mat4 globalTransformation = parentTransform * nodeTransformation;
    nodeTransforms[nodeName] = globalTransformation;

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        buildNodeTransformMap(animationTime, node->mChildren[i], globalTransformation, animation, nodeTransforms);
    }
}

void GLTFModel::readNodeHierarchy(float animationTime,
                                  const aiNode *node,
                                  const glm::mat4 &parentTransform,
                                  const aiAnimation *animation,
                                  std::vector<glm::mat4> &transforms) const
{
    if (!node)
    {
        return;
    }

    const std::string nodeName = node->mName.C_Str();
    glm::mat4 nodeTransformation = toGlm(node->mTransformation);

    const aiNodeAnim *nodeAnim = findNodeAnim(animation, nodeName);
    if (nodeAnim)
    {
        const glm::vec3 scaling = interpolateScaling(animationTime, nodeAnim);
        const glm::quat rotation = interpolateRotation(animationTime, nodeAnim);
        const glm::vec3 translation = interpolatePosition(animationTime, nodeAnim);

        const glm::mat4 scalingM = glm::scale(glm::mat4(1.0f), scaling);
        const glm::mat4 rotationM = glm::mat4_cast(rotation);
        const glm::mat4 translationM = glm::translate(glm::mat4(1.0f), translation);

        nodeTransformation = translationM * rotationM * scalingM;
    }

    const glm::mat4 globalTransformation = parentTransform * nodeTransformation;

    const std::uint32_t *boneIndexPtr = findBoneIndex(nodeName);
    if (boneIndexPtr)
    {
        const std::uint32_t boneIndex = *boneIndexPtr;
        if (boneIndex < transforms.size() && boneIndex < mBoneOffsets.size())
        {
            transforms[boneIndex] = mGlobalInverseTransform * globalTransformation * mBoneOffsets[boneIndex];
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        readNodeHierarchy(animationTime, node->mChildren[i], globalTransformation, animation, transforms);
    }
}

const aiNodeAnim *GLTFModel::findNodeAnim(const aiAnimation *animation, std::string_view nodeName) const
{
    if (!animation)
    {
        return nullptr;
    }

    for (unsigned int i = 0; i < animation->mNumChannels; ++i)
    {
        const aiNodeAnim *nodeAnim = animation->mChannels[i];
        if (nodeAnim && nodeName == nodeAnim->mNodeName.C_Str())
        {
            return nodeAnim;
        }
    }

    return nullptr;
}

std::uint32_t GLTFModel::findScaling(float animationTime, const aiNodeAnim *nodeAnim) const
{
    for (std::uint32_t i = 0; i + 1 < nodeAnim->mNumScalingKeys; ++i)
    {
        if (animationTime < static_cast<float>(nodeAnim->mScalingKeys[i + 1].mTime))
        {
            return i;
        }
    }

    return nodeAnim->mNumScalingKeys > 1 ? nodeAnim->mNumScalingKeys - 2 : 0;
}

std::uint32_t GLTFModel::findRotation(float animationTime, const aiNodeAnim *nodeAnim) const
{
    for (std::uint32_t i = 0; i + 1 < nodeAnim->mNumRotationKeys; ++i)
    {
        if (animationTime < static_cast<float>(nodeAnim->mRotationKeys[i + 1].mTime))
        {
            return i;
        }
    }

    return nodeAnim->mNumRotationKeys > 1 ? nodeAnim->mNumRotationKeys - 2 : 0;
}

std::uint32_t GLTFModel::findPosition(float animationTime, const aiNodeAnim *nodeAnim) const
{
    for (std::uint32_t i = 0; i + 1 < nodeAnim->mNumPositionKeys; ++i)
    {
        if (animationTime < static_cast<float>(nodeAnim->mPositionKeys[i + 1].mTime))
        {
            return i;
        }
    }

    return nodeAnim->mNumPositionKeys > 1 ? nodeAnim->mNumPositionKeys - 2 : 0;
}

glm::vec3 GLTFModel::interpolateScaling(float animationTime, const aiNodeAnim *nodeAnim) const
{
    if (nodeAnim->mNumScalingKeys == 1)
    {
        const aiVector3D v = nodeAnim->mScalingKeys[0].mValue;
        return glm::vec3(v.x, v.y, v.z);
    }

    const std::uint32_t index = findScaling(animationTime, nodeAnim);
    const std::uint32_t nextIndex = index + 1;

    const aiVector3D &start = nodeAnim->mScalingKeys[index].mValue;
    const aiVector3D &end = nodeAnim->mScalingKeys[nextIndex].mValue;

    const float factor = normalizedKeyLerp(
        animationTime,
        static_cast<float>(nodeAnim->mScalingKeys[index].mTime),
        static_cast<float>(nodeAnim->mScalingKeys[nextIndex].mTime));

    const aiVector3D delta = end - start;
    const aiVector3D out = start + factor * delta;
    return glm::vec3(out.x, out.y, out.z);
}

glm::quat GLTFModel::interpolateRotation(float animationTime, const aiNodeAnim *nodeAnim) const
{
    if (nodeAnim->mNumRotationKeys == 1)
    {
        const aiQuaternion &q = nodeAnim->mRotationKeys[0].mValue;
        return glm::normalize(glm::quat(q.w, q.x, q.y, q.z));
    }

    const std::uint32_t index = findRotation(animationTime, nodeAnim);
    const std::uint32_t nextIndex = index + 1;

    const aiQuaternion &start = nodeAnim->mRotationKeys[index].mValue;
    const aiQuaternion &end = nodeAnim->mRotationKeys[nextIndex].mValue;

    const float factor = normalizedKeyLerp(
        animationTime,
        static_cast<float>(nodeAnim->mRotationKeys[index].mTime),
        static_cast<float>(nodeAnim->mRotationKeys[nextIndex].mTime));

    aiQuaternion out;
    aiQuaternion::Interpolate(out, start, end, factor);
    out.Normalize();

    return glm::normalize(glm::quat(out.w, out.x, out.y, out.z));
}

glm::vec3 GLTFModel::interpolatePosition(float animationTime, const aiNodeAnim *nodeAnim) const
{
    if (nodeAnim->mNumPositionKeys == 1)
    {
        const aiVector3D v = nodeAnim->mPositionKeys[0].mValue;
        return glm::vec3(v.x, v.y, v.z);
    }

    const std::uint32_t index = findPosition(animationTime, nodeAnim);
    const std::uint32_t nextIndex = index + 1;

    const aiVector3D &start = nodeAnim->mPositionKeys[index].mValue;
    const aiVector3D &end = nodeAnim->mPositionKeys[nextIndex].mValue;

    const float factor = normalizedKeyLerp(
        animationTime,
        static_cast<float>(nodeAnim->mPositionKeys[index].mTime),
        static_cast<float>(nodeAnim->mPositionKeys[nextIndex].mTime));

    const aiVector3D delta = end - start;
    const aiVector3D out = start + factor * delta;
    return glm::vec3(out.x, out.y, out.z);
}

glm::mat4 GLTFModel::computeSkinMatrix(const Vertex &vertex, const std::vector<glm::mat4> &boneTransforms) const
{
    glm::mat4 skin(0.0f);
    float totalWeight = 0.0f;

    for (int i = 0; i < 4; ++i)
    {
        const int boneId = vertex.boneIds[i];
        const float weight = vertex.boneWeights[i];

        if (weight <= 0.0f)
        {
            continue;
        }

        if (boneId >= 0 && static_cast<std::size_t>(boneId) < boneTransforms.size())
        {
            skin += boneTransforms[static_cast<std::size_t>(boneId)] * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= 1e-6f)
    {
        return glm::mat4(1.0f);
    }

    skin *= (1.0f / totalWeight);

    return skin;
}

glm::mat4 GLTFModel::toGlm(const aiMatrix4x4 &matrix)
{
    glm::mat4 out;
    out[0][0] = matrix.a1;
    out[1][0] = matrix.a2;
    out[2][0] = matrix.a3;
    out[3][0] = matrix.a4;
    out[0][1] = matrix.b1;
    out[1][1] = matrix.b2;
    out[2][1] = matrix.b3;
    out[3][1] = matrix.b4;
    out[0][2] = matrix.c1;
    out[1][2] = matrix.c2;
    out[2][2] = matrix.c3;
    out[3][2] = matrix.c4;
    out[0][3] = matrix.d1;
    out[1][3] = matrix.d2;
    out[2][3] = matrix.d3;
    out[3][3] = matrix.d4;
    return out;
}
