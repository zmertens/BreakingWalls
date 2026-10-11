#ifndef GAME_AI_H
#define GAME_AI_H

#include "item.h"
#include "maze_game_model.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

// Autoplayer that owns its own stroke (maze_game_model::owner::ai) and advances one cell per tick:
// two steps toward its target, then one step back, speeding up as the level rises.
class game_ai final
{
public:
    void set_enabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return m_enabled; }

    void reset(const maze_game_model &board);
    void update(maze_game_model &board, float delta_time, std::mt19937 &rng);

private:
    static constexpr std::size_t INVALID_CELL = static_cast<std::size_t>(-1);
    static constexpr int MAX_STEPS_PER_STROKE = 40;

    static int target_priority(maze_game_model::cell_kind kind) noexcept;
    static std::size_t manhattan(std::size_t a, std::size_t b, std::size_t side) noexcept;
    static std::uint64_t bit_for(std::size_t cell) noexcept;

    void mark_explored(std::size_t cell);
    [[nodiscard]] bool explored(std::size_t cell) const noexcept;
    [[nodiscard]] std::size_t choose_start(const maze_game_model &board, std::size_t target) const;
    [[nodiscard]] std::size_t choose_target(const maze_game_model &board, std::mt19937 &rng) const;
    [[nodiscard]] std::size_t next_toward(const maze_game_model &board, std::size_t from, std::mt19937 &rng) const;
    void end_stroke(maze_game_model &board);

    bool m_enabled{false};
    float m_cooldown{};
    std::size_t m_target{INVALID_CELL};
    std::size_t m_side{};
    std::size_t m_level{};
    int m_steps{};
    std::vector<std::uint64_t> m_explored_bits;
};

#endif
