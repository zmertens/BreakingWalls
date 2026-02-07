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
    explicit Impl(SoundBufferManager& soundBuffers)
        : mSoundBuffers{ soundBuffers }
        , mSounds{}
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
        mSounds.emplace_back(sf::Sound{ mSoundBuffers.get(effect) });
        sf::Sound& sound = mSounds.back();

        // Set 3D position: X same, Y negated (audio Y is up), Z=0 (sound in 2D plane)
        sound.setPosition(sf::Vector3f{ position.x, -position.y, 0.f });
        sound.setAttenuation(ATTENUATION);
        sound.setMinDistance(MIN_DISTANCE_3D);

        sound.play();
    }

    void removeStoppedSounds()
    {
        std::erase_if(mSounds, [](const sf::Sound& sound) {
            return sound.getStatus() == sf::Sound::Status::Stopped;
            });
    }

    // Set listener position in 2D game coordinates
    void setListenerPosition(sf::Vector2f position)
    {
        // Convert 2D position to 3D listener position
        // X stays the same, Y is negated (audio Y is up), Z is listener distance from sound plane
        sf::Listener::setPosition(sf::Vector3f{ position.x, -position.y, LISTENER_Z });
    }

    // Get listener position in 2D game coordinates
    sf::Vector2f getListenerPosition() const
    {
        sf::Vector3f pos = sf::Listener::getPosition();
        // Convert back from 3D audio to 2D game coordinates (negate Y back)
        return sf::Vector2f{ pos.x, -pos.y };
    }

private:
    SoundBufferManager& mSoundBuffers;
    std::list<sf::Sound> mSounds;
};

// SoundPlayer public API

SoundPlayer::SoundPlayer(SoundBufferManager& soundBuffers)
    : mImpl{ std::make_unique<Impl>(soundBuffers) }
{
}

SoundPlayer::~SoundPlayer() = default;

SoundPlayer::SoundPlayer(SoundPlayer&&) noexcept = default;
SoundPlayer& SoundPlayer::operator=(SoundPlayer&&) noexcept = default;

void SoundPlayer::play(SoundEffect::ID effect)
{
    mImpl->play(effect);
}

void SoundPlayer::play(SoundEffect::ID effect, sf::Vector2f position)
{
    mImpl->play(effect, position);
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
