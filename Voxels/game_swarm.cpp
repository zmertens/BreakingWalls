#include "game_swarm.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float GRAVITY_HEAD = 5.0f, GRAVITY_TAIL = 2.5f;
constexpr float PARTICLE_INV_MASS = 10.0f;
constexpr float MAX_DIST = 3.0f;
constexpr float SOFTENING = 0.18f;
constexpr float DRAG = 1.4f;
constexpr float MAX_SPEED = 5.0f;
constexpr float SWARM_KIND = 5.0f;
}

void game_swarm::respawn(particle &p, std::mt19937 &rng) const
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float angle = unit(rng) * 6.2831853f, radius = 0.1f + unit(rng) * 0.5f;
    p.position = {m_head.first + std::cos(angle) * radius, m_head.second + std::sin(angle) * radius};
    // Tangential start velocity makes particles orbit instead of dropping straight in.
    const float speed = 0.8f + unit(rng) * 1.2f;
    p.velocity = {-std::sin(angle) * speed, std::cos(angle) * speed};
    p.tint = unit(rng);
}

void game_swarm::clear() noexcept
{
    m_strength = 0.0f;
    m_seeded = false;
    m_accumulator = 0.0f;
}

void game_swarm::update(const float seconds, const std::optional<point> &head, std::mt19937 &rng)
{
    if (!std::isfinite(seconds) || seconds < 0.0f)
        return;
    const float dt = std::min(seconds, 0.1f);
    if (head)
    {
        m_head = *head;
        if (!m_seeded)
        {
            m_tail = m_head;
            for (auto &p : m_particles)
                respawn(p, rng);
            m_seeded = true;
        }
        m_strength = std::min(1.0f, m_strength + dt * 8.0f);
    }
    else
        m_strength = std::max(0.0f, m_strength - dt * 2.5f);
    if (m_strength <= 0.0f)
    {
        m_seeded = false;
        return;
    }
    m_accumulator += dt;
    while (m_accumulator >= FIXED_STEP)
    {
        m_accumulator -= FIXED_STEP;
        // The tail trails the head so the swarm stretches along the gesture.
        const float follow = std::min(1.0f, 5.0f * FIXED_STEP);
        m_tail.first += (m_tail.first - m_head.first) * -follow;
        m_tail.second += (m_tail.second - m_head.second) * -follow;
        for (auto &p : m_particles)
        {
            float ax = 0, az = 0;
            for (const auto &[attractor, gravity] : {std::pair{m_head, GRAVITY_HEAD}, std::pair{m_tail, GRAVITY_TAIL}})
            {
                const float dx = attractor.first - p.position.first, dz = attractor.second - p.position.second;
                const float dist = std::max(std::hypot(dx, dz), SOFTENING);
                const float scale = gravity / dist / dist * PARTICLE_INV_MASS * 0.1f;
                ax += dx * scale;
                az += dz * scale;
            }
            p.velocity.first += (ax - p.velocity.first * DRAG) * FIXED_STEP;
            p.velocity.second += (az - p.velocity.second * DRAG) * FIXED_STEP;
            const float speed = std::hypot(p.velocity.first, p.velocity.second);
            if (speed > MAX_SPEED)
            {
                p.velocity.first *= MAX_SPEED / speed;
                p.velocity.second *= MAX_SPEED / speed;
            }
            p.position.first += p.velocity.first * FIXED_STEP;
            p.position.second += p.velocity.second * FIXED_STEP;
            if (std::hypot(p.position.first - m_head.first, p.position.second - m_head.second) > MAX_DIST)
                respawn(p, rng);
        }
    }
}

void game_swarm::append_vertices(std::vector<float> &out, const float seconds) const
{
    if (!visible())
        return;
    constexpr int indices[]{0, 1, 2, 0, 2, 3};
    constexpr float corners[4][2]{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    out.reserve(out.size() + COUNT * 60);
    for (std::size_t i = 0; i < COUNT; ++i)
    {
        const auto &p = m_particles[i];
        // Per-bee flutter so the cloud buzzes rather than glides.
        const float flutter = 0.5f + 0.5f * std::sin(seconds * 24.0f + static_cast<float>(i) * 1.7f);
        const float half = (0.045f + 0.03f * p.tint) * (0.7f + 0.3f * flutter) * (0.4f + 0.6f * m_strength);
        for (const int corner : indices)
            out.insert(out.end(), {p.position.first + corners[corner][0] * half, HEIGHT,
                                   p.position.second + corners[corner][1] * half,
                                   p.tint, m_strength, 0,
                                   corners[corner][0], corners[corner][1], 0, SWARM_KIND});
    }
}

bool game_swarm::visible() const noexcept
{
    return m_strength > 0.0f;
}
