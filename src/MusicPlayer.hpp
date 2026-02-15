#ifndef MUSICPLAYER_HPP
#define MUSICPLAYER_HPP

#include <memory>
#include <string_view>

#include "ResourceIdentifiers.hpp"

class MusicPlayer
{
public:
    /// @brief Default constructor
    explicit MusicPlayer();

    /// @brief Destructor
    ~MusicPlayer();

    // Non-copyable, movable
    MusicPlayer(const MusicPlayer &) = delete;
    MusicPlayer &operator=(const MusicPlayer &) = delete;
    MusicPlayer(MusicPlayer &&) noexcept;
    MusicPlayer &operator=(MusicPlayer &&) noexcept;

    /// @brief Open a music file from disk (used by ResourceManager)
    /// @param filename Path to the music file
    /// @return true if the file was loaded successfully
    bool openFromFile(std::string_view filename);

    /// @brief Play the loaded music
    void play() noexcept;

    /// @brief Stop playing music
    void stop() noexcept;

    /// @brief Set the music volume
    /// @param volume Volume level (0-100)
    void setVolume(float volume) noexcept;

    /// @brief Set whether the music should loop
    /// @param loop True to loop, false to play once
    void setLoop(bool loop) noexcept;

    /// @brief Pause or unpause the music
    /// @param paused True to pause, false to resume
    void setPaused(bool paused) noexcept;

    /// @brief Check if the music is currently playing
    /// @return true if playing
    bool isPlaying() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

#endif // MUSICPLAYER_HPP
