
#include "PluginApi.h"
#include "PluginHelper.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ------------------------------------------------------------
// Data structures
// ------------------------------------------------------------

struct InjectorRow {
    char address[16] = "";
    char data[256] = "";
};

struct TestResult {
    uint64_t cycles = 0;
    std::string got;
    bool cmp = false;
};

struct TestCase {
    std::vector<InjectorRow> expectations;
    TestResult result;
};

struct TestRoot {
    const char* name;

    std::vector<InjectorRow> input;
    char pcExit[6] = "00000";

    std::array<TestCase, 10> tests;
};

static constexpr const char* ROOT_NAMES[] = {
    "ATEST", "BTEST", "CTEST",
    "DTEST", "ETEST", "FTEST"
};

// ------------------------------------------------------------
// Dummy runner
// ------------------------------------------------------------

static TestResult Dummy(
    const std::string& testName,
    const std::vector<InjectorRow>& input,
    const std::vector<InjectorRow>& expectations,
    uint32_t pcExit
) {
    // TODO:
    // Implement actual test execution here.
    //
    // input:
    //   Shared input injectors belonging to the root.
    //
    // expectations:
    //   Expected memory values for this testcase.
    //
    // pcExit:
    //   PC address at which the test should stop.
    //
    // Return:
    //   cycles, actual memory data (got), comparison result.

    (void)testName;
    (void)input;
    (void)expectations;
    (void)pcExit;

    return {};
}

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static uint32_t ParseHex(const char* value) {
    if (!value || !*value)
        return 0;

    return static_cast<uint32_t>(
        std::strtoul(value, nullptr, 16)
    );
}

static std::string GetTestName(
    const TestRoot& root,
    int testIndex
) {
    return std::string(root.name) +
        std::to_string(testIndex + 1);
}

static void RenderInjectorRows(
    std::vector<InjectorRow>& rows,
    const char* id
) {
    ImGui::PushID(id);

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        ImGui::PushID(i);

        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Address", rows[i].address,
            sizeof(rows[i].address));

        ImGui::SameLine();

        ImGui::SetNextItemWidth(220);
        ImGui::InputText("Data", rows[i].data,
            sizeof(rows[i].data));

        ImGui::SameLine();

        if (ImGui::SmallButton("Remove")) {
            rows.erase(rows.begin() + i);
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    if (ImGui::Button("+ Add row")) {
        rows.emplace_back();
    }

    ImGui::PopID();
}

// ------------------------------------------------------------
// Test Case Window
// ------------------------------------------------------------

class TestCaseWindow : public UIWindow {
public:
    TestCaseWindow()
        : UIWindow("Test Case Manager") {

        inital_size = ImVec2(750, 750);

        for (int i = 0; i < 6; ++i)
            roots[i].name = ROOT_NAMES[i];
    }

private:
    std::array<TestRoot, 6> roots;

    int selectedRoot = 0;
    int selectedTest = 0;

    IPlatformHost* platform = nullptr;

    std::string statusMessage;

    // --------------------------------------------------------
    // Config path
    // --------------------------------------------------------

    std::string GetConfigPath() {
        if (!platform)
            return {};

        const char* directory =
            platform->GetInternalStoragePath();

        if (!directory || !*directory)
            return {};

        std::string path = directory;

        if (path.back() != '/' && path.back() != '\\')
            path += '/';

        path += "test_expectations.ini";

        return path;
    }

    // --------------------------------------------------------
    // Save expectations only
    // --------------------------------------------------------

    bool SaveConfig() {
        const std::string path = GetConfigPath();

        if (path.empty())
            return false;

        std::ofstream file(path);

        if (!file.is_open())
            return false;

        for (const auto& root : roots) {
            for (int i = 0; i < 10; ++i) {
                const auto& test = root.tests[i];

                const std::string testName =
                    GetTestName(root, i);

                for (const auto& row : test.expectations) {
                    file << testName << '|'
                         << row.address << '|'
                         << row.data << '\n';
                }
            }
        }

        return file.good();
    }

    // --------------------------------------------------------
    // Load expectations only
    // --------------------------------------------------------

    bool LoadConfig() {
        const std::string path = GetConfigPath();

        if (path.empty())
            return false;

        std::ifstream file(path);

        if (!file.is_open())
            return false;

        // Clear existing expectations.
        for (auto& root : roots) {
            for (auto& test : root.tests)
                test.expectations.clear();
        }

        std::string line;

        while (std::getline(file, line)) {
            const size_t first = line.find('|');

            if (first == std::string::npos)
                continue;

            const size_t second = line.find('|', first + 1);

            if (second == std::string::npos)
                continue;

            const std::string testName =
                line.substr(0, first);

            const std::string address =
                line.substr(first + 1, second - first - 1);

            const std::string data =
                line.substr(second + 1);

            for (auto& root : roots) {
                for (int i = 0; i < 10; ++i) {
                    if (testName != GetTestName(root, i))
                        continue;

                    InjectorRow row{};

                    std::snprintf(
                        row.address,
                        sizeof(row.address),
                        "%s", address.c_str()
                    );

                    std::snprintf(
                        row.data,
                        sizeof(row.data),
                        "%s", data.c_str()
                    );

                    root.tests[i].expectations.push_back(row);
                }
            }
        }

        return true;
    }

    // --------------------------------------------------------
    // Root selector
    // --------------------------------------------------------

    void RenderRootSelector() {
        ImGui::Text("Root");

        ImGui::SameLine();

        if (ImGui::BeginCombo(
            "##RootSelector",
            roots[selectedRoot].name
        )) {
            for (int i = 0; i < 6; ++i) {
                const bool selected = selectedRoot == i;

                if (ImGui::Selectable(ROOT_NAMES[i], selected))
                    selectedRoot = i;

                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }
    }

    // --------------------------------------------------------
    // Root input
    // --------------------------------------------------------

    void RenderRootInput(TestRoot& root) {
        ImGui::Text("Input");

        RenderInjectorRows(root.input, "RootInput");

        ImGui::Separator();

        ImGui::Text("PCExit");

        ImGui::SameLine();

        ImGui::TextUnformatted("0x");

        ImGui::SameLine();

        ImGui::SetNextItemWidth(100);

        ImGui::InputText(
            "##PCExit",
            root.pcExit,
            sizeof(root.pcExit)
        );

        ImGui::SameLine();

        if (ImGui::Button("Save config")) {
            statusMessage = SaveConfig()
                ? "Expectations saved."
                : "Failed to save config.";
        }

        ImGui::SameLine();

        if (ImGui::Button("Load config")) {
            statusMessage = LoadConfig()
                ? "Expectations loaded."
                : "Failed to load config.";
        }

        if (!statusMessage.empty())
            ImGui::TextDisabled("%s", statusMessage.c_str());
    }

    // --------------------------------------------------------
    // Test selector
    // --------------------------------------------------------

    void RenderTestSelector() {
        char preview[32];

        std::snprintf(
            preview,
            sizeof(preview),
            "TEST%d",
            selectedTest + 1
        );

        ImGui::Text("Test case");

        ImGui::SameLine();

        if (ImGui::BeginCombo("##TestSelector", preview)) {
            for (int i = 0; i < 10; ++i) {
                char label[32];

                std::snprintf(
                    label,
                    sizeof(label),
                    "TEST%d",
                    i + 1
                );

                if (ImGui::Selectable(label, selectedTest == i))
                    selectedTest = i;
            }

            ImGui::EndCombo();
        }
    }

    // --------------------------------------------------------
    // Run and result
    // --------------------------------------------------------

    void RenderTestResult(TestRoot& root, TestCase& test) {
        const bool hasRun = test.result.cycles != 0;

        if (ImGui::Button(hasRun ? "Rerun" : "Run")) {
            test.result = Dummy(
                GetTestName(root, selectedTest),
                root.input,
                test.expectations,
                ParseHex(root.pcExit)
            );
        }

        ImGui::SameLine();

        ImGui::Text(
            "Cycles: %llu",
            static_cast<unsigned long long>(test.result.cycles)
        );

        ImGui::SameLine();

        const double seconds =
            static_cast<double>(test.result.cycles) / 2097152.0;

        ImGui::Text("%.6f s", seconds);

        ImGui::SameLine();

        if (test.result.cycles == 0) {
            ImGui::TextDisabled("Not run");
        }
        else if (test.result.cmp) {
            ImGui::TextColored(
                ImVec4(0.3f, 0.85f, 0.3f, 1.0f),
                "Yes / Correct"
            );
        }
        else {
            ImGui::TextColored(
                ImVec4(0.95f, 0.3f, 0.3f, 1.0f),
                "No / Incorrect"
            );
        }

        ImGui::Separator();

        ImGui::Text("Got");

        std::string gotBuffer = test.result.got;
        gotBuffer.push_back('\0');
        
        ImGui::InputTextMultiline(
            "##Got",
            gotBuffer.data(),
            gotBuffer.size(),
            ImVec2(-1, 100),
            ImGuiInputTextFlags_ReadOnly
        );

        ImGui::Separator();
    }

public:
    void SetPlatform(IPlatformHost* host) {
        platform = host;
    }

    void RenderCore() override {
        RenderRootSelector();

        TestRoot& root = roots[selectedRoot];

        ImGui::Separator();

        RenderRootInput(root);

        ImGui::Spacing();

        RenderTestSelector();

        TestCase& test = root.tests[selectedTest];

        RenderTestResult(root, test);

        ImGui::Text("Expectations");

        RenderInjectorRows(
            test.expectations,
            "Expectations"
        );
    }
};

// ------------------------------------------------------------
// Plugin entry
// ------------------------------------------------------------

#ifdef _WIN32
    #define PLUGIN_EXPORT __declspec(dllexport)
#else
    #define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PLUGIN_EXPORT
void fPluginLoad(PluginApi* api) {
    if (!api)
        return;

    SetupPluginEnvironment(api);

    if (!api->RegisterPlugin(
        "com.bhlk64.casioemu.TestCase",
        "Test Case Manager",
        "1.0.0",
        "bhlk64",
        "Test case manager for CasioEmu"
    )) {
        return;
    }

    auto* window = new TestCaseWindow();

    window->SetPlatform(
        api->QueryInterface<IPlatformHost>()
    );

    api->AddWindow(window);
}
