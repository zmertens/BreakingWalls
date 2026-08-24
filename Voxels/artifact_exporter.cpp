// artifact_exporter.cpp — async OBJ export extracted from player.cpp
#include "artifact_exporter.h"

#include <future>
#include <sstream>

#include <SDL3/SDL.h>

std::string artifact_exporter::blocks_to_wavefront_obj(
    const std::vector<std::tuple<int, int, int, int>> &blocks) noexcept
{
    if (blocks.empty())
        return "";

    const auto start_time = SDL_GetTicks();

    std::ostringstream result;
    result << "# Maze Builder Export\n";
    result << "# Generated from database\n";
    result << "# Block count: " << blocks.size() << "\n\n";

    static constexpr float cube_vertices[8][3] = {
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},   {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f},    {-0.5f, 0.5f, 0.5f}
    };

    static constexpr int cube_faces[6][6] = {
        {4, 5, 6, 4, 6, 7}, // front  (+Z)
        {1, 0, 3, 1, 3, 2}, // back   (-Z)
        {3, 7, 6, 3, 6, 2}, // top    (+Y)
        {0, 1, 5, 0, 5, 4}, // bottom (-Y)
        {1, 2, 6, 1, 6, 5}, // right  (+X)
        {0, 4, 7, 0, 7, 3}  // left   (-X)
    };

    int vertex_count = 0, processed_blocks = 0;
    for (const auto &[x, y, z, w] : blocks)
    {
        if (w == 0) continue;
        if (++processed_blocks % 10000 == 0)
            SDL_Log("OBJ export progress: %d / %zu blocks\n", processed_blocks, blocks.size());

        for (int v = 0; v < 8; ++v)
        {
            result << "v " << (static_cast<float>(x) + cube_vertices[v][0])
                   << " "  << (static_cast<float>(y) + cube_vertices[v][1])
                   << " "  << (static_cast<float>(z) + cube_vertices[v][2]) << "\n";
        }
        for (int face = 0; face < 6; ++face)
        {
            const int base = vertex_count + 1;
            result << "f " << base + cube_faces[face][0] << " " << base + cube_faces[face][1] << " " << base + cube_faces[face][2] << "\n"
                   << "f " << base + cube_faces[face][3] << " " << base + cube_faces[face][4] << " " << base + cube_faces[face][5] << "\n";
        }
        vertex_count += 8;
    }

    SDL_Log("blocks_to_wavefront_obj: Generated OBJ with %d blocks in %lu ms\n",
            processed_blocks, SDL_GetTicks() - start_time);
    return result.str();
}

void artifact_exporter::start_async(std::vector<std::tuple<int, int, int, int>> blocks) noexcept
{
    m_cached.clear();
    m_future = std::async(std::launch::async,
                          [b = std::move(blocks)]() -> std::string
                          { return blocks_to_wavefront_obj(b); });
}

bool artifact_exporter::is_ready() const noexcept
{
    return m_future.valid() &&
           m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::string artifact_exporter::get_result() noexcept
{
    if (!is_ready()) return "";
    if (m_cached.empty())
    {
        m_cached = m_future.get();
        SDL_Log("Async export complete: %zu bytes\n", m_cached.size());
    }
    return m_cached;
}

bool artifact_exporter::is_running() const noexcept
{
    return m_future.valid() &&
           m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}
