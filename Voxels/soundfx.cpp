#include "soundfx.h"

#include <SFML/Audio.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

struct soundfx::audio_state
{
    sf::Music music;
    sf::SoundBuffer boink;
    std::array<std::unique_ptr<sf::Sound>, 8> voices;
    std::size_t next_voice{};
    sf::Time music_offset{}, boink_offset{};
    bool music_loaded{}, boink_loaded{};
    float min_distance{1.0f}, attenuation{1.0f};

    void require_loaded(SoundIdentifier id) const
    {
        if ((id == SoundIdentifier::GAME_THEME_SONG_1 && music_loaded) ||
            (id == SoundIdentifier::BOINK && boink_loaded))
            return;
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
    if (id == SoundIdentifier::GAME_THEME_SONG_1)
    {
        audio.music.stop();
        audio.music_loaded = false;
        if (!audio.music.openFromFile(std::string(filename)))
            return false;
        if (sf::seconds(offset) >= audio.music.getDuration())
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Music offset exceeds duration");
            return false;
        }
        audio.music_offset = sf::seconds(offset);
        audio.music.setPitch(pitch);
        audio.music.setVolume(volume);
        audio.music.setLooping(loop);
        // Background music must not attenuate/pan as the listener moves.
        audio.music.setSpatializationEnabled(false);
        audio.music_loaded = true;
        return true;
    }
    if (id == SoundIdentifier::BOINK)
    {
        for (auto &voice : audio.voices)
            voice.reset();
        audio.boink_loaded = false;
        if (!audio.boink.loadFromFile(std::string(filename)))
            return false;
        if (sf::seconds(offset) >= audio.boink.getDuration())
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Sound offset exceeds duration");
            return false;
        }
        audio.boink_offset = sf::seconds(offset);
        audio.next_voice = 0;
        for (auto &voice : audio.voices)
        {
            voice = std::make_unique<sf::Sound>(audio.boink);
            voice->setPitch(pitch);
            voice->setVolume(volume);
            voice->setLooping(loop);
        }
        audio.boink_loaded = true;
        return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Unsupported sound identifier %u", static_cast<unsigned int>(id));
    return false;
}

float soundfx::volume_factor(float min_distance, float attenuation, float distance) noexcept
{
    if (!std::isfinite(min_distance) || min_distance <= 0 || !std::isfinite(attenuation) || attenuation < 0 ||
        std::isnan(distance))
        return 0.0f;
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
    if (id == SoundIdentifier::GAME_THEME_SONG_1)
    {
        audio.music.setSpatializationEnabled(true);
        audio.music.setPosition({x, y, z});
        audio.music.setMinDistance(audio.min_distance);
        audio.music.setAttenuation(audio.attenuation);
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
    if (id == SoundIdentifier::GAME_THEME_SONG_1)
    {
        if (!is_playing(id))
        {
            audio.music.setPlayingOffset(audio.music_offset);
            audio.music.play();
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
    if (id == SoundIdentifier::GAME_THEME_SONG_1)
        audio.music.stop();
    else
        for (auto &voice : audio.voices)
            voice->stop();
}

bool soundfx::is_playing(SoundIdentifier id) const
{
    const auto &audio = *m_audio;
    audio.require_loaded(id);
    if (id == SoundIdentifier::GAME_THEME_SONG_1)
        return audio.music.getStatus() == sf::SoundSource::Status::Playing;
    return std::any_of(audio.voices.begin(), audio.voices.end(), [](const auto &voice)
    {
        return voice->getStatus() == sf::SoundSource::Status::Playing;
    });
}
