#ifndef SOUNDFX_H
#define SOUNDFX_H

#include <MazeBuilder/singleton_base.h>

#include "resource_identifiers.h"

#include <memory>
#include <string_view>

class soundfx final : public mazes::singleton_base<soundfx>
{
    friend class mazes::singleton_base<soundfx>;
public:
    soundfx();
    ~soundfx() override;
    bool load_from_file(SoundIdentifier id, std::string_view filename,
                        float offset, float pitch, float volume, bool loop);
    void play(SoundIdentifier id);
    // Spatialized playback: SFML applies the inverse-clamped model internally.
    void play_at(SoundIdentifier id, float x, float y, float z);
    void set_listener_position(float x, float y, float z);
    // Applies to subsequently played spatialized sounds; both must be finite, min_distance > 0, attenuation >= 0.
    bool set_spatialization(float min_distance, float attenuation);
    // MinDistance / (MinDistance + Attenuation * (max(Distance, MinDistance) - MinDistance)), in [0, 1].
    [[nodiscard]] static float volume_factor(float min_distance, float attenuation, float distance) noexcept;
    // Scales in [0, 1]; applied to loaded and future sounds.
    void set_volumes(float master, float music, float effects, bool muted);
    void stop(SoundIdentifier id);
    [[nodiscard]] bool is_playing(SoundIdentifier id) const;
private:
    struct audio_state;
    std::unique_ptr<audio_state> m_audio;
};

#endif // SOUNDFX_H
