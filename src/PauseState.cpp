#include "PauseState.hpp"

#include <dearimgui/imgui.h>

#include <SFML/Window.hpp>
#include <SFML/Window/Event.hpp>
#include <iostream>

#include "Font.hpp"
#include "MenuState.hpp"
#include "MusicPlayer.hpp"
#include "ResourceIdentifiers.hpp"
#include "ResourceManager.hpp"
#include "StateStack.hpp"
#include "GameState.hpp"

PauseState::PauseState(StateStack &stack, Context context)
    : State(stack, context), mMusic{}, mPlanetComplete(false), mSelectedMenuItem(static_cast<unsigned int>(States::ID::PAUSE))
{
    if (auto *renderWindow = getContext().getRenderWindow(); renderWindow != nullptr)
    {
        if (sf::Window *window = renderWindow->getSFMLWindow(); window != nullptr)
        {
            window->setMouseCursorGrabbed(false);
            window->setMouseCursorVisible(true);
        }
    }

    try
    {
        mMusic = &getContext().getMusicManager()->get(Music::ID::GAME_MUSIC);
        if (mMusic && !mMusic->isPlaying())
        {
            mMusic->play();
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "PauseState: Failed to access music player: " << e.what() << "\n";
        mMusic = nullptr;
    }
    
    try
    {
        mFont = &getContext().getFontManager()->get(Fonts::ID::NUNITO_SANS);
    }
    catch (const std::exception &e)
    {
        std::cerr << "PauseState: Failed to load font: " << e.what() << "\n";
        mFont = nullptr;
    }
}

void PauseState::draw() const noexcept
{
    if (mFont)
    {
        ImGui::PushFont(mFont->get());
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.058f, 0.027f, 0.114f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.078f, 0.039f, 0.149f, 0.84f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.165f, 0.055f, 0.294f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.263f, 0.098f, 0.451f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.365f, 0.094f, 0.569f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.612f, 0.208f, 0.851f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.765f, 0.341f, 0.965f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.184f, 0.459f, 0.808f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.208f, 0.610f, 0.961f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.341f, 0.733f, 1.0f, 0.84f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.109f, 0.055f, 0.184f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.165f, 0.090f, 0.278f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.220f, 0.114f, 0.369f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.846f, 0.965f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.337f, 0.761f, 1.0f, 0.55f));

    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    const float panelWidth = std::clamp(io.DisplaySize.x * 0.30f, 320.0f, 560.0f);
    const float panelHeight = std::clamp(io.DisplaySize.y * 0.34f, 260.0f, 460.0f);
    const ImVec2 buttonSize(
        std::clamp(panelWidth - 96.0f, 200.0f, 420.0f),
        std::clamp(panelHeight * 0.14f, 40.0f, 56.0f));

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.825f);

    constexpr ImGuiWindowFlags pauseFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("PauseMenuPanel", nullptr, pauseFlags))
    {
        const float contentWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;

        const char *titleText = "**Breaking Walls is Paused**";
        const float titleWidth = ImGui::CalcTextSize(titleText).x;
        ImGui::SetCursorPosX((contentWidth - titleWidth) * 0.5f);
        ImGui::Text("%s", titleText);

        ImGui::Separator();
        ImGui::Spacing();

        // Navigation options
        const char *navText = "Navigation Options:";
        const float navWidth = ImGui::CalcTextSize(navText).x;
        ImGui::SetCursorPosX((contentWidth - navWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.745f, 0.863f, 0.498f, 1.0f), "%s", navText);
        ImGui::Spacing();

        ImGui::SetCursorPosX((contentWidth - buttonSize.x) * 0.5f);
        const char* resumeText = mPlanetComplete ? "New Game?" : "Resume Game";
        if (ImGui::Button(resumeText, buttonSize))
        {
            mSelectedMenuItem = static_cast<unsigned int>(States::ID::GAME);
        }
        ImGui::Spacing();

        ImGui::SetCursorPosX((contentWidth - buttonSize.x) * 0.5f);
        if (ImGui::Button("Main Menu", buttonSize))
        {
            mSelectedMenuItem = static_cast<unsigned int>(States::ID::MENU);
        }
        ImGui::Spacing();

        ImGui::SetCursorPosX((contentWidth - buttonSize.x) * 0.5f);
        if (ImGui::Button("Exit Game", buttonSize))
        {
            mSelectedMenuItem = static_cast<unsigned int>(States::ID::DONE);
        }
        ImGui::Spacing();
    }
    ImGui::End();
    ImGui::PopStyleColor(16);
    ImGui::PopStyleVar(5);
    if (mFont)
    {
        ImGui::PopFont();
    }
}

bool PauseState::update(float dt, unsigned int subSteps) noexcept
{
    switch (mSelectedMenuItem)
    {
    case static_cast<unsigned int>(States::ID::DONE):
        std::cerr << "PauseState: Exiting game...\n";
        requestStateClear();
        requestStackPush(States::ID::SPLASH);
        break;
    case static_cast<unsigned int>(States::ID::GAME):
        std::cerr << "PauseState: Resuming/Starting game...\n";
        if (auto *renderWindow = getContext().getRenderWindow(); renderWindow != nullptr)
        {
            if (sf::Window *window = renderWindow->getSFMLWindow(); window != nullptr)
            {
                window->setMouseCursorGrabbed(true);
                window->setMouseCursorVisible(false);
            }
        }
        if (mPlanetComplete)
        {
            // Start new game by popping pause state AND game state, then pushing a fresh game state
            std::cerr << "PauseState: Planet complete! Starting new game...\n";
            requestStackPop();  // Pop PauseState
            requestStackPop();  // Pop current GameState
            requestStackPush(States::ID::GAME);  // Push fresh GameState
        }
        else
        {
            // Resume current game
            requestStackPop();
        }
        break;
    case static_cast<unsigned int>(States::ID::MENU):
        std::cerr << "PauseState: Menu selected, entering MenuState\n";
        if (mMusic && mMusic->isPlaying())
        {
            mMusic->stop();
        }
        requestStackPop();
        requestStackPush(States::ID::MENU);
        break;
    default:
        break;
    }

    return false;
}

bool PauseState::handleEvent(const sf::Event &event) noexcept
{

    if (const auto* e = event.getIf<sf::Event::KeyPressed>())
    {
        if (e->code == sf::Keyboard::Key::Escape)
        {
            std::cerr << "PauseState: Escape Key pressed, returning to previous state\n";
            mSelectedMenuItem = static_cast<unsigned int>(States::ID::GAME);
        }
    }

    return false;
}
