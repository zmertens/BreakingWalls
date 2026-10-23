#include "heightmap.h"

#include "db.h"

#include <bvhv2/bvh.h>
#include <bvhv2/default_builder.h>
#include <bvhv2/executor.h>
#include <bvhv2/stack.h>
#include <bvhv2/thread_pool.h>

#include <MazeBuilder/algos.h>
#include <MazeBuilder/randomizer.h>
#include <MazeBuilder/runtime_app.h>

#include <SDL3/SDL.h>
#include <noise/noise.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <thread>

namespace
{
    // Work is handed out in row blocks, like the SFML island example's thread pool.
    template <typename RowFn>
    void parallel_rows(const int rows, RowFn &&fn)
    {
#if defined(__EMSCRIPTEN__)
        for (int row = 0; row < rows; ++row)
            fn(row);
#else
        constexpr int ROWS_PER_BLOCK = 8;
        const unsigned int workers = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
        std::atomic<int> next_block{0};
        const auto run = [&]
        {
            for (int begin; (begin = next_block.fetch_add(ROWS_PER_BLOCK)) < rows;)
            {
                for (int row = begin; row < std::min(rows, begin + ROWS_PER_BLOCK); ++row)
                    fn(row);
            }
        };
        std::vector<std::thread> pool;
        for (unsigned int i = 1; i < workers; ++i)
            pool.emplace_back(run);
        run();
        for (auto &t : pool)
            t.join();
#endif
    }

    float island_factor(const int x, const int z, const int side, const float edge, const float dropoff)
    {
        const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(side) - 0.5f;
        const float nz = (static_cast<float>(z) + 0.5f) / static_cast<float>(side) - 0.5f;
        const float distance = 2.0f * std::sqrt(nx * nx + nz * nz);
        return std::clamp(1.0f - edge * std::pow(distance, dropoff), 0.0f, 1.0f);
    }

    std::vector<std::uint8_t> quantize(const std::vector<float> &data)
    {
        std::vector<std::uint8_t> bytes(data.size());
        std::ranges::transform(data, bytes.begin(), [](const float v)
                               { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); });
        return bytes;
    }

    struct maze_obj
    {
        unsigned int rows{};
        unsigned int columns{};
        std::vector<std::uint8_t> walls;
    };

    std::optional<maze_obj> parse_maze_obj(const std::string_view obj)
    {
        std::istringstream input{std::string{obj}};
        std::string line;
        unsigned int rows = 0;
        unsigned int columns = 0;
        std::vector<std::array<float, 3>> vertices;

        while (std::getline(input, line))
        {
            std::istringstream fields{line};
            std::string type;
            fields >> type;
            if (type == "#")
            {
                std::string key;
                while (fields >> key)
                {
                    if (key.rfind("rows=", 0) == 0)
                        rows = static_cast<unsigned int>(std::stoul(key.substr(5)));
                    else if (key.rfind("columns=", 0) == 0)
                        columns = static_cast<unsigned int>(std::stoul(key.substr(8)));
                }
            }
            else if (type == "v")
            {
                std::array<float, 3> vertex{};
                if (!(fields >> vertex[0] >> vertex[1] >> vertex[2]))
                    return std::nullopt;
                vertices.push_back(vertex);
            }
        }

        if (rows == 0 || columns == 0 || vertices.empty() || vertices.size() % 8 != 0)
            return std::nullopt;

        const int width = static_cast<int>(columns) * 2 + 1;
        const int height = static_cast<int>(rows) * 2 + 1;
        maze_obj result{rows, columns, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u)};
        const auto mark = [&](const int x, const int y)
        {
            if (x >= 0 && x < width && y >= 0 && y < height)
                result.walls[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)] = 255u;
        };

        // MazeBuilder emits one eight-vertex box for every wall. The box's
        // centre identifies the grid line, while its long axis identifies
        // whether that line is horizontal or vertical.
        for (std::size_t offset = 0; offset < vertices.size(); offset += 8)
        {
            float min_x = vertices[offset][0], max_x = min_x;
            float min_z = vertices[offset][2], max_z = min_z;
            for (std::size_t index = offset + 1; index < offset + 8; ++index)
            {
                min_x = std::min(min_x, vertices[index][0]);
                max_x = std::max(max_x, vertices[index][0]);
                min_z = std::min(min_z, vertices[index][2]);
                max_z = std::max(max_z, vertices[index][2]);
            }

            const float centre_x = (min_x + max_x) * 0.5f;
            const float centre_z = (min_z + max_z) * 0.5f;
            if (max_x - min_x > max_z - min_z)
            {
                const int y = static_cast<int>(std::lround(centre_z * 2.0f));
                const int first_x = static_cast<int>(std::lround(min_x * 2.0f));
                const int last_x = static_cast<int>(std::lround(max_x * 2.0f));
                for (int x = first_x; x <= last_x; ++x)
                    mark(x, y);
            }
            else
            {
                const int x = static_cast<int>(std::lround(centre_x * 2.0f));
                const int first_y = static_cast<int>(std::lround(min_z * 2.0f));
                const int last_y = static_cast<int>(std::lround(max_z * 2.0f));
                for (int y = first_y; y <= last_y; ++y)
                    mark(x, y);
            }
        }

        return result;
    }
}

float heightmap::elevation(const int x, const int z) const noexcept
{
    return contains(x, z) ? data[static_cast<std::size_t>(z) * static_cast<std::size_t>(side) + static_cast<std::size_t>(x)] : 0.0f;
}

int heightmap::column_height(const int x, const int z) const noexcept
{
    return 1 + static_cast<int>(elevation(x, z) * static_cast<float>(max_height - 1));
}

const heightmap_material &heightmap::material_for(const float elevation) noexcept
{
    for (const auto &material : HEIGHTMAP_MATERIALS)
    {
        if (elevation < material.max_elevation)
            return material;
    }
    return HEIGHTMAP_MATERIALS.back();
}

std::filesystem::path write_maze_image(const mazes::configurator &config, const int side)
{
    namespace fs = std::filesystem;
    const auto app = mazes::runtime_app::instance();
    if (!app)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze image: MazeBuilder runtime is unavailable\n");
        return {};
    }

    thread_local mazes::randomizer rng{};
    const std::string request = "--rows=" + std::to_string(config.rows()) +
                                " --columns=" + std::to_string(config.columns()) +
                                " --levels=1 --algo=" + std::string{mazes::to_sv_from_algo(config.algo_id())} +
                                " --seed=" + std::to_string(rng.get_int(0, 1'000)) +
                                " --output=obj";

    // Reconstruct the wall raster from MazeBuilder's Wavefront OBJ output.
    const auto topo = parse_maze_obj(app->apply(request));
    if (!topo)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze image: failed to generate maze layout\n");
        return {};
    }

    const int width = static_cast<int>(topo->columns) * 2 + 1;
    const int height = static_cast<int>(topo->rows) * 2 + 1;
    auto walls = topo->walls;
    const auto open = [&](const int x, const int y)
    { walls[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 0u; };
    for (unsigned int row = 0; row < topo->rows; ++row)
    {
        for (unsigned int col = 0; col < topo->columns; ++col)
        {
            const int x = static_cast<int>(col) * 2 + 1;
            const int y = static_cast<int>(row) * 2 + 1;
            open(x, y);
        }
    }

    std::error_code ec;
    const auto directory = fs::temp_directory_path(ec) / "breakingwalls_maze";
    if (ec || (fs::create_directories(directory, ec), ec))
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze image: temp directory unavailable: %s\n", ec.message().c_str());
        return {};
    }
    const auto path = directory / ("maze_" + std::to_string(rng.get_int(0, 1'000)) + ".pgm");
    std::ofstream out{path, std::ios::binary};
    if (!out)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Maze image: cannot write %s\n", path.string().c_str());
        return {};
    }

    // Nearest-neighbour upscale to the heightmap resolution.
    out << "P5\n"
        << side << ' ' << side << "\n255\n";
    std::vector<char> row_bytes(static_cast<std::size_t>(side));
    for (int py = 0; py < side; ++py)
    {
        const int sy = py * height / side;
        for (int px = 0; px < side; ++px)
        {
            const int sx = px * width / side;
            row_bytes[static_cast<std::size_t>(px)] =
                static_cast<char>(walls[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)]);
        }
        out.write(row_bytes.data(), static_cast<std::streamsize>(row_bytes.size()));
    }
    return out ? path : std::filesystem::path{};
}

std::vector<float> decode_maze_image(const std::filesystem::path &image, const int side)
{
    std::ifstream in{image, std::ios::binary};
    std::string magic;
    int width = 0, height = 0, max_value = 0;
    if (!(in >> magic >> width >> height >> max_value) || magic != "P5" || width != side || height != side || max_value != 255)
        return {};
    in.get(); // single whitespace after the header

    const auto count = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
    std::vector<std::uint8_t> pixels(count);
    if (!in.read(reinterpret_cast<char *>(pixels.data()), static_cast<std::streamsize>(count)))
        return {};

    std::vector<float> target(count), h(count, 0.0f), grad(count);
    std::ranges::transform(pixels, target.begin(), [](const std::uint8_t p)
                           { return static_cast<float>(p) / 255.0f; });

    // Step size stays below 2 / (1 + 8 * lambda), the stability limit of this explicit scheme.
    constexpr float lambda = 0.5f, step = 0.2f;
    constexpr int iterations = 60;
    const auto at = [&](const std::vector<float> &v, const int x, const int y)
    {
        return v[static_cast<std::size_t>(std::clamp(y, 0, side - 1)) * static_cast<std::size_t>(side) +
                 static_cast<std::size_t>(std::clamp(x, 0, side - 1))];
    };
    for (int it = 0; it < iterations; ++it)
    {
        parallel_rows(side, [&](const int y)
                      {
            for (int x = 0; x < side; ++x)
            {
                const float laplacian = at(h, x - 1, y) + at(h, x + 1, y) + at(h, x, y - 1) + at(h, x, y + 1) - 4.0f * at(h, x, y);
                grad[static_cast<std::size_t>(y) * static_cast<std::size_t>(side) + static_cast<std::size_t>(x)] =
                    (at(h, x, y) - at(target, x, y)) - lambda * laplacian;
            } });
        for (std::size_t i = 0; i < count; ++i)
            h[i] = std::clamp(h[i] - step * grad[i], 0.0f, 1.0f);
    }
    return h;
}

heightmap generate_heightmap(const heightmap_params &params)
{
    heightmap map;
    map.side = std::clamp(params.side, heightmap::MIN_SIDE, heightmap::MAX_SIDE);
    map.max_height = std::clamp(params.max_height, 8, 120);
    map.name = std::string{params.apply_maze ? "Maze " : "Island "} + std::to_string(params.seed);
    const auto side = static_cast<std::size_t>(map.side);
    map.data.assign(side * side, 0.0f);

    std::vector<float> decoded;
    if (params.apply_maze)
    {
        const auto image = write_maze_image(params.maze, map.side);
        if (!image.empty())
            decoded = decode_maze_image(image, map.side);
        if (decoded.empty())
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Maze heightmap unavailable; using noise instead\n");
    }

    const float offset = static_cast<float>(params.seed) * 37.13f;
    parallel_rows(map.side, [&](const int z)
                  {
        for (int x = 0; x < map.side; ++x)
        {
            const auto index = static_cast<std::size_t>(z) * side + static_cast<std::size_t>(x);
            float elevation;
            if (!decoded.empty())
            {
                elevation = decoded[index];
            }
            else
            {
                const float nx = static_cast<float>(x) / static_cast<float>(map.side) - 0.5f;
                const float nz = static_cast<float>(z) / static_cast<float>(map.side) - 0.5f;
                elevation = simplex2(nx * params.frequency + offset, nz * params.frequency + offset,
                                     std::clamp(params.octaves, 1, 8), 0.5f, 2.0f);
            }
            map.data[index] = std::clamp(elevation * island_factor(x, z, map.side, params.edge_factor, params.edge_dropoff), 0.0f, 1.0f);
        } });
    return map;
}

struct heightmap_bvh::impl
{
    using Node = bvh::v2::Node<float, 3>;
    using Bvh = bvh::v2::Bvh<Node>;
    using Vec3 = bvh::v2::Vec<float, 3>;
    using BBox = bvh::v2::BBox<float, 3>;
    using Ray = bvh::v2::Ray<float, 3>;

    Bvh bvh;
    std::vector<BBox> boxes; // permuted into BVH primitive order
    std::vector<std::pair<int, int>> columns;
};

heightmap_bvh::heightmap_bvh() = default;
heightmap_bvh::~heightmap_bvh() = default;
heightmap_bvh::heightmap_bvh(heightmap_bvh &&) noexcept = default;
heightmap_bvh &heightmap_bvh::operator=(heightmap_bvh &&) noexcept = default;

bool heightmap_bvh::empty() const noexcept
{
    return !m_impl || m_impl->boxes.empty();
}

void heightmap_bvh::build(const heightmap &map, bvh::v2::ThreadPool &pool)
{
    using Vec3 = impl::Vec3;
    const auto side = static_cast<std::size_t>(map.side);
    const std::size_t count = side * side;
    if (count == 0)
    {
        m_impl.reset();
        return;
    }

    std::vector<impl::BBox> bboxes(count);
    std::vector<Vec3> centers(count);
    bvh::v2::ParallelExecutor executor(pool);
    executor.for_each(0, count, [&](std::size_t begin, std::size_t end)
                      {
        for (std::size_t i = begin; i < end; ++i)
        {
            const int x = static_cast<int>(i % side), z = static_cast<int>(i / side);
            const auto fx = static_cast<float>(x), fz = static_cast<float>(z);
            bboxes[i] = impl::BBox{Vec3{fx, 0.0f, fz}, Vec3{fx + 1.0f, static_cast<float>(map.column_height(x, z)), fz + 1.0f}};
            centers[i] = bboxes[i].get_center();
        } });

    using Builder = bvh::v2::DefaultBuilder<impl::Node>;
    Builder::Config config;
    config.quality = Builder::Quality::Medium;

    auto next = std::make_unique<impl>();
    next->bvh = Builder::build(pool, bboxes, centers, config);

    // Permute so the leaf callback reads primitives sequentially.
    next->boxes.resize(count);
    next->columns.resize(count);
    executor.for_each(0, count, [&](std::size_t begin, std::size_t end)
                      {
        for (std::size_t i = begin; i < end; ++i)
        {
            const std::size_t j = next->bvh.prim_ids[i];
            next->boxes[i] = bboxes[j];
            next->columns[i] = {static_cast<int>(j % side), static_cast<int>(j / side)};
        } });
    m_impl = std::move(next);
}

std::optional<heightmap_bvh::hit> heightmap_bvh::raycast(const float origin[3], const float dir[3],
                                                         const float max_distance) const
{
    if (empty())
        return std::nullopt;
    using Vec3 = impl::Vec3;
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 0.0f)
        return std::nullopt;

    impl::Ray ray{Vec3{origin[0], origin[1], origin[2]}, Vec3{dir[0] / len, dir[1] / len, dir[2] / len}, 0.0f, max_distance};
    const auto inv = ray.get_inv_dir<true>();

    constexpr std::size_t none = static_cast<std::size_t>(-1);
    std::size_t best = none;
    bvh::v2::SmallStack<impl::Bvh::Index, 64> stack;
    m_impl->bvh.intersect<false, false>(ray, m_impl->bvh.get_root().index, stack,
                                        [&](std::size_t begin, std::size_t end)
                                        {
        for (std::size_t i = begin; i < end; ++i)
        {
            // Slab test against the column box; shrinking tmax keeps only the closest hit.
            const auto &b = m_impl->boxes[i];
            float t0 = ray.tmin, t1 = ray.tmax;
            for (std::size_t a = 0; a < 3; ++a)
            {
                float n = (b.min[a] - ray.org[a]) * inv[a];
                float f = (b.max[a] - ray.org[a]) * inv[a];
                if (n > f)
                    std::swap(n, f);
                t0 = std::max(t0, n);
                t1 = std::min(t1, f);
            }
            if (t0 <= t1)
            {
                ray.tmax = t0;
                best = i;
            }
        }
        return best != none; });

    if (best == none)
        return std::nullopt;
    hit h;
    h.x = m_impl->columns[best].first;
    h.z = m_impl->columns[best].second;
    h.t = ray.tmax;
    for (int a = 0; a < 3; ++a)
        h.point[a] = origin[a] + ray.dir[a] * ray.tmax;
    return h;
}

heightmap_library &heightmap_library::instance()
{
    static heightmap_library library;
    return library;
}

void heightmap_library::ensure_loaded()
{
    if (m_loaded)
        return;
    m_loaded = true;
    for (auto &record : db::load_heightmaps())
    {
        auto map = std::make_shared<heightmap>();
        map->name = std::move(record.name);
        map->side = record.side;
        map->max_height = record.max_height;
        map->data.resize(record.data.size());
        std::ranges::transform(record.data, map->data.begin(), [](const std::uint8_t b)
                               { return static_cast<float>(b) / 255.0f; });
        m_entries.push_back({record.id, std::move(map)});
    }
    m_selected = m_entries.empty() ? 0 : m_entries.size() - 1;
}

std::shared_ptr<const heightmap> heightmap_library::add(const heightmap_params &params)
{
    ensure_loaded();
    auto map = std::make_shared<heightmap>(generate_heightmap(params));
    const int id = db::save_heightmap(map->name, map->side, map->max_height, quantize(map->data));
    m_entries.push_back({id, map});
    m_selected = m_entries.size() - 1;
    return map;
}

void heightmap_library::select(const std::size_t index)
{
    if (index < m_entries.size())
        m_selected = index;
}

void heightmap_library::remove(const std::size_t index)
{
    if (index >= m_entries.size())
        return;
    db::delete_heightmap(m_entries[index].id);
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_selected > index || m_selected >= m_entries.size())
        m_selected = m_selected > 0 ? m_selected - 1 : 0;
}

std::shared_ptr<const heightmap> heightmap_library::current() const noexcept
{
    return m_selected < m_entries.size() ? m_entries[m_selected].map : nullptr;
}

std::deque<std::shared_ptr<const heightmap>> heightmap_library::make_play_queue(const std::size_t count)
{
    ensure_loaded();
    std::deque<std::shared_ptr<const heightmap>> queue;
    for (const auto &entry : m_entries)
        queue.push_back(entry.map);
    if (!queue.empty())
        return queue;

    std::random_device rd;
    for (std::size_t i = 0; i < std::max<std::size_t>(count, 1); ++i)
    {
        heightmap_params params;
        params.seed = static_cast<int>(rd() % 100000u);
        queue.push_back(std::make_shared<heightmap>(generate_heightmap(params)));
    }
    return queue;
}
