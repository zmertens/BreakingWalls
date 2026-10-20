#include "soundfx.h"

#include <SFML/Audio.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>

struct soundfx::audio_state
{
    struct music_track
    {
        sf::Music music;
        sf::Time offset{};
        float base_volume{100.0f};
        bool loaded{};
    };

    music_track game_theme, loading_theme;
    sf::SoundBuffer boink;
    std::array<std::unique_ptr<sf::Sound>, 8> voices;
    std::size_t next_voice{};
    sf::Time boink_offset{};
    float boink_volume{100.0f};
    float master{1.0f}, music_scale{1.0f}, effects_scale{1.0f};
    bool muted{};
    bool boink_loaded{};
    float min_distance{1.0f}, attenuation{1.0f};

    // Streamed songs; nullptr for buffered effects.
    music_track *track(SoundIdentifier id) noexcept
    {
        if (id == SoundIdentifier::GAME_THEME_SONG_1)
        {
            return &game_theme;
        }
        if (id == SoundIdentifier::LOADING_THEME_SONG)
        {
            return &loading_theme;
        }
        return nullptr;
    }
    const music_track *track(SoundIdentifier id) const noexcept
    {
        return const_cast<audio_state *>(this)->track(id);
    }

    void apply_volumes()
    {
        using V = std::unique_ptr<sf::Sound>;
        sf::Listener::setGlobalVolume(muted ? 0.0f : master * 100.0f);
        std::array<music_track *, 2> tracks = {&game_theme, &loading_theme};
        std::ranges::for_each(tracks, [&](auto *t)
                              { t->music.setVolume(t->base_volume * music_scale); });
        std::ranges::for_each(voices | std::views::filter([](V &voice)
                                                          { return static_cast<bool>(voice); }),
                              [&](V &voice)
                              { voice->setVolume(boink_volume * effects_scale); });
    }

    void require_loaded(SoundIdentifier id) const
    {
        const auto *t = track(id);
        if ((t && t->loaded) || (id == SoundIdentifier::BOINK && boink_loaded))
        {
            return;
        }
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Sound identifier %u is not loaded", static_cast<unsigned int>(id));
        throw std::logic_error("Sound identifier is not loaded");
    }
};

soundfx::soundfx() : m_audio{std::make_unique<audio_state>()} {}
soundfx::~soundfx() = default;

bool soundfx::load_from_file(SoundIdentifier id, std::string_view filename,
                             float offset, float pitch, float volume, bool loop)
{
    if (!std::isfinite(offset) || offset < 0 || !std::isfinite(pitch) || pitch <= 0 ||
        !std::isfinite(volume) || volume < 0 || volume > 100)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Invalid sound playback parameters");
        return false;
    }
    auto &audio = *m_audio;
    if (auto *t = audio.track(id))
    {
        t->music.stop();
        t->loaded = false;
        if (!t->music.openFromFile(std::string(filename)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to open music from file %s", std::string(filename).c_str());
            return false;
        }
        if (sf::seconds(offset) >= t->music.getDuration())
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Music offset exceeds duration");
            return false;
        }
        t->offset = sf::seconds(offset);
        t->music.setPitch(pitch);
        t->base_volume = volume;
        t->music.setVolume(volume * audio.music_scale);
        t->music.setLooping(loop);
        // Background music must not attenuate/pan as the listener moves.
        t->music.setSpatializationEnabled(false);
        t->loaded = true;
        return true;
    }
    if (id == SoundIdentifier::BOINK)
    {
        for (auto &voice : audio.voices)
        {
            voice.reset();
        }
        audio.boink_loaded = false;
        if (!audio.boink.loadFromFile(std::string(filename)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to load sound from file %s", std::string(filename).c_str());
            return false;
        }
        if (sf::seconds(offset) >= audio.boink.getDuration())
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Sound offset exceeds duration");
            return false;
        }
        audio.boink_offset = sf::seconds(offset);
        audio.boink_volume = volume;
        audio.next_voice = 0;
        for (auto &voice : audio.voices)
        {
            voice = std::make_unique<sf::Sound>(audio.boink);
            voice->setPitch(pitch);
            voice->setVolume(volume * audio.effects_scale);
            voice->setLooping(loop);
        }
        audio.boink_loaded = true;
        return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Unsupported sound identifier %u", static_cast<unsigned int>(id));
    return false;
}

void soundfx::set_volumes(float master, float music, float effects, bool muted)
{
    const auto clamp01 = [](auto v)
    { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    auto &audio = *m_audio;
    audio.master = clamp01(master);
    audio.music_scale = clamp01(music);
    audio.effects_scale = clamp01(effects);
    audio.muted = muted;
    audio.apply_volumes();
}

float soundfx::volume_factor(float min_distance, float attenuation, float distance) noexcept
{
    if (!std::isfinite(min_distance) || min_distance <= 0 || !std::isfinite(attenuation) || attenuation < 0 ||
        std::isnan(distance))
    {
        return 0.0f;
    }
    return min_distance / (min_distance + attenuation * (std::max(distance, min_distance) - min_distance));
}

bool soundfx::set_spatialization(float min_distance, float attenuation)
{
    if (!std::isfinite(min_distance) || min_distance <= 0 || !std::isfinite(attenuation) || attenuation < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Invalid spatialization parameters");
        return false;
    }
    m_audio->min_distance = min_distance;
    m_audio->attenuation = attenuation;
    return true;
}

void soundfx::set_listener_position(float x, float y, float z)
{
    sf::Listener::setPosition({x, y, z});
}

void soundfx::play_at(SoundIdentifier id, float x, float y, float z)
{
    auto &audio = *m_audio;
    audio.require_loaded(id);
    if (auto *t = audio.track(id))
    {
        t->music.setSpatializationEnabled(true);
        t->music.setPosition({x, y, z});
        t->music.setMinDistance(audio.min_distance);
        t->music.setAttenuation(audio.attenuation);
        play(id);
        return;
    }
    auto &voice = audio.voices[audio.next_voice];
    voice->setSpatializationEnabled(true);
    voice->setPosition({x, y, z});
    voice->setMinDistance(audio.min_distance);
    voice->setAttenuation(audio.attenuation);
    play(id);
}

void soundfx::play(SoundIdentifier id)
{
    auto &audio = *m_audio;
    audio.require_loaded(id);
    if (auto *t = audio.track(id))
    {
        if (!is_playing(id))
        {
            t->music.setPlayingOffset(t->offset);
            t->music.play();
        }
        return;
    }
    auto &voice = audio.voices[audio.next_voice];
    voice->stop();
    voice->setPlayingOffset(audio.boink_offset);
    voice->play();
    audio.next_voice = (audio.next_voice + 1) % audio.voices.size();
}

void soundfx::stop(SoundIdentifier id)
{
    auto &audio = *m_audio;
    audio.require_loaded(id);
    if (auto *t = audio.track(id))
    {
        t->music.stop();
    }
    else
    {
        std::ranges::for_each(audio.voices, [](auto &voice)
                              { voice->stop(); });
    }
}

bool soundfx::is_playing(SoundIdentifier id) const
{
    const auto &audio = *m_audio;
    audio.require_loaded(id);
    if (const auto *t = audio.track(id))
    {
        return t->music.getStatus() == sf::SoundSource::Status::Playing;
    }
    return std::any_of(audio.voices.begin(), audio.voices.end(), [](const auto &voice)
                       { return voice->getStatus() == sf::SoundSource::Status::Playing; });
}
