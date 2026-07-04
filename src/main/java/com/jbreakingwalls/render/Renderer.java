package com.jbreakingwalls.render;

import com.jbreakingwalls.config.GameConfig;
import org.joml.Matrix4f;
import org.lwjgl.system.MemoryUtil;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;

import static org.lwjgl.opengl.GL33C.*;

/**
 * GPU-side 2-D renderer — the sole owner of the quad VAO/VBO/EBO and the
 * game's single {@link Shader}.
 *
 * All game objects (maze cells, patrol sprites) are drawn as axis-aligned
 * coloured quads via {@link #drawRect}. This replaces the full 3-D pipeline
 * from the original C++ project with something that has zero external native
 * library requirements beyond LWJGL itself.
 *
 * Coordinate system:
 * World origin (0,0) is at the bottom-left of the viewport.
 * Positive X runs right, positive Y runs up.
 */
public final class Renderer {

    // ── Projection constants ─────────────────────────────────────────────────

    /** Visible height of the world viewport in world-units (fixed). */
    public static final float VIEW_HEIGHT = 20.0f;

    /** How far below y=0 the projection extends. */
    // public so MazeRenderer and GameState can compute viewport centre
    public static final float VIEW_BELOW_GROUND = 1.5f;

    /** World-Y of the orthographic viewport centre: (top+bottom)/2 = 8.5. */
    public static final float VIEW_CENTER_Y =
            (VIEW_HEIGHT - 2f * VIEW_BELOW_GROUND) / 2f; // 8.5

    // ── VAO geometry ─────────────────────────────────────────────────────────

    private static final float[] VERTICES = {
        // aPos       aTexCoord
        0.0f, 0.0f,   0.0f, 0.0f,
        1.0f, 0.0f,   1.0f, 0.0f,
        1.0f, 1.0f,   1.0f, 1.0f,
        0.0f, 1.0f,   0.0f, 1.0f,
    };

    private static final int[] INDICES = { 0, 1, 2,  2, 3, 0 };

    private static final int FLOATS_PER_VERTEX = 4;
    private static final int STRIDE = FLOATS_PER_VERTEX * Float.BYTES;

    // ── State ─────────────────────────────────────────────────────────────────

    private Shader shader;
    private int vao, vbo, ebo;

    private final Matrix4f projection = new Matrix4f();
    private final Matrix4f model      = new Matrix4f();
    private final float[]  matScratch = new float[16];

    private float cameraX   = 0f;
    private float viewWidth = VIEW_HEIGHT * (16f / 9f);

    private float time        = 0f;
    private float lightX      = 0f, lightY = 0f;
    private float lightRadius = 10f;

    // ── Init / dispose ────────────────────────────────────────────────────────

    public void init(GameConfig config) {
        shader = new Shader(config.shaderQuadVert, config.shaderQuadFrag);

        vao = glGenVertexArrays();
        vbo = glGenBuffers();
        ebo = glGenBuffers();

        glBindVertexArray(vao);

        FloatBuffer vboBuf = MemoryUtil.memAllocFloat(VERTICES.length);
        try {
            vboBuf.put(VERTICES).flip();
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, vboBuf, GL_STATIC_DRAW);
        } finally {
            MemoryUtil.memFree(vboBuf);
        }

        IntBuffer eboBuf = MemoryUtil.memAllocInt(INDICES.length);
        try {
            eboBuf.put(INDICES).flip();
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, eboBuf, GL_STATIC_DRAW);
        } finally {
            MemoryUtil.memFree(eboBuf);
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, false, STRIDE, 0L);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, false, STRIDE, 2L * Float.BYTES);

        glBindVertexArray(0);
    }

    public void dispose() {
        if (shader != null) shader.dispose();
        glDeleteVertexArrays(vao);
        glDeleteBuffers(vbo);
        glDeleteBuffers(ebo);
    }

    // ── Camera / projection ───────────────────────────────────────────────────

    public void setCameraX(float x) { this.cameraX = x; }

    public void setViewDimensions(float viewW, float viewH) {
        this.viewWidth = viewW;
    }

    public void updateProjection(int fbW, int fbH) {
        if (fbH == 0) return;
        float aspect = (float) fbW / fbH;
        viewWidth = VIEW_HEIGHT * aspect;

        float left   = cameraX;
        float right  = cameraX + viewWidth;
        float bottom = -VIEW_BELOW_GROUND;
        float top    = VIEW_HEIGHT - VIEW_BELOW_GROUND;

        projection.setOrtho(left, right, bottom, top, -1f, 1f);
    }

    // ── Per-frame shader state ────────────────────────────────────────────────

    public void setTime(float t) { this.time = t; }

    public void setLightSource(float x, float y, float radius) {
        this.lightX      = x;
        this.lightY      = y;
        this.lightRadius = radius;
    }

    // ── Drawing API ───────────────────────────────────────────────────────────

    /** Flat-coloured quad — no lighting, no glow. */
    public void drawRect(float x, float y, float w, float h, float[] rgba) {
        draw(x, y, w, h, rgba, 0f, false, false);
    }

    /** Quad with a pulsing rim glow driven by the shader clock. */
    public void drawRectGlow(float x, float y, float w, float h, float[] rgba, float glow) {
        draw(x, y, w, h, rgba, glow, false, false);
    }

    /** Quad that receives the warm point light set by setLightSource. */
    public void drawRectLit(float x, float y, float w, float h, float[] rgba) {
        draw(x, y, w, h, rgba, 0f, true, false);
    }

    /** Full-sky quad that generates a procedural, twinkling star field. */
    public void drawRectStars(float x, float y, float w, float h, float[] rgba) {
        draw(x, y, w, h, rgba, 0f, false, true);
    }

    // ── Internal dispatch ─────────────────────────────────────────────────────

    private void draw(float x, float y, float w, float h, float[] rgba,
                      float glow, boolean lit, boolean starField) {
        model.identity().translate(x, y, 0f).scale(w, h, 1f);

        shader.use();

        projection.get(matScratch);
        shader.setMat4("uProjection", matScratch);
        model.get(matScratch);
        shader.setMat4("uModel", matScratch);

        shader.setVec4 ("uColor",        rgba[0], rgba[1], rgba[2], rgba[3]);
        shader.setInt  ("uUseTexture",   0);
        shader.setFloat("uTime",         time);
        shader.setFloat("uGlow",         glow);
        shader.setInt  ("uStarField",    starField ? 1 : 0);
        shader.setInt  ("uLightEnabled", lit       ? 1 : 0);
        shader.setVec2 ("uLightPos",     lightX, lightY);
        shader.setFloat("uLightRadius",  lightRadius);
        shader.setFloat("uAmbient",      0.38f);

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0L);
        glBindVertexArray(0);

        shader.unuse();
    }
}
