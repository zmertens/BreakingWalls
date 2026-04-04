#ifndef PAUSE_STATE_HPP
#define PAUSE_STATE_HPP

#include "State.hpp"

class Font;
class MusicPlayer;

class PauseState : public State
{
public:
    explicit PauseState(StateStack &stack, Context context);

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const sf::Event &event) noexcept override;

private:
    Font *mFont;
    MusicPlayer *mMusic;
    bool mPlanetComplete;

    mutable unsigned int mSelectedMenuItem;
};

#endif // PAUSE_STATE_HPP
