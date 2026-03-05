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
    m_radius = 50.0f;
    m_center = glm::vec3(0.0f);
    generateSeeds(seedCount);
    generateUVSphere(lonSteps, latSteps);
    // init per-cell colors with better visibility - use more distinct colors
    m_cellColors.resize(m_seedPositions.size());
    m_paintedStates.resize(m_seedPositions.size(), 0);
    
    // Use seed position for deterministic vibrant colors
    for (size_t i = 0; i < m_cellColors.size(); ++i)
    {
        const glm::vec3 &seedPos = m_seedPositions[i];
        
        // Create vibrant pseudo-random colors using fract with different scaling for each channel
        // This produces distinct, saturated colors across the spectrum
        float h = glm::fract(seedPos.x * 12.9898f + seedPos.y * 78.233f + seedPos.z * 45.164f);
        float g = glm::fract(seedPos.x * 94.673f + seedPos.y * 23.456f + seedPos.z * 67.345f);
        float b = glm::fract(seedPos.x * 56.789f + seedPos.y * 89.012f + seedPos.z * 12.098f);
        
        // Ensure vibrant range: scale to [0.3, 1.0] to avoid dark colors while maintaining saturation
        m_cellColors[i] = glm::vec3(h * 0.7f + 0.3f, g * 0.7f + 0.3f, b * 0.7f + 0.3f);
    }
    std::fill(m_paintedStates.begin(), m_paintedStates.end(), 0);
}

void VoronoiPlanet::initialize(float radius, const glm::vec3 &center, int seedCount, int lonSteps, int latSteps)
{
    m_radius = radius;
    m_center = center;
    generateSeeds(seedCount);
    generateUVSphere(lonSteps, latSteps);
    // init per-cell colors with better visibility - use more distinct colors
    m_cellColors.resize(m_seedPositions.size());
    m_paintedStates.resize(m_seedPositions.size(), 0);
    
    // Use seed position for deterministic vibrant colors
    for (size_t i = 0; i < m_cellColors.size(); ++i)
    {
        const glm::vec3 &seedPos = m_seedPositions[i];
        
        // Create vibrant pseudo-random colors using fract with different scaling for each channel
        // This produces distinct, saturated colors across the spectrum
        float h = glm::fract(seedPos.x * 12.9898f + seedPos.y * 78.233f + seedPos.z * 45.164f);
        float g = glm::fract(seedPos.x * 94.673f + seedPos.y * 23.456f + seedPos.z * 67.345f);
        float b = glm::fract(seedPos.x * 56.789f + seedPos.y * 89.012f + seedPos.z * 12.098f);
        
        // Ensure vibrant range: scale to [0.3, 1.0] to avoid dark colors while maintaining saturation
        m_cellColors[i] = glm::vec3(h * 0.7f + 0.3f, g * 0.7f + 0.3f, b * 0.7f + 0.3f);
    }
    std::fill(m_paintedStates.begin(), m_paintedStates.end(), 0);
}

void VoronoiPlanet::generateSeeds(int count)
{
    m_seedPositions.clear();
    m_seedPositions.reserve(count);
    
    // Use improved spherical distribution combining latitude stratification with golden angle
    // This ensures uniform coverage across entire sphere including both poles
    const float goldenRatio = (1.0f + glm::sqrt(5.0f)) / 2.0f;
    const float angleIncrement = 2.0f * glm::pi<float>() / goldenRatio;
    
    for (int i = 0; i < count; ++i)
    {
        // Linearly distribute height from south pole to north pole
        float h = -1.0f + (2.0f * i) / static_cast<float>(count - 1);  // Range: [-1, 1]
        
        // Use golden angle for azimuth to ensure even angular distribution
        float phi = angleIncrement * i;
        
        // Convert spherical to Cartesian coordinates
        float radius_at_h = glm::sqrt(1.0f - h * h);
        float x = radius_at_h * glm::cos(phi);
        float y = h;
        float z = radius_at_h * glm::sin(phi);
        
        // Scale by planet radius and offset by center
        glm::vec3 seedPos = m_center + glm::vec3(
            x * m_radius,
            y * m_radius,
            z * m_radius
        );
        m_seedPositions.emplace_back(seedPos);
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
            
            // Scale to planet radius and offset by center
            glm::vec3 pos = m_center + glm::vec3(x, y, z) * m_radius;
            glm::vec3 normal = glm::normalize(pos - m_center); // Normal points outward from center
            
            m_vertices.push_back(pos);
            m_normals.push_back(normal);
            const int cid = findNearestSeed(pos);
            m_cellIds.push_back(static_cast<unsigned int>(cid));
        }
    }
    const int vertsPerRow = lonSteps + 1;
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
    if (m_seedPositions.empty())
    {
        return 0;
    }
    int best = 0;
    float bestDist = glm::distance(p, m_seedPositions[0]);
    float bestDistSq = bestDist * bestDist;

    for (size_t i = 1; i < m_seedPositions.size(); ++i)
    {
        glm::vec3 delta = p - m_seedPositions[i];
        float distSq = glm::dot(delta, delta);
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
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
    // Project world position onto sphere surface
    glm::vec3 fromCenter = worldPos - m_center;
    float distFromCenter = glm::length(fromCenter);
    
    glm::vec3 surfacePos;
    if (distFromCenter > 0.001f)
    {
        // Project onto sphere surface
        surfacePos = m_center + glm::normalize(fromCenter) * m_radius;
    }
    else
    {
        // If at center, use a default direction
        surfacePos = m_center + glm::vec3(m_radius, 0.0f, 0.0f);
    }
    
    const int id = findNearestSeed(surfacePos);
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
            uint32_t painted = 1;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_paintedSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, id * sizeof(uint32_t), sizeof(uint32_t), &painted);
        }
    }
}

size_t VoronoiPlanet::getPaintedCellCount() const
{
    return std::count(m_paintedStates.begin(), m_paintedStates.end(), 1u);
}

bool VoronoiPlanet::isPlanetComplete() const
{
    if (m_cellColors.empty())
        return false;
    return getPaintedCellCount() == m_cellColors.size();
}
