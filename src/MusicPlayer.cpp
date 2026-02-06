#include "MusicPlayer.hpp"

#include <string>

#include <SFML/Audio.hpp>

class MusicPlayer::Impl
{
public:
    Impl() : mVolume{100.f}, mLoop{false}
    {
    }

    ~Impl() = default;

    bool openFromFile(std::string_view filename)
    {
        mFilename = filename;
        return mMusic.openFromFile(std::string(filename));
    }

    void play() noexcept
    {
        mMusic.play();
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
        return mMusic.getStatus() == sf::Music::Status::Playing;
    }

private:
    sf::Music mMusic;
    std::string mFilename;
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

