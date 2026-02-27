#pragma once
#include <vector>
#include <glm/glm.hpp>

class VoronoiPlanet {
public:
    VoronoiPlanet();
    ~VoronoiPlanet();

    // generate seeds and sphere mesh; seedCount controls Voronoi resolution
    void initialize(int seedCount = 512, int lonSteps = 64, int latSteps = 32);

    // upload GPU buffers (requires GL context)
    void uploadToGPU();

    // render with provided shader program (shader must set uModel/uView/uProjection)
    void draw() const;

    // paint nearest cell to world-space position (normalized position on unit sphere)
    void paintAtPosition(const glm::vec3 &worldPos, const glm::vec3 &color);

private:
    void generateSeeds(int count);
    void generateUVSphere(int lonSteps, int latSteps);
    int findNearestSeed(const glm::vec3 &p) const;

    // CPU-side data
    std::vector<glm::vec3> m_vertices;
    std::vector<glm::vec3> m_normals;
    std::vector<unsigned int> m_indices;
    std::vector<unsigned int> m_cellIds; // per-vertex cell id
    std::vector<glm::vec3> m_seedPositions;
    std::vector<glm::vec3> m_cellColors;

    // GPU handles
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_cellIdVbo = 0; // per-vertex cell id
    unsigned int m_cellColorSSBO = 0; // per-cell colors

    bool m_uploaded = false;
};
