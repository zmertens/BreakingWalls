#ifndef TICKERBOX_H
#define TICKERBOX_H

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

/// Status line that scrolls like a news ticker. Messages are queued; each one is shown until it has
/// scrolled through once (and for at least MIN_DISPLAY_MS), then the next one takes over.
/// Not thread-safe: use from the main thread only.
class tickerbox final
{
public:
    static constexpr float STEP_INTERVAL_MS{350.0f};
    static constexpr float MIN_DISPLAY_MS{2500.0f};
    static constexpr std::size_t MAX_QUEUED{8};

    /// Replaces the current message and drops anything queued.
    void set_message(std::string message);
    /// Queues a message behind the current one (oldest queued entry is dropped when full).
    void push(std::string message);
    [[nodiscard]] const std::string &get_message() const noexcept { return m_current; }

    void set_enabled(const bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return m_enabled; }

    void update(float delta_ms) noexcept;

    /// Text for the current scroll position, at most max_chars wide. Short messages are shown whole,
    /// so the box width follows the text.
    [[nodiscard]] std::string visible_text(std::size_t max_chars) const;

private:
    static constexpr std::string_view GAP{"   "};

    std::string m_current;
    std::deque<std::string> m_pending;
    std::size_t m_offset{0};
    std::size_t m_cycle_len{0};
    float m_step_ms{0.0f};
    float m_shown_ms{0.0f};
    bool m_cycle_done{false};
    bool m_enabled{true};
};

#endif // TICKERBOX_H
