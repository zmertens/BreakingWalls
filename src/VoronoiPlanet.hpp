#ifndef VORONOIPLANET_HPP
#define VORONOIPLANET_HPP

#include <vector>
#include <glm/glm.hpp>

class VoronoiPlanet {
public:
    VoronoiPlanet();
    ~VoronoiPlanet();

    // generate seeds and ground-plane Voronoi mesh on sphere; seedCount controls Voronoi resolution
    void initialize(int seedCount = 512, int lonSteps = 64, int latSteps = 32);
    
    // generate seeds and ground-plane Voronoi mesh on sphere with custom radius and center
    void initialize(float radius, const glm::vec3 &center, int seedCount = 512, int lonSteps = 64, int latSteps = 32);

    // upload GPU buffers (requires GL context)
    void uploadToGPU();

    // Upload cell color data to a provided SSBO (for compute shader use)
    void uploadCellColorsToSSBO(unsigned int ssbo) const;

    // Getters for cell color and seed data (for CPU-side buffer upload)
    const std::vector<glm::vec3>& getCellColors() const { return m_cellColors; }
    const std::vector<glm::vec3>& getSeedPositions() const { return m_seedPositions; }
    size_t getCellCount() const { return m_cellColors.size(); }
    size_t getPaintedCellCount() const;
    bool isPlanetComplete() const;

    // render with provided shader program (shader must set uModel/uView/uProjection)
    void draw() const;

    // paint nearest cell to world-space position projected onto sphere surface
    void paintAtPosition(const glm::vec3 &worldPos, const glm::vec3 &color);
    
    // getters for sphere parameters
    float getRadius() const { return m_radius; }
    glm::vec3 getCenter() const { return m_center; }

private:
    void generateSeeds(int count);
    void generateUVSphere(int lonSteps, int latSteps);
    int findNearestSeed(const glm::vec3 &p) const;

    // CPU-side data
    float m_radius = 50.0f;
    glm::vec3 m_center = glm::vec3(0.0f);
    std::vector<glm::vec3> m_vertices;
    std::vector<glm::vec3> m_normals;
    std::vector<unsigned int> m_indices;
    std::vector<unsigned int> m_cellIds; // per-vertex cell id
    std::vector<glm::vec3> m_seedPositions;
    std::vector<glm::vec3> m_cellColors;
    std::vector<uint32_t> m_paintedStates; // 0 = not painted, 1 = painted

    // GPU handles
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_cellIdVbo = 0; // per-vertex cell id
    unsigned int m_cellColorSSBO = 0; // per-cell colors (for draw shader)
    unsigned int m_seedSSBO = 0; // per-cell seed positions
    unsigned int m_paintedSSBO = 0; // per-cell painted state

    bool m_uploaded = false;
public:
    void uploadCellSeedsToSSBO(unsigned int ssbo) const;
    void uploadPaintedStatesToSSBO(unsigned int ssbo) const;
};

#endif // VORONOIPLANET_HPP
