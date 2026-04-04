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
struct Options;

class MenuState : public State
{
public:
    explicit MenuState(StateStack &stack, Context context);
    ~MenuState() override;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const sf::Event &event) noexcept override;

private:
    enum class MenuTab : unsigned int
    {
        NAVIGATION = 0,
        SETTINGS = 1,
        PARTICLE_SCENE = 2
    };

    enum class MenuItem : unsigned int
    {
        CONTINUE = 0,
        NEW_GAME = 1,
        NETWORK_GAME = 2,
        SETTINGS = 3,
        PARTICLE_SCENE = 4,
        SPLASH = 5,
        QUIT = 6,
        COUNT = 7
    };

    Font *mFont;
    MusicPlayer *mMusic;

    // Navigation state variables
    mutable MenuItem mSelectedMenuItem;
    mutable MenuItem mConfirmedMenuItem;
    mutable MenuTab mActiveTab;

    mutable bool mShowMainMenu;
    mutable bool mPendingMenuAction;

    mutable std::array<bool, static_cast<size_t>(MenuItem::COUNT)> mItemSelectedFlags;

    struct SettingsUiState
    {
        bool initialized{false};
        bool enableMusic{true};
        bool enableSound{true};
        bool vsync{true};
        bool fullscreen{false};
        bool antialiasing{true};
        bool showDebugOverlay{false};
        bool arcadeModeEnabled{true};

        float masterVolume{50.0f};
        float musicVolume{75.0f};
        float renderQuality{1.0f};
        float sfxVolume{10.0f};
        float runnerSpeed{30.0f};
        float runnerStrafeLimit{35.0f};
        float runnerPickupSpacing{18.0f};
        float runnerCollisionCooldown{0.40f};
    };

    mutable SettingsUiState mSettingsUi;

    mutable bool mParticlesInitialized{false};
    mutable Shader *mParticlesComputeShader{nullptr};
    mutable Shader *mParticlesRenderShader{nullptr};

    mutable GLuint mParticlesVAO{0};
    mutable GLuint mParticlesAttractorVAO{0};
    mutable GLuint mParticlesPosSSBO{0};
    mutable GLuint mParticlesVelSSBO{0};
    mutable GLuint mParticlesAttractorVBO{0};
    mutable GLuint mParticlesRenderFBO{0};
    mutable GLuint mParticlesRenderTexture{0};
    mutable GLuint mParticlesRenderDepthRBO{0};
    mutable int mParticleRenderWidth{0};
    mutable int mParticleRenderHeight{0};

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
    void ensureParticleRenderTarget(int width, int height) const noexcept;

    void pushSynthwaveStyle() const noexcept;
    void popSynthwaveStyle() const noexcept;
    void initializeSettingsUiFromOptions() const noexcept;
    void drawNavigationTab() const noexcept;
    void drawSettingsTab() const noexcept;
    void drawSettingsPreviewPanel() const noexcept;
    void drawParticleSceneTab() const noexcept;
    void drawParticleControls() const noexcept;
    void resetSettingsToDefaults() const noexcept;
    void applySettingsFromUi() const noexcept;
    void applySettings(const Options &options) const noexcept;
};

#endif // MENU_STATE_HPP
