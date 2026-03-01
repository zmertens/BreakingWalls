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
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_seedPositions.size() * sizeof(glm::vec3), m_seedPositions.data(), GL_DYNAMIC_DRAW);
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
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_cellColors.size() * sizeof(glm::vec3), m_cellColors.data(), GL_DYNAMIC_DRAW);
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
    std::mt19937 rng(static_cast<unsigned int>(SDL_GetTicks()));
    std::uniform_real_distribution<float> distX(-50.0f, 50.0f); // Adjust range as needed
    std::uniform_real_distribution<float> distZ(-50.0f, 50.0f);
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

    for (int lat = 0; lat <= latSteps; ++lat)
    {
        float v = (float)lat / (float)latSteps;
        float theta = v * glm::pi<float>();
        for (int lon = 0; lon <= lonSteps; ++lon)
        {
            float u = (float)lon / (float)lonSteps;
            float phi = u * glm::two_pi<float>();
            float x = sin(theta) * cos(phi);
            float y = cos(theta);
            float z = sin(theta) * sin(phi);
            glm::vec3 pos = glm::normalize(glm::vec3(x, y, z));
            m_vertices.push_back(pos);
            m_normals.push_back(pos);
            int cid = findNearestSeed(pos);
            m_cellIds.push_back((unsigned int)cid);
        }
    }

    int vertsPerRow = lonSteps + 1;
    for (int lat = 0; lat < latSteps; ++lat)
    {
        for (int lon = 0; lon < lonSteps; ++lon)
        {
            int a = lat * vertsPerRow + lon;
            int b = a + vertsPerRow;
            int c = b + 1;
            int d = a + 1;
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
    int best = 0;
    float bestDist = glm::dot(p - m_seedPositions[0], p - m_seedPositions[0]);
    for (size_t i = 1; i < m_seedPositions.size(); ++i)
    {
        float d = glm::dot(p - m_seedPositions[i], p - m_seedPositions[i]);
        if (d < bestDist)
        {
            bestDist = d;
            best = (int)i;
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
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cellColorSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_cellColors.size() * sizeof(glm::vec3), m_cellColors.data(), GL_DYNAMIC_DRAW);
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
    // worldPos should be normalized to unit sphere
    glm::vec3 p = glm::normalize(worldPos);
    int id = findNearestSeed(p);
    if (id < 0 || id >= (int)m_cellColors.size())
        return;
    m_cellColors[id] = color;
    m_paintedStates[id] = 1;
    // Log painted cell for debugging mapping issues
    if (m_uploaded)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_cellColorSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, id * sizeof(glm::vec3), sizeof(glm::vec3), glm::value_ptr(color));
        if (m_paintedSSBO != 0)
        {
            uint32_t painted = 1;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_paintedSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, id * sizeof(uint32_t), sizeof(uint32_t), &painted);
        }
    }
}
