#include "VoronoiPlanet.hpp"
#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>
#include <SDL3/SDL.h>
#include <cstring>

// Upload cell seed data to a provided SSBO (for compute shader use)
void VoronoiPlanet::uploadCellSeedsToSSBO(unsigned int ssbo) const
{
    if (m_seedPositions.empty() || ssbo == 0)
        return;

    std::vector<glm::vec4> packedSeeds;
    packedSeeds.reserve(m_seedPositions.size());
    for (const glm::vec3 &seed : m_seedPositions)
    {
        packedSeeds.emplace_back(seed, 0.0f);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 packedSeeds.size() * sizeof(glm::vec4),
                 packedSeeds.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo); // Binding 4 for compute shader
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Upload painted state data to a provided SSBO (for compute shader use)
void VoronoiPlanet::uploadPaintedStatesToSSBO(unsigned int ssbo) const
{
    if (m_paintedStates.empty() || ssbo == 0)
        return;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_paintedStates.size() * sizeof(uint32_t), m_paintedStates.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo); // Binding 5 for compute shader
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
// Upload cell color data to a provided SSBO (for compute shader use)
void VoronoiPlanet::uploadCellColorsToSSBO(unsigned int ssbo) const
{
    if (m_cellColors.empty() || ssbo == 0)
        return;

    std::vector<glm::vec4> packedColors;
    packedColors.reserve(m_cellColors.size());
    for (const glm::vec3 &color : m_cellColors)
    {
        packedColors.emplace_back(color, 1.0f);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 packedColors.size() * sizeof(glm::vec4),
                 packedColors.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo); // Binding 3 for compute shader (convention)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

VoronoiPlanet::VoronoiPlanet() {}
VoronoiPlanet::~VoronoiPlanet()
{
    if (m_uploaded)
    {
        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ebo);
        glDeleteBuffers(1, &m_cellIdVbo);
        glDeleteBuffers(1, &m_cellColorSSBO);
        glDeleteVertexArrays(1, &m_vao);
    }
}

void VoronoiPlanet::initialize(int seedCount, int lonSteps, int latSteps)
{
    generateSeeds(seedCount);
    generateUVSphere(lonSteps, latSteps);
    // init per-cell colors to pastel
    m_cellColors.resize(m_seedPositions.size());
    m_paintedStates.resize(m_seedPositions.size(), 0);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> d(0.5f, 1.0f);
    for (size_t i = 0; i < m_cellColors.size(); ++i)
    {
        m_cellColors[i] = glm::vec3(d(rng), d(rng), d(rng));
    }
    std::fill(m_paintedStates.begin(), m_paintedStates.end(), 0);
}

void VoronoiPlanet::generateSeeds(int count)
{
    m_seedPositions.clear();
    m_seedPositions.reserve(count);
    // 2D random distribution in XZ for ground plane
    // Player runs in positive X starting from ~100, extended range for runner gameplay
    std::mt19937 rng(static_cast<unsigned int>(SDL_GetTicks()));
    std::uniform_real_distribution<float> distX(0.0f, 2000.0f);
    std::uniform_real_distribution<float> distZ(-75.0f, 75.0f);
    for (int i = 0; i < count; ++i)
    {
        float x = distX(rng);
        float z = distZ(rng);
        m_seedPositions.emplace_back(x, 0.0f, z);
    }
}

void VoronoiPlanet::generateUVSphere(int lonSteps, int latSteps)
{
    m_vertices.clear();
    m_normals.clear();
    m_indices.clear();
    m_cellIds.clear();

    constexpr float kGroundHalfExtent = 50.0f;

    for (int zStep = 0; zStep <= latSteps; ++zStep)
    {
        const float v = static_cast<float>(zStep) / static_cast<float>(latSteps);
        const float z = glm::mix(-kGroundHalfExtent, kGroundHalfExtent, v);

        for (int xStep = 0; xStep <= lonSteps; ++xStep)
        {
            const float u = static_cast<float>(xStep) / static_cast<float>(lonSteps);
            const float x = glm::mix(-kGroundHalfExtent, kGroundHalfExtent, u);

            const glm::vec3 pos(x, 0.0f, z);
            m_vertices.push_back(pos);
            m_normals.emplace_back(0.0f, 1.0f, 0.0f);

            const int cid = findNearestSeed(pos);
            m_cellIds.push_back(static_cast<unsigned int>(cid));
        }
    }

    const int vertsPerRow = lonSteps + 1;
    for (int zStep = 0; zStep < latSteps; ++zStep)
    {
        for (int xStep = 0; xStep < lonSteps; ++xStep)
        {
            const int a = zStep * vertsPerRow + xStep;
            const int b = a + vertsPerRow;
            const int c = b + 1;
            const int d = a + 1;
            m_indices.push_back(a);
            m_indices.push_back(b);
            m_indices.push_back(c);
            m_indices.push_back(a);
            m_indices.push_back(c);
            m_indices.push_back(d);
        }
    }
}

int VoronoiPlanet::findNearestSeed(const glm::vec3 &p) const
{
    if (m_seedPositions.empty())
    {
        return 0;
    }

    int best = 0;
    const glm::vec2 pXZ(p.x, p.z);
    const glm::vec2 bestSeedXZ(m_seedPositions[0].x, m_seedPositions[0].z);
    float bestDist = glm::dot(pXZ - bestSeedXZ, pXZ - bestSeedXZ);

    for (size_t i = 1; i < m_seedPositions.size(); ++i)
    {
        const glm::vec2 seedXZ(m_seedPositions[i].x, m_seedPositions[i].z);
        const glm::vec2 delta = pXZ - seedXZ;
        const float d = glm::dot(delta, delta);
        if (d < bestDist)
        {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void VoronoiPlanet::uploadToGPU()
{
    if (m_uploaded)
        return;
    // create interleaved VBO: position (3), normal (3)
    struct Vertex
    {
        float p[3];
        float n[3];
    };
    std::vector<Vertex> verts;
    verts.reserve(m_vertices.size());
    for (size_t i = 0; i < m_vertices.size(); ++i)
    {
        Vertex v;
        v.p[0] = m_vertices[i].x;
        v.p[1] = m_vertices[i].y;
        v.p[2] = m_vertices[i].z;
        v.n[0] = m_normals[i].x;
        v.n[1] = m_normals[i].y;
        v.n[2] = m_normals[i].z;
        verts.push_back(v);
    }

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, n)));

    // cell id buffer (per-vertex uint)
    glGenBuffers(1, &m_cellIdVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_cellIdVbo);
    glBufferData(GL_ARRAY_BUFFER, m_cellIds.size() * sizeof(unsigned int), m_cellIds.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, 0, (void *)0);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int), m_indices.data(), GL_STATIC_DRAW);

    // SSBO for cell colors
    glGenBuffers(1, &m_cellColorSSBO);
    std::vector<glm::vec4> packedColors;
    packedColors.reserve(m_cellColors.size());
    for (const glm::vec3 &color : m_cellColors)
    {
        packedColors.emplace_back(color, 1.0f);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cellColorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 packedColors.size() * sizeof(glm::vec4),
                 packedColors.data(),
                 GL_DYNAMIC_DRAW);
    // bind to binding point 1 by default
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_cellColorSSBO);

    glBindVertexArray(0);

    m_uploaded = true;
}

void VoronoiPlanet::draw() const
{
    if (!m_uploaded)
        return;
    glBindVertexArray(m_vao);
    // ensure SSBO bound
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_cellColorSSBO);
    glDrawElements(GL_TRIANGLES, (GLsizei)m_indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void VoronoiPlanet::paintAtPosition(const glm::vec3 &worldPos, const glm::vec3 &color)
{
    const glm::vec3 planePos(worldPos.x, 0.0f, worldPos.z);
    const int id = findNearestSeed(planePos);
    if (id < 0 || id >= static_cast<int>(m_cellColors.size()))
        return;

    m_cellColors[id] = color;
    m_paintedStates[id] = 1;

    if (m_uploaded)
    {
        const glm::vec4 packedColor(color, 1.0f);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cellColorSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                        id * sizeof(glm::vec4),
                        sizeof(glm::vec4),
                        glm::value_ptr(packedColor));
        if (m_paintedSSBO != 0)
        {
            const uint32_t painted = 1;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_paintedSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, id * sizeof(uint32_t), sizeof(uint32_t), &painted);
        }
    }
}
