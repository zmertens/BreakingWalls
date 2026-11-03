#ifndef ARCADE_GAME_H
#define ARCADE_GAME_H

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mazes
{
    class randomizer;
}

struct heightmap;

/// Tunable scoring and pacing. Placeholder values, meant to be adjusted by play-testing.
struct arcade_rules
{
    int wall_penalty{10};
    int block_points{25};
    int time_bonus_per_second{5};
    float time_limit{120.0f};
    float agent_interval{0.45f};  // seconds between worker cursor steps
    int max_pickups{10};
    int base_cells{6};            // maze size (cells per side) at level 1
    int max_cells{14};
    float blocker_per_level{0.04f};
    float max_blocker_density{0.35f};
};

/// An immutable maze arena. Built once per level and shared read-only with the chunk workers.
/// The layout is expanded: every maze cell sits at an odd (row, column) and passages between linked
/// cells are open; every other character is a wall that can be broken.
class arcade_level
{
public:
    static constexpr char WALL = '#';
    static constexpr char OPEN = ' ';
    static constexpr int FLOOR_BLOCK = 1;
    static constexpr int WALL_BLOCK = 4;
    static constexpr int WALL_HEIGHT = 2;
    static constexpr int CLEAR_HEIGHT = 6;
    /// Width of the sloped border that blends the flat arena pad back into the heightmap.
    static constexpr int SKIRT = 10;
    /// Terrain decorations (trees) are cleared this far above a carved column.
    static constexpr int TREE_CLEAR = 9;
    static constexpr int UNDERFILL_BLOCK = 3;

    /// Builds a masked maze: mask text -> mazes::mask -> masked_grid -> depth first search.
    /// `blocker_density` is the fraction of cells replaced by blockers ('X') in the mask source.
    [[nodiscard]] static std::shared_ptr<const arcade_level> build(int cells, float blocker_density,
                                                                   mazes::randomizer &rng, int origin_x,
                                                                   int origin_y, int origin_z,
                                                                   std::shared_ptr<const heightmap> terrain = nullptr);

    [[nodiscard]] int rows() const noexcept { return m_rows; }
    [[nodiscard]] int columns() const noexcept { return m_cols; }
    [[nodiscard]] bool in_bounds(int r, int c) const noexcept { return r >= 0 && c >= 0 && r < m_rows && c < m_cols; }
    [[nodiscard]] bool is_wall(int r, int c) const noexcept { return text_at(r, c) == WALL; }
    /// Walls (including blockers) on the outer border are never breakable.
    [[nodiscard]] bool is_border(int r, int c) const noexcept { return r == 0 || c == 0 || r == m_rows - 1 || c == m_cols - 1; }
    [[nodiscard]] char text_at(int r, int c) const noexcept;
    /// Rows joined by newlines; shared with worker cursors.
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }
    [[nodiscard]] std::size_t index_of(int r, int c) const noexcept { return static_cast<std::size_t>(r) * (m_cols + 1) + c; }
    /// Steps from the start along open cells, or -1 for walls.
    [[nodiscard]] int distance_at(int r, int c) const noexcept;
    [[nodiscard]] int max_distance() const noexcept { return m_max_distance; }

    [[nodiscard]] int start_row() const noexcept { return 1; }
    [[nodiscard]] int start_column() const noexcept { return 1; }
    [[nodiscard]] int goal_row() const noexcept { return m_rows - 2; }
    [[nodiscard]] int goal_column() const noexcept { return m_cols - 2; }
    [[nodiscard]] int blocker_count() const noexcept { return m_blockers; }
    [[nodiscard]] int cells() const noexcept { return (m_rows - 1) / 2; }

    [[nodiscard]] int origin_x() const noexcept { return m_ox; }
    [[nodiscard]] int origin_y() const noexcept { return m_oy; }
    [[nodiscard]] int origin_z() const noexcept { return m_oz; }
    [[nodiscard]] int world_x(int c) const noexcept { return m_ox + c; }
    [[nodiscard]] int world_z(int r) const noexcept { return m_oz + r; }

    /// Block the arena wants at a world position, or -1 when the arena does not own it. With terrain the
    /// maze sits on a flat plateau that slopes into the heightmap. Safe to call from worker threads.
    [[nodiscard]] int block_at(int x, int y, int z) const noexcept;
    /// The heightmap's own block at a position (air without terrain); used to undo the stamp.
    [[nodiscard]] int terrain_block_at(int x, int y, int z) const noexcept;
    [[nodiscard]] int terrain_height(int x, int z) const noexcept;
    /// Area the arena may edit, including the skirt. Maximums are exclusive.
    [[nodiscard]] int min_x() const noexcept { return m_ox - m_skirt; }
    [[nodiscard]] int min_z() const noexcept { return m_oz - m_skirt; }
    [[nodiscard]] int max_x() const noexcept { return m_ox + m_cols + m_skirt; }
    [[nodiscard]] int max_z() const noexcept { return m_oz + m_rows + m_skirt; }
    /// Highest y (inclusive) the arena may edit in a column.
    [[nodiscard]] int top_y(int x, int z) const noexcept;

    /// Generates the same layout as a string, one character per cell, e.g. for debugging.
    [[nodiscard]] static std::string stringify(const arcade_level &level);

private:
    arcade_level() = default;

    int m_rows{0};
    int m_cols{0};
    int m_ox{0}, m_oy{0}, m_oz{0};
    int m_blockers{0};
    int m_max_distance{0};
    int m_skirt{0};
    std::shared_ptr<const heightmap> m_terrain;
    std::string m_text;
    std::vector<int> m_dist;
};

enum class arcade_state
{
    IDLE,
    RUNNING,
    PAUSED,
    WON
};

enum class arcade_event
{
    NONE,
    BROKE_WALL,
    WON
};

/// A worker agent walks its row of the shared maze text with an iterator and "discovers" items.
struct arcade_agent
{
    std::string_view::const_iterator text_iter{};
    int direction{1};
    float timer{0.0f};
};

struct arcade_pickup
{
    int row{0};
    int column{0};
    int block{0};
};

struct arcade_block_change
{
    int x{0}, y{0}, z{0};
    int block{0};
};

/// Arcade session: score, timer, player cell and worker agents. Lives on the main thread.
class arcade_game
{
public:
    static constexpr int AGENT_COUNT = 4;

    arcade_rules rules{};

    [[nodiscard]] int cells_for_level(int level) const noexcept;
    void start_level(int level, mazes::randomizer &rng, int origin_x, int origin_y, int origin_z,
                     std::shared_ptr<const heightmap> terrain = nullptr);
    void stop() noexcept;

    void toggle_pause() noexcept;
    void pause() noexcept;
    void resume() noexcept;

    /// Advances the clock, tweens and agents. Block edits for the world are appended to `changes`.
    void update(float dt, mazes::randomizer &rng, std::vector<arcade_block_change> &changes);

    /// Free-roam update from the player's world position (`feet_y` is the height of their feet) and velocity.
    /// Pushing into a wall below its top breaks it for a penalty; jumping clears it. Reaching the goal wins.
    arcade_event move_player(float x, float feet_y, float z, float vx, float vz,
                             std::vector<arcade_block_change> &changes);
    /// Collects a pickup on or next to the cursor that matches `block`. Returns true when scored.
    bool collect(int block, std::vector<arcade_block_change> &changes);

    [[nodiscard]] arcade_state state() const noexcept { return m_state; }
    [[nodiscard]] bool running() const noexcept { return m_state == arcade_state::RUNNING; }
    [[nodiscard]] bool active() const noexcept { return m_state != arcade_state::IDLE; }
    [[nodiscard]] int level_number() const noexcept { return m_level_number; }
    [[nodiscard]] int score() const noexcept { return m_score; }
    [[nodiscard]] int walls_broken() const noexcept { return m_walls_broken; }
    [[nodiscard]] float elapsed() const noexcept { return m_elapsed; }
    [[nodiscard]] float time_left() const noexcept;
    [[nodiscard]] int final_bonus() const noexcept { return m_bonus; }
    [[nodiscard]] int row() const noexcept { return m_row; }
    [[nodiscard]] int column() const noexcept { return m_col; }
    [[nodiscard]] std::shared_ptr<const arcade_level> level() const noexcept { return m_level; }
    [[nodiscard]] const std::vector<arcade_pickup> &pickups() const noexcept { return m_pickups; }
    [[nodiscard]] const std::array<arcade_agent, AGENT_COUNT> &agents() const noexcept { return m_agents; }
    [[nodiscard]] bool is_open(int r, int c) const noexcept;

    /// Returns true once after the player wins, so the caller can save the score.
    [[nodiscard]] bool take_win() noexcept;

private:
    void step_agent(arcade_agent &agent, mazes::randomizer &rng, std::vector<arcade_block_change> &changes);

    std::shared_ptr<const arcade_level> m_level;
    std::vector<char> m_broken; // walls the player removed, same layout as the level
    std::vector<arcade_pickup> m_pickups;
    std::array<arcade_agent, AGENT_COUNT> m_agents{};

    arcade_state m_state{
#if defined(BREAKING_WALLS_DEBUG_DEF)
        arcade_state::RUNNING,
#else
        arcade_state::IDLE
#endif
    };
    
    int m_level_number{1};
    int m_score{0};
    int m_walls_broken{0};
    int m_bonus{0};
    int m_row{1}, m_col{1};
    float m_elapsed{0.0f};
    bool m_win_pending{false};
};

#endif // ARCADE_GAME_H
