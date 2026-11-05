
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
    char address[10] = "";
    char data[65536] = "";
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
    char pcEnter[6] = "00000";


    std::array<TestCase, 10> tests;
};

static constexpr const char* ROOT_NAMES[] = {
    "ATEST", "BTEST", "CTEST",
    "DTEST", "ETEST", "FTEST"
};

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
    int testi
) {
    return std::string(root.name) +
        std::to_string(testi + 1);
}

static void RenderInjectorRows(
    std::vector<InjectorRow>& inj,
    const char* id
) {
    ImGui::PushID(id);

    for (int i = 0; i < static_cast<int>(inj.size()); ++i) {
        ImGui::PushID(i);

        ImGui::SetNextItemWidth(80);
        ImGui::Text("Addr");
        ImGui::SameLine();
        ImGui::InputText("##address", inj[i].address, 10);
    
        if (ImGui::Button("Paste")) {
            if (ImGui::GetClipboardText() != nullptr) {
                strncpy(inj[i].data, ImGui::GetClipboardText(), sizeof(inj[i].data) - 1);
                inj[i].data[sizeof(inj[i].data) - 1] = '\0';
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            memset(inj[i].data, 0, sizeof(inj[i].data));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            inj.erase(inj.begin() + i);
            ImGui::PopID();
            break;
        }
    
        ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 1.4f);
        ImGui::InputTextMultiline(
            ("##hex" + std::to_string(i)).c_str(),
            inj[i].data,
            IM_ARRAYSIZE(inj[i].data) - 1,
            ImVec2(-1, ImGui::GetTextLineHeight() * 4));
        
        ImGui::PopID();
    }

    if (ImGui::Button("+ Add row")) {
        inj.emplace_back();
    }

    ImGui::PopID();
}

// ------------------------------------------------------------
// Injection
// ------------------------------------------------------------

struct InjectionPair {
    std::string address;
    std::string data;
    uint32_t addr_value;
    std::vector<uint8_t> data_bytes;
};

struct CustomInjection {
    std::string name;
    std::vector<InjectionPair> pairs;
};

std::vector<CustomInjection> customInjections;

const bool ParseCustomInjections(std::string content) {
    std::vector<CustomInjection> newInjections;

    // State machine for robust parsing
    enum class ParseState {
        NAME,         // Looking for name
        EQUALS,         // Looking for equals sign after name
        OPEN_BRACE,     // Looking for opening brace
        KEY,         // Looking for address
        KEY_EQUALS,     // Looking for equals sign after address
        VALUE_START, // Looking for quote
        VALUE,         // Reading value data
        AFTER_VALUE, // Looking for comma or close brace
    };

    // First pass: preprocess to handle comments and normalize content
    std::string preprocessed;
    bool in_comment = false;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];

        if (c == '#') {
            in_comment = true;
        }
        else if (c == '\n') {
            in_comment = false;
            preprocessed += ' '; // Convert newlines to spaces
        }
        else if (!in_comment) {
            preprocessed += c;
        }
    }

    // Second pass: parse with state machine
    ParseState state = ParseState::NAME;
    std::string current_name;
    std::string current_address;
    std::string current_value;
    CustomInjection current_injection;
    bool in_quotes = false;
    int brace_level = 0;

    for (size_t i = 0; i < preprocessed.size(); ++i) {
        char c = preprocessed[i];

        switch (state) {
        case ParseState::NAME:
            if (std::isalnum(c) || c == '_') {
                current_name += c;
            }
            else if (c == '=' && !current_name.empty()) {
                TrimString(current_name);
                state = ParseState::EQUALS;
            }
            else if (!std::isspace(c)) {
                // Invalid character, reset
                current_name.clear();
            }
            break;

        case ParseState::EQUALS:
            if (c == '{') {
                current_injection = CustomInjection();
                current_injection.name = current_name;
                current_name.clear();
                brace_level = 1;
                state = ParseState::KEY;
            }
            else if (!std::isspace(c) && c != '=') {
                // Unexpected character
                state = ParseState::NAME;
                current_name.clear();
            }
            break;

        case ParseState::OPEN_BRACE:
            if (c == '{') {
                current_injection = CustomInjection();
                current_injection.name = current_name;
                current_name.clear();
                brace_level = 1;
                state = ParseState::KEY;
            }
            else if (!std::isspace(c)) {
                // Unexpected character
                state = ParseState::NAME;
                current_name.clear();
            }
            break;

        case ParseState::KEY:
            if (std::isxdigit(c) || c == 'x' || c == 'X' || c == '0') {
                current_address += c;
            }
            else if (c == '=' && !current_address.empty()) {
                TrimString(current_address);
                state = ParseState::VALUE_START;
            }
            else if (c == '}' && brace_level == 1) {
                // End of block
                if (!current_injection.pairs.empty()) {
                    newInjections.push_back(std::move(current_injection));
                }
                brace_level = 0;
                state = ParseState::NAME;
            }
            else if (!std::isspace(c)) {
                // Reset on unexpected char
                current_address.clear();
            }
            break;

        case ParseState::VALUE_START:
            if (c == '"') {
                in_quotes = true;
                current_value.clear();
                state = ParseState::VALUE;
            }
            else if (!std::isspace(c)) {
                // Reset on unexpected char
                state = ParseState::KEY;
                current_address.clear();
            }
            break;

        case ParseState::VALUE:
            if (c == '"' && in_quotes) {
                in_quotes = false;
                // Process the pair
                if (IsHexString(current_address)) {
                    InjectionPair pair;
                    pair.address = current_address;
                    pair.data = current_value;

                    try {
                        PrecomputeInjectionValues(pair);
                        if (!pair.data_bytes.empty()) {
                            current_injection.pairs.push_back(std::move(pair));
                        }
                    }
                    catch (...) {
                        // Invalid hex, skip
                    }
                }

                current_address.clear();
                current_value.clear();
                state = ParseState::AFTER_VALUE;
            }
            else if (in_quotes) {
                // Accept any character inside quotes
                current_value += c;
            }
            break;

        case ParseState::AFTER_VALUE:
            if (c == ',') {
                state = ParseState::KEY;
            }
            else if (c == '}' && brace_level == 1) {
                if (!current_injection.pairs.empty()) {
                    newInjections.push_back(std::move(current_injection));
                }
                brace_level = 0;
                state = ParseState::NAME;
            }
            else if (c == '{') {
                brace_level++;
            }
            else if (c == '}') {
                brace_level--;
                if (brace_level == 0) {
                    if (!current_injection.pairs.empty()) {
                        newInjections.push_back(std::move(current_injection));
                    }
                    state = ParseState::NAME;
                }
            }
            break;

        case ParseState::KEY_EQUALS:
            if (c == '=') {
                state = ParseState::VALUE_START;
            }
            else if (!std::isspace(c)) {
                state = ParseState::KEY;
                current_address.clear();
            }
            break;
        }
    }

    // Handle case where file ends in a valid state
    if (state == ParseState::AFTER_VALUE && brace_level == 1) {
        if (!current_injection.pairs.empty()) {
            newInjections.push_back(std::move(current_injection));
        }
    }

    customInjections = std::move(newInjections);
    return true;
}

static std::vector<uint8_t> ConvertVector(std::string content, const std::string& hextext = nullptr) {
  std::vector<uint8_t> bytes = {};
  auto plc = strtol(inj.addr, 0, 16);
  size_t i = 0, j = 0;
  char hex_buf[3];
  while (content[i] != '\0' && content[i + 1] != '\0') {
    if (content[i] == ';' || content[i + 1] == ';') {
      for (;; ++i) {
        if (content[i] == '\0')
          goto exit;
        if (content[i] == '\n') {
          ++i;
          break;
        }
      }
    }
    else {
      if (!(valid_hex(content[i]) && valid_hex(content[i + 1]))) {
        ++i;
        continue;
      }
      hex_buf[0] = content[i];
      hex_buf[1] = content[i + 1];
      hex_buf[2] = '\0';
      uint8_t byte = strtoul(hex_buf, nullptr, 16);
      bytes+=byte;
      hex_buf[2] = " ";
      hextext+=hex_buf
      i += 2;
      ++j;
    }
  }
  exit:
  return bytes
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
    Hooks* hooks = nullptr;
    ICPU* cpu = nullptr;
    IKeyboard* keyboard = nullptr;
    IMMU* mmu = nullptr;
    IEmulator* emu = nullptr;

    std::string statusMessage;
    
    std::string getGot(std::vector<InjectorRow> expectations,const bool* cmp = nullptr) {
      std::string got = "";
      for (int i; i<expectations.size();++i) {
        InjectorRow expc = expectations[i];
        got += ParseHex(expc.address);
        got += ": ";
        std::vector<uint8_t> hexvec = ConvertVector(expc.data);
        bool is_correct = true;
        for (int hi; hi < hexvec.size(); ++hi) {
          uint8_t bytegot = mmu->ReadData(expc.address+hi);
          std::string htextgot = "";
          sprintf(htextgot.c_str(), "%02X", bytegot);
          got += htextgot;
          if (!(hexvec[hi] == bytegot)) {
            is_correct = false;
          }
        }
        got += '\n';
      }
      got += '\0';
      return got;
    }
    
    TestResult Run(
      const std::string& testName,
      const std::vector<InjectorRow>& input,
      const std::vector<InjectorRow>& expectations,
      uint32_t pcExit,
      uint32_t pcEnter,
      const bool& pause
    ) {
      
      bool cmp;
      std::string got;
      uint64_t cycles;
      
      cpu->SetCyclePause(true);
      cpu->ClearCycle();
      cpu->Reset();
      bool iswaiting = false;
      hooks->SetupOnCallFunctionHook({
        if (args.pc_after == 0x1f24c) {
          iswaiting = true;
        }
      })
      while (not iswaiting) {
        if (pause) {
          emu->Pause();
          cycles = cpu->GetCycles();
          got = getGot(expectations, cmp);
          return {cycles, got, cmp};
        }
      }
      for (int i; i < customInjections.size(); ++i) {
        if (customInjection[i].name == testName) {
          ApplyInjection(customInjection[i]);
          break;
        }
      }
      keyboard->PressCode(0x60, true)
      hooks->SetupOnInstructionHook({
        if (args.pc_after == pcEnter) {
          cpu->SetCyclePause(false);
          keyboard->PressCode(0x60, false)
        }
      });
      bool done = false;
      hooks->SetupOnInstructionHook({
        if (args.pc_after == pcExit) {
          cpu->SetCyclePause(true);
          emu->Pause();
          done = true;
        }
      });
      while (not done) {
        if (pause) {
          emu->Pause();
          cycles = cpu->GetCycles();
          got = getGot(expectations, cmp);
          return {cycles, got, cmp};
        }
      }
      cycles = cpu->GetCycle();
      got = cpu->getGot(expectations, cmp);
      return {cycles, got, cmp};
    }

    bool Reload() {
      customInjections = {};
      std::string injectionFilePath = platform->GetExternalStoragePath();
      if (injectionFilePath.back() != '/' && injectionFilePath.back() != '\\')
        injectionFilePath += '/';
      injectionFilePath += "hc-inj.txt";
      
      std::string content;
      try {
        std::ifstream file(injectionFilePath, std::ios::binary);
        if (!file.is_open()) {
          throw "eror"
        }
    
        content = std::string(
          (std::istreambuf_iterator<char>(file)),
          std::istreambuf_iterator<char>());
        file.close();
      }
      ParseCustomInjections(content);
      catch (...) {return false}
      return true
    }
    
    bool ApplyInjection(const CustomInjection& inj) {
        try {
            for (const auto& pair : inj.pairs) {
                for (size_t i = 0; i < pair.data_bytes.size(); ++i) {
                    mmu->WriteData(pair.addr_value + i, pair.data_bytes[i]);
                }
            }
        } catch (...) {}
    }

    // --------------------------------------------------------
    // Config path
    // --------------------------------------------------------

    std::string GetConfigPath() {
        if (!platform)
            return {};

        const char* directory =
            platform->GetExternalStoragePath();

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

        ImGui::Text("PC Exit:");

        ImGui::SameLine();

        ImGui::SetNextItemWidth(100);

        ImGui::InputText(
            "##PCExit",
            root.pcExit,
            sizeof(root.pcExit)
        );

        ImGui::SameLine();

        ImGui::Text("PC Enter:");

        ImGui::SameLine();

        ImGui::SetNextItemWidth(100);

        ImGui::InputText(
            "##PCEnter",
            root.pcEnter,
            sizeof(root.pcEnter)
        );

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
        
        ImGui::SameLine();
        
        if (ImGui::Button("Reload")) {
          std::string state = "Reloading";
          ImGui::OpenPopup("Reloading"));
          if (BeginPopup("Reloading")) {
            ImGui::Text(state);
            state = Reload() ? "Reloaded" : "Reload Failed";
          }
          ImGui::EndPopup();
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
        int state = 0;
        std::vector<std::string> statestr = {"Run","Stop Test","Rerun"};

        if (ImGui::Button(statestr[state]) {
            bool pause = false;
            if (state == 1) {
                pause = true;
            } else {
                test.result = Run(
                    GetTestName(root, selectedTest),
                    root.input,
                    test.expectations,
                    ParseHex(root.pcExit),
                    ParseHex(root.pcEnter),
                    pause
                );
            }
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
    void SetHooks(Hooks* hook) {
        hooks = hook;
    }
    void SetCPU(ICPU* icpu) {
        cpu = icpu;
    }
    void SetKeyboard(IKeyboard* _keyboard) {
        keyboard = _keyboard;
    }
    void SetMMU(IMMU* _mmu) {
        mmu = _mmu;
    }
    void SetEmu(IEmulator* _emu) {
        emu = _emu;
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

    window->SetPlatform(api->QueryInterface<IPlatformHost>());
    window->SetHooks(api->QueryInterface<Hooks>());
    window->SetCPU(api->QueryInterface<ICPU>());
    window->SetKeyboard(api->QueryInterface<IKeyboard>());
    window->SetMMU(api->QueryInterface<IMMU>());
    window->SetEmu(api->QueryInterface<IEmulator>());

    api->AddWindow(window);
}
