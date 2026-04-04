// dear imgui: Platform Backend for SFML3
#pragma once
#include "imgui.h"
#ifndef IMGUI_DISABLE

namespace sf {
    class Window;
    class Event;
}

IMGUI_IMPL_API bool ImGui_ImplSFML_Init(sf::Window* window);
IMGUI_IMPL_API void ImGui_ImplSFML_Shutdown();
IMGUI_IMPL_API void ImGui_ImplSFML_NewFrame();
IMGUI_IMPL_API bool ImGui_ImplSFML_ProcessEvent(const sf::Event& event);

#endif // IMGUI_DISABLE
