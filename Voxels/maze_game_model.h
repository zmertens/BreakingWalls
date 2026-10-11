#ifndef MAZE_GAME_MODEL_H
#define MAZE_GAME_MODEL_H

#include <algorithm>
#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

class maze_game_model
{
public:
    enum class cell_kind { empty, start, goal, hidden_good, hidden_bad, good, bad, completed };
    // Each owner keeps an independent stroke, score and light count, so strokes can run concurrently.
    enum class owner : std::size_t { player = 0, ai = 1 };
    static constexpr std::size_t OWNER_COUNT = 2;

    static constexpr int GOOD_REWARD = 5;
    static constexpr int BAD_PENALTY = 2;
    static constexpr int GOAL_REWARD = 10;
    static constexpr int GROWTH_INTERVAL = 50;
    static constexpr std::size_t MAX_SIDE = 40;

    explicit maze_game_model(std::uint32_t seed = std::random_device{}(), bool stacked = false,
                             std::size_t side = 8);
    void reset();
    bool begin(std::size_t cell, owner who = owner::player);
    void trace(std::size_t cell, owner who = owner::player);
    void release(owner who = owner::player);
    void cancel(owner who = owner::player);

    [[nodiscard]] std::size_t side() const noexcept { return m_side; }
    [[nodiscard]] const std::vector<cell_kind> &cells() const noexcept { return m_cells; }
    [[nodiscard]] const std::vector<std::size_t> &path(owner who = owner::player) const noexcept
    { return stroke_of(who).path; }
    using edge = std::pair<std::size_t, std::size_t>;
    struct completed_level
    {
        std::vector<cell_kind> cells;
        std::set<edge> lines;
    };
    [[nodiscard]] std::size_t level() const noexcept { return m_history.size() + 1; }
    [[nodiscard]] int lights_activated() const noexcept
    { return std::max(m_strokes[0].lights, m_strokes[1].lights); }
    [[nodiscard]] int lights_activated(owner who) const noexcept { return stroke_of(who).lights; }
    static constexpr std::size_t VISIBLE_LEVELS = 3;
    [[nodiscard]] const std::vector<cell_kind> &next_cells() const noexcept { return m_previews.front(); }
    [[nodiscard]] const std::array<std::vector<cell_kind>, VISIBLE_LEVELS - 1> &previews() const noexcept
    { return m_previews; }
    [[nodiscard]] const std::vector<completed_level> &history() const noexcept { return m_history; }
    static constexpr int LIGHTS_PER_LEVEL = 5;
    [[nodiscard]] const std::set<edge> &saved_edges(owner who = owner::player) const noexcept
    { return stroke_of(who).saved_edges; }
    [[nodiscard]] const std::set<edge> &active_edges(owner who = owner::player) const noexcept
    { return stroke_of(who).active_edges; }
    [[nodiscard]] std::int64_t score(owner who = owner::player) const noexcept { return stroke_of(who).score; }
    [[nodiscard]] int streak(owner who = owner::player) const noexcept { return stroke_of(who).streak; }
    [[nodiscard]] bool drawing(owner who = owner::player) const noexcept { return stroke_of(who).drawing; }
    [[nodiscard]] int growth_progress() const noexcept { return m_growth_progress; }
    [[nodiscard]] const std::string &message() const noexcept { return m_message; }

private:
    struct stroke
    {
        std::vector<std::size_t> path;
        std::set<edge> saved_edges, active_edges;
        std::int64_t score{};
        int streak{}, lights{}, run_good{}, run_bad{};
        bool reached_goal{}, drawing{};
    };
    [[nodiscard]] const stroke &stroke_of(owner who) const noexcept { return m_strokes[static_cast<std::size_t>(who)]; }
    [[nodiscard]] stroke &stroke_of(owner who) noexcept { return m_strokes[static_cast<std::size_t>(who)]; }
    [[nodiscard]] bool any_drawing() const noexcept;

    std::optional<std::size_t> spawn(cell_kind kind);
    void enter(std::size_t cell, owner who);
    void award(int points, owner who);
    void grow();
    void finish(bool allow_bonus, owner who);
    void descend(std::size_t cell, owner who);
    void prepare_next_level();

    std::mt19937 m_rng;
    std::size_t m_initial_side{8};
    std::size_t m_side{8};
    std::vector<cell_kind> m_cells;
    std::array<stroke, OWNER_COUNT> m_strokes;
    bool m_stacked{};
    std::array<std::vector<cell_kind>, VISIBLE_LEVELS - 1> m_previews;
    std::vector<completed_level> m_history;
    int m_growth_progress{};
    std::string m_message;
};

#endif
