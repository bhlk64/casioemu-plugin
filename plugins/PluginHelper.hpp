#pragma once
#include "PluginApi.h"
#include <imgui.h>

inline void SetupPluginEnvironment(PluginApi* api) {
    if (api) {
        ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(api->GetImGuiContext()));
    }
}