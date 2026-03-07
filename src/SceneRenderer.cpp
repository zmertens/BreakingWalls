#include "SceneRenderer.hpp"
#include "PathTraceScene.hpp"
#include "Shader.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// ────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ────────────────────────────────────────────────────────────────────────────

SceneRenderer::SceneRenderer(PathTraceScene *scene)
    : scene(scene)
{
    if (!scene)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SceneRenderer: null scene");
        return;
    }

    if (!scene->initialized)
        scene->processScene();

    createQuad();
    initGPUData();
    initFBOs();
}

SceneRenderer::~SceneRenderer()
{
    cleanup();
}

// ────────────────────────────────────────────────────────────────────────────
// Quad (attribute-based, matched to simple.vert.glsl)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::createQuad()
{
    // 6 vertices, 4 floats each (pos.xy, uv.st) – two triangles
    float vertices[] =
    {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // layout(location=0) in vec2 position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    // layout(location=1) in vec2 texCoords
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(2 * sizeof(float)));

    glBindVertexArray(0);
}

void SceneRenderer::drawQuad(Shader *shader)
{
    if (!shader)
        return;
    shader->bind();
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ────────────────────────────────────────────────────────────────────────────
// Shader assignment
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::setShaders(Shader *tile, Shader *preview, Shader *output, Shader *tonemap)
{
    tileShader    = tile;
    previewShader = preview;
    outputShader  = output;
    tonemapShader = tonemap;
}

// ────────────────────────────────────────────────────────────────────────────
// GPU data buffers (ported from GLSLPT Renderer::InitGPUDataBuffers)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::initGPUData()
{
    if (!scene || !scene->initialized)
        return;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    const auto &vi  = scene->getVertIndices();
    const auto &vtx = scene->getVerticesUVX();
    const auto &nrm = scene->getNormalsUVY();
    const auto &mat = scene->getMaterials();
    const auto &xfm = scene->getTransforms();
    const auto &lts = scene->getLights();

    // ── Vertex indices → GL_TEXTURE_BUFFER / RGB32I ─────────────────
    if (!vi.empty())
    {
        glGenBuffers(1, &vertexIndicesBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, vertexIndicesBuffer);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(PTIndices) * vi.size()),
                     vi.data(), GL_STATIC_DRAW);
        glGenTextures(1, &vertexIndicesTex);
        glBindTexture(GL_TEXTURE_BUFFER, vertexIndicesTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32I, vertexIndicesBuffer);
    }

    // ── Vertices → GL_TEXTURE_BUFFER / RGBA32F ─────────────────────
    if (!vtx.empty())
    {
        glGenBuffers(1, &verticesBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, verticesBuffer);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(glm::vec4) * vtx.size()),
                     vtx.data(), GL_STATIC_DRAW);
        glGenTextures(1, &verticesTex);
        glBindTexture(GL_TEXTURE_BUFFER, verticesTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, verticesBuffer);
    }

    // ── Normals → GL_TEXTURE_BUFFER / RGBA32F ──────────────────────
    if (!nrm.empty())
    {
        glGenBuffers(1, &normalsBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, normalsBuffer);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(glm::vec4) * nrm.size()),
                     nrm.data(), GL_STATIC_DRAW);
        glGenTextures(1, &normalsTex);
        glBindTexture(GL_TEXTURE_BUFFER, normalsTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, normalsBuffer);
    }

    // ── Materials → GL_TEXTURE_2D / RGBA32F (packed) ────────────────
    if (!mat.empty())
    {
        glGenTextures(1, &materialsTex);
        glBindTexture(GL_TEXTURE_2D, materialsTex);
        const int matTexWidth = static_cast<int>((sizeof(PTMaterial) / sizeof(glm::vec4)) * mat.size());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, matTexWidth, 1, 0,
                     GL_RGBA, GL_FLOAT, mat.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── Transforms → GL_TEXTURE_2D / RGBA32F ────────────────────────
    if (!xfm.empty())
    {
        glGenTextures(1, &transformsTex);
        glBindTexture(GL_TEXTURE_2D, transformsTex);
        const int xfmTexWidth = static_cast<int>((sizeof(glm::mat4) / sizeof(glm::vec4)) * xfm.size());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, xfmTexWidth, 1, 0,
                     GL_RGBA, GL_FLOAT, xfm.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── Lights → GL_TEXTURE_2D / RGB32F ─────────────────────────────
    if (!lts.empty())
    {
        glGenTextures(1, &lightsTex);
        glBindTexture(GL_TEXTURE_2D, lightsTex);
        const int lgtTexWidth = static_cast<int>((sizeof(PTLight) / sizeof(glm::vec3)) * lts.size());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, lgtTexWidth, 1, 0,
                     GL_RGB, GL_FLOAT, lts.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── Mesh instance info → GL_TEXTURE_2D / RGBA32F (for brute-force) ──
    const auto &mii = scene->getMeshInstanceInfos();
    if (!mii.empty())
    {
        glGenTextures(1, &meshInstanceInfoTex);
        glBindTexture(GL_TEXTURE_2D, meshInstanceInfoTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, static_cast<int>(mii.size()), 1, 0,
                     GL_RGBA, GL_FLOAT, mii.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── Environment map ─────────────────────────────────────────────
    if (scene->hasEnvMap())
    {
        const auto &env = scene->getEnvMap();
        glGenTextures(1, &envMapTex);
        glBindTexture(GL_TEXTURE_2D, envMapTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, env.width, env.height, 0,
                     GL_RGB, GL_FLOAT, env.img.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!env.cdf.empty())
        {
            glGenTextures(1, &envMapCDFTex);
            glBindTexture(GL_TEXTURE_2D, envMapCDFTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, env.width, env.height, 0,
                         GL_RED, GL_FLOAT, env.cdf.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // ── Bind scene textures to fixed texture units 1..10 ────────────
    // (These remain bound for the lifetime of this renderer)
    // Slot 0 is reserved for the accumulation texture (bound per-draw).
    // BVH at slot 1 is skipped for iteration 1 (no BVH).
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, vertexIndicesTex);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_BUFFER, verticesTex);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, normalsTex);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, materialsTex);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, transformsTex);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, lightsTex);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureMapsArrayTex);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, envMapTex);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, envMapCDFTex);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, meshInstanceInfoTex);

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                "SceneRenderer: GPU data uploaded  vi=%zu vtx=%zu mat=%zu xfm=%zu lgt=%zu",
                vi.size(), vtx.size(), mat.size(), xfm.size(), lts.size());
}

// ────────────────────────────────────────────────────────────────────────────
// FBOs (ported from GLSLPT Renderer::InitFBOs)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::initFBOs()
{
    if (!scene)
        return;

    // Reset to unit 0 so the texture binds below don't clobber whatever unit
    // initGPUData() left active (typically GL_TEXTURE11 = meshInstanceInfoTex).
    glActiveTexture(GL_TEXTURE0);

    const auto &opts = scene->getRenderOptions();

    sampleCounter = 1;
    currentBuffer = 0;
    frameCounter  = 1;

    renderSize = opts.renderResolution;
    windowSize = opts.windowResolution;
    tileWidth  = opts.tileWidth;
    tileHeight = opts.tileHeight;

    invNumTiles.x = static_cast<float>(tileWidth) / static_cast<float>(renderSize.x);
    invNumTiles.y = static_cast<float>(tileHeight) / static_cast<float>(renderSize.y);

    numTiles.x = static_cast<int>(std::ceil(static_cast<float>(renderSize.x) / tileWidth));
    numTiles.y = static_cast<int>(std::ceil(static_cast<float>(renderSize.y) / tileHeight));

    tile.x = -1;
    tile.y = numTiles.y - 1;

    // ── Path trace FBO (tile size) ──────────────────────────────────
    glGenFramebuffers(1, &pathTraceFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, pathTraceFBO);

    glGenTextures(1, &pathTraceTexture);
    glBindTexture(GL_TEXTURE_2D, pathTraceTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tileWidth, tileHeight, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, pathTraceTexture, 0);

    // ── Low-res preview FBO ─────────────────────────────────────────
    const int previewW = static_cast<int>(windowSize.x * pixelRatio);
    const int previewH = static_cast<int>(windowSize.y * pixelRatio);

    glGenFramebuffers(1, &pathTraceFBOLowRes);
    glBindFramebuffer(GL_FRAMEBUFFER, pathTraceFBOLowRes);

    glGenTextures(1, &pathTraceTextureLowRes);
    glBindTexture(GL_TEXTURE_2D, pathTraceTextureLowRes);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, previewW, previewH, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, pathTraceTextureLowRes, 0);

    // ── Accumulation FBO (full render size) ─────────────────────────
    glGenFramebuffers(1, &accumFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);

    glGenTextures(1, &accumTexture);
    glBindTexture(GL_TEXTURE_2D, accumTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, renderSize.x, renderSize.y, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, accumTexture, 0);

    // ── Output FBO (double-buffered, full render size) ──────────────
    glGenFramebuffers(1, &outputFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);

    for (int i = 0; i < 2; ++i)
    {
        glGenTextures(1, &tileOutputTexture[i]);
        glBindTexture(GL_TEXTURE_2D, tileOutputTexture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, renderSize.x, renderSize.y, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tileOutputTexture[currentBuffer], 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    initialized = true;

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                "SceneRenderer: FBOs created  render=%dx%d  tile=%dx%d  preview=%dx%d  numTiles=%dx%d",
                renderSize.x, renderSize.y, tileWidth, tileHeight, previewW, previewH,
                numTiles.x, numTiles.y);
}

// ────────────────────────────────────────────────────────────────────────────
// Shader uniform initialisation (static uniforms set once)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::initShaderUniforms()
{
    if (!tileShader || !previewShader || !outputShader || !tonemapShader)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "SceneRenderer: cannot init uniforms – shaders not set");
        return;
    }

    const auto &opts = scene->getRenderOptions();

    const bool hasEnvMap = scene->hasEnvMap() && opts.enableEnvMap;
    const bool hasBVH = false; // Iteration 1: brute-force only
    const bool hasBackground = opts.enableBackground || opts.transparentBackground;

    // ── Tile shader ─────────────────────────────────────────────────
    tileShader->bind();
    tileShader->setUniform("resolution", glm::vec2(renderSize));
    tileShader->setUniform("invNumTiles", invNumTiles);
    tileShader->setUniform("numOfLights", static_cast<int>(scene->getLights().size()));
    tileShader->setUniform("accumTexture", 0);
    tileShader->setUniform("vertexIndicesTex", 2);
    tileShader->setUniform("verticesTex", 3);
    tileShader->setUniform("normalsTex", 4);
    tileShader->setUniform("materialsTex", 5);
    tileShader->setUniform("transformsTex", 6);
    tileShader->setUniform("lightsTex", 7);
    tileShader->setUniform("textureMapsArrayTex", 8);
    if (hasBVH)
    {
        tileShader->setUniform("topBVHIndex", 0);
        tileShader->setUniform("BVH", 1);
    }
    else
    {
        tileShader->setUniform("numMeshInstances", static_cast<int>(scene->getMeshInstanceInfos().size()));
        tileShader->setUniform("meshInstanceInfoTex", 11);
    }
    if (hasEnvMap)
    {
        const auto &env = scene->getEnvMap();
        tileShader->setUniform("envMapTex", 9);
        tileShader->setUniform("envMapCDFTex", 10);
        tileShader->setUniform("envMapRes", glm::vec2(env.width, env.height));
        tileShader->setUniform("envMapTotalSum", env.totalSum);
    }

    // ── Preview shader (same sampler mapping) ───────────────────────
    previewShader->bind();
    previewShader->setUniform("resolution", glm::vec2(renderSize));
    previewShader->setUniform("numOfLights", static_cast<int>(scene->getLights().size()));
    previewShader->setUniform("vertexIndicesTex", 2);
    previewShader->setUniform("verticesTex", 3);
    previewShader->setUniform("normalsTex", 4);
    previewShader->setUniform("materialsTex", 5);
    previewShader->setUniform("transformsTex", 6);
    previewShader->setUniform("lightsTex", 7);
    previewShader->setUniform("textureMapsArrayTex", 8);
    if (hasBVH)
    {
        previewShader->setUniform("topBVHIndex", 0);
        previewShader->setUniform("BVH", 1);
    }
    else
    {
        previewShader->setUniform("numMeshInstances", static_cast<int>(scene->getMeshInstanceInfos().size()));
        previewShader->setUniform("meshInstanceInfoTex", 11);
    }
    if (hasEnvMap)
    {
        const auto &env = scene->getEnvMap();
        previewShader->setUniform("envMapTex", 9);
        previewShader->setUniform("envMapCDFTex", 10);
        previewShader->setUniform("envMapRes", glm::vec2(env.width, env.height));
        previewShader->setUniform("envMapTotalSum", env.totalSum);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "SceneRenderer: shader uniforms initialised");
}

// ────────────────────────────────────────────────────────────────────────────
// Render (ported from GLSLPT Renderer::Render)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::render()
{
    if (!initialized || !scene)
        return;

    // Respect maxSpp limit
    const auto &opts = scene->getRenderOptions();
    if (!scene->dirty && opts.maxSpp != -1 && sampleCounter >= opts.maxSpp)
        return;

    glActiveTexture(GL_TEXTURE0);

    if (scene->dirty)
    {
        // Low-res preview when camera/scene is modified
        const int previewW = static_cast<int>(windowSize.x * pixelRatio);
        const int previewH = static_cast<int>(windowSize.y * pixelRatio);

        glBindFramebuffer(GL_FRAMEBUFFER, pathTraceFBOLowRes);
        glViewport(0, 0, previewW, previewH);
        drawQuad(previewShader);

        scene->instancesModified = false;
        scene->dirty = false;
        scene->envMapModified = false;
    }
    else
    {
        // Tile path trace → accum → tonemap (exactly like GLSLPT)
        glBindFramebuffer(GL_FRAMEBUFFER, pathTraceFBO);
        glViewport(0, 0, tileWidth, tileHeight);
        glBindTexture(GL_TEXTURE_2D, accumTexture);
        drawQuad(tileShader);

        // Copy tile into accumulation buffer
        glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
        glViewport(tileWidth * tile.x, tileHeight * tile.y, tileWidth, tileHeight);
        glBindTexture(GL_TEXTURE_2D, pathTraceTexture);
        drawQuad(outputShader);

        // Tonemap accumulation into the current output buffer
        glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tileOutputTexture[currentBuffer], 0);
        glViewport(0, 0, renderSize.x, renderSize.y);
        glBindTexture(GL_TEXTURE_2D, accumTexture);
        drawQuad(tonemapShader);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ────────────────────────────────────────────────────────────────────────────
// Present (ported from GLSLPT Renderer::Present)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::present()
{
    if (!initialized)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowSize.x, windowSize.y);

    glActiveTexture(GL_TEXTURE0);

    if (scene->dirty)
    {
        // Show low-res preview (tonemapped)
        glBindTexture(GL_TEXTURE_2D, pathTraceTextureLowRes);
        drawQuad(tonemapShader);
    }
    else
    {
        // Show live accumulation so progressive updates are visible every tile.
        glBindTexture(GL_TEXTURE_2D, accumTexture);
        drawQuad(tonemapShader);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Update (ported from GLSLPT Renderer::Update)
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::update(float /*secondsElapsed*/)
{
    if (!initialized || !scene)
        return;

    const auto &opts = scene->getRenderOptions();
    if (!scene->dirty && opts.maxSpp != -1 && sampleCounter >= opts.maxSpp)
        return;

    // ── If scene was modified, reset accumulation ───────────────────
    if (scene->dirty)
    {
        tile.x = -1;
        tile.y = numTiles.y - 1;
        sampleCounter = 1;
        frameCounter  = 1;

        glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    else
    {
        frameCounter++;
        tile.x++;
        if (tile.x >= numTiles.x)
        {
            tile.x = 0;
            tile.y--;
            if (tile.y < 0)
            {
                tile.x = 0;
                tile.y = numTiles.y - 1;
                sampleCounter++;
                currentBuffer = 1 - currentBuffer;
            }
        }
    }

    // ── Per-frame uniforms ──────────────────────────────────────────
    const auto &cam = scene->getCamera();

    if (tileShader)
    {
        tileShader->bind();
        tileShader->setUniform("camera.position", cam.position);
        tileShader->setUniform("camera.right", cam.right);
        tileShader->setUniform("camera.up", cam.up);
        tileShader->setUniform("camera.forward", cam.forward);
        tileShader->setUniform("camera.fov", cam.fov);
        tileShader->setUniform("camera.focalDist", cam.focalDist);
        tileShader->setUniform("camera.aperture", cam.aperture);
        tileShader->setUniform("maxDepth", opts.maxDepth);
        tileShader->setUniform("tileOffset", glm::vec2(
            static_cast<float>(tile.x) * invNumTiles.x,
            static_cast<float>(tile.y) * invNumTiles.y));
        if (opts.enableUniformLight)
            tileShader->setUniform("uniformLightCol", opts.uniformLightCol);
        tileShader->setUniform("frameNum", frameCounter);
        if (scene->hasEnvMap() && opts.enableEnvMap)
        {
            tileShader->setUniform("envMapIntensity", opts.envMapIntensity);
            tileShader->setUniform("envMapRot", opts.envMapRot / 360.0f);
        }
        if (opts.enableRoughnessMollification)
            tileShader->setUniform("roughnessMollificationAmt", opts.roughnessMollificationAmt);
    }

    if (previewShader)
    {
        previewShader->bind();
        previewShader->setUniform("camera.position", cam.position);
        previewShader->setUniform("camera.right", cam.right);
        previewShader->setUniform("camera.up", cam.up);
        previewShader->setUniform("camera.forward", cam.forward);
        previewShader->setUniform("camera.fov", cam.fov);
        previewShader->setUniform("camera.focalDist", cam.focalDist);
        previewShader->setUniform("camera.aperture", cam.aperture);
        previewShader->setUniform("maxDepth", scene->dirty ? 2 : opts.maxDepth);
        if (opts.enableUniformLight)
            previewShader->setUniform("uniformLightCol", opts.uniformLightCol);
        if (scene->hasEnvMap() && opts.enableEnvMap)
        {
            previewShader->setUniform("envMapIntensity", opts.envMapIntensity);
            previewShader->setUniform("envMapRot", opts.envMapRot / 360.0f);
        }
        if (opts.enableRoughnessMollification)
            previewShader->setUniform("roughnessMollificationAmt", opts.roughnessMollificationAmt);
    }

    if (tonemapShader)
    {
        tonemapShader->bind();
        tonemapShader->setUniform("invSampleCounter", 1.0f / static_cast<float>(sampleCounter));
        tonemapShader->setUniform("enableTonemap", opts.enableTonemap);
        tonemapShader->setUniform("enableAces", opts.enableAces);
        tonemapShader->setUniform("simpleAcesFit", opts.simpleAcesFit);
        if (opts.enableBackground || opts.transparentBackground)
            tonemapShader->setUniform("backgroundCol", opts.backgroundCol);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Mark dirty
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::markDirty()
{
    if (scene)
        scene->dirty = true;
}

// ────────────────────────────────────────────────────────────────────────────
// Rebuild shaders with scene-specific #defines
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::rebuildShadersWithDefines()
{
    if (!scene || !tileShader || !previewShader || !tonemapShader)
        return;

    const auto &opts = scene->getRenderOptions();

    // ── Path-trace defines (tile + preview shaders) ─────────────────
    std::string ptDefines;
    ptDefines += "#define OPT_NO_BVH\n"; // Iteration 1: brute-force intersection

    if (!scene->getLights().empty())
        ptDefines += "#define OPT_LIGHTS\n";

    if (opts.enableEnvMap && scene->hasEnvMap())
        ptDefines += "#define OPT_ENVMAP\n";

    if (opts.enableUniformLight)
        ptDefines += "#define OPT_UNIFORM_LIGHT\n";

    if (opts.enableRoughnessMollification)
        ptDefines += "#define OPT_ROUGHNESS_MOLLIFICATION\n";

    if (opts.enableRR)
    {
        ptDefines += "#define OPT_RR\n";
        ptDefines += "#define OPT_RR_DEPTH " + std::to_string(opts.RRDepth) + "\n";
    }

    if (opts.hideEmitters)
        ptDefines += "#define OPT_HIDE_EMITTERS\n";

    if (opts.enableBackground)
        ptDefines += "#define OPT_BACKGROUND\n";

    if (opts.transparentBackground)
        ptDefines += "#define OPT_TRANSPARENT_BACKGROUND\n";

    // ── Tonemap defines ─────────────────────────────────────────────
    std::string tmDefines;
    if (opts.enableBackground)
        tmDefines += "#define OPT_BACKGROUND\n";
    if (opts.transparentBackground)
        tmDefines += "#define OPT_TRANSPARENT_BACKGROUND\n";

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "SceneRenderer: recompiling tile/preview shaders with defines:\n%s", ptDefines.c_str());

    tileShader->recompileWithDefines(ptDefines);
    previewShader->recompileWithDefines(ptDefines);

    if (!tmDefines.empty())
        tonemapShader->recompileWithDefines(tmDefines);

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "SceneRenderer: shaders rebuilt with defines");
}

// ────────────────────────────────────────────────────────────────────────────
// Cleanup
// ────────────────────────────────────────────────────────────────────────────

void SceneRenderer::cleanup()
{
    // Textures
    glDeleteTextures(1, &vertexIndicesTex);
    glDeleteTextures(1, &verticesTex);
    glDeleteTextures(1, &normalsTex);
    glDeleteTextures(1, &materialsTex);
    glDeleteTextures(1, &transformsTex);
    glDeleteTextures(1, &lightsTex);
    glDeleteTextures(1, &textureMapsArrayTex);
    glDeleteTextures(1, &envMapTex);
    glDeleteTextures(1, &envMapCDFTex);
    glDeleteTextures(1, &meshInstanceInfoTex);
    glDeleteTextures(1, &pathTraceTexture);
    glDeleteTextures(1, &pathTraceTextureLowRes);
    glDeleteTextures(1, &accumTexture);
    glDeleteTextures(1, &tileOutputTexture[0]);
    glDeleteTextures(1, &tileOutputTexture[1]);

    // Buffers
    glDeleteBuffers(1, &vertexIndicesBuffer);
    glDeleteBuffers(1, &verticesBuffer);
    glDeleteBuffers(1, &normalsBuffer);

    // FBOs
    glDeleteFramebuffers(1, &pathTraceFBO);
    glDeleteFramebuffers(1, &pathTraceFBOLowRes);
    glDeleteFramebuffers(1, &accumFBO);
    glDeleteFramebuffers(1, &outputFBO);

    // Quad
    if (quadVAO)
    {
        glDeleteVertexArrays(1, &quadVAO);
        quadVAO = 0;
    }
    if (quadVBO)
    {
        glDeleteBuffers(1, &quadVBO);
        quadVBO = 0;
    }

    initialized = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "SceneRenderer: resources cleaned up");
}
