#include "SoundPlayer.hpp"

#include <algorithm>
#include <cmath>
#include <list>
#include <ranges>
#include <string>

#include <SFML/Audio/Listener.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include "ResourceManager.hpp"
#include "SDLAudioStream.hpp"

namespace
{
    // Spatial audio constants following SFML pattern
    // LISTENER_Z: Distance from listener to the 2D sound plane
    const float LISTENER_Z = 300.f;
    // ATTENUATION: How fast sound volume decreases with distance
    const float ATTENUATION = 8.f;
    // MIN_DISTANCE_2D: Distance in 2D at which sound is at full volume
    const float MIN_DISTANCE_2D = 200.f;
    // MIN_DISTANCE_3D: Actual 3D minimum distance calculated with Pythagorean theorem
    const float MIN_DISTANCE_3D = std::sqrt(MIN_DISTANCE_2D * MIN_DISTANCE_2D + LISTENER_Z * LISTENER_Z);
}

class SoundPlayer::Impl
{
public:
    explicit Impl(SoundBufferManager &soundBuffers)
        : mSoundBuffers{soundBuffers}, mSounds{}, mVolume{100.0f}, mEnabled{true}, mStream{}, mStreamInitialized{false}
    {
    }

    ~Impl() = default;

    // Play non-spatialized sound (at listener position for full volume)
    void play(SoundEffect::ID effect)
    {
        play(effect, getListenerPosition());
    }

    // Play spatialized sound at a 2D position in the game world
    void play(SoundEffect::ID effect, sf::Vector2f position)
    {
        if (!mEnabled)
        {
            return;
        }

        if (effect == SoundEffect::ID::WHITE_NOISE)
        {
            playStreamEffect();
            return;
        }

        mSounds.emplace_back(sf::Sound{mSoundBuffers.get(effect)});
        sf::Sound &sound = mSounds.back();

        // Set 3D position: X same, Y negated (audio Y is up), Z=0 (sound in 2D plane)
        sound.setPosition(sf::Vector3f{position.x, -position.y, 0.f});
        sound.setAttenuation(ATTENUATION);
        sound.setMinDistance(MIN_DISTANCE_3D);
        sound.setVolume(mVolume);

        sound.play();
    }

    void removeStoppedSounds()
    {
        std::erase_if(mSounds, [](const sf::Sound &sound)
                      { return sound.getStatus() == sf::Sound::Status::Stopped; });
    }

    void stop(SoundEffect::ID effect)
    {
        if (effect == SoundEffect::ID::WHITE_NOISE)
        {
            if (mStreamInitialized)
            {
                mStream.pause();
            }
            return;
        }
    }

    // Set listener position in 2D game coordinates
    void setListenerPosition(sf::Vector2f position)
    {
        // Convert 2D position to 3D listener position
        // X stays the same, Y is negated (audio Y is up), Z is listener distance from sound plane
        sf::Listener::setPosition(sf::Vector3f{position.x, -position.y, LISTENER_Z});
    }

    // Get listener position in 2D game coordinates
    sf::Vector2f getListenerPosition() const
    {
        sf::Vector3f pos = sf::Listener::getPosition();
        // Convert back from 3D audio to 2D game coordinates (negate Y back)
        return sf::Vector2f{pos.x, -pos.y};
    }

    void setVolume(float volume)
    {
        mVolume = std::clamp(volume, 0.0f, 100.0f);
        // Update volume for all currently playing sounds
        for (auto &sound : mSounds)
        {
            sound.setVolume(mVolume);
        }
    }

    float getVolume() const
    {
        return mVolume;
    }

    void setEnabled(bool enabled)
    {
        mEnabled = enabled;
        if (!mEnabled)
        {
            // Stop all currently playing sounds when disabled
            for (auto &sound : mSounds)
            {
                sound.stop();
            }

            if (mStreamInitialized)
            {
                mStream.pause();
            }
        }
    }

    bool isEnabled() const
    {
        return mEnabled;
    }

private:
    void playStreamEffect()
    {
        constexpr int STREAM_SAMPLE_RATE = 48000;
        constexpr int STREAM_CHANNELS = 1;
        constexpr float LOOP_DURATION_SECONDS = 0.0f;
        constexpr float NOISE_SCALE = 20.0f;

        if (!mStreamInitialized)
        {
            mStream.generateWhiteNoise(LOOP_DURATION_SECONDS, mVolume / 100.0f, NOISE_SCALE);
            mStreamInitialized = mStream.initialize(STREAM_SAMPLE_RATE, STREAM_CHANNELS, {});
            if (!mStreamInitialized)
            {
                return;
            }
        }

        if (!mStream.isPlaying())
        {
            mStream.play();
        }
    }

    SoundBufferManager &mSoundBuffers;
    std::list<sf::Sound> mSounds;
    float mVolume;
    bool mEnabled;
    SDLAudioStream mStream;
    bool mStreamInitialized;
};

// SoundPlayer public API

SoundPlayer::SoundPlayer(SoundBufferManager &soundBuffers)
    : mImpl{std::make_unique<Impl>(soundBuffers)}
{
}

SoundPlayer::~SoundPlayer() = default;

SoundPlayer::SoundPlayer(SoundPlayer &&) noexcept = default;
SoundPlayer &SoundPlayer::operator=(SoundPlayer &&) noexcept = default;

void SoundPlayer::play(SoundEffect::ID effect)
{
    mImpl->play(effect);
}

void SoundPlayer::play(SoundEffect::ID effect, sf::Vector2f position)
{
    mImpl->play(effect, position);
}

void SoundPlayer::stop(SoundEffect::ID effect)
{
    mImpl->stop(effect);
}

void SoundPlayer::removeStoppedSounds()
{
    mImpl->removeStoppedSounds();
}

void SoundPlayer::setListenerPosition(sf::Vector2f position)
{
    mImpl->setListenerPosition(position);
}

sf::Vector2f SoundPlayer::getListenerPosition() const
{
    return mImpl->getListenerPosition();
}

void SoundPlayer::setVolume(float volume)
{
    mImpl->setVolume(volume);
}

float SoundPlayer::getVolume() const
{
    return mImpl->getVolume();
}

void SoundPlayer::setEnabled(bool enabled)
{
    mImpl->setEnabled(enabled);
}

bool SoundPlayer::isEnabled() const
{
    return mImpl->isEnabled();
}
