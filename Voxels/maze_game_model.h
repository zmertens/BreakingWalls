#ifndef MAZE_GAME_MODEL_H
#define MAZE_GAME_MODEL_H

#include <cstddef>
#include <array>
#include <cstdint>
#include <filesystem>
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

    static constexpr int GOOD_REWARD = 5;
    static constexpr int BAD_PENALTY = 2;
    static constexpr int GOAL_REWARD = 10;
    static constexpr int GROWTH_INTERVAL = 50;
    static constexpr std::size_t MAX_SIDE = 40;
    static constexpr std::array<std::string_view, 6> ENVIRONMENT_FACE_NAMES{
        "positive_x", "negative_x", "positive_y", "negative_y", "positive_z", "negative_z"};

    explicit maze_game_model(std::uint32_t seed = std::random_device{}(), bool stacked = false,
                             std::size_t side = 8);
    void reset();
    bool begin(std::size_t cell);
    void trace(std::size_t cell);
    void release();
    void cancel();
    [[nodiscard]] std::array<std::filesystem::path, 6> write_environment_images(
        const std::filesystem::path &directory, std::uint32_t seed) const;

    [[nodiscard]] std::size_t side() const noexcept { return m_side; }
    [[nodiscard]] const std::vector<cell_kind> &cells() const noexcept { return m_cells; }
    [[nodiscard]] const std::vector<std::size_t> &path() const noexcept { return m_path; }
    using edge = std::pair<std::size_t, std::size_t>;
    struct completed_level
    {
        std::vector<cell_kind> cells;
        std::set<edge> lines;
    };
    [[nodiscard]] std::size_t level() const noexcept { return m_history.size() + 1; }
    [[nodiscard]] int lights_activated() const noexcept { return m_lights; }
    static constexpr std::size_t VISIBLE_LEVELS = 3;
    [[nodiscard]] const std::vector<cell_kind> &next_cells() const noexcept { return m_previews.front(); }
    [[nodiscard]] const std::array<std::vector<cell_kind>, VISIBLE_LEVELS - 1> &previews() const noexcept
    { return m_previews; }
    [[nodiscard]] const std::vector<completed_level> &history() const noexcept { return m_history; }
    static constexpr int LIGHTS_PER_LEVEL = 5;
    [[nodiscard]] const std::set<edge> &saved_edges() const noexcept { return m_saved_edges; }
    [[nodiscard]] const std::set<edge> &active_edges() const noexcept { return m_active_edges; }
    [[nodiscard]] std::int64_t score() const noexcept { return m_score; }
    [[nodiscard]] int streak() const noexcept { return m_streak; }
    [[nodiscard]] bool drawing() const noexcept { return m_drawing; }
    [[nodiscard]] int growth_progress() const noexcept { return m_growth_progress; }
    [[nodiscard]] const std::string &message() const noexcept { return m_message; }

private:
    std::optional<std::size_t> spawn(cell_kind kind);
    void enter(std::size_t cell);
    void award(int points);
    void grow();
    void finish(bool allow_bonus);
    void descend(std::size_t cell);
    void prepare_next_level();

    std::mt19937 m_rng;
    std::size_t m_initial_side{8};
    std::size_t m_side{8};
    std::vector<cell_kind> m_cells;
    std::vector<std::size_t> m_path;
    std::set<edge> m_saved_edges;
    std::set<edge> m_active_edges;
    bool m_stacked{};
    int m_lights{};
    std::array<std::vector<cell_kind>, VISIBLE_LEVELS - 1> m_previews;
    std::vector<completed_level> m_history;
    std::int64_t m_score{};
    int m_streak{};
    int m_growth_progress{};
    int m_run_good{};
    int m_run_bad{};
    bool m_reached_goal{};
    bool m_drawing{};
    std::string m_message;
};

#endif
