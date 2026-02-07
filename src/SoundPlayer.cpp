#include "SoundPlayer.hpp"

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
    // ListenerZ: Distance from listener to the 2D sound plane
    const float ListenerZ = 300.f;
    // Attenuation: How fast sound volume decreases with distance
    const float Attenuation = 8.f;
    // MinDistance2D: Distance in 2D at which sound is at full volume
    const float MinDistance2D = 200.f;
    // MinDistance3D: Actual 3D minimum distance calculated with Pythagorean theorem
    const float MinDistance3D = std::sqrt(MinDistance2D * MinDistance2D + ListenerZ * ListenerZ);
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
        sound.setAttenuation(Attenuation);
        sound.setMinDistance(MinDistance3D);

        sound.play();
    }

    void removeStoppedSounds()
    {
        // Use erase-remove idiom with ranges
        auto removed = std::ranges::remove_if(mSounds, [](const sf::Sound& sound) {
            return sound.getStatus() == sf::Sound::Status::Stopped;
            });
        mSounds.erase(removed.begin(), removed.end());
    }

    // Set listener position in 2D game coordinates
    void setListenerPosition(sf::Vector2f position)
    {
        // Convert 2D position to 3D listener position
        // X stays the same, Y is negated (audio Y is up), Z is listener distance from sound plane
        sf::Listener::setPosition(sf::Vector3f{ position.x, -position.y, ListenerZ });
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
