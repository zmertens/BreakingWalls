#ifndef HEIGHTMAP_H
#define HEIGHTMAP_H

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <MazeBuilder/configurator.h>

/// Surface material shared by the editor (authoring) and the game world (consumer).
/// Elevation bands pick the block type; the colour/shininess are exported with the OBJ materials.
struct heightmap_material
{
    const char *name;
    float max_elevation; // upper bound of the elevation band (0..1)
    int block;           // item::BlockType value used for the surface voxel
    float kd[3];         // diffuse colour
    float ns;            // specular exponent
};

inline constexpr std::array<heightmap_material, 4> HEIGHTMAP_MATERIALS{{
    {"sand", 0.18f, 2, {0.86f, 0.80f, 0.55f}, 8.0f},
    {"grass", 0.55f, 1, {0.30f, 0.62f, 0.25f}, 4.0f},
    {"stone", 0.80f, 3, {0.50f, 0.50f, 0.52f}, 16.0f},
    {"snow", 1.01f, 9, {0.95f, 0.96f, 1.00f}, 32.0f},
}};

/// A constrained (island style) square height field. Elevation is normalized to [0, 1].
struct heightmap
{
    static constexpr int MIN_SIDE = 32;
    static constexpr int MAX_SIDE = 256;

    std::string name;
    int side{0};
    int max_height{32};
    std::vector<float> data;

    [[nodiscard]] bool contains(int x, int z) const noexcept { return x >= 0 && z >= 0 && x < side && z < side; }
    [[nodiscard]] float elevation(int x, int z) const noexcept;
    /// Number of solid voxels in the column (at least one inside the map).
    [[nodiscard]] int column_height(int x, int z) const noexcept;
    [[nodiscard]] static const heightmap_material &material_for(float elevation) noexcept;
};

struct heightmap_params
{
    int side{128};
    int seed{0};
    int octaves{3};
    float frequency{3.0f};
    float edge_factor{0.9f};
    float edge_dropoff{1.5f};
    int max_height{32};
    bool apply_maze{false};
    mazes::configurator maze{};
};

/// Wall raster of a maze: (2 * columns + 1) x (2 * rows + 1) pixels; cell (r, c) is pixel (2c + 1, 2r + 1).
struct maze_layout
{
    unsigned int rows{};
    unsigned int columns{};
    std::vector<std::uint8_t> walls; // 255 = wall, 0 = open

    [[nodiscard]] int width() const noexcept { return static_cast<int>(columns) * 2 + 1; }
    [[nodiscard]] int height() const noexcept { return static_cast<int>(rows) * 2 + 1; }
    [[nodiscard]] bool is_wall(const int x, const int y) const noexcept
    {
        return x < 0 || y < 0 || x >= width() || y >= height() ||
               walls[static_cast<std::size_t>(y) * static_cast<std::size_t>(width()) + static_cast<std::size_t>(x)] != 0u;
    }
};

/// Asks the mazes::runtime_app singleton for an OBJ maze and rasterizes its walls.
/// Returns nullopt if the runtime is unavailable or its output cannot be parsed.
[[nodiscard]] std::optional<maze_layout> generate_maze_layout(const mazes::configurator &maze, int seed);

/// Writes a grayscale (PGM) maze image under the system temp directory; walls are white.
[[nodiscard]] std::filesystem::path write_maze_image(const mazes::configurator &maze, int side);

/// Reads a maze image back into elevations using gradient descent on a smoothness-regularized energy,
/// E(h) = 1/2 sum (h - pixel)^2 + lambda/2 sum |grad h|^2.
[[nodiscard]] std::vector<float> decode_maze_image(const std::filesystem::path &image, int side);

/// Builds a heightmap, splitting rows over worker threads.
[[nodiscard]] heightmap generate_heightmap(const heightmap_params &params);

namespace bvh::v2
{
    class ThreadPool;
}

/// Proof of concept: BVH v2 over one AABB per heightmap column, for ray picking against the terrain.
/// Built in parallel on the given pool; queries are const and safe from multiple threads.
class heightmap_bvh
{
public:
    struct hit
    {
        int x{0}, z{0};  // column hit
        float t{0.0f};   // distance along the (normalized) ray
        float point[3]{}; // world-space entry point, voxel units (column x spans [x, x + 1))
    };

    heightmap_bvh();
    ~heightmap_bvh();
    heightmap_bvh(heightmap_bvh &&) noexcept;
    heightmap_bvh &operator=(heightmap_bvh &&) noexcept;

    void build(const heightmap &map, bvh::v2::ThreadPool &pool);
    [[nodiscard]] bool empty() const noexcept;
    /// Closest column hit within [0, max_distance]; `dir` need not be normalized.
    [[nodiscard]] std::optional<hit> raycast(const float origin[3], const float dir[3], float max_distance) const;

private:
    struct impl;
    std::unique_ptr<impl> m_impl;
};

/// Process-wide list of generated heightmaps, persisted through db.
class heightmap_library
{
public:
    static heightmap_library &instance();

    struct entry
    {
        int id{0}; // db row id, or 0 if not persisted
        std::shared_ptr<const heightmap> map;
    };

    /// Loads persisted heightmaps once.
    void ensure_loaded();
    /// Generates, stores, and selects a new heightmap.
    std::shared_ptr<const heightmap> add(const heightmap_params &params);
    void select(std::size_t index);
    void remove(std::size_t index);

    [[nodiscard]] const std::vector<entry> &entries() const noexcept { return m_entries; }
    [[nodiscard]] std::size_t selected() const noexcept { return m_selected; }
    [[nodiscard]] std::shared_ptr<const heightmap> current() const noexcept;

    /// Play queue for the game world: saved heightmaps first, otherwise freshly generated ones.
    [[nodiscard]] std::deque<std::shared_ptr<const heightmap>> make_play_queue(std::size_t count);

private:
    std::vector<entry> m_entries;
    std::size_t m_selected{0};
    bool m_loaded{false};
};

#endif // HEIGHTMAP_H
