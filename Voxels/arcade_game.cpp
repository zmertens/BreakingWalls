#include "arcade_game.h"

#include "heightmap.h"

#include "item.h"

#include <MazeBuilder/cell.h>
#include <MazeBuilder/lab.h>
#include <MazeBuilder/masked_grid.h>
#include <MazeBuilder/randomizer.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <stack>
#include <unordered_set>

namespace
{
    constexpr std::string_view BASE36 = "0123456789abcdefghijklmnopqrstuvwxyz";

    std::string make_mask_text(int cells, float density, mazes::randomizer &rng)
    {
        std::string text;
        text.reserve(static_cast<std::size_t>(cells) * (cells + 1));
        const int threshold = static_cast<int>(density * 1000.0f);
        for (int r = 0; r < cells; ++r)
        {
            for (int c = 0; c < cells; ++c)
            {
                const bool corner = (r == 0 && c == 0) || (r == cells - 1 && c == cells - 1);
                text += (!corner && rng(0, 999) < threshold) ? 'X' : '.';
            }
            text += '\n';
        }
        return text;
    }

    // Returns the expanded layout, or an empty string if the goal is unreachable.
    std::string carve(const std::string &mask_text, int cells, mazes::randomizer &rng)
    {
        const auto mask = mazes::mask::from_string(mask_text);
        mazes::masked_grid grid(mask);
        auto &ops = grid.operations();
        const auto cols = mask.columns();

        auto start = ops.search(0);
        if (!start)
        {
            return {};
        }

        std::stack<std::shared_ptr<mazes::cell>> stack;
        std::unordered_set<int> visited;
        stack.push(start);
        visited.insert(start->get_index());
        mazes::grid_operations::neighbor_buffer buffer;
        while (!stack.empty())
        {
            auto current = stack.top();
            std::vector<std::shared_ptr<mazes::cell>> unvisited;
            const auto count = ops.collect_neighbors(current, buffer);
            for (const auto &n : std::span(buffer).first(count))
            {
                if (n && mask(n->get_index() / cols, n->get_index() % cols) && !visited.contains(n->get_index()))
                {
                    unvisited.push_back(n);
                }
            }
            if (unvisited.empty())
            {
                stack.pop();
                continue;
            }
            const auto chosen = unvisited[rng(0, static_cast<int>(unvisited.size()) - 1)];
            mazes::lab::link(current, chosen, true);
            visited.insert(chosen->get_index());
            stack.push(chosen);
        }

        if (!visited.contains(cells * cells - 1))
        {
            return {};
        }

        const int size = 2 * cells + 1;
        std::string layout(static_cast<std::size_t>(size) * (size + 1), arcade_level::WALL);
        for (int r = 0; r < size; ++r)
        {
            layout[static_cast<std::size_t>(r) * (size + 1) + size] = '\n';
        }
        auto open = [&](int r, int c)
        { layout[static_cast<std::size_t>(r) * (size + 1) + c] = arcade_level::OPEN; };

        for (int r = 0; r < cells; ++r)
        {
            for (int c = 0; c < cells; ++c)
            {
                const int index = r * cells + c;
                if (!visited.contains(index))
                {
                    continue;
                }
                const auto cp = ops.search(index);
                open(2 * r + 1, 2 * c + 1);
                if (const auto e = ops.get_east(cp); e && cp->is_linked(e))
                {
                    open(2 * r + 1, 2 * c + 2);
                }
                if (const auto s = ops.get_south(cp); s && cp->is_linked(s))
                {
                    open(2 * r + 2, 2 * c + 1);
                }
            }
        }
        return layout;
    }
} // namespace

std::shared_ptr<const arcade_level> arcade_level::build(const int cells, const float blocker_density,
                                                        mazes::randomizer &rng, const int origin_x,
                                                        const int origin_y, const int origin_z,
                                                        std::shared_ptr<const heightmap> terrain)
{
    const int n = std::max(2, cells);
    std::string layout;
    float density = blocker_density;
    for (int attempt = 0; attempt < 12 && layout.empty(); ++attempt)
    {
        try
        {
            layout = carve(make_mask_text(n, density, rng), n, rng);
        }
        catch (const std::exception &)
        {
            layout.clear();
        }
        density *= 0.8f; // unreachable goal: relax the blockers and try again
    }
    if (layout.empty())
    {
        layout = carve(make_mask_text(n, 0.0f, rng), n, rng);
    }

    std::shared_ptr<arcade_level> level(new arcade_level());
    level->m_rows = level->m_cols = 2 * n + 1;
    level->m_ox = origin_x;
    level->m_oy = origin_y;
    level->m_oz = origin_z;
    level->m_skirt = terrain ? SKIRT : 0;
    level->m_terrain = std::move(terrain);
    level->m_text = std::move(layout);

    // Breadth first distances from the start, used by worker agents to value cells.
    level->m_dist.assign(static_cast<std::size_t>(level->m_rows) * level->m_cols, -1);
    std::deque<std::pair<int, int>> queue;
    auto dist = [&](int r, int c) -> int &
    { return level->m_dist[static_cast<std::size_t>(r) * level->m_cols + c]; };
    dist(1, 1) = 0;
    queue.emplace_back(1, 1);
    while (!queue.empty())
    {
        const auto [r, c] = queue.front();
        queue.pop_front();
        level->m_max_distance = std::max(level->m_max_distance, dist(r, c));
        for (const auto [dr, dc] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
        {
            const int nr = r + dr, nc = c + dc;
            if (level->in_bounds(nr, nc) && !level->is_wall(nr, nc) && dist(nr, nc) < 0)
            {
                dist(nr, nc) = dist(r, c) + 1;
                queue.emplace_back(nr, nc);
            }
        }
    }

    for (int r = 0; r < n; ++r)
    {
        for (int c = 0; c < n; ++c)
        {
            if (level->is_wall(2 * r + 1, 2 * c + 1))
            {
                ++level->m_blockers;
            }
        }
    }
    return level;
}

char arcade_level::text_at(const int r, const int c) const noexcept
{
    return in_bounds(r, c) ? m_text[index_of(r, c)] : WALL;
}

int arcade_level::distance_at(const int r, const int c) const noexcept
{
    return in_bounds(r, c) ? m_dist[static_cast<std::size_t>(r) * m_cols + c] : -1;
}

int arcade_level::terrain_height(const int x, const int z) const noexcept
{
    return m_terrain && m_terrain->contains(x, z) ? m_terrain->column_height(x, z) : 0;
}

int arcade_level::terrain_block_at(const int x, const int y, const int z) const noexcept
{
    const int h = terrain_height(x, z);
    if (y < 0 || y >= h)
    {
        return 0;
    }
    return y == h - 1 ? heightmap::material_for(m_terrain->elevation(x, z)).block : UNDERFILL_BLOCK;
}

int arcade_level::top_y(const int x, const int z) const noexcept
{
    return std::min(250, std::max(m_oy + CLEAR_HEIGHT, terrain_height(x, z) + TREE_CLEAR));
}

int arcade_level::block_at(const int x, const int y, const int z) const noexcept
{
    const int c = x - m_ox, r = z - m_oz;
    const int th = terrain_height(x, z);
    if (in_bounds(r, c))
    {
        const int dy = y - m_oy;
        if (dy < 0)
        {
            // Raise terrain that sits below the pad.
            return (m_terrain && y >= th) ? UNDERFILL_BLOCK : -1;
        }
        if (dy == 0)
        {
            return FLOOR_BLOCK;
        }
        if (dy <= WALL_HEIGHT)
        {
            return is_wall(r, c) ? WALL_BLOCK : 0;
        }
        if (dy <= CLEAR_HEIGHT)
        {
            return 0;
        }
        return (m_terrain && y < th + TREE_CLEAR) ? 0 : -1;
    }
    if (m_skirt == 0)
    {
        return -1;
    }

    // Outside the maze the surface eases from the pad height back to the natural terrain.
    const int ring = std::max({-c, c - (m_cols - 1), -r, r - (m_rows - 1)});
    if (ring < 1 || ring > m_skirt)
    {
        return -1;
    }
    const float w = static_cast<float>(ring) / static_cast<float>(m_skirt + 1);
    const int target = static_cast<int>(std::lround(static_cast<float>(m_oy + 1) * (1.0f - w) + static_cast<float>(th) * w));
    if (y < target)
    {
        if (y < th)
        {
            return -1;
        }
        return y == target - 1 && th > 0 ? heightmap::material_for(m_terrain->elevation(x, z)).block : UNDERFILL_BLOCK;
    }
    return y < th ? 0 : -1;
}

std::string arcade_level::stringify(const arcade_level &level)
{
    return std::string{level.m_text};
}

int arcade_game::cells_for_level(const int level) const noexcept
{
    return std::min(rules.max_cells, rules.base_cells + (std::max(1, level) - 1) / 2);
}

void arcade_game::start_level(const int level, mazes::randomizer &rng, const int origin_x, const int origin_y,
                              const int origin_z, std::shared_ptr<const heightmap> terrain)
{
    m_level_number = std::max(1, level);
    const int cells = cells_for_level(m_level_number);
    const float density = std::min(rules.max_blocker_density, rules.blocker_per_level * (m_level_number - 1));
    m_level = arcade_level::build(cells, density, rng, origin_x, origin_y, origin_z, std::move(terrain));

    m_broken.assign(static_cast<std::size_t>(m_level->rows()) * (m_level->columns() + 1), 0);
    m_pickups.clear();
    m_score = m_walls_broken = m_bonus = 0;
    m_elapsed = 0.0f;
    m_row = m_level->start_row();
    m_col = m_level->start_column();
    m_win_pending = false;

    for (int i = 0; i < AGENT_COUNT; ++i)
    {
        const int cell_row = (m_level->cells() * (2 * i + 1)) / (2 * AGENT_COUNT);
        const int r = 2 * cell_row + 1;
        int c = 1;
        while (c < m_level->columns() && m_level->is_wall(r, c))
        {
            ++c;
        }
        auto &agent = m_agents[i];
        agent.text_iter = m_level->text().begin() + m_level->index_of(r, c);
        agent.direction = (i % 2 == 0) ? 1 : -1;
        agent.timer = rules.agent_interval * static_cast<float>(i) / AGENT_COUNT;
    }
    m_state = arcade_state::RUNNING;
}

void arcade_game::stop() noexcept
{
    m_state = arcade_state::IDLE;
    m_pickups.clear();
    m_level.reset();
}

void arcade_game::toggle_pause() noexcept
{
    if (m_state == arcade_state::RUNNING)
    {
        m_state = arcade_state::PAUSED;
    }
    else if (m_state == arcade_state::PAUSED)
    {
        m_state = arcade_state::RUNNING;
    }
}

void arcade_game::pause() noexcept
{
    if (m_state == arcade_state::RUNNING)
    {
        m_state = arcade_state::PAUSED;
    }
}

void arcade_game::resume() noexcept
{
    if (m_state == arcade_state::PAUSED)
    {
        m_state = arcade_state::RUNNING;
    }
}

float arcade_game::time_left() const noexcept
{
    return std::max(0.0f, rules.time_limit - m_elapsed);
}

bool arcade_game::is_open(const int r, const int c) const noexcept
{
    if (!m_level || !m_level->in_bounds(r, c))
    {
        return false;
    }
    return !m_level->is_wall(r, c) || m_broken[m_level->index_of(r, c)];
}

bool arcade_game::take_win() noexcept
{
    const bool pending = m_win_pending;
    m_win_pending = false;
    return pending;
}

void arcade_game::update(const float dt, mazes::randomizer &rng, std::vector<arcade_block_change> &changes)
{
    if (m_state != arcade_state::RUNNING || !m_level)
    {
        return;
    }
    m_elapsed += dt;
    for (auto &agent : m_agents)
    {
        agent.timer += dt;
        while (agent.timer >= rules.agent_interval)
        {
            agent.timer -= rules.agent_interval;
            step_agent(agent, rng, changes);
        }
    }
}

void arcade_game::step_agent(arcade_agent &agent, mazes::randomizer &rng, std::vector<arcade_block_change> &changes)
{
    const auto text = m_level->text();
    const auto next = agent.text_iter + agent.direction;
    if (next < text.begin() || next >= text.end() || *next != arcade_level::OPEN)
    {
        agent.direction = -agent.direction; // hit a wall: turn around
        return;
    }
    agent.text_iter = next;

    const auto index = static_cast<int>(agent.text_iter - text.begin());
    const int r = index / (m_level->columns() + 1);
    const int c = index % (m_level->columns() + 1);
    const bool special = (r == m_level->start_row() && c == m_level->start_column()) ||
                         (r == m_level->goal_row() && c == m_level->goal_column()) || (r == m_row && c == m_col);
    const bool taken = std::any_of(m_pickups.begin(), m_pickups.end(),
                                   [&](const arcade_pickup &p)
                                   { return p.row == r && p.column == c; });
    if (special || taken || static_cast<int>(m_pickups.size()) >= rules.max_pickups || rng(0, 99) >= 30)
    {
        return;
    }

    // The cell's distance is read as a base36 digit and folded into the item palette.
    const int value = static_cast<int>(BASE36.find(BASE36[std::max(0, m_level->distance_at(r, c)) % 36]));
    const auto agent_index = static_cast<int>(&agent - m_agents.data());
    const int item_index = (value + agent_index * 16) % item::TOTAL_ITEMS;
    const int block = std::max(1, item::ITEMS[item_index]);
    m_pickups.push_back({r, c, block});
    changes.push_back({m_level->world_x(c), m_level->origin_y() + 1, m_level->world_z(r), block});
}

arcade_event arcade_game::move_player(const float x, const float feet_y, const float z, const float vx,
                                      const float vz, std::vector<arcade_block_change> &changes)
{
    if (m_state != arcade_state::RUNNING || !m_level)
    {
        return arcade_event::NONE;
    }
    const float ox = static_cast<float>(m_level->origin_x()), oz = static_cast<float>(m_level->origin_z());
    m_col = std::clamp(static_cast<int>(std::lround(x - ox)), 0, m_level->columns() - 1);
    m_row = std::clamp(static_cast<int>(std::lround(z - oz)), 0, m_level->rows() - 1);

    constexpr float REACH = 0.45f; // player radius plus a little slack, so the break lands before the collision
    const float wall_top = static_cast<float>(m_level->origin_y() + arcade_level::WALL_HEIGHT) + 0.35f;
    auto event = arcade_event::NONE;
    if (feet_y < wall_top)
    {
        for (int r = m_row - 1; r <= m_row + 1; ++r)
        {
            for (int c = m_col - 1; c <= m_col + 1; ++c)
            {
                if (!m_level->in_bounds(r, c) || m_level->is_border(r, c) || is_open(r, c))
                {
                    continue;
                }
                const float cx = ox + static_cast<float>(c), cz = oz + static_cast<float>(r);
                const float gap_x = std::max(std::abs(x - cx) - 0.5f, 0.0f);
                const float gap_z = std::max(std::abs(z - cz) - 0.5f, 0.0f);
                const bool pushing = vx * (cx - x) + vz * (cz - z) > 0.05f;
                if (!pushing || gap_x * gap_x + gap_z * gap_z > REACH * REACH)
                {
                    continue;
                }
                m_broken[m_level->index_of(r, c)] = 1;
                m_score = std::max(0, m_score - rules.wall_penalty);
                ++m_walls_broken;
                for (int dy = 1; dy <= arcade_level::WALL_HEIGHT; ++dy)
                {
                    changes.push_back({m_level->world_x(c), m_level->origin_y() + dy, m_level->world_z(r), 0});
                }
                event = arcade_event::BROKE_WALL;
            }
        }
    }

    if (m_row == m_level->goal_row() && m_col == m_level->goal_column())
    {
        m_bonus = static_cast<int>(time_left()) * rules.time_bonus_per_second;
        m_score += m_bonus;
        m_state = arcade_state::WON;
        m_win_pending = true;
        return arcade_event::WON;
    }
    return event;
}

bool arcade_game::collect(const int block, std::vector<arcade_block_change> &changes)
{
    if (m_state != arcade_state::RUNNING || !m_level)
    {
        return false;
    }
    for (auto it = m_pickups.begin(); it != m_pickups.end(); ++it)
    {
        if (it->block == block && std::abs(it->row - m_row) <= 1 && std::abs(it->column - m_col) <= 1)
        {
            changes.push_back({m_level->world_x(it->column), m_level->origin_y() + 1, m_level->world_z(it->row), 0});
            m_pickups.erase(it);
            m_score += rules.block_points;
            return true;
        }
    }
    return false;
}
