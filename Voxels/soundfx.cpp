#include "soundfx.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

struct soundfx::audio_state
{
    struct music_track
    {
        MIX_Audio *audio{nullptr};
        MIX_Track *track{nullptr};
        Sint64 offset_ms{};
        float base_volume{100.0f};
        float pitch{1.0f};
        float spatial{1.0f};
        bool loop{};
        bool loaded{};
    };

    static constexpr std::size_t VOICE_COUNT = 8;

    MIX_Mixer *mixer{nullptr};
    music_track game_theme, loading_theme;
    MIX_Audio *boink{nullptr};
    std::array<MIX_Track *, VOICE_COUNT> voices{};
    std::array<float, VOICE_COUNT> voice_spatial{};
    std::size_t next_voice{};
    Sint64 boink_offset_ms{};
    float boink_volume{100.0f};
    float boink_pitch{1.0f};
    bool boink_loop{};
    float master{1.0f}, music_scale{1.0f}, effects_scale{1.0f};
    bool muted{};
    bool boink_loaded{};
    float min_distance{1.0f}, attenuation{1.0f};
    float listener[3]{};

    audio_state()
    {
        voice_spatial.fill(1.0f);
        if (!MIX_Init())
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to initialize SDL_mixer: %s", SDL_GetError());
            return;
        }
        mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!mixer)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to create mixer device: %s", SDL_GetError());
            MIX_Quit();
            return;
        }
        game_theme.track = MIX_CreateTrack(mixer);
        loading_theme.track = MIX_CreateTrack(mixer);
        for (auto &voice : voices)
        {
            voice = MIX_CreateTrack(mixer);
        }
    }

    audio_state(const audio_state &) = delete;
    audio_state &operator=(const audio_state &) = delete;

    // Destroying the mixer also destroys its tracks; audio must be released before MIX_Quit.
    ~audio_state()
    {
        if (!mixer)
        {
            return;
        }
        MIX_DestroyMixer(mixer);
        MIX_DestroyAudio(game_theme.audio);
        MIX_DestroyAudio(loading_theme.audio);
        MIX_DestroyAudio(boink);
        MIX_Quit();
    }

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
        if (!mixer)
        {
            return;
        }
        MIX_SetMixerGain(mixer, muted ? 0.0f : master);
        for (auto *t : {&game_theme, &loading_theme})
        {
            if (t->track)
            {
                MIX_SetTrackGain(t->track, t->base_volume / 100.0f * music_scale * t->spatial);
            }
        }
        for (std::size_t i = 0; i < voices.size(); ++i)
        {
            if (voices[i])
            {
                MIX_SetTrackGain(voices[i], boink_volume / 100.0f * effects_scale * voice_spatial[i]);
            }
        }
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

    static bool start(MIX_Track *track, Sint64 offset_ms, bool loop)
    {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_START_MILLISECOND_NUMBER, offset_ms);
        const bool ok = MIX_PlayTrack(track, props);
        SDL_DestroyProperties(props);
        if (!ok)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to play track: %s", SDL_GetError());
        }
        return ok;
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
    if (!audio.mixer)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio mixer is unavailable");
        return false;
    }
    const std::string path(filename);
    const auto offset_ms = static_cast<Sint64>(offset * 1000.0f);

    if (auto *t = audio.track(id))
    {
        t->loaded = false;
        MIX_StopTrack(t->track, 0);
        // Streamed (not predecoded) so long songs stay out of memory.
        MIX_Audio *loaded = MIX_LoadAudio(audio.mixer, path.c_str(), false);
        if (!loaded)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to open music from file %s: %s", path.c_str(), SDL_GetError());
            return false;
        }
        const Sint64 duration = MIX_GetAudioDuration(loaded);
        if (duration >= 0 && offset_ms >= MIX_AudioFramesToMS(loaded, duration))
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Music offset exceeds duration");
            MIX_DestroyAudio(loaded);
            return false;
        }
        if (!MIX_SetTrackAudio(t->track, loaded))
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to assign music: %s", SDL_GetError());
            MIX_DestroyAudio(loaded);
            return false;
        }
        MIX_DestroyAudio(t->audio);
        t->audio = loaded;
        t->offset_ms = offset_ms;
        t->pitch = pitch;
        t->base_volume = volume;
        t->loop = loop;
        t->spatial = 1.0f;
        MIX_SetTrackFrequencyRatio(t->track, pitch);
        t->loaded = true;
        audio.apply_volumes();
        return true;
    }
    if (id == SoundIdentifier::BOINK)
    {
        audio.boink_loaded = false;
        for (auto *voice : audio.voices)
        {
            MIX_StopTrack(voice, 0);
        }
        MIX_Audio *loaded = MIX_LoadAudio(audio.mixer, path.c_str(), true);
        if (!loaded)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to load sound from file %s: %s", path.c_str(), SDL_GetError());
            return false;
        }
        const Sint64 duration = MIX_GetAudioDuration(loaded);
        if (duration >= 0 && offset_ms >= MIX_AudioFramesToMS(loaded, duration))
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Sound offset exceeds duration");
            MIX_DestroyAudio(loaded);
            return false;
        }
        for (auto *voice : audio.voices)
        {
            if (!MIX_SetTrackAudio(voice, loaded))
            {
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to assign sound: %s", SDL_GetError());
                MIX_DestroyAudio(loaded);
                return false;
            }
            MIX_SetTrackFrequencyRatio(voice, pitch);
        }
        MIX_DestroyAudio(audio.boink);
        audio.boink = loaded;
        audio.boink_offset_ms = offset_ms;
        audio.boink_volume = volume;
        audio.boink_pitch = pitch;
        audio.boink_loop = loop;
        audio.next_voice = 0;
        audio.voice_spatial.fill(1.0f);
        audio.boink_loaded = true;
        audio.apply_volumes();
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
    m_audio->listener[0] = x;
    m_audio->listener[1] = y;
    m_audio->listener[2] = z;
}

void soundfx::play_at(SoundIdentifier id, float x, float y, float z)
{
    auto &audio = *m_audio;
    audio.require_loaded(id);
    const float dx = x - audio.listener[0];
    const float dy = y - audio.listener[1];
    const float dz = z - audio.listener[2];
    const float gain = volume_factor(audio.min_distance, audio.attenuation, std::sqrt(dx * dx + dy * dy + dz * dz));
    const auto voice_index = audio.next_voice;
    play(id);
    if (auto *t = audio.track(id))
    {
        t->spatial = gain;
    }
    else
    {
        audio.voice_spatial[voice_index] = gain;
    }
    audio.apply_volumes();
}

void soundfx::play(SoundIdentifier id)
{
    auto &audio = *m_audio;
    audio.require_loaded(id);
    if (!audio.mixer)
    {
        return;
    }
    if (auto *t = audio.track(id))
    {
        if (!is_playing(id))
        {
            t->spatial = 1.0f;
            audio.apply_volumes();
            audio_state::start(t->track, t->offset_ms, t->loop);
        }
        return;
    }
    auto *voice = audio.voices[audio.next_voice];
    MIX_StopTrack(voice, 0);
    audio.voice_spatial[audio.next_voice] = 1.0f;
    audio.apply_volumes();
    audio_state::start(voice, audio.boink_offset_ms, audio.boink_loop);
    audio.next_voice = (audio.next_voice + 1) % audio.voices.size();
}

void soundfx::stop(SoundIdentifier id)
{
    auto &audio = *m_audio;
    audio.require_loaded(id);
    if (!audio.mixer)
    {
        return;
    }
    if (auto *t = audio.track(id))
    {
        MIX_StopTrack(t->track, 0);
    }
    else
    {
        for (auto *voice : audio.voices)
        {
            MIX_StopTrack(voice, 0);
        }
    }
}

bool soundfx::is_playing(SoundIdentifier id) const
{
    const auto &audio = *m_audio;
    audio.require_loaded(id);
    if (!audio.mixer)
    {
        return false;
    }
    if (const auto *t = audio.track(id))
    {
        return MIX_TrackPlaying(t->track);
    }
    return std::any_of(audio.voices.begin(), audio.voices.end(), [](auto *voice)
                       { return MIX_TrackPlaying(voice); });
}
