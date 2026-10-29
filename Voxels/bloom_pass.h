#ifndef BLOOM_PASS_H
#define BLOOM_PASS_H

#include <array>
#include <cstdint>

#include "resource_identifiers.h"

class shader;

// Gaussian bloom compositor inspired by OGRE3D's Compositor system.
//
// Rendering flow (three logical passes):
//   1. bind_scene()         — bind scene MRT FBO; caller renders all geometry here.
//   2. set_mrt_mode()       — enable both colour attachments (scene + bright extraction).
//      set_single_mode()    — enable only attachment 0 (sky, UI, wireframe — no bloom).
//   3. execute()            — blur bright pass × BLUR_PASSES*2 (ping-pong), composite to screen.
//
// Loaded once by bloom_pass_manager after shaders are ready. Worlds borrow the
// pass and resize it to their viewport; unchanged sizes retain the cached FBOs.
class bloom_pass
{
public:
    // Number of horizontal+vertical blur iterations.  Higher = wider, softer bloom.
    static constexpr int BLUR_PASSES = 5;
    static constexpr float BLOOM_STRENGTH = 0.6f;

    bloom_pass() = default;
    ~bloom_pass() noexcept;
    bloom_pass(const bloom_pass &) = delete;
    bloom_pass &operator=(const bloom_pass &) = delete;
    bloom_pass(bloom_pass &&) = delete;
    bloom_pass &operator=(bloom_pass &&) = delete;

    [[nodiscard]] bool init(std::uint32_t bloom,
                            std::uint32_t composite,
                            std::uint32_t depth_field,
                            std::uint32_t glass_resolve,
                            std::uint32_t width,
                            std::uint32_t height, bool depth_effects = false) noexcept;

    // Bind scene MRT FBO and activate both draw buffers.
    void bind_scene() const noexcept;

    // Switch to MRT mode (colour attachment 0 + 1) for bloom-eligible geometry.
    void set_mrt_mode() const noexcept;

    // Switch to single-buffer mode (colour attachment 0 only) for sky and UI.
    void set_single_mode() const noexcept;

    // Run blur passes then composite scene + bloom to the default framebuffer.
    // Pass strength=0 to skip blur and composite the scene without any bloom.
    // When progress is in (0, 1) the composite is rendered to an offscreen target and then
    // drawn to the screen through the chosen transition effect, fading out as progress -> 1.
    void execute(float strength = BLOOM_STRENGTH, int blur_iterations = BLUR_PASSES,
                 ShaderEffect effect = ShaderEffect::BLUR, float progress = 0.0f,
                 float seconds = 0.0f) const noexcept;
    void set_transition_programs(const std::array<std::uint32_t, static_cast<std::size_t>(ShaderEffect::COUNT)> &programs) noexcept;

    // Recreate FBOs at a new resolution (call on window resize).
    [[nodiscard]] bool resize(int new_width, int new_height) noexcept;

    void destroy() noexcept;
    void init_depth_effects();
    void draw_depth_field(float seconds, float level, float lights) const noexcept;
    // Draws the shared full-screen quad with whatever program is currently bound.
    void draw_quad() const noexcept;
    void bind_glass() const noexcept;
    void resolve_glass() const noexcept;

private:
    struct components final
    {
        static constexpr std::uint32_t MAX_COLOR_ATTACHMENTS = 2u;

        std::uint32_t fbo{};
        std::array<std::uint32_t, MAX_COLOR_ATTACHMENTS> color_tex{};
        std::uint32_t depth_rbo{};
        int num_color_attachments{1};

        components();
        ~components() noexcept;
        void destroy() noexcept;
        components &operator=(const components &) = delete;
        components &operator=(components &&) noexcept = delete;
        components(const components &) = delete;
        components(components &&) noexcept = delete;

        [[nodiscard]] bool init(int w, int h,
                                int color_attachments,
                                bool depth = false, bool normalized = false) noexcept;
    };

    [[nodiscard]] bool create_fbos(int w, int h) noexcept;
    void destroy_fbos() noexcept;
    void create_screen_quad() noexcept;

    // Pass 1 — MRT scene FBO:
    //   color_tex[0] = full scene colour
    //   color_tex[1] = pixels above brightness threshold (bloom candidates)
    components m_scene_pass{};
    components m_glass_pass{};
    // Allocated on first use so games that never transition pay nothing.
    mutable components m_transition_pass{};
    mutable bool m_transition_ready{};
    std::array<std::uint32_t, static_cast<std::size_t>(ShaderEffect::COUNT)> m_transition_programs{};

    // GL program ids
    std::uint32_t m_blur_program{};
    std::uint32_t m_composite_program{};
    std::uint32_t m_depth_field_program{};
    std::uint32_t m_glass_resolve_program{};

    bool m_depth_enabled{};
    int m_field_time{-1}, m_field_level{-1}, m_field_lights{-1};
    int m_resolve_accumulation{-1}, m_resolve_optical_depth{-1};

    // Passes 2-3 — ping-pong Gaussian blur on the bright texture.
    std::array<components, 2> m_blur_pass{};

    // Shared full-screen triangle-strip quad (NDC positions + UVs).
    std::uint32_t m_quad_vao{};
    std::uint32_t m_quad_vbo{};

    // Cached uniform locations (set during init to avoid per-frame queries).
    std::uint32_t m_blur_loc_horizontal{-1u};
    std::uint32_t m_blur_loc_image{-1u};
    std::uint32_t m_comp_loc_scene{-1u};
    std::uint32_t m_comp_loc_bloom{-1u};
    std::uint32_t m_comp_loc_strength{-1u};
    std::uint32_t m_depth_field_loc_image{-1u};
    std::uint32_t m_glass_resolve_loc_image{-1u};

    std::uint32_t m_width{};
    std::uint32_t m_height{};
};

#endif // BLOOM_PASS_H
