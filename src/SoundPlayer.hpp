#ifndef SOUNDPLAYER_HPP
#define SOUNDPLAYER_HPP

#include <memory>
#include <string_view>

#include "ResourceIdentifiers.hpp"

#include <SFML/System/Vector2.hpp>

class SoundPlayer
{
public:
    /// @brief Constructor that takes a reference to an external SoundBufferManager
    explicit SoundPlayer(SoundBufferManager &soundBuffers);

    /// @brief Destructor
    ~SoundPlayer();

    // Non-copyable, movable
    SoundPlayer(const SoundPlayer &) = delete;
    SoundPlayer &operator=(const SoundPlayer &) = delete;
    SoundPlayer(SoundPlayer &&) noexcept;
    SoundPlayer &operator=(SoundPlayer &&) noexcept;

    void play(SoundEffect::ID effect);
    void play(SoundEffect::ID effect,
              sf::Vector2f position);
    void stop(SoundEffect::ID effect);
    void removeStoppedSounds();
    void setListenerPosition(sf::Vector2f position);
    sf::Vector2f getListenerPosition() const;

    /// @brief Set the volume for all sound effects (0-100)
    void setVolume(float volume);

    /// @brief Get the current volume (0-100)
    float getVolume() const;

    /// @brief Enable or disable sound effects
    void setEnabled(bool enabled);

    /// @brief Check if sound effects are enabled
    bool isEnabled() const;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

#endif // SOUNDPLAYER_HPP
