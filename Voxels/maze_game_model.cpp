#include "maze_game_model.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

maze_game_model::maze_game_model(const std::uint32_t seed, const bool stacked, const std::size_t side)
    : m_rng(seed), m_initial_side(side), m_stacked(stacked)
{
    if (side < 4 || side > MAX_SIDE)
        throw std::invalid_argument("Maze board side must be between 4 and 40");
    reset();
}

void maze_game_model::reset()
{
    m_side = m_initial_side;
    m_cells.assign(m_side * m_side, cell_kind::empty);
    m_cells.front() = cell_kind::start;
    m_cells.back() = cell_kind::goal;
    m_growth_progress = 0;
    for (auto &s : m_strokes)
        s = stroke{};
    m_history.clear();
    for (auto &preview : m_previews)
        preview.clear();

    std::vector<std::size_t> route;
    std::size_t row = 0, column = 0;
    while (row + 1 < m_side || column + 1 < m_side)
    {
        if (column + 1 == m_side || (row + 1 < m_side && m_rng() % 2 == 0))
            ++row;
        else
            ++column;
        if (row * m_side + column != m_cells.size() - 1)
            route.push_back(row * m_side + column);
    }
    std::shuffle(route.begin(), route.end(), m_rng);
    for (std::size_t i = 0; i < 4; ++i)
        m_cells[route[i]] = cell_kind::hidden_good;
    for (int i = 0; i < 5; ++i)
        spawn(cell_kind::hidden_bad);
    spawn(cell_kind::start);
    if (m_stacked)
        for (auto &preview : m_previews)
            preview = maze_game_model(m_rng(), false, m_side).cells();
    m_message = "Hold a blue square and trace toward green. Explore for hidden rewards!";
}

void maze_game_model::prepare_next_level()
{
    for (std::size_t i = 1; i < m_previews.size(); ++i)
        m_previews[i - 1] = std::move(m_previews[i]);
    m_previews.back() = maze_game_model(m_rng(), false, m_side).cells();
}

void maze_game_model::descend(const std::size_t cell, const owner who)
{
    std::set<edge> lines;
    for (auto &s : m_strokes)
    {
        lines.insert(s.saved_edges.begin(), s.saved_edges.end());
        lines.insert(s.active_edges.begin(), s.active_edges.end());
        s.saved_edges.clear();
        s.active_edges.clear();
        s.lights = 0;
        s.run_good = s.run_bad = 0;
        // The previous level's board is gone, so another owner's held stroke ends here.
        if (&s != &stroke_of(who))
        {
            s.drawing = false;
            s.path.clear();
        }
    }
    m_history.push_back({m_cells, std::move(lines)});
    m_cells = std::move(m_previews.front());
    // Keep the held pointer on the same grid coordinate, even if the preview had a goal there.
    const bool replaced_goal = m_cells[cell] == cell_kind::goal;
    m_cells[cell] = cell_kind::start;
    if (replaced_goal)
        spawn(cell_kind::goal);
    stroke_of(who).path = {cell};
    prepare_next_level();
    m_message = "Level " + std::to_string(level()) + " unlocked! Keep holding toward the next green.";
}

std::optional<std::size_t> maze_game_model::spawn(const cell_kind kind)
{
    std::vector<std::size_t> free_cells;
    for (std::size_t i = 0; i < m_cells.size(); ++i)
        if (m_cells[i] == cell_kind::empty)
            free_cells.push_back(i);
    if (free_cells.empty())
        return std::nullopt;
    const auto cell = free_cells[std::uniform_int_distribution<std::size_t>(0, free_cells.size() - 1)(m_rng)];
    m_cells[cell] = kind;
    return cell;
}

bool maze_game_model::any_drawing() const noexcept
{
    return std::any_of(m_strokes.begin(), m_strokes.end(), [](const stroke &s) { return s.drawing; });
}

bool maze_game_model::begin(const std::size_t cell, const owner who)
{
    auto &s = stroke_of(who);
    if (s.drawing || cell >= m_cells.size() || m_cells[cell] != cell_kind::start)
        return false;
    s.drawing = true;
    s.path = {cell};
    s.active_edges.clear();
    s.run_good = s.run_bad = 0;
    s.reached_goal = false;
    m_message = "Keep holding: blue +5, red -2, green +10.";
    return true;
}

void maze_game_model::trace(const std::size_t cell, const owner who)
{
    auto &s = stroke_of(who);
    if (!s.drawing || cell >= m_cells.size() || s.path.empty() || cell == s.path.back())
        return;

    int x = static_cast<int>(s.path.back() % m_side);
    int y = static_cast<int>(s.path.back() / m_side);
    const int target_x = static_cast<int>(cell % m_side);
    const int target_y = static_cast<int>(cell / m_side);
    const int dx = std::abs(target_x - x), dy = std::abs(target_y - y);
    const int sx = x < target_x ? 1 : -1, sy = y < target_y ? 1 : -1;
    int ix = 0, iy = 0;
    // Visit crossed cells even when pointer events skip over several squares.
    while (ix < dx || iy < dy)
    {
        if (ix < dx && (iy == dy || (1 + 2 * ix) * dy < (1 + 2 * iy) * dx))
        {
            x += sx;
            ++ix;
        }
        else
        {
            y += sy;
            ++iy;
        }
        const auto crossed = static_cast<std::size_t>(y) * m_side + static_cast<std::size_t>(x);
        const auto previous = s.path.back();
        s.active_edges.emplace(std::min(previous, crossed), std::max(previous, crossed));
        const auto existing = std::find(s.path.begin(), s.path.end(), crossed);
        if (existing == s.path.end())
            s.path.push_back(crossed);
        else
            s.path.erase(existing + 1, s.path.end());
        enter(crossed, who);
    }
}

void maze_game_model::enter(const std::size_t cell, const owner who)
{
    auto &s = stroke_of(who);
    switch (m_cells[cell])
    {
    case cell_kind::hidden_good:
        m_cells[cell] = cell_kind::good;
        ++s.run_good;
        ++s.streak;
        award(GOOD_REWARD, who);
        m_message = "Hidden blue found: +5! Keep tracing.";
        break;
    case cell_kind::hidden_bad:
        m_cells[cell] = cell_kind::bad;
        s.streak = 0;
        ++s.run_bad;
        award(-BAD_PENALTY, who);
        m_message = "Red revealed: -2. Avoid it on your next run.";
        break;
    case cell_kind::bad:
        ++s.run_bad;
        s.streak = 0;
        break;
    case cell_kind::goal:
        m_cells[cell] = cell_kind::completed;
        s.reached_goal = true;
        award(GOAL_REWARD, who);
        if (m_stacked && ++s.lights == LIGHTS_PER_LEVEL)
        {
            descend(cell, who);
            break;
        }
        // Reserve the next goal before adding optional rewards and anchors.
        if (!spawn(cell_kind::goal))
        {
            auto next = std::find_if(m_cells.begin(), m_cells.end(), [this, cell](const cell_kind &kind)
            {
                return &kind != &m_cells[cell] && kind != cell_kind::start;
            });
            if (next != m_cells.end())
                *next = cell_kind::goal;
        }
        spawn(cell_kind::start);
        spawn(cell_kind::hidden_good);
        m_message = "Green reached: +10! Chase the next green, or release for a clean-run bonus.";
        break;
    default:
        break;
    }
}

void maze_game_model::award(const int points, const owner who)
{
    stroke_of(who).score += points;
    if (!m_stacked)
        m_growth_progress += points;
}

void maze_game_model::grow()
{
    if (m_stacked || any_drawing())
        return;
    // Expand between strokes so cell coordinates cannot change under a held pointer.
    while (std::abs(m_growth_progress) >= GROWTH_INTERVAL && m_side < MAX_SIDE)
    {
        m_growth_progress += m_growth_progress > 0 ? -GROWTH_INTERVAL : GROWTH_INTERVAL;
        const auto new_side = std::min(MAX_SIDE, m_side + 2);
        std::vector<cell_kind> expanded(new_side * new_side, cell_kind::empty);
        for (std::size_t row = 0; row < m_side; ++row)
            std::copy_n(m_cells.begin() + row * m_side, m_side, expanded.begin() + row * new_side);
        m_cells = std::move(expanded);
        const auto remap = [this, new_side](const std::size_t cell)
        {
            return (cell / m_side) * new_side + cell % m_side;
        };
        for (auto &s : m_strokes)
        {
            std::set<edge> remapped;
            for (const auto &[from, to] : s.saved_edges)
                remapped.emplace(remap(from), remap(to));
            s.saved_edges = std::move(remapped);
        }
        m_side = new_side;
        spawn(cell_kind::hidden_good);
        spawn(cell_kind::hidden_good);
        spawn(cell_kind::hidden_bad);
        m_message = "Board expanded! New hidden rewards and hazards are waiting.";
    }
    if (m_side == MAX_SIDE)
        m_growth_progress %= GROWTH_INTERVAL;
}

void maze_game_model::finish(const bool allow_bonus, const owner who)
{
    auto &s = stroke_of(who);
    if (!s.drawing)
        return;
    if (allow_bonus)
        s.saved_edges.insert(s.active_edges.begin(), s.active_edges.end());
    if (allow_bonus && s.reached_goal && s.run_bad == 0 && s.run_good > 0)
    {
        const int bonus = std::min(s.streak, 10);
        award(bonus, who);
        m_message = "Line saved! Clean run: +" + std::to_string(bonus) + " streak bonus. Resume from any S.";
    }
    else
        m_message = allow_bonus ? "Line saved. Take a break, then resume from any S/blue cell."
                               : "Trace canceled. Earlier saved lines and earned points are kept.";
    s.drawing = false;
    s.path.clear();
    s.active_edges.clear();
    grow();
}

void maze_game_model::release(const owner who) { finish(true, who); }
void maze_game_model::cancel(const owner who) { finish(false, who); }
