#include "SDLAudioStream.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>

// Extern declarations for Simplex noise functions from noise.c
extern "C" {
    float simplex2(float x, float y, int octaves, float persistence, float lacunarity);
    float simplex3(float x, float y, float z, int octaves, float persistence, float lacunarity);
}

SDLAudioStream::SDLAudioStream() = default;

SDLAudioStream::~SDLAudioStream()
{
    stop();
}

SDLAudioStream::SDLAudioStream(SDLAudioStream&& other) noexcept
    : mStream(other.mStream)
    , mSpec(other.mSpec)
    , mCallback(std::move(other.mCallback))
    , mIsPlaying(other.mIsPlaying)
    , mCurrentSineSample(other.mCurrentSineSample)
    , mSineFrequency(other.mSineFrequency)
    , mSineVolume(other.mSineVolume)
    , mSineDuration(other.mSineDuration)
    , mSineElapsed(other.mSineElapsed)
    , mCurrentNoiseSample(other.mCurrentNoiseSample)
    , mNoiseVolume(other.mNoiseVolume)
    , mNoiseDuration(other.mNoiseDuration)
    , mNoiseScale(other.mNoiseScale)
{
    other.mStream = nullptr;
    other.mIsPlaying = false;
}

SDLAudioStream& SDLAudioStream::operator=(SDLAudioStream&& other) noexcept
{
    if (this != &other)
    {
        stop();

        mStream = other.mStream;
        mSpec = other.mSpec;
        mCallback = std::move(other.mCallback);
        mIsPlaying = other.mIsPlaying;
        mCurrentSineSample = other.mCurrentSineSample;
        mSineFrequency = other.mSineFrequency;
        mSineVolume = other.mSineVolume;
        mSineDuration = other.mSineDuration;
        mSineElapsed = other.mSineElapsed;
        mCurrentNoiseSample = other.mCurrentNoiseSample;
        mNoiseVolume = other.mNoiseVolume;
        mNoiseDuration = other.mNoiseDuration;
        mNoiseScale = other.mNoiseScale;

        other.mStream = nullptr;
        other.mIsPlaying = false;
    }
    return *this;
}

bool SDLAudioStream::initialize(int freq, int channels, AudioCallback callback)
{
    // Store the spec first (needed for generateSineWave if callback is nullptr)
    mSpec.freq = freq;
    mSpec.format = SDL_AUDIO_F32;  // 32-bit float (matches official example)
    mSpec.channels = channels;

    // If no callback provided, check if we have one from generateSineWave()
    if (!callback && !mCallback)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDLAudioStream: No audio callback provided");
        return false;
    }

    // Use provided callback or keep existing one (from generateSineWave)
    if (callback)
    {
        mCallback = callback;
    }

    SDL_Log("SDLAudioStream: Initializing with %d Hz, %d channels", freq, channels);

    // Open the audio device stream (combines device opening + stream creation + binding)
    // This is the modern SDL3 API as shown in the official example
    mStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &mSpec,
        sdlAudioCallbackWrapper,
        this  // userdata passed to callback
    );

    if (!mStream)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDLAudioStream: Failed to open audio device stream: %s", SDL_GetError());
        return false;
    }

    SDL_Log("SDLAudioStream: Audio device stream opened successfully");
    SDL_Log("  - Sample rate: %d Hz", mSpec.freq);
    SDL_Log("  - Channels: %d", mSpec.channels);
    SDL_Log("  - Format: F32");

    return true;
}

void SDLAudioStream::play()
{
    if (!mStream)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "SDLAudioStream: Cannot play - not initialized");
        return;
    }

    // SDL_OpenAudioDeviceStream starts the device paused
    // Use SDL_ResumeAudioStreamDevice to start it (not SDL_ResumeAudioDevice)
    if (SDL_ResumeAudioStreamDevice(mStream))
    {
        mIsPlaying = true;
        SDL_Log("SDLAudioStream: Playback started");
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDLAudioStream: Failed to resume audio stream: %s", SDL_GetError());
    }
}

void SDLAudioStream::pause()
{
    if (!mStream)
    {
        return;
    }

    if (SDL_PauseAudioStreamDevice(mStream))
    {
        mIsPlaying = false;
        SDL_Log("SDLAudioStream: Playback paused");
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "SDLAudioStream: Failed to pause audio stream: %s", SDL_GetError());
    }
}

void SDLAudioStream::stop()
{
    if (mStream)
    {
        // SDL will clean up the audio device for us when we destroy the stream
        SDL_DestroyAudioStream(mStream);
        SDL_Log("SDLAudioStream: Audio stream destroyed");
        mStream = nullptr;
    }

    mIsPlaying = false;
}

void SDLCALL SDLAudioStream::sdlAudioCallbackWrapper(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)
{
    auto* audioStream = static_cast<SDLAudioStream*>(userdata);
    if (!audioStream || !audioStream->mCallback)
    {
        return;
    }

    // Call the user callback with the SDL3 signature
    audioStream->mCallback(stream, additional_amount, total_amount);
}

void SDLAudioStream::generateSineWave(float frequency, float duration, float volume)
{
    mSineFrequency = frequency;
    mSineDuration = duration;
    mSineVolume = std::clamp(volume, 0.0f, 1.0f);
    mCurrentSineSample = 0;
    mSineElapsed = 0.0f;

    SDL_Log("SDLAudioStream: Configuring sine wave - %.2f Hz, %.2f seconds, %.2f%% volume",
        frequency, duration, volume * 100.0f);

    // Set up callback to generate sine wave (following the official SDL3 example pattern)
    // Note: mSpec.freq might not be set yet if this is called before initialize()
    mCallback = [this](SDL_AudioStream* stream, int additional_amount, int total_amount) {
        // Use mSpec.freq which will be set by initialize()
        const int sample_rate = mSpec.freq;
        
        // Convert from bytes to samples (each sample is a float)
        additional_amount /= sizeof(float);
        
        while (additional_amount > 0)
        {
            // Feed 128 samples at a time (matches official example)
            float samples[128];
            const int total = SDL_min(additional_amount, static_cast<int>(SDL_arraysize(samples)));
            
            // Calculate how many samples we've generated in total
            const float total_seconds = static_cast<float>(mCurrentSineSample) / static_cast<float>(sample_rate);
            
            // Generate the sine wave samples
            for (int i = 0; i < total; i++)
            {
                // Check if we've exceeded the duration
                if (total_seconds + (static_cast<float>(i) / static_cast<float>(sample_rate)) >= mSineDuration)
                {
                    // Fill rest with silence
                    for (int j = i; j < total; j++)
                    {
                        samples[j] = 0.0f;
                    }
                    break;
                }
                
                // Generate sine wave (following official example pattern)
                const float phase = static_cast<float>(mCurrentSineSample + i) * mSineFrequency / static_cast<float>(sample_rate);
                samples[i] = SDL_sinf(phase * 2.0f * SDL_PI_F) * mSineVolume;
            }
            
            mCurrentSineSample += total;
            
            // Wrap around to avoid floating-point errors (following official example)
            mCurrentSineSample %= sample_rate;
            
            // Feed the new data to the stream
            SDL_PutAudioStreamData(stream, samples, total * sizeof(float));
            additional_amount -= total;
        }
    };
}

void SDLAudioStream::generateWhiteNoise(float duration, float volume, float scale)
{
    mNoiseVolume = std::clamp(volume, 0.0f, 1.0f);
    mNoiseDuration = duration;
    mNoiseScale = std::max(0.1f, scale);  // Ensure scale is positive
    mCurrentNoiseSample = 0;

    SDL_Log("SDLAudioStream: Configuring white noise - %.2f seconds, %.2f%% volume, scale: %.2f",
        duration, volume * 100.0f, scale);

    // Set up callback to generate white noise using Simplex noise
    mCallback = [this](SDL_AudioStream* stream, int additional_amount, int total_amount) {
        const int sample_rate = mSpec.freq;
        
        // Convert from bytes to samples (each sample is a float)
        additional_amount /= sizeof(float);
        
        while (additional_amount > 0)
        {
            // Feed 128 samples at a time
            float samples[128];
            const int total = SDL_min(additional_amount, static_cast<int>(SDL_arraysize(samples)));
            
            // Calculate current time in seconds
            const float current_time = static_cast<float>(mCurrentNoiseSample) / static_cast<float>(sample_rate);
            
            // Generate white noise samples using Simplex noise
            for (int i = 0; i < total; i++)
            {
                // Check if we've exceeded the duration
                const float sample_time = current_time + (static_cast<float>(i) / static_cast<float>(sample_rate));
                if (sample_time >= mNoiseDuration)
                {
                    // Fill rest with silence
                    for (int j = i; j < total; j++)
                    {
                        samples[j] = 0.0f;
                    }
                    break;
                }
                
                // Use 2D simplex noise for white noise generation
                // x varies with sample count, y is a constant for variety
                const float x = (static_cast<float>(mCurrentNoiseSample + i) / static_cast<float>(sample_rate)) * mNoiseScale;
                const float y = mNoiseScale * 0.5f;
                
                // Generate noise value (simplex2 returns value in range [0, 1])
                // We map it to [-1, 1] by multiplying by 2 and subtracting 1
                const float noise_value = simplex2(x, y, 1, 1.0f, 2.0f) * 2.0f - 1.0f;
                samples[i] = noise_value * mNoiseVolume;
            }
            
            mCurrentNoiseSample += total;
            
            // Wrap around to avoid integer overflow
            if (mCurrentNoiseSample > sample_rate * 10)
            {
                mCurrentNoiseSample = 0;
            }
            
            // Feed the new data to the stream
            SDL_PutAudioStreamData(stream, samples, total * sizeof(float));
            additional_amount -= total;
        }
    };
}
