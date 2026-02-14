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
        SDL_Log("SFML global audio volume set to 100%%");
    }

    bool openFromFile(std::string_view filename)
    {
        if (auto result = mMusic.openFromFile(std::string(filename)))
        {
            SDL_Log("MusicPlayer: ? Successfully opened: %s", filename.data());
            SDL_Log("  Duration: %.2f seconds", mMusic.getDuration().asSeconds());
            return true;
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                "MusicPlayer: ? Failed to open: %s", filename.data());
        }
        return false;
    }

    void play() noexcept
    {
        mMusic.play();

        if (mMusic.getStatus() != sf::Music::Status::Playing)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                "MusicPlayer: Music did not start playing!");
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                "  Possible causes: audio device unavailable or audio system stopped");
        }
        else
        {
            SDL_Log("MusicPlayer: ? Music playback confirmed (loop=%s, volume=%.1f%%)",
                mMusic.isLooping() ? "true" : "false",
                mMusic.getVolume());
        }
    }

    void stop() noexcept
    {
        SDL_Log("MusicPlayer: stop() called");
        mMusic.stop();
    }

    void setVolume(float volume) noexcept
    {
        mVolume = volume;
        mMusic.setVolume(volume);
        SDL_Log("MusicPlayer: Volume set to %.1f%%", volume);
    }

    void setLoop(bool loop) noexcept
    {
        mLoop = loop;
        mMusic.setLooping(loop);
        SDL_Log("MusicPlayer: Loop set to %s", loop ? "true" : "false");
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
            SDL_Log("MusicPlayer: isPlaying() = %s (status=%d)",
                playing ? "true" : "false", static_cast<int>(status));
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

MusicPlayer::MusicPlayer(MusicPlayer&&) noexcept = default;
MusicPlayer& MusicPlayer::operator=(MusicPlayer&&) noexcept = default;

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

