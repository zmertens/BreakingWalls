#include "MusicPlayer.hpp"

#include <string>

#include <SFML/Audio.hpp>
#include <SFML/Audio/Listener.hpp>

#include <iostream>

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
        if (auto result = mMusic.openFromFile(std::string(filename)))
        {            
            // Make music non-spatialized (always at full volume regardless of listener position)
            mMusic.setRelativeToListener(true);
            
            return true;
        }
        else
        {
            std::cerr << "MusicPlayer: Failed to open: " << filename.data() << "\n";
            std::cerr << "MusicPlayer: This could mean: file not found, unsupported format, or corrupted file\n";
        }
        return false;
    }

    void play() noexcept
    {        
        mMusic.play();

        if (auto status = mMusic.getStatus(); status != sf::Music::Status::Playing)
        {
            std::cerr << "MusicPlayer: Music did not start playing!\n";
            std::cerr << "  Status: " << static_cast<int>(status) << " (expected 2 for Playing)\n";
            std::cerr << "  Possible causes: audio device unavailable, audio system stopped, or file issue\n";
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

    void print() const noexcept {
        auto status = mMusic.getStatus();
        std::cerr << "MusicPlayer: Status: " << (status == sf::Music::Status::Playing ? "Playing" : status == sf::Music::Status::Paused ? "Paused" : "Stopped") << "\n";
        std::cerr << "MusicPlayer: Looping? " << (mLoop ? "Yes" : "No") << "\n";
        std::cerr << "MusicPlayer: Volume: " << mVolume << "\n";
        std::cerr << "MusicPlayer: Duration: " << mMusic.getDuration().asSeconds() << " seconds\n";
        std::cerr << "MusicPlayer: Channel count: " << mMusic.getChannelCount() << "\n";
        std::cerr << "MusicPlayer: Sample rate: " << mMusic.getSampleRate() << " Hz\n";
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

void MusicPlayer::print() const noexcept
{
    mImpl->print();
}