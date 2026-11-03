#include "tickerbox.h"

#include <utility>

void tickerbox::set_message(std::string message)
{
    m_pending.clear();
    m_current = std::move(message);
    m_offset = 0;
    m_step_ms = 0.0f;
    m_shown_ms = 0.0f;
    m_cycle_done = false;
}

void tickerbox::push(std::string message)
{
    if (message.empty())
    {
        return;
    }
    if (m_current.empty())
    {
        set_message(std::move(message));
        return;
    }
    if (m_pending.size() >= MAX_QUEUED)
    {
        m_pending.pop_front();
    }
    m_pending.push_back(std::move(message));
}

void tickerbox::update(const float delta_ms) noexcept
{
    if (m_current.empty())
    {
        return;
    }
    m_shown_ms += delta_ms;
    m_step_ms += delta_ms;
    m_cycle_len = m_current.size() + GAP.size();
    while (m_step_ms >= STEP_INTERVAL_MS)
    {
        m_step_ms -= STEP_INTERVAL_MS;
        if (++m_offset >= m_cycle_len)
        {
            m_offset = 0;
            m_cycle_done = true;
        }
    }
    if (m_cycle_done && m_shown_ms >= MIN_DISPLAY_MS && !m_pending.empty())
    {
        auto next = std::move(m_pending.front());
        m_pending.pop_front();
        auto rest = std::move(m_pending);
        set_message(std::move(next));
        m_pending = std::move(rest);
    }
}

std::string tickerbox::visible_text(const std::size_t max_chars) const
{
    if (m_current.empty() || max_chars == 0)
    {
        return {};
    }
    if (m_current.size() <= max_chars)
    {
        return m_current;
    }
    // Scroll over the message followed by a gap so it wraps around smoothly.
    std::string loop{m_current};
    loop.append(GAP);
    std::string out;
    out.reserve(max_chars);
    for (std::size_t i = 0; i < max_chars; ++i)
    {
        out.push_back(loop[(m_offset + i) % loop.size()]);
    }
    return out;
}
