#ifndef SPLASH_STATE_HPP
#define SPLASH_STATE_HPP

#include "Camera.hpp"
#include "Shader.hpp"
#include "State.hpp"
#include "World.hpp"

#include <glad/glad.h>

#include <glm/glm.hpp>

class Texture;

class SplashState : public State
{
public:
    explicit SplashState(StateStack &stack, Context context);
    ~SplashState() override;

    // Non-copyable, movable
    SplashState(const SplashState &) = delete;
    SplashState &operator=(const SplashState &) = delete;
    SplashState(SplashState &&) noexcept;
    SplashState &operator=(SplashState &&) noexcept;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event &event) noexcept override;

private:
    bool isLoadingComplete() const noexcept;
    SoundPlayer *mWhiteNoise;
    Texture* mSplashTexture;
};

#endif // SPLASH_STATE_HPP
