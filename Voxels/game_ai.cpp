#include "game_ai.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

using owner = maze_game_model::owner;
using cell_kind = maze_game_model::cell_kind;

int game_ai::target_priority(const cell_kind kind) noexcept
{
    switch (kind)
    {
    case cell_kind::goal:
        return static_cast<int>(item::BlockType::GRASS);
    case cell_kind::hidden_good:
        return static_cast<int>(item::BlockType::COLOR_00);
    case cell_kind::good:
        return static_cast<int>(item::BlockType::COLOR_01);
    default:
        return -1;
    }
}

std::size_t game_ai::manhattan(const std::size_t a, const std::size_t b, const std::size_t side) noexcept
{
    return static_cast<std::size_t>(std::abs(static_cast<int>(a % side) - static_cast<int>(b % side)) +
                                    std::abs(static_cast<int>(a / side) - static_cast<int>(b / side)));
}

std::uint64_t game_ai::bit_for(const std::size_t cell) noexcept { return std::uint64_t{1} << (cell & 63u); }

void game_ai::mark_explored(const std::size_t cell)
{
    if ((cell >> 6) < m_explored_bits.size())
        m_explored_bits[cell >> 6] |= bit_for(cell);
}

bool game_ai::explored(const std::size_t cell) const noexcept
{
    return (cell >> 6) < m_explored_bits.size() && (m_explored_bits[cell >> 6] & bit_for(cell)) != 0u;
}

void game_ai::reset(const maze_game_model &board)
{
    m_cooldown = 0.0f;
    m_target = INVALID_CELL;
    m_steps = 0;
    m_side = board.side();
    m_level = board.level();
    m_explored_bits.assign((m_side * m_side + 63u) / 64u, 0ULL);
}

std::size_t game_ai::choose_start(const maze_game_model &board, const std::size_t target) const
{
    const auto &cells = board.cells();
    std::size_t best = INVALID_CELL, best_distance = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i] != cell_kind::start)
            continue;
        const auto distance = target == INVALID_CELL ? 0 : manhattan(i, target, board.side());
        if (distance < best_distance)
        {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

std::size_t game_ai::choose_target(const maze_game_model &board, std::mt19937 &rng) const
{
    const auto &cells = board.cells();
    const auto side = board.side();
    std::size_t best = INVALID_CELL;
    int best_score = std::numeric_limits<int>::max();
    std::uniform_int_distribution<int> jitter(0, 3);

    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        const auto priority = target_priority(cells[i]);
        if (priority < 0)
            continue;
        const auto start = choose_start(board, i);
        if (start == INVALID_CELL)
            return INVALID_CELL;
        // Goals matter most; already-visited squares are less interesting than unexplored ones.
        const int score = static_cast<int>(manhattan(start, i, side)) - (cells[i] == cell_kind::goal ? 6 : 0) +
                          (explored(i) && cells[i] != cell_kind::goal ? 3 : 0) + jitter(rng);
        if (score < best_score)
        {
            best = i;
            best_score = score;
        }
    }
    if (best != INVALID_CELL)
        return best;

    // Nothing known: probe a random unexplored square.
    std::vector<std::size_t> unknown;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (!explored(i) && cells[i] != cell_kind::bad)
            unknown.push_back(i);
    if (unknown.empty())
        return INVALID_CELL;
    return unknown[std::uniform_int_distribution<std::size_t>(0, unknown.size() - 1)(rng)];
}

std::size_t game_ai::next_toward(const maze_game_model &board, const std::size_t from, std::mt19937 &rng) const
{
    const auto side = static_cast<int>(board.side());
    const auto x = static_cast<int>(from % side), y = static_cast<int>(from / side);
    const auto &path = board.path(owner::ai);
    std::array<std::size_t, 4> options{};
    std::size_t count = 0;
    std::size_t best_distance = std::numeric_limits<std::size_t>::max();
    for (const auto &[dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
    {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= side || ny >= side)
            continue;
        const auto cell = static_cast<std::size_t>(ny * side + nx);
        if (board.cells()[cell] == cell_kind::bad || std::find(path.begin(), path.end(), cell) != path.end())
            continue;
        const auto distance = manhattan(cell, m_target, board.side());
        if (distance < best_distance)
        {
            best_distance = distance;
            count = 0;
        }
        if (distance == best_distance)
            options[count++] = cell;
    }
    if (count == 0)
        return INVALID_CELL;
    return options[std::uniform_int_distribution<std::size_t>(0, count - 1)(rng)];
}

void game_ai::end_stroke(maze_game_model &board)
{
    board.release(owner::ai);
    m_target = INVALID_CELL;
    m_steps = 0;
}

void game_ai::update(maze_game_model &board, const float delta_time, std::mt19937 &rng)
{
    if (!m_enabled)
        return;
    if (board.side() != m_side || board.level() != m_level)
    {
        // The AI's stroke does not survive a board change, so start fresh.
        board.cancel(owner::ai);
        reset(board);
    }

    m_cooldown -= delta_time;
    if (m_cooldown > 0.0f)
        return;
    // Faster steps at higher levels.
    m_cooldown = std::clamp(0.55f / (1.0f + 0.15f * static_cast<float>(board.level())), 0.08f, 0.55f);

    if (!board.drawing(owner::ai))
    {
        m_target = choose_target(board, rng);
        if (m_target == INVALID_CELL)
            return;
        const auto start = choose_start(board, m_target);
        if (start == INVALID_CELL || !board.begin(start, owner::ai))
        {
            m_target = INVALID_CELL;
            return;
        }
        mark_explored(start);
        m_steps = 0;
        return;
    }

    const auto &path = board.path(owner::ai);
    if (path.empty() || m_target == INVALID_CELL || board.cells()[m_target] == cell_kind::completed ||
        m_steps >= MAX_STEPS_PER_STROKE)
    {
        end_stroke(board);
        return;
    }

    const auto here = path.back();
    if (here == m_target)
    {
        end_stroke(board);
        return;
    }

    // Two steps forward, one step back.
    if (m_steps % 3 == 2 && path.size() >= 2)
    {
        board.trace(path[path.size() - 2], owner::ai);
    }
    else
    {
        const auto next = next_toward(board, here, rng);
        if (next == INVALID_CELL)
        {
            end_stroke(board);
            return;
        }
        board.trace(next, owner::ai);
        mark_explored(next);
    }
    ++m_steps;
}
