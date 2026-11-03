#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <array>
#include <cstdint>
#include <vector>

/// GPU particles simulated with GLES3 transform feedback (also valid for WebGL2 and desktop GL 3.3).
/// Particles live in a ring buffer; the update pass reads one buffer and writes the other, so the CPU
/// only uploads newly emitted particles.
class particle_system
{
public:
    enum class kind : int
    {
        FIRE = 0,   // rises and cools from yellow to red
        TRAIL = 1,  // small, short lived, falls slowly
        SPARK = 2   // bright confetti that falls
    };

    static constexpr int CAPACITY = 4096;

    particle_system() = default;
    ~particle_system();
    particle_system(const particle_system &) = delete;
    particle_system &operator=(const particle_system &) = delete;

    /// Compiles the programs and allocates buffers; false if the GL context cannot do it.
    bool init() noexcept;
    void destroy() noexcept;
    [[nodiscard]] bool ready() const noexcept { return m_ready; }

    /// Queues particles; they are uploaded on the next update().
    void burst(float x, float y, float z, int count, kind k, float spread = 1.0f);
    void burst_colored(float x, float y, float z, int count, kind k, float r, float g, float b, float spread = 1.0f);

    /// Advances the simulation by `dt` seconds (GL context required).
    void update(float dt) noexcept;
    /// Draws all live particles with the given view-projection matrix.
    void draw(const float matrix[16], float viewport_height, bool perspective) const noexcept;

    [[nodiscard]] std::size_t pending() const noexcept { return m_pending.size() / FLOATS; }

private:
    static constexpr int FLOATS = 12; // pos3 life vel3 max_life color3 kind

    void push(float x, float y, float z, float vx, float vy, float vz, float life, float r, float g, float b,
              float k);
    [[nodiscard]] float random_unit() noexcept;

    std::uint32_t m_update_program{0};
    std::uint32_t m_draw_program{0};
    std::array<std::uint32_t, 2> m_vbo{};
    std::array<std::uint32_t, 2> m_vao{};
    int m_current{0};
    int m_ring{0};
    bool m_ready{false};
    std::uint32_t m_rng{0x9E3779B9u};
    std::vector<float> m_pending;

    int m_u_dt{-1};
    int m_u_matrix{-1};
    int m_u_scale{-1};
    int m_u_perspective{-1};
};

#endif // PARTICLE_SYSTEM_H
