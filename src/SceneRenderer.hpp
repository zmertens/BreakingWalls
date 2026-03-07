#ifndef SCENE_RENDERER_HPP
#define SCENE_RENDERER_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>

class PathTraceScene;
class Shader;

/// Fragment-shader-based path tracing renderer ported from GLSL-PathTracer.
/// Manages GPU data buffers, FBOs, tile-based progressive rendering, and
/// tonemapping.  Designed to run alongside the existing compute pipeline
/// as an alternative render mode.
class SceneRenderer
{
public:
    explicit SceneRenderer(PathTraceScene *scene);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer &) = delete;
    SceneRenderer &operator=(const SceneRenderer &) = delete;

    /// Assign pre-compiled shaders (owned by ShaderManager)
    void setShaders(Shader *tile, Shader *preview, Shader *output, Shader *tonemap);

    /// Recompile tile/preview/tonemap shaders with scene-specific #defines.
    /// Call after setShaders() and before initShaderUniforms().
    void rebuildShadersWithDefines();

    /// Upload scene geometry/material data to GPU texture buffers.
    /// Call after PathTraceScene::processScene().
    void initGPUData();

    /// Create / resize FBOs and render textures to match current resolution.
    void initFBOs();

    /// One-time initialisation after shaders and GPU buffers are ready.
    void initShaderUniforms();

    /// Render one frame (tile-based path trace into accumulation buffer,
    /// then tonemap into double-buffered output).
    void render();

    /// Present the current output to the default framebuffer.
    void present();

    /// Advance tile state and update per-frame uniforms.
    /// Call once per frame *after* render().
    void update(float secondsElapsed);

    /// Mark scene as dirty (camera moved, geometry changed) – triggers
    /// accumulation reset and low-res preview on next frame.
    void markDirty();

    /// Free all GPU resources.
    void cleanup();

    // ── Accessors ───────────────────────────────────────────────────
    int  getSampleCount() const { return sampleCounter; }
    bool isInitialized() const { return initialized; }

private:
    PathTraceScene *scene{nullptr};

    // ── Fullscreen quad (attribute-based for simple.vert.glsl) ──────
    GLuint quadVAO{0};
    GLuint quadVBO{0};
    void createQuad();

    // ── GPU texture buffers (scene data) ────────────────────────────
    GLuint vertexIndicesBuffer{0};
    GLuint vertexIndicesTex{0};
    GLuint verticesBuffer{0};
    GLuint verticesTex{0};
    GLuint normalsBuffer{0};
    GLuint normalsTex{0};
    GLuint materialsTex{0};
    GLuint transformsTex{0};
    GLuint lightsTex{0};
    GLuint textureMapsArrayTex{0};
    GLuint envMapTex{0};
    GLuint envMapCDFTex{0};
    GLuint meshInstanceInfoTex{0};

    // ── FBOs ────────────────────────────────────────────────────────
    GLuint pathTraceFBO{0};
    GLuint pathTraceFBOLowRes{0};
    GLuint accumFBO{0};
    GLuint outputFBO{0};

    // ── Render textures ─────────────────────────────────────────────
    GLuint pathTraceTexture{0};
    GLuint pathTraceTextureLowRes{0};
    GLuint accumTexture{0};
    GLuint tileOutputTexture[2]{0, 0};

    // ── Shaders (non-owning, managed by ShaderManager) ──────────────
    Shader *tileShader{nullptr};
    Shader *previewShader{nullptr};
    Shader *outputShader{nullptr};
    Shader *tonemapShader{nullptr};

    // ── Tile-based rendering state ──────────────────────────────────
    glm::ivec2 renderSize{1280, 720};
    glm::ivec2 windowSize{1280, 720};
    glm::ivec2 tile{-1, 0};
    glm::ivec2 numTiles{1, 1};
    glm::vec2 invNumTiles{1.0f};
    int tileWidth{100};
    int tileHeight{100};
    int currentBuffer{0};
    int frameCounter{1};
    int sampleCounter{1};
    float pixelRatio{0.25f};

    bool initialized{false};

    // ── Helpers ─────────────────────────────────────────────────────
    void drawQuad(Shader *shader);
};

#endif // SCENE_RENDERER_HPP
