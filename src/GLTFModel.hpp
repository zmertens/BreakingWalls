#ifndef GLTF_MODEL_HPP
#define GLTF_MODEL_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <assimp/matrix4x4.h>

namespace Assimp
{
    class Importer;
}

struct aiAnimation;
struct aiMesh;
struct aiNode;
struct aiNodeAnim;
struct aiScene;

class Shader;

class GLTFModel
{
public:
    struct RayTraceTriangle
    {
        glm::vec4 v0{0.0f};
        glm::vec4 v1{0.0f};
        glm::vec4 v2{0.0f};
        glm::vec4 n0{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 n1{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 n2{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 albedoAndMaterial{0.8f, 0.82f, 0.9f, 0.0f};
        glm::vec4 materialParams{0.0f, 1.0f, 0.0f, 0.0f};
    };

    explicit GLTFModel();
    ~GLTFModel();

    GLTFModel(const GLTFModel &) = delete;
    GLTFModel &operator=(const GLTFModel &) = delete;

    GLTFModel(GLTFModel &&) = delete;
    GLTFModel &operator=(GLTFModel &&) = delete;

    bool readFile(std::string_view filename);
    void render(Shader &shader,
                const glm::mat4 &model,
                const glm::mat4 &view,
                const glm::mat4 &projection,
                float animationTimeSeconds) const;
    void extractRayTraceTriangles(std::vector<RayTraceTriangle> &outTriangles,
                                  const glm::mat4 &model,
                                  float animationTimeSeconds,
                                  std::size_t maxTriangles) const;

    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] std::size_t getBoneCount() const noexcept;

    [[nodiscard]] std::vector<std::string> getAnimationNames() const;
    [[nodiscard]] std::string getActiveAnimationName() const;
    [[nodiscard]] std::size_t getAnimationCount() const noexcept;
    bool setPreferredAnimationIndex(int animationIndex) noexcept;

    [[nodiscard]] std::vector<std::string> getMeshes() const;
    [[nodiscard]] std::size_t getTotalMeshBones() const noexcept;

private:
    static constexpr std::uint32_t kMaxBonesPerVertex = 4;
    static constexpr std::uint32_t kMaxShaderBones = 200;

    struct Vertex
    {
        glm::vec3 position{0.0f};
        glm::vec2 texCoord{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::ivec4 boneIds{0, 0, 0, 0};
        glm::vec4 boneWeights{0.0f};
    };

    struct MeshBuffers
    {
        GLuint vao{0};
        GLuint vbo{0};
        GLuint ebo{0};
        GLsizei indexCount{0};
        bool usesSkinning{false};
        glm::mat4 nodeTransform{1.0f};
        std::string nodeName;
    };

    struct CpuMeshData
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        bool usesSkinning{false};
        glm::mat4 nodeTransform{1.0f};
        std::string nodeName;
        glm::vec4 rayTraceAlbedoAndMaterial{0.8f, 0.82f, 0.9f, 0.0f};
        glm::vec4 rayTraceMaterialParams{0.0f, 1.0f, 0.0f, 0.0f};
    };

    struct VertexBoneData
    {
        std::uint32_t ids[kMaxBonesPerVertex]{0, 0, 0, 0};
        float weights[kMaxBonesPerVertex]{0.0f, 0.0f, 0.0f, 0.0f};

        void addBoneData(std::uint32_t boneId, float weight) noexcept;
    };

    void clearGpuBuffers() noexcept;
    void buildMeshesFromScene(const aiScene *scene);
    void loadBones(const struct aiMesh *mesh,
                   std::vector<VertexBoneData> &vertexBones,
                   const glm::mat4 &meshNodeTransform);

    [[nodiscard]] std::vector<glm::mat4> computeBoneTransforms(float timeSeconds) const;
    [[nodiscard]] const aiAnimation *getActiveAnimation() const;
    [[nodiscard]] float computeAnimationTimeTicks(const aiAnimation *animation, float timeSeconds) const;
    void buildNodeTransformMap(float animationTime,
                               const aiNode *node,
                               const glm::mat4 &parentTransform,
                               const aiAnimation *animation,
                               std::unordered_map<std::string, glm::mat4> &nodeTransforms) const;
    void readNodeHierarchy(float animationTime,
                           const aiNode *node,
                           const glm::mat4 &parentTransform,
                           const aiAnimation *animation,
                           std::vector<glm::mat4> &transforms) const;

    [[nodiscard]] const aiNodeAnim *findNodeAnim(const aiAnimation *animation, std::string_view nodeName) const;
    [[nodiscard]] const std::uint32_t *findBoneIndex(std::string_view nodeName) const;
    [[nodiscard]] std::uint32_t findScaling(float animationTime, const aiNodeAnim *nodeAnim) const;
    [[nodiscard]] std::uint32_t findRotation(float animationTime, const aiNodeAnim *nodeAnim) const;
    [[nodiscard]] std::uint32_t findPosition(float animationTime, const aiNodeAnim *nodeAnim) const;

    [[nodiscard]] glm::vec3 interpolateScaling(float animationTime, const aiNodeAnim *nodeAnim) const;
    [[nodiscard]] glm::quat interpolateRotation(float animationTime, const aiNodeAnim *nodeAnim) const;
    [[nodiscard]] glm::vec3 interpolatePosition(float animationTime, const aiNodeAnim *nodeAnim) const;
    [[nodiscard]] glm::mat4 computeSkinMatrix(const Vertex &vertex, const std::vector<glm::mat4> &boneTransforms) const;

    [[nodiscard]] static glm::mat4 toGlm(const aiMatrix4x4 &matrix);

private:
    std::unique_ptr<Assimp::Importer> mImporter;
    const aiScene *mScene{nullptr};
    glm::mat4 mGlobalInverseTransform{1.0f};

    std::vector<MeshBuffers> mMeshes;
    std::vector<CpuMeshData> mCpuMeshes;
    std::unordered_map<std::string, std::uint32_t> mBoneMapping;
    std::unordered_map<std::string, std::uint32_t> mCanonicalBoneMapping;
    std::vector<glm::mat4> mBoneOffsets;
    std::vector<glm::mat4> mBoneMeshNodeTransforms;
    int mPreferredAnimationIndex{-1};
};

#endif // GLTF_MODEL_HPP
