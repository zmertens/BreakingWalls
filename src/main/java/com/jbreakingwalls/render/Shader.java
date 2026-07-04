package com.jbreakingwalls.render;

import org.lwjgl.system.MemoryStack;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

import static org.lwjgl.opengl.GL33C.*;

/**
 * Thin wrapper around an OpenGL shader program — mirrors {@code Shader.hpp}
 * from the C++ source.
 *
 * Loads GLSL source from the Java classpath, compiles and links the
 * program, and exposes uniform setters for the types used by the renderer.
 */
public final class Shader {

    private final int programId;
    private final Map<String, Integer> uniformCache = new HashMap<>();

    /**
     * Compile and link a program from classpath resources.
     *
     * @param vertPath classpath-relative path to the vertex shader
     * @param fragPath classpath-relative path to the fragment shader
     */
    public Shader(String vertPath, String fragPath) {
        int vert = compileStage(vertPath, GL_VERTEX_SHADER);
        int frag = compileStage(fragPath, GL_FRAGMENT_SHADER);

        programId = glCreateProgram();
        glAttachShader(programId, vert);
        glAttachShader(programId, frag);
        glLinkProgram(programId);

        if (glGetProgrami(programId, GL_LINK_STATUS) == GL_FALSE) {
            String log = glGetProgramInfoLog(programId);
            glDeleteProgram(programId);
            throw new RuntimeException("Shader link error:\n" + log);
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    // ── Binding ───────────────────────────────────────────────────────────────

    public void use()   { glUseProgram(programId); }
    public void unuse() { glUseProgram(0); }

    public void dispose() { glDeleteProgram(programId); }

    // ── Uniform setters ───────────────────────────────────────────────────────

    public void setInt(String name, int v)                       { glUniform1i(loc(name), v); }
    public void setFloat(String name, float v)                   { glUniform1f(loc(name), v); }
    public void setVec2(String name, float x, float y)          { glUniform2f(loc(name), x, y); }
    public void setVec4(String name, float r, float g, float b, float a) { glUniform4f(loc(name), r, g, b, a); }

    public void setMat4(String name, float[] m) {
        try (MemoryStack stack = MemoryStack.stackPush()) {
            glUniformMatrix4fv(loc(name), false, m);
        }
    }

    // ── Private ───────────────────────────────────────────────────────────────

    // Uniform location cache: HashMap avoids repeated glGetUniformLocation JNI calls
    private int loc(String name) {
        return uniformCache.computeIfAbsent(name, n -> glGetUniformLocation(programId, n));
    }

    private static int compileStage(String path, int type) {
        String src = loadResource(path);
        int id = glCreateShader(type);
        glShaderSource(id, src);
        glCompileShader(id);
        if (glGetShaderi(id, GL_COMPILE_STATUS) == GL_FALSE) {
            String log = glGetShaderInfoLog(id);
            glDeleteShader(id);
            throw new RuntimeException("Shader compile error [" + path + "]:\n" + log);
        }
        return id;
    }

    private static String loadResource(String path) {
        try (InputStream is = Shader.class.getClassLoader().getResourceAsStream(path)) {
            if (is == null) throw new IOException("Classpath resource not found: " + path);
            return new String(is.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new RuntimeException("Failed to load shader: " + path, e);
        }
    }
}
