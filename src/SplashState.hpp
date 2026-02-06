#ifndef SPLASH_STATE_HPP
#define SPLASH_STATE_HPP

#include "State.hpp"
#include "World.hpp"
#include "Camera.hpp"
#include "Shader.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

class SplashState : public State
{
public:
    explicit SplashState(StateStack& stack, Context context);
    ~SplashState() override;

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event& event) noexcept override;

private:
    bool isLoadingComplete() const noexcept;

    // GPU rendering resources (same as GameState)
    void initializeGraphicsResources() noexcept;
    void createPathTracerTextures() noexcept;
    void renderWithComputeShaders() const noexcept;
    bool checkCameraMovement() const noexcept;
    void cleanupResources() noexcept;

    // World for sphere scene
    World mWorld;

    // Free-floating camera that moves automatically
    Camera mCamera;

    // Shader references
    Shader* mDisplayShader{ nullptr };
    Shader* mComputeShader{ nullptr };

    // GPU resources
    GLuint mShapeSSBO{ 0 };
    GLuint mVAO{ 0 };
    GLuint mAccumTex{ 0 };
    GLuint mDisplayTex{ 0 };

    // Progressive rendering state
    mutable uint32_t mCurrentBatch{ 0 };
    uint32_t mSamplesPerBatch{ 4 };
    uint32_t mTotalBatches{ 250 };

    // Camera movement tracking
    mutable glm::vec3 mLastCameraPosition;
    mutable float mLastCameraYaw{ 0.0f };
    mutable float mLastCameraPitch{ 0.0f };

    // Auto-float camera parameters
    float mFloatSpeed{ 15.0f };  // Units per second for gentle floating
    float mFloatTime{ 0.0f };    // Accumulated time for gentle oscillation

    bool mShadersInitialized{ false };
    int mWindowWidth{ 1280 };
    int mWindowHeight{ 720 };
};

#endif // SPLASH_STATE_HPP
