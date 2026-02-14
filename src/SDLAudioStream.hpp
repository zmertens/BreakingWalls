#ifndef SDL_AUDIO_STREAM_HPP
#define SDL_AUDIO_STREAM_HPP

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

/// @brief SDL3 audio streaming wrapper for procedural audio and real-time effects
/// @details This class provides a modern C++ wrapper around SDL3's audio streaming API.
/// It can be used alongside SFML audio for scenarios requiring real-time audio processing.
class SDLAudioStream
{
public:
    /// @brief Audio callback function type (matches SDL3's callback signature)
    /// @param stream The audio stream being fed
    /// @param additional_amount How much more data is needed (in bytes)
    /// @param total_amount Total amount of data the stream is consuming right now (in bytes)
    using AudioCallback = std::function<void(SDL_AudioStream* stream, int additional_amount, int total_amount)>;

    /// @brief Constructor
    explicit SDLAudioStream();

    /// @brief Destructor
    ~SDLAudioStream();

    // Non-copyable, movable
    SDLAudioStream(const SDLAudioStream&) = delete;
    SDLAudioStream& operator=(const SDLAudioStream&) = delete;
    SDLAudioStream(SDLAudioStream&&) noexcept;
    SDLAudioStream& operator=(SDLAudioStream&&) noexcept;

    /// @brief Initialize the audio stream with specified parameters
    /// @param freq Sample rate (e.g., 8000, 44100, 48000)
    /// @param channels Number of audio channels (1=mono, 2=stereo)
    /// @param callback Audio callback function
    /// @return true if successful
    bool initialize(int freq, int channels, AudioCallback callback);

    /// @brief Start audio playback
    void play();

    /// @brief Pause audio playback
    void pause();

    /// @brief Stop and close the audio stream
    void stop();

    /// @brief Check if audio is currently playing
    /// @return true if playing
    bool isPlaying() const noexcept { return mIsPlaying; }

    /// @brief Get the current sample rate
    /// @return Sample rate in Hz
    int getSampleRate() const noexcept { return mSpec.freq; }

    /// @brief Get the number of channels
    /// @return Channel count
    int getChannels() const noexcept { return mSpec.channels; }

    /// @brief Generate a simple sine wave tone (for testing)
    /// @param frequency Frequency in Hz
    /// @param duration Duration in seconds
    /// @param volume Volume (0.0 to 1.0)
    void generateSineWave(float frequency, float duration, float volume = 0.5f);

    /// @brief Generate white noise using Simplex noise (procedural)
    /// @param duration Duration in seconds
    /// @param volume Volume (0.0 to 1.0)
    /// @param scale Noise scale/frequency (higher = more variation)
    void generateWhiteNoise(float duration, float volume = 0.5f, float scale = 1.0f);

private:
    /// @brief Internal SDL audio callback (static wrapper)
    static void SDLCALL sdlAudioCallbackWrapper(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);

    SDL_AudioStream* mStream{ nullptr };
    SDL_AudioSpec mSpec{};
    AudioCallback mCallback;
    bool mIsPlaying{ false };

    // For sine wave generation (example/testing)
    int mCurrentSineSample{ 0 };
    float mSineFrequency{ 440.0f };
    float mSineVolume{ 0.5f };
    float mSineDuration{ 0.0f };
    float mSineElapsed{ 0.0f };

    // For white noise generation
    int mCurrentNoiseSample{ 0 };
    float mNoiseVolume{ 0.5f };
    float mNoiseDuration{ 0.0f };
    float mNoiseScale{ 1.0f };
};

#endif // SDL_AUDIO_STREAM_HPP
