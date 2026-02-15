#include "MusicPlayer.hpp"

#include <string>

#include <SFML/Audio.hpp>
#include <SFML/Audio/Listener.hpp>

#include <SDL3/SDL.h>

class MusicPlayer::Impl
{
public:
    Impl() : mVolume{100.f}, mLoop{false}
    {
        // Ensure SFML global volume is at maximum
        sf::Listener::setGlobalVolume(100.f);
    }

    bool openFromFile(std::string_view filename)
    {
        SDL_Log("MusicPlayer: Attempting to open file: %s", filename.data());
        
        if (auto result = mMusic.openFromFile(std::string(filename)))
        {
            SDL_Log("MusicPlayer: Successfully opened file: %s", filename.data());
            SDL_Log("MusicPlayer: Duration: %.2f seconds", mMusic.getDuration().asSeconds());
            SDL_Log("MusicPlayer: Channel count: %u", mMusic.getChannelCount());
            SDL_Log("MusicPlayer: Sample rate: %u Hz", mMusic.getSampleRate());
            
            // Make music non-spatialized (always at full volume regardless of listener position)
            mMusic.setRelativeToListener(true);
            SDL_Log("MusicPlayer: Set music to be relative to listener (non-spatialized)");
            
            return true;
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                         "MusicPlayer: Failed to open: %s", filename.data());
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                         "MusicPlayer: This could mean: file not found, unsupported format, or corrupted file");
        }
        return false;
    }

    void play() noexcept
    {
        SDL_Log("MusicPlayer: Calling play()...");
        SDL_Log("MusicPlayer: Current status before play: %d (0=Stopped, 1=Paused, 2=Playing)",
                static_cast<int>(mMusic.getStatus()));
        SDL_Log("MusicPlayer: Volume: %.1f", mVolume);
        SDL_Log("MusicPlayer: Loop: %s", mLoop ? "true" : "false");
        
        mMusic.play();
        
        auto status = mMusic.getStatus();
        SDL_Log("MusicPlayer: Status after play: %d (0=Stopped, 1=Paused, 2=Playing)",
                static_cast<int>(status));

        if (status != sf::Music::Status::Playing)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                        "MusicPlayer: ⚠ Music did not start playing!");
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                        "  Status: %d (expected 2 for Playing)", static_cast<int>(status));
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                        "  Possible causes: audio device unavailable, audio system stopped, or file issue");
        }
        else
        {
            SDL_Log("MusicPlayer: Music is now playing!");
        }
    }

    void stop() noexcept
    {
        mMusic.stop();
    }

    void setVolume(float volume) noexcept
    {
        mVolume = volume;
        mMusic.setVolume(volume);
    }

    void setLoop(bool loop) noexcept
    {
        mLoop = loop;
        mMusic.setLooping(loop);
    }

    void setPaused(bool paused) noexcept
    {
        if (paused)
        {
            mMusic.pause();
        }
        else
        {
            mMusic.play();
        }
    }

    bool isPlaying() const noexcept
    {
        auto status = mMusic.getStatus();
        bool playing = (status == sf::Music::Status::Playing);
        // Only log on state changes to reduce spam
        static sf::Music::Status lastLoggedStatus = sf::Music::Status::Stopped;
        if (status != lastLoggedStatus)
        {
            lastLoggedStatus = status;
        }
        return playing;
    }

private:
    sf::Music mMusic;
    float mVolume;
    bool mLoop;
};

// MusicPlayer public API

MusicPlayer::MusicPlayer()
    : mImpl{std::make_unique<Impl>()}
{
}

MusicPlayer::~MusicPlayer() = default;

MusicPlayer::MusicPlayer(MusicPlayer &&) noexcept = default;
MusicPlayer &MusicPlayer::operator=(MusicPlayer &&) noexcept = default;

bool MusicPlayer::openFromFile(std::string_view filename)
{
    return mImpl->openFromFile(filename);
}

void MusicPlayer::play() noexcept
{
    mImpl->play();
}

void MusicPlayer::stop() noexcept
{
    mImpl->stop();
}

void MusicPlayer::setPaused(bool paused) noexcept
{
    mImpl->setPaused(paused);
}

void MusicPlayer::setVolume(float volume) noexcept
{
    mImpl->setVolume(volume);
}

void MusicPlayer::setLoop(bool loop) noexcept
{
    mImpl->setLoop(loop);
}

bool MusicPlayer::isPlaying() const noexcept
{
    return mImpl->isPlaying();
}
