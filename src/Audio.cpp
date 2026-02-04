#include "Audio.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

// stb_vorbis for OGG support
#define STB_VORBIS_HEADER_ONLY
#include <stb/stb_vorbis.c>

Audio::Audio()
    : mAudioStream{nullptr}
    , mAudioBuffer{nullptr}
    , mAudioLength{0}
    , mVolume{1.0f}
    , mIsPlaying{false}
{
}

Audio::~Audio() noexcept
{
    cleanup();
}

Audio::Audio(Audio&& other) noexcept
    : mAudioStream{other.mAudioStream}
    , mAudioBuffer{other.mAudioBuffer}
    , mAudioLength{other.mAudioLength}
    , mVolume{other.mVolume}
    , mIsPlaying{other.mIsPlaying}
{
    other.mAudioStream = nullptr;
    other.mAudioBuffer = nullptr;
    other.mAudioLength = 0;
    other.mVolume = 1.0f;
    other.mIsPlaying = false;
}

Audio& Audio::operator=(Audio&& other) noexcept
{
    if (this != &other)
    {
        cleanup();

        mAudioStream = other.mAudioStream;
        mAudioBuffer = other.mAudioBuffer;
        mAudioLength = other.mAudioLength;
        mVolume = other.mVolume;
        mIsPlaying = other.mIsPlaying;

        other.mAudioStream = nullptr;
        other.mAudioBuffer = nullptr;
        other.mAudioLength = 0;
        other.mVolume = 1.0f;
        other.mIsPlaying = false;
    }
    return *this;
}

bool Audio::loadFromFile(std::string_view filename) noexcept
{
    cleanup();

    // Check file extension to determine format
    std::string filenameStr(filename);
    bool isOgg = filenameStr.find(".ogg") != std::string::npos || 
                 filenameStr.find(".OGG") != std::string::npos;

    SDL_AudioSpec spec;
    std::uint8_t* audioBuffer = nullptr;
    std::uint32_t audioLength = 0;

    if (isOgg)
    {
        // Load OGG file using stb_vorbis
        int channels = 0;
        int sampleRate = 0;
        short* samples = nullptr;
        int numSamples = stb_vorbis_decode_filename(filename.data(), &channels, &sampleRate, &samples);
        
        if (numSamples <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to load OGG file %s", filename.data());
            return false;
        }

        // Check for potential integer overflow before converting to bytes
        const std::int64_t totalBytes = static_cast<std::int64_t>(numSamples) * channels * sizeof(short);
        if (totalBytes > UINT32_MAX || totalBytes <= 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio file too large or invalid size for %s", filename.data());
            free(samples);
            return false;
        }

        // Convert to bytes
        audioLength = static_cast<std::uint32_t>(totalBytes);
        audioBuffer = static_cast<std::uint8_t*>(SDL_malloc(audioLength));
        if (!audioBuffer)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to allocate audio buffer for %s", filename.data());
            free(samples);
            return false;
        }
        std::memcpy(audioBuffer, samples, audioLength);
        free(samples);

        // Set up SDL audio spec for decoded OGG data
        spec.freq = sampleRate;
        spec.format = SDL_AUDIO_S16;
        spec.channels = static_cast<std::uint8_t>(channels);
    }
    else
    {
        // Load WAV file using SDL
        if (!SDL_LoadWAV(filename.data(), &spec, &audioBuffer, &audioLength))
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to load WAV file %s: %s", 
                         filename.data(), SDL_GetError());
            return false;
        }
    }

    // Create audio stream for playback
    mAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 
                                              &spec, nullptr, nullptr);
    if (!mAudioStream)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to create audio stream: %s", 
                     SDL_GetError());
        if (isOgg)
            SDL_free(audioBuffer);
        else
            SDL_free(audioBuffer);
        return false;
    }

    mAudioBuffer = audioBuffer;
    mAudioLength = audioLength;

    SDL_Log("Audio loaded successfully: %s (length: %u bytes)", filename.data(), audioLength);
    return true;
}

void Audio::play() noexcept
{
    if (!mAudioStream || !mAudioBuffer)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Cannot play: audio not loaded");
        return;
    }

    // Put audio data into the stream
    if (!SDL_PutAudioStreamData(mAudioStream, mAudioBuffer, mAudioLength))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to put audio data: %s", SDL_GetError());
        return;
    }

    // Resume the stream
    if (!SDL_ResumeAudioStreamDevice(mAudioStream))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to resume audio stream: %s", SDL_GetError());
        return;
    }

    mIsPlaying = true;
}

void Audio::pause() noexcept
{
    if (mAudioStream)
    {
        SDL_PauseAudioStreamDevice(mAudioStream);
        mIsPlaying = false;
    }
}

void Audio::stop() noexcept
{
    if (mAudioStream)
    {
        SDL_PauseAudioStreamDevice(mAudioStream);
        SDL_ClearAudioStream(mAudioStream);
        mIsPlaying = false;
    }
}

void Audio::setVolume(float volume) noexcept
{
    mVolume = std::clamp(volume, 0.0f, 1.0f);
    
    if (mAudioStream)
    {
        SDL_SetAudioStreamGain(mAudioStream, mVolume);
    }
}

float Audio::getVolume() const noexcept
{
    return mVolume;
}

bool Audio::isPlaying() const noexcept
{
    return mIsPlaying;
}

void Audio::cleanup() noexcept
{
    if (mAudioStream)
    {
        SDL_DestroyAudioStream(mAudioStream);
        mAudioStream = nullptr;
    }
    
    if (mAudioBuffer)
    {
        SDL_free(mAudioBuffer);
        mAudioBuffer = nullptr;
    }
    
    mAudioLength = 0;
    mIsPlaying = false;
}
