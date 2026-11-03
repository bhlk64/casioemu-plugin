#include "PluginApi.h"
#include "PluginHelper.hpp"

class ExampleWindow;

static PluginApi* g_api = nullptr;
static ExampleWindow* wnd = nullptr;

class ExampleWindow : public UIWindow {
public:
    ExampleWindow() : UIWindow("ExamplePlugin") {
        inital_size = ImVec2(400, 300);
    }

    void RenderCore() override {
        ImGui::Text("This is an example!");
    }
};

extern "C" __attribute__((visibility("default")))
void fPluginLoad(PluginApi* api) {
    if (!api) return;

    SetupPluginEnvironment(api);
    g_api = api;

    if (!api->RegisterPlugin(
        "com.hieuxyz.casioemu.example",
        "Example Plugin",
        "1.0.0",
        "hieuxyz",
        "A simple example plugin"
    )) return;

    wnd = new ExampleWindow();
    api->AddWindow(wnd);
}