#include "imgui_impl_sfml.h"
#include "imgui.h"
#include <SFML/Window.hpp>
#include <SFML/Window/Event.hpp>

static sf::Window* g_Window = nullptr;

static ImGuiKey SFMLKeyToImGuiKey(sf::Keyboard::Key key)
{
    switch (key) {
        case sf::Keyboard::Key::Tab:       return ImGuiKey_Tab;
        case sf::Keyboard::Key::Left:      return ImGuiKey_LeftArrow;
        case sf::Keyboard::Key::Right:     return ImGuiKey_RightArrow;
        case sf::Keyboard::Key::Up:        return ImGuiKey_UpArrow;
        case sf::Keyboard::Key::Down:      return ImGuiKey_DownArrow;
        case sf::Keyboard::Key::PageUp:    return ImGuiKey_PageUp;
        case sf::Keyboard::Key::PageDown:  return ImGuiKey_PageDown;
        case sf::Keyboard::Key::Home:      return ImGuiKey_Home;
        case sf::Keyboard::Key::End:       return ImGuiKey_End;
        case sf::Keyboard::Key::Insert:    return ImGuiKey_Insert;
        case sf::Keyboard::Key::Delete:    return ImGuiKey_Delete;
        case sf::Keyboard::Key::Backspace: return ImGuiKey_Backspace;
        case sf::Keyboard::Key::Space:     return ImGuiKey_Space;
        case sf::Keyboard::Key::Enter:     return ImGuiKey_Enter;
        case sf::Keyboard::Key::Escape:    return ImGuiKey_Escape;
        case sf::Keyboard::Key::A:         return ImGuiKey_A;
        case sf::Keyboard::Key::C:         return ImGuiKey_C;
        case sf::Keyboard::Key::V:         return ImGuiKey_V;
        case sf::Keyboard::Key::X:         return ImGuiKey_X;
        case sf::Keyboard::Key::Y:         return ImGuiKey_Y;
        case sf::Keyboard::Key::Z:         return ImGuiKey_Z;
        default:                           return ImGuiKey_None;
    }
}

bool ImGui_ImplSFML_Init(sf::Window* window)
{
    g_Window = window;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_sfml";
    return true;
}

void ImGui_ImplSFML_Shutdown()
{
    g_Window = nullptr;
}

void ImGui_ImplSFML_NewFrame()
{
    if (!g_Window) return;
    ImGuiIO& io = ImGui::GetIO();
    auto size = g_Window->getSize();
    io.DisplaySize = ImVec2(static_cast<float>(size.x), static_cast<float>(size.y));
    if (size.x > 0 && size.y > 0)
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    if (g_Window->hasFocus()) {
        auto pos = sf::Mouse::getPosition(*g_Window);
        io.AddMousePosEvent(static_cast<float>(pos.x), static_cast<float>(pos.y));
    }
    io.AddMouseButtonEvent(0, sf::Mouse::isButtonPressed(sf::Mouse::Button::Left));
    io.AddMouseButtonEvent(1, sf::Mouse::isButtonPressed(sf::Mouse::Button::Right));
    io.AddMouseButtonEvent(2, sf::Mouse::isButtonPressed(sf::Mouse::Button::Middle));
}

bool ImGui_ImplSFML_ProcessEvent(const sf::Event& event)
{
    ImGuiIO& io = ImGui::GetIO();
    if (const auto* e = event.getIf<sf::Event::MouseMoved>()) {
        io.AddMousePosEvent(static_cast<float>(e->position.x), static_cast<float>(e->position.y));
        return false;
    }
    if (const auto* e = event.getIf<sf::Event::MouseButtonPressed>()) {
        int btn = (e->button == sf::Mouse::Button::Left)   ? 0 :
                  (e->button == sf::Mouse::Button::Right)  ? 1 :
                  (e->button == sf::Mouse::Button::Middle) ? 2 : -1;
        if (btn >= 0) io.AddMouseButtonEvent(btn, true);
        return io.WantCaptureMouse;
    }
    if (const auto* e = event.getIf<sf::Event::MouseButtonReleased>()) {
        int btn = (e->button == sf::Mouse::Button::Left)   ? 0 :
                  (e->button == sf::Mouse::Button::Right)  ? 1 :
                  (e->button == sf::Mouse::Button::Middle) ? 2 : -1;
        if (btn >= 0) io.AddMouseButtonEvent(btn, false);
        return io.WantCaptureMouse;
    }
    if (const auto* e = event.getIf<sf::Event::MouseWheelScrolled>()) {
        if (e->wheel == sf::Mouse::Wheel::Vertical)
            io.AddMouseWheelEvent(0.0f, e->delta);
        else
            io.AddMouseWheelEvent(e->delta, 0.0f);
        return io.WantCaptureMouse;
    }
    if (const auto* e = event.getIf<sf::Event::KeyPressed>()) {
        ImGuiKey key = SFMLKeyToImGuiKey(e->code);
        io.AddKeyEvent(ImGuiMod_Ctrl,  e->control);
        io.AddKeyEvent(ImGuiMod_Shift, e->shift);
        io.AddKeyEvent(ImGuiMod_Alt,   e->alt);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, true);
        return io.WantCaptureKeyboard;
    }
    if (const auto* e = event.getIf<sf::Event::KeyReleased>()) {
        ImGuiKey key = SFMLKeyToImGuiKey(e->code);
        io.AddKeyEvent(ImGuiMod_Ctrl,  e->control);
        io.AddKeyEvent(ImGuiMod_Shift, e->shift);
        io.AddKeyEvent(ImGuiMod_Alt,   e->alt);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, false);
        return io.WantCaptureKeyboard;
    }
    if (const auto* e = event.getIf<sf::Event::TextEntered>()) {
        if (e->unicode > 0 && e->unicode < 0x10000)
            io.AddInputCharacter(static_cast<unsigned int>(e->unicode));
        return io.WantTextInput;
    }
    return false;
}
