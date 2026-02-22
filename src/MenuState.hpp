#ifndef MENU_STATE_HPP
#define MENU_STATE_HPP

#include "State.hpp"

#include <array>
#include <memory>

#include <glad/glad.h>
#include <glm/glm.hpp>

class Font;
class MusicPlayer;
class Shader;

class MenuState : public State
{
public:
    explicit MenuState(StateStack &stack, Context context);
    ~MenuState() override;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event &event) noexcept override;

private:
    enum class MenuItem : unsigned int
    {
        CONTINUE = 0,
        NEW_GAME = 1,
        NETWORK_GAME = 2,
        SETTINGS = 3,
        SPLASH = 4,
        QUIT = 5,
        COUNT = 6
    };

    Font *mFont;
    MusicPlayer *mMusic;

    // Navigation state variables
    mutable MenuItem mSelectedMenuItem;

    mutable bool mShowMainMenu;

    mutable std::array<bool, static_cast<size_t>(MenuItem::COUNT)> mItemSelectedFlags;

    mutable bool mParticlesInitialized{false};
    mutable Shader *mParticlesComputeShader{nullptr};
    mutable Shader *mParticlesRenderShader{nullptr};

    mutable GLuint mParticlesVAO{0};
    mutable GLuint mParticlesAttractorVAO{0};
    mutable GLuint mParticlesPosSSBO{0};
    mutable GLuint mParticlesVelSSBO{0};
    mutable GLuint mParticlesAttractorVBO{0};

    mutable glm::ivec3 mParticleGrid{64, 32, 32};
    mutable GLuint mTotalParticles{0};
    mutable glm::mat4 mParticleProjection{1.0f};
    mutable int mWindowWidth{1280};
    mutable int mWindowHeight{720};

    mutable float mParticleTime{0.0f};
    mutable float mParticleDeltaT{0.0f};
    mutable float mParticleSpeed{35.0f};
    mutable float mParticleAngle{0.0f};
    mutable float mParticleGravity1{1000.0f};
    mutable float mParticleGravity2{1000.0f};
    mutable float mParticleMass{0.1f};
    mutable float mParticleMaxDist{45.0f};
    mutable float mParticleDtScale{0.2f};
    mutable float mParticlePointSize{1.0f};
    mutable float mAttractorPointSize{5.0f};
    mutable float mParticleResetIntervalSeconds{10.0f};
    mutable float mParticleResetAccumulator{0.0f};
    mutable glm::vec4 mBlackHoleBase1{5.0f, 0.0f, 0.0f, 1.0f};
    mutable glm::vec4 mBlackHoleBase2{-5.0f, 0.0f, 0.0f, 1.0f};

    void initializeParticleScene() const noexcept;
    void renderParticleScene() const noexcept;
    void resetParticleSimulation() const noexcept;
    void cleanupParticleScene() noexcept;
    void updateParticleProjection() const noexcept;
};

#endif // MENU_STATE_HPP
