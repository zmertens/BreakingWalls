#include "MenuState.hpp"

#include <SDL3/SDL.h>

#include <glad/glad.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <dearimgui/imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Font.hpp"
#include "GameState.hpp"
#include "MusicPlayer.hpp"
#include "PauseState.hpp"
#include "Player.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "Shader.hpp"
#include "SoundPlayer.hpp"
#include "StateStack.hpp"

MenuState::MenuState(StateStack &stack, Context context)
    : State(stack, context), mSelectedMenuItem(MenuItem::NEW_GAME), mShowMainMenu(true), mItemSelectedFlags{}, 
    mFont{nullptr},
    mMusic{nullptr}
{
    // Load font with error handling
    try
    {
        mFont = &context.getFontManager()->get(Fonts::ID::NUNITO_SANS);
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MenuState: Failed to load font: %s", e.what());
        mFont = nullptr;
    }
    
    // Load menu music with error handling
    try
    {
        mMusic = &context.getMusicManager()->get(Music::ID::MENU_MUSIC);
        mMusic->play();
    }
    catch (const std::exception &e)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MenuState: Failed to load menu music: %s", e.what());
        mMusic = nullptr;
    }
    
    // initialize selection flags so UI shows correct selected item
    mItemSelectedFlags.fill(false);
    mItemSelectedFlags[static_cast<size_t>(mSelectedMenuItem)] = true;
}

MenuState::~MenuState()
{
    if (mMusic)
    {
        mMusic->stop();
    }
    cleanupParticleScene();
}

void MenuState::draw() const noexcept
{
    initializeParticleScene();
    renderParticleScene();

    // Early exit BEFORE any ImGui operations to avoid push/pop imbalance
    if (!mShowMainMenu)
    {
        return;
    }

    using std::array;
    using std::size_t;
    using std::string;

    // Use font if available, otherwise use default ImGui font
    if (mFont)
    {
        ImGui::PushFont(mFont->get());
    }

    if constexpr (false)
    {
        auto showDemoWindow = false;
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    // Apply color schema
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.016f, 0.047f, 0.024f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.067f, 0.137f, 0.094f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.118f, 0.227f, 0.161f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.188f, 0.365f, 0.259f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.302f, 0.502f, 0.380f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.537f, 0.635f, 0.341f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.302f, 0.502f, 0.380f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.537f, 0.635f, 0.341f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.745f, 0.863f, 0.498f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.933f, 1.0f, 0.8f, 1.0f));

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Main Menu", &mShowMainMenu, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::Text("Main Menu");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Particle Scene", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Gravity #1", &mParticleGravity1, 10.0f, 3000.0f, "%.1f");
            ImGui::SliderFloat("Gravity #2", &mParticleGravity2, 10.0f, 3000.0f, "%.1f");
            ImGui::SliderFloat("Orbit Speed", &mParticleSpeed, 1.0f, 120.0f, "%.1f");
            // ImGui::SliderFloat("Particle Mass", &mParticleMass, 0.01f, 2.0f, "%.3f");
            ImGui::SliderFloat("Max Distance", &mParticleMaxDist, 5.0f, 120.0f, "%.1f");
            ImGui::SliderFloat("DeltaT Scale", &mParticleDtScale, 0.01f, 1.0f, "%.3f");
            ImGui::SliderFloat("Reset Interval (s)", &mParticleResetIntervalSeconds, 5.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("Particle Size", &mParticlePointSize, 1.0f, 6.0f, "%.1f");
            ImGui::SliderFloat("Attractor Size", &mAttractorPointSize, 2.0f, 14.0f, "%.1f");
            ImGui::Spacing();
        }

        // Navigation options
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "Navigation Options:");
        ImGui::Spacing();

        const array<string, static_cast<size_t>(MenuItem::COUNT)> menuItems = {
            "Resume", "Just run endlessly", "Multiplayer Game", "Settings", "Return to Splash Screen", "Quit"};

        // Use Selectable with bool* overload so ImGui keeps a consistent toggled state
        const auto active = static_cast<size_t>(getContext().getPlayer()->isActive());
        for (size_t i{static_cast<size_t>(active ? 0 : 1)}; i < menuItems.size(); ++i)
        {
            if (bool *flag = &mItemSelectedFlags[i]; ImGui::Selectable(menuItems[i].c_str(), flag))
            {
                // When an item is selected, clear others and set the selected index
                for (auto &f : mItemSelectedFlags)
                {
                    f = false;
                }
                *flag = true;
                mSelectedMenuItem = static_cast<MenuItem>(i);
            }
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Display selected menu info
        ImGui::TextColored(ImVec4(0.933f, 1.0f, 0.8f, 1.0f), "Selected: ");
        ImGui::SameLine();
        if (static_cast<unsigned int>(mSelectedMenuItem) < static_cast<unsigned int>(MenuItem::COUNT))
        {
            ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "%s",
                               menuItems.at(static_cast<unsigned int>(mSelectedMenuItem)).c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        if (ImGui::Button("Confirm Selection", ImVec2(180, 40)))
        {
            getContext().getSoundPlayer()->play(SoundEffect::ID::SELECT);
            // Close the menu window to trigger state transition in update()
            mShowMainMenu = false;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(10);

    if (mFont)
    {
        ImGui::PopFont();
    }
}

bool MenuState::update(float dt, unsigned int subSteps) noexcept
{
    // Periodic music health check
    static float musicCheckTimer = 0.0f;
    musicCheckTimer += dt;

    if (musicCheckTimer >= 5.0f) // Check every 5 seconds
    {
        musicCheckTimer = 0.0f;

        if (mMusic)
        {
            if (!mMusic->isPlaying())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "MenuState: Music stopped unexpectedly! Attempting restart...");
                mMusic->play();
            }
        }
    }

    // If menu is visible, just keep it showing (no transitions yet)
    if (mShowMainMenu)
    {
        // Block updates to underlying states while menu is active
        return false;
    }

    // Menu was closed by user - process the selected action
    switch (mSelectedMenuItem)
    {
    case MenuItem::CONTINUE:
        // Only pop if there's a GameState to return to
        if (getStack().peekState<GameState *>() != nullptr)
        {
            // Pop menu state, returning to game
            requestStackPop();
        }
        else
        {
            // If no game state exists, start a new game
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MenuState: Cannot continue - no game in progress. Starting new game.");
            requestStackPop();
            requestStackPush(States::ID::GAME);
        }
        break;

    case MenuItem::NEW_GAME:
        requestStateClear();
        requestStackPush(States::ID::GAME);
        break;

    case MenuItem::NETWORK_GAME:
        requestStateClear();
        requestStackPush(States::ID::MULTIPLAYER_GAME);
        break;

    case MenuItem::SETTINGS:
        requestStackPush(States::ID::SETTINGS);
        break;

    case MenuItem::SPLASH:
        mShowMainMenu = false;
        requestStateClear();
        requestStackPush(States::ID::SPLASH);
        return true;

    case MenuItem::QUIT:
        requestStateClear();
        break;

    default:
        break;
    }

    mShowMainMenu = true;
    // Keep underlying states paused during menu transition processing
    return false;
}

bool MenuState::handleEvent(const SDL_Event &event) noexcept
{
    if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        const int newWidth = event.window.data1;
        const int newHeight = event.window.data2;
        if (newWidth > 0 && newHeight > 0)
        {
            mWindowWidth = newWidth;
            mWindowHeight = newHeight;
            updateParticleProjection();
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            // Commented because returning to Menu from Settings with ESCAPE causes fallthrough effect
            // mShowMainMenu = false;
        }
    }

    // Consume events so gameplay does not react while menu is open
    return false;
}

void MenuState::initializeParticleScene() const noexcept
{
    if (mParticlesInitialized)
    {
        return;
    }

    if (auto *window = getContext().getRenderWindow(); window != nullptr)
    {
        SDL_GetWindowSize(window->getSDLWindow(), &mWindowWidth, &mWindowHeight);
    }
    updateParticleProjection();

    try
    {
        mParticlesComputeShader = &getContext().getShaderManager()->get(Shaders::ID::GLSL_PARTICLES_COMPUTE);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "MenuState: Failed to get particle compute shader: %s", e.what());
        mParticlesComputeShader = nullptr;
        return;
    }

    try
    {
        mParticlesRenderShader = &getContext().getShaderManager()->get(Shaders::ID::GLSL_FULLSCREEN_QUAD_MVP);
    }
    catch (const std::exception &e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "MenuState: Failed to build particle render shader: %s", e.what());
        mParticlesRenderShader = nullptr;
        return;
    }

    mTotalParticles = static_cast<GLuint>(mParticleGrid.x * mParticleGrid.y * mParticleGrid.z);
    if (mTotalParticles == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "MenuState: Particle scene has zero particles");
        return;
    }

    std::vector<GLfloat> initPos;
    initPos.reserve(static_cast<size_t>(mTotalParticles) * 4u);
    std::vector<GLfloat> initVel(static_cast<size_t>(mTotalParticles) * 4u, 0.0f);

    const GLfloat dx = 2.0f / static_cast<GLfloat>(mParticleGrid.x - 1);
    const GLfloat dy = 2.0f / static_cast<GLfloat>(mParticleGrid.y - 1);
    const GLfloat dz = 2.0f / static_cast<GLfloat>(mParticleGrid.z - 1);
    const glm::mat4 centerTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, -1.0f));

    for (int i = 0; i < mParticleGrid.x; ++i)
    {
        for (int j = 0; j < mParticleGrid.y; ++j)
        {
            for (int k = 0; k < mParticleGrid.z; ++k)
            {
                glm::vec4 p(dx * static_cast<GLfloat>(i),
                            dy * static_cast<GLfloat>(j),
                            dz * static_cast<GLfloat>(k),
                            1.0f);
                p = centerTransform * p;
                initPos.push_back(p.x);
                initPos.push_back(p.y);
                initPos.push_back(p.z);
                initPos.push_back(p.w);
            }
        }
    }

    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(static_cast<size_t>(mTotalParticles) * 4u * sizeof(GLfloat));

    glGenBuffers(1, &mParticlesPosSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesPosSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initPos.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mParticlesPosSSBO);

    glGenBuffers(1, &mParticlesVelSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesVelSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, initVel.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mParticlesVelSSBO);

    glGenVertexArrays(1, &mParticlesVAO);
    glBindVertexArray(mParticlesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesPosSSBO);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenBuffers(1, &mParticlesAttractorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesAttractorVBO);
    const GLfloat attractorData[] = {
        mBlackHoleBase1.x, mBlackHoleBase1.y, mBlackHoleBase1.z, mBlackHoleBase1.w,
        mBlackHoleBase2.x, mBlackHoleBase2.y, mBlackHoleBase2.z, mBlackHoleBase2.w};
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(attractorData)), attractorData, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &mParticlesAttractorVAO);
    glBindVertexArray(mParticlesAttractorVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesAttractorVBO);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mParticlesInitialized = true;
}

void MenuState::renderParticleScene() const noexcept
{
    if (!mParticlesInitialized || !mParticlesComputeShader || !mParticlesRenderShader || mTotalParticles == 0)
    {
        return;
    }

    const float now = static_cast<float>(SDL_GetTicks()) * 0.001f;
    mParticleDeltaT = (mParticleTime == 0.0f) ? 0.0f : std::min(0.033f, now - mParticleTime);
    mParticleTime = now;
    mParticleResetAccumulator += mParticleDeltaT;

    if (mParticleResetAccumulator >= mParticleResetIntervalSeconds)
    {
        resetParticleSimulation();
        mParticleResetAccumulator = 0.0f;
    }

    mParticleAngle += mParticleSpeed * mParticleDeltaT;
    if (mParticleAngle > 360.0f)
    {
        mParticleAngle -= 360.0f;
    }

    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(mParticleAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 attractor1 = glm::vec3(rotation * mBlackHoleBase1);
    const glm::vec3 attractor2 = glm::vec3(rotation * mBlackHoleBase2);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mParticlesPosSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mParticlesVelSSBO);

    mParticlesComputeShader->bind();
    mParticlesComputeShader->setUniform("BlackHolePos1", attractor1);
    mParticlesComputeShader->setUniform("BlackHolePos2", attractor2);
    const float clampedMass = std::max(0.0001f, mParticleMass);
    mParticlesComputeShader->setUniform("Gravity1", mParticleGravity1);
    mParticlesComputeShader->setUniform("Gravity2", mParticleGravity2);
    // mParticlesComputeShader->setUniform("ParticleMass", clampedMass);
    mParticlesComputeShader->setUniform("ParticleInvMass", 1.0f / clampedMass);
    mParticlesComputeShader->setUniform("DeltaT", std::max(0.0001f, mParticleDeltaT * mParticleDtScale));
    mParticlesComputeShader->setUniform("MaxDist", mParticleMaxDist);
    mParticlesComputeShader->setUniform("ParticleCount", mTotalParticles);

    const GLuint groupsX = (mTotalParticles + 999u) / 1000u;
    glDispatchCompute(groupsX, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glViewport(0, 0, mWindowWidth, mWindowHeight);
    glClearColor(0.015f, 0.025f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 0.0f, 20.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 mvp = mParticleProjection * view;

    mParticlesRenderShader->bind();
    mParticlesRenderShader->setUniform("MVP", mvp);

    glEnable(GL_DEPTH_TEST);
    glPointSize(mParticlePointSize);
    mParticlesRenderShader->setUniform("Color", glm::vec4(0.92f, 0.98f, 1.0f, 0.16f));
    glBindVertexArray(mParticlesVAO);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mTotalParticles));

    const GLfloat attractorData[] = {
        attractor1.x, attractor1.y, attractor1.z, 1.0f,
        attractor2.x, attractor2.y, attractor2.z, 1.0f};
    glBindBuffer(GL_ARRAY_BUFFER, mParticlesAttractorVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(attractorData)), attractorData);

    glPointSize(mAttractorPointSize);
    mParticlesRenderShader->setUniform("Color", glm::vec4(1.0f, 0.9f, 0.35f, 1.0f));
    glBindVertexArray(mParticlesAttractorVAO);
    glDrawArrays(GL_POINTS, 0, 2);

    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);
}

void MenuState::resetParticleSimulation() const noexcept
{
    if (mTotalParticles == 0 || mParticlesPosSSBO == 0 || mParticlesVelSSBO == 0)
    {
        return;
    }

    std::vector<GLfloat> resetPositions;
    resetPositions.reserve(static_cast<size_t>(mTotalParticles) * 4u);

    const GLfloat dx = 2.0f / static_cast<GLfloat>(mParticleGrid.x - 1);
    const GLfloat dy = 2.0f / static_cast<GLfloat>(mParticleGrid.y - 1);
    const GLfloat dz = 2.0f / static_cast<GLfloat>(mParticleGrid.z - 1);
    const glm::mat4 centerTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, -1.0f));

    for (int i = 0; i < mParticleGrid.x; ++i)
    {
        for (int j = 0; j < mParticleGrid.y; ++j)
        {
            for (int k = 0; k < mParticleGrid.z; ++k)
            {
                glm::vec4 p(dx * static_cast<GLfloat>(i),
                            dy * static_cast<GLfloat>(j),
                            dz * static_cast<GLfloat>(k),
                            1.0f);
                p = centerTransform * p;
                resetPositions.push_back(p.x);
                resetPositions.push_back(p.y);
                resetPositions.push_back(p.z);
                resetPositions.push_back(p.w);
            }
        }
    }

    std::vector<GLfloat> resetVelocities(static_cast<size_t>(mTotalParticles) * 4u, 0.0f);
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(static_cast<size_t>(mTotalParticles) * 4u * sizeof(GLfloat));

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesPosSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, resetPositions.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mParticlesVelSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, resetVelocities.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void MenuState::cleanupParticleScene() noexcept
{
    if (mParticlesVAO != 0)
    {
        glDeleteVertexArrays(1, &mParticlesVAO);
        mParticlesVAO = 0;
    }
    if (mParticlesAttractorVAO != 0)
    {
        glDeleteVertexArrays(1, &mParticlesAttractorVAO);
        mParticlesAttractorVAO = 0;
    }
    if (mParticlesPosSSBO != 0)
    {
        glDeleteBuffers(1, &mParticlesPosSSBO);
        mParticlesPosSSBO = 0;
    }
    if (mParticlesVelSSBO != 0)
    {
        glDeleteBuffers(1, &mParticlesVelSSBO);
        mParticlesVelSSBO = 0;
    }
    if (mParticlesAttractorVBO != 0)
    {
        glDeleteBuffers(1, &mParticlesAttractorVBO);
        mParticlesAttractorVBO = 0;
    }

    mParticlesRenderShader = nullptr;
    mParticlesComputeShader = nullptr;
    mParticlesInitialized = false;

    // Ensure all OpenGL commands are processed before state destruction completes
    // This prevents race conditions when transitioning to GameState
    glFlush();
}

void MenuState::updateParticleProjection() const noexcept
{
    if (mWindowWidth <= 0 || mWindowHeight <= 0)
    {
        mWindowWidth = 1;
        mWindowHeight = 1;
    }

    const float aspectRatio = static_cast<float>(mWindowWidth) / static_cast<float>(mWindowHeight);
    mParticleProjection = glm::perspective(glm::radians(50.0f), aspectRatio, 1.0f, 100.0f);
}
