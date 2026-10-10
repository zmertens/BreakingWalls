#ifndef GAME_SWARM_H
#define GAME_SWARM_H

#include <array>
#include <optional>
#include <random>
#include <utility>
#include <vector>

// CPU port of the GLSL Cookbook's two-attractor particle compute shader:
// A bee-like cloud orbits the swipe head and a lagging tail point.
// Particle count is small enough that no GPU simulation is needed.
class game_swarm final
{
public:
    using point = std::pair<float, float>;
    static constexpr std::size_t COUNT = 160;
    static constexpr float FIXED_STEP = 1.0f / 120.0f;
    static constexpr float HEIGHT = 7.58f;

    // head is the stroke position in board space while swiping; nullopt lets the swarm disperse.
    void update(float seconds, const std::optional<point> &head, std::mt19937 &rng);
    void clear() noexcept;
    [[nodiscard]] bool visible() const noexcept;
    // Appends 10-float vertices (position, normal = {tint, strength, 0}, uv = {corner, 0, kind}).
    void append_vertices(std::vector<float> &out, float seconds) const;

private:
    struct particle
    {
        point position{}, velocity{};
        float tint{};
    };
    void respawn(particle &p, std::mt19937 &rng) const;

    std::array<particle, COUNT> m_particles{};
    point m_head{}, m_tail{};
    float m_strength{};
    float m_accumulator{};
    bool m_seeded{};
};

#endif
