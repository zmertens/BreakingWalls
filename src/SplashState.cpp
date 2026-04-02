#include "SplashState.hpp"

#include <SFML/Window.hpp>
#include <SFML/Window/Event.hpp>
#include <iostream>
#include <chrono>
#include <cmath>

#include <cmath>

#include <dearimgui/imgui.h>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include "Font.hpp"
#include "LoadingState.hpp"
#include "Options.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "StateStack.hpp"
#include "Texture.hpp"

SplashState::SplashState(StateStack &stack, Context context)
    : State(stack, context), mSplashTexture{}
{
    try
    {
        mWhiteNoise = getContext().getSoundPlayer();
    }
    catch(const std::exception& e)
    {
        std::cerr << "SplashState: Failed to get SoundPlayer from context: " << e.what() << "\n";
        mWhiteNoise = nullptr;
    }
    
    try
    {
        mSplashTexture = &getContext().getTextureManager()->get(Textures::ID::FAA_LOGO);
    }
    catch (const std::exception &e)
    {
        std::cerr << "SplashState: Failed to load splash texture: " << e.what() << "\n";
        mSplashTexture = nullptr;
    }

    if (mWhiteNoise && mWhiteNoise->isEnabled())
    {
        mWhiteNoise->setVolume(getContext().getOptionsManager()->get(GUIOptions::ID::DE_FACTO).getSfxVolume());
        mWhiteNoise->play(SoundEffect::ID::WHITE_NOISE);
    }

    try
    {
        mFont = &getContext().getFontManager()->get(Fonts::ID::NUNITO_SANS);
    }
    catch (const std::exception &e)
    {
        std::cerr << "SplashState: Failed to load font: " << e.what() << "\n";
        mFont = nullptr;
    }
}

SplashState::~SplashState()
{
    if (mWhiteNoise && mWhiteNoise->isEnabled())
    {
        mWhiteNoise->stop(SoundEffect::ID::WHITE_NOISE);
        mWhiteNoise->removeStoppedSounds();
    }
}

void SplashState::draw() const noexcept
{
    if (!mSplashTexture)
    {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    ImVec2 screenSize = io.DisplaySize;

    ImGui::PushFont(mFont->get());

    // Draw the splash image to cover the whole screen
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(screenSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // fully transparent
    ImGui::Begin("SplashImageBg", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    ImGui::SetCursorPos(ImVec2(0, 0));
    // Flip horizontally: swap uv0 and uv1 x values
    ImGui::Image(
        static_cast<ImTextureID>(static_cast<intptr_t>(mSplashTexture->get())),
        screenSize,
        ImVec2(0, 1),
        ImVec2(1, 0)
    );
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Flashing prompt box near center
    static auto startTime = std::chrono::steady_clock::now();
    float time = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
    float alpha = 0.5f + 0.5f * std::sin(time * 3.0f);
    // keep it between 0.5 and 1.0
    alpha = 0.5f + 0.5f * alpha;

    const char *msg = "Press any key to continue . . .";
    ImVec2 textSize = ImGui::CalcTextSize(msg);
    ImVec2 boxSize = ImVec2(textSize.x + 60, textSize.y + 40);
    ImVec2 boxPos = ImVec2((screenSize.x - boxSize.x) * 0.5f, (screenSize.y - boxSize.y) * 0.55f);

    ImGui::SetNextWindowPos(boxPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(boxSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.6f * alpha));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 0.95f));
    ImGui::Begin("SplashPrompt", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::SetCursorPos(ImVec2((boxSize.x - textSize.x) * 0.5f, (boxSize.y - textSize.y) * 0.5f));
    ImGui::Text("%s", msg);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::PopFont();
}

bool SplashState::update(float dt, unsigned int subSteps) noexcept
{
    return true;
}

bool SplashState::handleEvent(const sf::Event &event) noexcept
{
    // Any key press or mouse click transitions to menu
    if (event.is<sf::Event::KeyPressed>() || event.is<sf::Event::MouseButtonPressed>())
    {
        // Only allow transition if loading is complete
        if (!isLoadingComplete())
        {
            return true;
        }

        std::cerr << "SplashState: Input received, transitioning to MenuState...\n";
        if (mWhiteNoise && mWhiteNoise->isEnabled())
        {
            mWhiteNoise->stop(SoundEffect::ID::WHITE_NOISE);
        }
        // Pop SplashState only, keeping LoadingState and its loaded resources below
        requestStackPop();
        // Push MenuState on top of LoadingState
        requestStackPush(States::ID::MENU);
    }

    return true;
}

bool SplashState::isLoadingComplete() const noexcept
{
    // Check if the state below us (LoadingState) is finished
    if (const auto *loadingState = getStack().peekState<LoadingState *>())
    {
        return loadingState->isFinished();
    }

    // If there's no LoadingState below, assume loading is complete
    return true;
}
