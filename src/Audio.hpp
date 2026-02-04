#ifndef AUDIO_HPP
#define AUDIO_HPP

#include <cstdint>
#include <memory>
#include <string_view>

struct SDL_AudioStream;

/// @brief Audio resource class using SDL3 audio system
/// @details Manages audio playback for both sound effects and music
/// Supports WAV and OGG file formats through SDL3
class Audio
{
public:
    Audio();
    ~Audio() noexcept;

    // Non-copyable, movable
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&& other) noexcept;
    Audio& operator=(Audio&& other) noexcept;

    /// Load audio from file
    /// @param filename Path to audio file (WAV or OGG)
    /// @return true if successful, false otherwise
    bool loadFromFile(std::string_view filename) noexcept;

    /// Play the audio
    void play() noexcept;

    /// Pause the audio
    void pause() noexcept;

    /// Stop the audio
    void stop() noexcept;

    /// Set volume (0.0 to 1.0)
    void setVolume(float volume) noexcept;

    /// Get current volume (0.0 to 1.0)
    [[nodiscard]] float getVolume() const noexcept;

    /// Check if audio is currently playing
    [[nodiscard]] bool isPlaying() const noexcept;

private:
    void cleanup() noexcept;

private:
    SDL_AudioStream* mAudioStream;
    std::uint8_t* mAudioBuffer;
    std::uint32_t mAudioLength;
    float mVolume;
    bool mIsPlaying;
};

#endif // AUDIO_HPP
