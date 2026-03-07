#ifndef PATH_TRACE_SCENE_HPP
#define PATH_TRACE_SCENE_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

/// Disney BRDF material packed as 8 x vec4 for GPU upload (matches GLSLPT layout)
struct PTMaterial
{
    glm::vec3 baseColor{1.0f};
    float anisotropic{0.0f};

    glm::vec3 emission{0.0f};
    float padding1{0.0f};

    float metallic{0.0f};
    float roughness{0.5f};
    float subsurface{0.0f};
    float specularTint{0.0f};

    float sheen{0.0f};
    float sheenTint{0.0f};
    float clearcoat{0.0f};
    float clearcoatGloss{0.0f};

    float specTrans{0.0f};
    float ior{1.5f};
    float mediumType{0.0f};
    float mediumDensity{0.0f};

    glm::vec3 mediumColor{1.0f};
    float mediumAnisotropy{0.0f};

    float baseColorTexId{-1.0f};
    float metallicRoughnessTexID{-1.0f};
    float normalmapTexID{-1.0f};
    float emissionmapTexID{-1.0f};

    float opacity{1.0f};
    float alphaMode{0.0f};
    float alphaCutoff{0.0f};
    float padding2{0.0f};
};

/// Triangle index triplet
struct PTIndices
{
    int x, y, z;
};

/// Mesh instance referencing a mesh and material with a transform
struct PTMeshInstance
{
    std::string name;
    int meshID{0};
    int materialID{0};
    glm::mat4 transform{1.0f};
};

/// Light (rect, sphere, or distant)
struct PTLight
{
    glm::vec3 position{0.0f};
    glm::vec3 emission{0.0f};
    glm::vec3 u{0.0f};
    glm::vec3 v{0.0f};
    float radius{0.0f};
    float area{0.0f};
    float type{0.0f}; // 0=quad, 1=sphere, 2=distant
};

/// Per-instance info for brute-force ray traversal (no BVH)
struct PTMeshInstanceInfo
{
    float triStart;
    float triCount;
    float materialID;
    float transformIndex;
};

/// Camera state for the path tracer shaders
struct PTCamera
{
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float fov{45.0f};
    float focalDist{1.0f};
    float aperture{0.0f};
    bool isMoving{false};
};

/// Render options controlling the path tracer behavior
struct PTRenderOptions
{
    glm::ivec2 renderResolution{1280, 720};
    glm::ivec2 windowResolution{1280, 720};
    glm::vec3 uniformLightCol{0.3f};
    glm::vec3 backgroundCol{1.0f};
    int tileWidth{100};
    int tileHeight{100};
    int maxDepth{2};
    int maxSpp{-1};
    int RRDepth{2};
    int texArrayWidth{2048};
    int texArrayHeight{2048};
    bool enableRR{true};
    bool enableTonemap{true};
    bool enableAces{false};
    bool simpleAcesFit{false};
    bool enableEnvMap{false};
    bool enableUniformLight{false};
    bool hideEmitters{false};
    bool enableBackground{false};
    bool transparentBackground{false};
    bool enableRoughnessMollification{false};
    bool enableVolumeMIS{false};
    float envMapIntensity{1.0f};
    float envMapRot{0.0f};
    float roughnessMollificationAmt{0.0f};
};

/// Per-mesh vertex data (stored separately, flattened into scene arrays by ProcessScene)
struct PTMesh
{
    std::vector<glm::vec4> verticesUVX; // xyz=position, w=u
    std::vector<glm::vec4> normalsUVY;  // xyz=normal,   w=v
    std::string name;
};

/// HDR environment map data
struct PTEnvMap
{
    int width{0};
    int height{0};
    float totalSum{0.0f};
    std::vector<float> img; // RGB32F interleaved
    std::vector<float> cdf; // R32F CDF for importance sampling
};

/// Complete path trace scene – owns all geometry, materials, lights, env map
class PathTraceScene
{
public:
    PathTraceScene() = default;
    ~PathTraceScene() = default;

    // ── Scene building ──────────────────────────────────────────────
    int addMesh(PTMesh mesh);
    int addMaterial(const PTMaterial &material);
    int addMeshInstance(const PTMeshInstance &instance);
    int addLight(const PTLight &light);
    void setCamera(const PTCamera &cam);
    void setEnvMap(PTEnvMap map);

    /// Flatten per-mesh data into scene-wide arrays for GPU upload.
    /// Must be called after all meshes/instances/materials are added.
    void processScene();

    // ── Accessors ───────────────────────────────────────────────────
    const std::vector<PTIndices> &getVertIndices() const { return vertIndices; }
    const std::vector<glm::vec4> &getVerticesUVX() const { return verticesUVX; }
    const std::vector<glm::vec4> &getNormalsUVY() const { return normalsUVY; }
    const std::vector<glm::mat4> &getTransforms() const { return transforms; }
    const std::vector<PTMaterial> &getMaterials() const { return materials; }
    const std::vector<PTMeshInstance> &getMeshInstances() const { return meshInstances; }
    const std::vector<PTLight> &getLights() const { return lights; }
    const std::vector<PTMeshInstanceInfo> &getMeshInstanceInfos() const { return meshInstanceInfos; }
    const PTCamera &getCamera() const { return camera; }
    PTCamera &getCamera() { return camera; }
    const PTRenderOptions &getRenderOptions() const { return renderOptions; }
    PTRenderOptions &getRenderOptions() { return renderOptions; }
    bool hasEnvMap() const { return envMap.width > 0 && envMap.height > 0; }
    const PTEnvMap &getEnvMap() const { return envMap; }

    bool dirty{true};
    bool initialized{false};
    bool instancesModified{false};
    bool envMapModified{false};

private:
    // Per-mesh storage
    std::vector<PTMesh> meshes;

    // Scene-wide flattened data (filled by processScene)
    std::vector<PTIndices> vertIndices;
    std::vector<glm::vec4> verticesUVX;
    std::vector<glm::vec4> normalsUVY;
    std::vector<glm::mat4> transforms;

    // Materials, instances, lights
    std::vector<PTMaterial> materials;
    std::vector<PTMeshInstance> meshInstances;
    std::vector<PTMeshInstanceInfo> meshInstanceInfos;
    std::vector<PTLight> lights;

    // Camera & render options
    PTCamera camera;
    PTRenderOptions renderOptions;

    // Environment map
    PTEnvMap envMap;
};

#endif // PATH_TRACE_SCENE_HPP
