
#include "PluginApi.h"
#include "PluginHelper.hpp"
#if !defined(__ANDROID__)
#include "pch.h"
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <mutex>
#include <limits>
#include <utility>
#include <thread>
#include <chrono>

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
    const char* name = nullptr;

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
// Hex helpers
// ------------------------------------------------------------

static bool IsHexString(const std::string& str) {
    if (str.empty())
        return false;

    size_t start = 0;

    if (str.size() >= 2 &&
        str[0] == '0' &&
        (str[1] == 'x' || str[1] == 'X')) {
        start = 2;
    }

    if (start == str.size())
        return false;

    for (size_t i = start; i < str.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(str[i])))
            return false;
    }

    return true;
}

static bool ParseHexChecked(const std::string& value, uint32_t& result) {
    if (!IsHexString(value))
        return false;

    const char* ptr = value.c_str();

    if (value.size() >= 2 && value[0] == '0' &&
        (value[1] == 'x' || value[1] == 'X')) {
        ptr += 2;
    }

    char* end = nullptr;

    unsigned long parsed = std::strtoul(ptr, &end, 16);

    if (!end || *end != '\0' ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    result = static_cast<uint32_t>(parsed);
    return true;
}

static uint32_t ParseHex(const char* value) {
    if (!value)
        return 0;

    uint32_t result = 0;

    if (!ParseHexChecked(value, result))
        return 0;

    return result;
}

static int HexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

// Supports whitespace and semicolon comments.
static bool ConvertVector(
    const std::string& content,
    std::vector<uint8_t>& bytes
) {
    bytes.clear();

    std::string cleaned;

    bool comment = false;

    for (char c : content) {
        if (comment) {
            if (c == '\n')
                comment = false;

            continue;
        }

        if (c == ';') {
            comment = true;
            continue;
        }

        if (!std::isspace(static_cast<unsigned char>(c)))
            cleaned += c;
    }

    if (cleaned.size() % 2 != 0)
        return false;

    bytes.reserve(cleaned.size() / 2);

    for (size_t i = 0; i < cleaned.size(); i += 2) {
        int high = HexValue(cleaned[i]);
        int low = HexValue(cleaned[i + 1]);

        if (high < 0 || low < 0) {
            bytes.clear();
            return false;
        }

        bytes.push_back(
            static_cast<uint8_t>((high << 4) | low)
        );
    }

    return true;
}

static std::string GetTestName(
    const TestRoot& root,
    int testi
) {
    return std::string(root.name) +
        std::to_string(testi + 1);
}

// ------------------------------------------------------------
// Injection structures
// ------------------------------------------------------------

struct InjectionPair {
    std::string address;
    std::string data;

    uint32_t addr_value = 0;

    std::vector<uint8_t> data_bytes;
};

struct CustomInjection {
    std::string name;
    std::vector<InjectionPair> pairs;
};

static std::vector<CustomInjection> customInjections;

static bool PrecomputeInjectionValues(InjectionPair& pair) {
    if (!ParseHexChecked(pair.address, pair.addr_value))
        return false;

    if (!ConvertVector(pair.data, pair.data_bytes))
        return false;

    return !pair.data_bytes.empty();
}

// ------------------------------------------------------------
// Custom injection parser
//
// Format:
//
// test_name = {
//     0x12345 = "AABBCC",
//     0x12348 = "112233"
// }
//
// ------------------------------------------------------------

static void TrimString(std::string& str) {
    auto first = std::find_if(
        str.begin(),
        str.end(),
        [](unsigned char c) {
            return !std::isspace(c);
        }
    );

    str.erase(str.begin(), first);

    auto last = std::find_if(
        str.rbegin(),
        str.rend(),
        [](unsigned char c) {
            return !std::isspace(c);
        }
    );

    str.erase(last.base(), str.end());
}

static bool ParseCustomInjections(const std::string& content) {
    std::vector<CustomInjection> parsed;

    size_t pos = 0;

    while (pos < content.size()) {
        // Skip whitespace and comments.
        while (pos < content.size()) {
            if (std::isspace(static_cast<unsigned char>(content[pos]))) {
                ++pos;
                continue;
            }

            if (content[pos] == '#') {
                while (pos < content.size() && content[pos] != '\n')
                    ++pos;

                continue;
            }

            break;
        }

        if (pos >= content.size())
            break;

        // Read injection name.
        size_t nameStart = pos;

        while (pos < content.size() &&
            (std::isalnum(static_cast<unsigned char>(content[pos])) ||
             content[pos] == '_')) {
            ++pos;
        }

        if (nameStart == pos)
            return false;

        CustomInjection injection;
        injection.name = content.substr(nameStart, pos - nameStart);

        // Skip whitespace.
        while (pos < content.size() &&
            std::isspace(static_cast<unsigned char>(content[pos]))) {
            ++pos;
        }

        if (pos >= content.size() || content[pos++] != '=')
            return false;

        while (pos < content.size() &&
            std::isspace(static_cast<unsigned char>(content[pos]))) {
            ++pos;
        }

        if (pos >= content.size() || content[pos++] != '{')
            return false;

        bool closed = false;

        while (pos < content.size()) {
            while (pos < content.size()) {
                if (std::isspace(static_cast<unsigned char>(content[pos]))) {
                    ++pos;
                    continue;
                }

                if (content[pos] == '#') {
                    while (pos < content.size() && content[pos] != '\n')
                        ++pos;

                    continue;
                }

                break;
            }

            if (pos >= content.size())
                break;

            if (content[pos] == '}') {
                ++pos;
                closed = true;
                break;
            }

            // Address.
            size_t addressStart = pos;

            while (pos < content.size() &&
                !std::isspace(static_cast<unsigned char>(content[pos])) &&
                content[pos] != '=' &&
                content[pos] != '}') {
                ++pos;
            }

            if (addressStart == pos)
                return false;

            InjectionPair pair;
            pair.address = content.substr(
                addressStart,
                pos - addressStart
            );

            while (pos < content.size() &&
                std::isspace(static_cast<unsigned char>(content[pos]))) {
                ++pos;
            }

            if (pos >= content.size() || content[pos++] != '=')
                return false;

            while (pos < content.size() &&
                std::isspace(static_cast<unsigned char>(content[pos]))) {
                ++pos;
            }

            if (pos >= content.size() || content[pos++] != '"')
                return false;

            size_t valueStart = pos;

            while (pos < content.size() && content[pos] != '"')
                ++pos;

            if (pos >= content.size())
                return false;

            pair.data = content.substr(valueStart, pos - valueStart);
            ++pos;

            if (!PrecomputeInjectionValues(pair))
                return false;

            injection.pairs.push_back(std::move(pair));

            while (pos < content.size() &&
                std::isspace(static_cast<unsigned char>(content[pos]))) {
                ++pos;
            }

            if (pos < content.size() && content[pos] == ',') {
                ++pos;
                continue;
            }

            if (pos < content.size() && content[pos] == '}') {
                ++pos;
                closed = true;
                break;
            }

            return false;
        }

        if (!closed)
            return false;

        parsed.push_back(std::move(injection));
    }

    customInjections = std::move(parsed);
    return true;
}

// ------------------------------------------------------------
// Injector UI
// ------------------------------------------------------------

static void RenderInjectorRows(
    std::vector<InjectorRow>& inj,
    const char* id
) {
    ImGui::PushID(id);

    for (int i = 0; i < static_cast<int>(inj.size()); ++i) {
        ImGui::PushID(i);

        ImGui::Text("Addr");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(100);

        ImGui::InputText(
            "##address",
            inj[i].address,
            sizeof(inj[i].address)
        );

        if (ImGui::Button("Paste")) {
            const char* clipboard = ImGui::GetClipboardText();

            if (clipboard) {
                std::snprintf(
                    inj[i].data,
                    sizeof(inj[i].data),
                    "%s",
                    clipboard
                );
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear")) {
            inj[i].data[0] = '\0';
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Remove")) {
            inj.erase(inj.begin() + i);
            ImGui::PopID();
            break;
        }

        ImGui::SetNextItemWidth(-1);

        ImGui::InputTextMultiline(
            "##hex",
            inj[i].data,
            sizeof(inj[i].data),
            ImVec2(-1, ImGui::GetTextLineHeight() * 4)
        );

        ImGui::PopID();
    }

    if (ImGui::Button("+ Add row"))
        inj.emplace_back();

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
    Hooks* hooks = nullptr;
    ICPU* cpu = nullptr;
    IKeyboard* keyboard = nullptr;
    IMMU* mmu = nullptr;
    IEmulator* emu = nullptr;
    IChipset* chipset = nullptr;

    std::string statusMessage;

    enum class RunStage {
        IDLE,
        WAIT_CALL,
        WAIT_ENTER,
        WAIT_EXIT
    };

    std::atomic<RunStage> runStage{RunStage::IDLE};

    std::atomic<bool> cancelRequested{false};

    bool hooksRegistered = false;

    TestCase* activeTest = nullptr;

    std::vector<InjectorRow> activeInput;
    std::vector<InjectorRow> activeExpectations;

    std::string activeTestName;

    uint32_t activePCEnter = 0;
    uint32_t activePCExit = 0;

    mutable std::mutex resultMutex;

    // --------------------------------------------------------
    // Injection application
    // --------------------------------------------------------

    bool ApplyInjection(const CustomInjection& injection) {
        if (!mmu)
            return false;

        for (const auto& pair : injection.pairs) {
            for (size_t i = 0; i < pair.data_bytes.size(); ++i) {
                const uint64_t address =
                    static_cast<uint64_t>(pair.addr_value) + i;

                if (address > std::numeric_limits<uint32_t>::max())
                    return false;

                mmu->WriteData(
                    static_cast<size_t>(address),
                    pair.data_bytes[i]
                );
            }
        }

        return true;
    }

    bool ApplyInputRows(const std::vector<InjectorRow>& rows) {
        if (!mmu)
            return false;

        for (const auto& row : rows) {
            uint32_t address = 0;

            if (!ParseHexChecked(row.address, address))
                return false;

            std::vector<uint8_t> bytes;

            if (!ConvertVector(row.data, bytes))
                return false;

            for (size_t i = 0; i < bytes.size(); ++i) {
                const uint64_t target =
                    static_cast<uint64_t>(address) + i;

                if (target > std::numeric_limits<uint32_t>::max())
                    return false;

                mmu->WriteData(
                    static_cast<size_t>(target),
                    bytes[i]
                );
            }
        }

        return true;
    }

    // --------------------------------------------------------
    // Expectations comparison
    // --------------------------------------------------------

    TestResult GetResult(
        const std::vector<InjectorRow>& expectations
    ) {
        TestResult result;

        result.cycles = cpu ? cpu->GetCycle() : 0;
        result.cmp = !expectations.empty();

        if (!mmu) {
            result.cmp = false;
            result.got = "MMU unavailable";
            return result;
        }

        for (const auto& expectation : expectations) {
            uint32_t address = 0;

            if (!ParseHexChecked(expectation.address, address)) {
                result.cmp = false;
                result.got += "Invalid expectation address\n";
                continue;
            }

            std::vector<uint8_t> expectedBytes;

            if (!ConvertVector(expectation.data, expectedBytes) ||
                expectedBytes.empty()) {
                result.cmp = false;
                result.got += "Invalid expectation data\n";
                continue;
            }

            char addressText[16];

            std::snprintf(
                addressText,
                sizeof(addressText),
                "%04X: ",
                address
            );

            result.got += addressText;

            for (size_t i = 0; i < expectedBytes.size(); ++i) {
                const uint64_t target =
                    static_cast<uint64_t>(address) + i;

                if (target > std::numeric_limits<uint32_t>::max()) {
                    result.cmp = false;
                    result.got += "?? ";
                    continue;
                }

                const uint8_t actual =
                    mmu->ReadData(static_cast<size_t>(target));

                char byteText[4];

                std::snprintf(
                    byteText,
                    sizeof(byteText),
                    "%02X ",
                    actual
                );

                result.got += byteText;

                if (actual != expectedBytes[i])
                    result.cmp = false;
            }

            result.got += '\n';
        }

        return result;
    }

    // --------------------------------------------------------
    // Finish / stop
    // --------------------------------------------------------

    void FinishRun() {
        if (runStage.exchange(RunStage::IDLE) == RunStage::IDLE)
            return;

        if (keyboard)
            keyboard->ReleaseAll();

        if (cpu)
            cpu->SetCyclePause(true);

        if (emu)
            emu->Pause();

        TestResult result = GetResult(activeExpectations);

        {
            std::lock_guard<std::mutex> lock(resultMutex);

            if (activeTest)
                activeTest->result = std::move(result);
        }

        activeTest = nullptr;
    }

    void StopRun() {
        cancelRequested = true;

        runStage = RunStage::IDLE;

        if (keyboard)
            keyboard->ReleaseAll();

        if (cpu)
            cpu->SetCyclePause(true);

        if (emu)
            emu->Pause();

        activeTest = nullptr;
    }

    // --------------------------------------------------------
    // Hooks
    //
    // Register once. Callbacks remain installed, but only
    // operate while a test is active.
    // --------------------------------------------------------
    bool regiskey = false;

    void RegisterHooks() {
        if (hooksRegistered || !hooks)
            return;

        hooks->SetupOnCallFunctionHook(
            [this](const FunctionEventArgs& args) {
                if (runStage.load() != RunStage::WAIT_CALL)
                    return;
                
                if (keyboard && regiskey) {
                    if (args.lr == 0x1f33c) {
                        keyboard->PressCode(0x13, true);
                    }
    
                    if (args.pc == 0x28b1e) {
                        keyboard->ReleaseAll();
                        regiskey = false;
                    }
                    return;
                }

                if (args.lr != 0x1f33c && args.lr != 0x1f3be || regiskey)  //1f338
                    return;

                if (cancelRequested.load())
                    return;

                // Apply the custom injection for this testcase.
                for (const auto& injection : customInjections) {
                    if (injection.name == activeTestName) {
                        if (!ApplyInjection(injection)) {
                            FinishRun();
                            return;
                        }

                        break;
                    }
                }
                // Apply the root's shared input rows.
                if (!ApplyInputRows(activeInput)) {
                    FinishRun();
                    return;
                }
                if (keyboard)
                    keyboard->PressCode(0x60, true);

                runStage = RunStage::WAIT_ENTER;
            }
        );

        hooks->SetupOnInstructionHook(
            [this](InstructionEventArgs& args) {
                const RunStage stage = runStage.load();

                if (stage == RunStage::IDLE)
                    return;

                if (cancelRequested.load())
                    return;

                if (stage == RunStage::WAIT_ENTER &&
                    args.pc_after == activePCEnter) {

                    // Release the simulated key.
                    if (keyboard)
                        keyboard->ReleaseAll();
                    if (cpu)
                        cpu->SetCyclePause(false);

                    runStage = RunStage::WAIT_EXIT;
                    return;
                }

                if (stage == RunStage::WAIT_EXIT &&
                    args.pc_after == activePCExit) {

                    FinishRun();
                }
            }
        );

        hooksRegistered = true;
    }

    // --------------------------------------------------------
    // Start / run
    // --------------------------------------------------------

    bool StartRun(
        TestRoot& root,
        TestCase& test,
        int testIndex
    ) {
        if (!cpu || !emu || !hooks || !keyboard || !mmu) {
            statusMessage = "Required plugin interface unavailable.";
            return false;
        }

        if (runStage.load() != RunStage::IDLE) {
            statusMessage = "A test is already running.";
            return false;
        }

        uint32_t pcEnter = 0;
        uint32_t pcExit = 0;

        if (!ParseHexChecked(root.pcEnter, pcEnter) ||
            !ParseHexChecked(root.pcExit, pcExit)) {
            statusMessage = "Invalid PC Enter or PC Exit address.";
            return false;
        }

        // Validate and snapshot input rows before starting.
        for (const auto& row : root.input) {
            uint32_t address = 0;
            std::vector<uint8_t> bytes;

            if (!ParseHexChecked(row.address, address) ||
                !ConvertVector(row.data, bytes)) {
                statusMessage = "Invalid input row.";
                return false;
            }
        }

        activeInput = root.input;
        activeExpectations = test.expectations;

        activeTestName = GetTestName(root, testIndex);

        activePCEnter = pcEnter;
        activePCExit = pcExit;

        activeTest = &test;

        cancelRequested = false;

        regiskey = emu->IsPaused();

        RegisterHooks();

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            test.result = TestResult{};
        }

        // Stop CPU before resetting.
        emu->Pause();
        cpu->SetCyclePause(true);
        cpu->ClearCycle();
        chipset->Reset();

        // Start waiting for the initial function call.
        runStage = RunStage::WAIT_CALL;

        emu->Resume();

        statusMessage = "Test running.";
        return true;
    }

    // --------------------------------------------------------
    // Reload custom injections
    // --------------------------------------------------------

    bool Reload() {
        if (!platform)
            return false;

        const char* externalPath =
            platform->GetExternalStoragePath();

        if (!externalPath || !*externalPath)
            return false;

        std::string path = externalPath;

        if (path.back() != '/' && path.back() != '\\')
            path += '/';

        path += "hc-inj.txt";

        std::ifstream file(path, std::ios::binary);

        if (!file.is_open())
            return false;

        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        if (file.bad())
            return false;

        return ParseCustomInjections(content);
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

        file.flush();

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

        // Parse into temporary storage first.
        std::array<
            std::array<std::vector<InjectorRow>, 10>,
            6
        > loaded;

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

            int rootIndex = -1;
            int testIndex = -1;

            for (int r = 0; r < 6; ++r) {
                for (int t = 0; t < 10; ++t) {
                    if (testName == GetTestName(roots[r], t)) {
                        rootIndex = r;
                        testIndex = t;
                        break;
                    }
                }

                if (rootIndex >= 0)
                    break;
            }

            if (rootIndex < 0)
                continue;

            if (address.size() >= sizeof(InjectorRow::address) ||
                data.size() >= sizeof(InjectorRow::data)) {
                return false;
            }

            InjectorRow row{};

            std::snprintf(
                row.address,
                sizeof(row.address),
                "%s",
                address.c_str()
            );

            std::snprintf(
                row.data,
                sizeof(row.data),
                "%s",
                data.c_str()
            );

            loaded[rootIndex][testIndex].push_back(row);
        }

        if (file.bad())
            return false;

        // Commit only after successful reading.
        for (int r = 0; r < 6; ++r) {
            for (int t = 0; t < 10; ++t) {
                roots[r].tests[t].expectations =
                    std::move(loaded[r][t]);
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
            statusMessage = Reload()
                ? "Custom injections reloaded."
                : "Failed to reload custom injections.";
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
        const bool running =
            runStage.load() != RunStage::IDLE;

        if (running) {
            if (ImGui::Button("Stop Test")) {
                StopRun();
                statusMessage = "Test stopped.";
            }
        }
        else {
            if (ImGui::Button(
                test.result.cycles == 0 ? "Run" : "Rerun"
            )) {
                StartRun(root, test, selectedTest);
            }
        }

        TestResult displayedResult;

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            displayedResult = test.result;
        }

        ImGui::SameLine();

        ImGui::Text(
            "Cycles: %llu",
            static_cast<unsigned long long>(displayedResult.cycles)
        );

        ImGui::SameLine();

        const double seconds =
            static_cast<double>(displayedResult.cycles) / 2097152.0;

        ImGui::Text("| %.6f s", seconds);

        if (running) {
            ImGui::Text("Running...");
        }
        else if (displayedResult.cycles == 0) {
            ImGui::TextDisabled("Not run");
        }
        else if (displayedResult.cmp) {
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

        ImGui::Text("Got");

        ImGui::InputTextMultiline(
            "##Got",
            displayedResult.got.data(),
            displayedResult.got.size() + 1,
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

    void SetKeyboard(IKeyboard* value) {
        keyboard = value;
    }

    void SetMMU(IMMU* value) {
        mmu = value;
    }

    void SetEmu(IEmulator* value) {
        emu = value;
    }

    void SetChipset(IChipset* _chipset) {
        chipset = _chipset;
    }

    void RenderCore() override {
        bool temp = true;
        RenderRootSelector();

        TestRoot& root = roots[selectedRoot];

        ImGui::Separator();

        RenderRootInput(root);

        if (temp) {
            if (ImGui::Button("Run all")) {
                int old_selectedTest = selectedTest;
                temp = false;
                for (selectedTest = 0; selectedTest < 9; ++selectedTest) {
                    TestCase& test = root.tests[selectedTest];
                    StartRun(root, test, selectedTest);
                    while (1) {
                        if (runStage.load() == RunStage::IDLE || temp) break;
                    }
                    if (temp) break;
                }
                temp = true;
                selectedTest = old_selectedTest;
            }
        } else {
            if (ImGui::Button("Stop")) {
                temp = true;
            }
        }

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
    window->SetChipset(api->QueryInterface<IChipset>());

    api->AddWindow(window);
}

#if !defined(__ANDROID__)
BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
#endif