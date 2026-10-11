#include "editor/ui/dev_console.h"
#include "editor/ui/editor_ui.h"
#include "editor/scene/editor_scene.h"
#include "editor/camera/camera.h"
#include "editor/commands/command.h"
#include "editor/version.h"

#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <cctype>

DevConsole& DevConsole::Get() {
    static DevConsole s_instance;
    return s_instance;
}

DevConsole::DevConsole() {
    AddLog(LogLevel::System, "====================================================");
    AddLog(LogLevel::System, "  NavStudio v%s Developer Console", NAVSTUDIO_VERSION_STRING);
    AddLog(LogLevel::System, "  Type 'help' for a list of available console commands.");
    AddLog(LogLevel::System, "====================================================");
}

DevConsole::~DevConsole() = default;

void DevConsole::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items.clear();
}

void DevConsole::AddLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LogLevel level = LogLevel::Info;
    std::string s(buf);
    if (s.rfind("[Error]", 0) == 0 || s.rfind("[ERROR]", 0) == 0) {
        level = LogLevel::Error;
    } else if (s.rfind("[Warning]", 0) == 0 || s.rfind("[WARN]", 0) == 0) {
        level = LogLevel::Warning;
    } else if (s.rfind("[OpenGL]", 0) == 0 || s.rfind("[System]", 0) == 0) {
        level = LogLevel::System;
    }

    AddLog(level, "%s", buf);
}

void DevConsole::AddLog(LogLevel level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ImVec4 col;
    switch (level) {
        case LogLevel::Error:
            col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
            break;
        case LogLevel::Warning:
            col = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);
            break;
        case LogLevel::System:
            col = ImVec4(0.35f, 0.85f, 1.0f, 1.0f);
            break;
        case LogLevel::Command:
            col = ImVec4(0.40f, 0.95f, 0.45f, 1.0f);
            break;
        case LogLevel::Info:
        default:
            col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            break;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_items.push_back({ std::string(buf), level, col });
    if (m_items.size() > kMaxLogLines) {
        m_items.erase(m_items.begin(), m_items.begin() + (m_items.size() - kMaxLogLines));
    }

    if (m_autoScroll) {
        m_scrollToBottom = true;
    }
}

int DevConsole::TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
    DevConsole* console = static_cast<DevConsole*>(data->UserData);
    return console->TextEditCallback(data);
}

int DevConsole::TextEditCallback(ImGuiInputTextCallbackData* data) {
    static const std::vector<std::string> s_commands = {
        "help", "clear", "cls", "version", "status", "stats", "diagnostics",
        "fps", "compass", "skybox", "entities", "bsp_mode", "theme",
        "grid_snap", "grid_size", "save", "echo", "quit", "exit"
    };

    switch (data->EventFlag) {
        case ImGuiInputTextFlags_CallbackCompletion: {
            // Tab autocompletion
            const char* wordEnd = data->Buf + data->CursorPos;
            const char* wordStart = wordEnd;
            while (wordStart > data->Buf) {
                const char c = wordStart[-1];
                if (c == ' ' || c == '\t') break;
                wordStart--;
            }

            int matchLen = static_cast<int>(wordEnd - wordStart);
            if (matchLen > 0) {
                std::string prefix(wordStart, matchLen);
                std::string match;
                int matchCount = 0;
                for (const auto& cmd : s_commands) {
                    if (cmd.rfind(prefix, 0) == 0) {
                        match = cmd;
                        matchCount++;
                    }
                }
                if (matchCount == 1) {
                    data->DeleteChars(static_cast<int>(wordStart - data->Buf), matchLen);
                    data->InsertChars(data->CursorPos, match.c_str());
                    data->InsertChars(data->CursorPos, " ");
                }
            }
            break;
        }
        case ImGuiInputTextFlags_CallbackHistory: {
            const int prevHistoryPos = m_historyPos;
            if (data->EventKey == ImGuiKey_UpArrow) {
                if (m_historyPos == -1) {
                    m_historyPos = static_cast<int>(m_history.size()) - 1;
                } else if (m_historyPos > 0) {
                    m_historyPos--;
                }
            } else if (data->EventKey == ImGuiKey_DownArrow) {
                if (m_historyPos != -1) {
                    if (++m_historyPos >= static_cast<int>(m_history.size())) {
                        m_historyPos = -1;
                    }
                }
            }

            if (prevHistoryPos != m_historyPos) {
                const char* historyStr = (m_historyPos >= 0) ? m_history[m_historyPos].c_str() : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, historyStr);
            }
            break;
        }
    }
    return 0;
}

void DevConsole::ExecuteCommand(const std::string& cmdLine, EditorScene& scene, Camera& camera, CommandManager& cmdMgr, EditorUI& ui) {
    AddLog(LogLevel::Command, "> %s", cmdLine.c_str());

    std::string trimmed = cmdLine;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r')) trimmed.pop_back();

    if (trimmed.empty()) return;

    // History update
    m_historyPos = -1;
    if (m_history.empty() || m_history.back() != trimmed) {
        m_history.push_back(trimmed);
        if (m_history.size() > 100) {
            m_history.erase(m_history.begin());
        }
    }

    std::stringstream ss(trimmed);
    std::string cmd;
    ss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "help") {
        AddLog(LogLevel::System, "Available Developer Console Commands:");
        AddLog(LogLevel::Info, "  help                  - Show list of console commands");
        AddLog(LogLevel::Info, "  clear / cls           - Clear console output window");
        AddLog(LogLevel::Info, "  version               - Display NavStudio version & build metadata");
        AddLog(LogLevel::Info, "  status                - View loaded map, navmesh, and waypoint metrics");
        AddLog(LogLevel::Info, "  stats                 - Print detailed geometric & node graph statistics");
        AddLog(LogLevel::Info, "  diagnostics           - Run Mesh Doctor & Waypoint validation suite");
        AddLog(LogLevel::Info, "  save                  - Save current navigation mesh or bot waypoints");
        AddLog(LogLevel::Info, "  fps                   - Toggle FPS & frame performance overlay");
        AddLog(LogLevel::Info, "  compass               - Toggle Blender viewport compass");
        AddLog(LogLevel::Info, "  skybox                - Toggle 3D skybox rendering");
        AddLog(LogLevel::Info, "  entities              - Toggle map entity rendering (spawns/triggers)");
        AddLog(LogLevel::Info, "  bsp_mode <0-3>        - Shading mode: 0=textured, 1=solid, 2=wire, 3=ghost");
        AddLog(LogLevel::Info, "  theme <0-3>           - Color theme: 0=Slate, 1=GoldSrc, 2=Studio, 3=Light");
        AddLog(LogLevel::Info, "  grid_snap <0|1>       - Toggle grid snapping");
        AddLog(LogLevel::Info, "  grid_size <units>     - Set grid snapping unit size (e.g. 16, 32, 64)");
        AddLog(LogLevel::Info, "  echo <text>           - Print text back to console");
        AddLog(LogLevel::Info, "  quit / exit           - Safely exit application with save confirmation");
    } else if (cmd == "clear" || cmd == "cls") {
        Clear();
    } else if (cmd == "version") {
        AddLog(LogLevel::System, "NavStudio v%s (Win32 / Linux)", NAVSTUDIO_VERSION_STRING);
        AddLog(LogLevel::Info, "Official repository: https://github.com/ZeroDiamond7601/NavStudio");
    } else if (cmd == "status") {
        AddLog(LogLevel::System, "--- NavStudio Runtime Status ---");
        AddLog(LogLevel::Info, "  Map:        %s", scene.HasBSP() ? scene.GetBSPName().c_str() : "[None]");
        if (scene.HasNAV()) {
            AddLog(LogLevel::Info, "  NavMesh:    %zu areas, %zu ladders (Modified: %s)",
                   scene.GetNAV().GetAreas().size(), scene.GetLadderCount(),
                   (scene.IsModified() || cmdMgr.HasUnsavedChanges()) ? "Yes" : "No");
        } else {
            AddLog(LogLevel::Info, "  NavMesh:    [None]");
        }
        if (scene.HasWaypoints()) {
            AddLog(LogLevel::Info, "  Waypoints:  %zu nodes, %zu links",
                   scene.GetWaypoints().GetNodes().size(), scene.GetWaypoints().GetTotalConnectionCount());
        } else {
            AddLog(LogLevel::Info, "  Waypoints:  [None]");
        }
        Vector3 pos = camera.GetPosition();
        AddLog(LogLevel::Info, "  Camera:     (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
    } else if (cmd == "stats") {
        AddLog(LogLevel::System, "--- Scene Statistics ---");
        if (scene.HasBSP()) {
            AddLog(LogLevel::Info, "  BSP Models:    %d", scene.GetBSP().GetModelCount());
            AddLog(LogLevel::Info, "  BSP Entities:  %d", scene.GetBSP().GetEntityCount());
            AddLog(LogLevel::Info, "  BSP Faces:     %d", scene.GetBSP().GetFaceCount());
        }
        if (scene.HasNAV()) {
            size_t hidingCount = 0;
            for (const auto* area : scene.GetNAV().GetAreas()) {
                if (area) hidingCount += area->GetHidingSpots().size();
            }
            AddLog(LogLevel::Info, "  NAV Areas:     %zu", scene.GetNAV().GetAreas().size());
            AddLog(LogLevel::Info, "  NAV Ladders:   %zu", scene.GetLadderCount());
            AddLog(LogLevel::Info, "  NAV Hiding:    %zu", hidingCount);
        }
        if (scene.HasWaypoints()) {
            AddLog(LogLevel::Info, "  Bot Nodes:     %zu", scene.GetWaypoints().GetNodes().size());
            AddLog(LogLevel::Info, "  Bot Links:     %zu", scene.GetWaypoints().GetTotalConnectionCount());
        }
    } else if (cmd == "diagnostics") {
        if (scene.HasNAV()) {
            scene.RunDiagnostics();
            AddLog(LogLevel::System, "Ran mesh diagnostics. Check Diagnostics modal or log output.");
        } else if (scene.HasWaypoints()) {
            bool valid = scene.GetWaypoints().ValidateNodes();
            AddLog(LogLevel::System, valid ? "All waypoint links valid." : "Repaired invalid links.");
        } else {
            AddLog(LogLevel::Warning, "No active NavMesh or Waypoints loaded to diagnose.");
        }
    } else if (cmd == "save") {
        if (scene.HasNAV()) {
            if (scene.SaveNAV()) {
                cmdMgr.MarkSaved();
                scene.SetModified(false);
                AddLog(LogLevel::System, "Successfully saved navigation mesh: %s", scene.GetNAVPath().c_str());
            } else {
                AddLog(LogLevel::Error, "Failed to save navigation mesh.");
            }
        } else if (scene.HasWaypoints()) {
            if (scene.SaveWaypoints()) {
                scene.SetModified(false);
                AddLog(LogLevel::System, "Successfully saved bot waypoints: %s", scene.GetWaypointPath().c_str());
            } else {
                AddLog(LogLevel::Error, "Failed to save bot waypoints.");
            }
        } else {
            AddLog(LogLevel::Warning, "No active NavMesh or Waypoints loaded to save.");
        }
    } else if (cmd == "fps") {
        auto& prefs = scene.GetPreferences();
        prefs.showFps = !prefs.showFps;
        AddLog(LogLevel::Info, "FPS overlay: %s", prefs.showFps ? "Enabled" : "Disabled");
    } else if (cmd == "compass") {
        auto& prefs = scene.GetPreferences();
        prefs.showCompass = !prefs.showCompass;
        AddLog(LogLevel::Info, "Viewport compass: %s", prefs.showCompass ? "Enabled" : "Disabled");
    } else if (cmd == "skybox") {
        scene.SetShowSkybox(!scene.GetShowSkybox());
        AddLog(LogLevel::Info, "3D Skybox: %s", scene.GetShowSkybox() ? "Enabled" : "Disabled");
    } else if (cmd == "entities") {
        auto& entR = scene.GetEntityRenderer();
        entR.SetShowEntities(!entR.GetShowEntities());
        AddLog(LogLevel::Info, "Entity rendering: %s", entR.GetShowEntities() ? "Enabled" : "Disabled");
    } else if (cmd == "bsp_mode") {
        std::string mode;
        if (ss >> mode) {
            int m = -1;
            if (mode == "0" || mode == "textured") m = 0;
            else if (mode == "1" || mode == "solid") m = 1;
            else if (mode == "2" || mode == "wire" || mode == "wireframe") m = 2;
            else if (mode == "3" || mode == "ghost") m = 3;
            if (m >= 0) {
                scene.SetBSPMode(static_cast<BSPRenderMode>(m));
                AddLog(LogLevel::Info, "BSP shading mode set to %d (%s)", m, mode.c_str());
            } else {
                AddLog(LogLevel::Error, "Invalid shading mode: %s. Use 0=textured, 1=solid, 2=wire, 3=ghost", mode.c_str());
            }
        } else {
            AddLog(LogLevel::Info, "Current BSP shading mode: %d", static_cast<int>(scene.GetBSPMode()));
        }
    } else if (cmd == "theme") {
        int t = 0;
        if (ss >> t && t >= 0 && t <= 3) {
            ui.ApplyTheme(t);
            scene.GetPreferences().themeIndex = t;
            AddLog(LogLevel::Info, "Applied theme %d", t);
        } else {
            AddLog(LogLevel::Error, "Usage: theme <0-3> (0=Modern Slate, 1=Classic GoldSrc, 2=Clean Studio, 3=Light Studio)");
        }
    } else if (cmd == "grid_snap") {
        int v = 0;
        if (ss >> v) {
            scene.SetGridSnap(v != 0);
            AddLog(LogLevel::Info, "Grid snap %s", (v != 0) ? "Enabled" : "Disabled");
        } else {
            scene.SetGridSnap(!scene.GetGridSnap());
            AddLog(LogLevel::Info, "Grid snap toggled to %s", scene.GetGridSnap() ? "Enabled" : "Disabled");
        }
    } else if (cmd == "grid_size") {
        float sz = 0.0f;
        if (ss >> sz && sz >= 1.0f) {
            scene.SetGridSize(sz);
            AddLog(LogLevel::Info, "Grid snap size set to %.0f units", sz);
        } else {
            AddLog(LogLevel::Info, "Current grid snap size: %.0f units", scene.GetGridSize());
        }
    } else if (cmd == "echo") {
        std::string rest;
        std::getline(ss, rest);
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        AddLog(LogLevel::Info, "%s", rest.c_str());
    } else if (cmd == "quit" || cmd == "exit") {
        ui.PromptQuit(scene, cmdMgr);
    } else {
        AddLog(LogLevel::Error, "Unknown command: '%s'. Type 'help' for available commands.", cmd.c_str());
    }
}

void DevConsole::Render(EditorScene& scene, Camera& camera, CommandManager& cmdMgr, EditorUI& ui) {
    if (!m_isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(720, 440), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.12f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.50f, 0.85f, 0.80f));

    if (ImGui::Begin("Developer Console (~)###DevConsoleWindow", &m_isOpen)) {
        // Top Toolbar
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##ConsoleFilter", "Filter logs...", m_filterBuf, sizeof(m_filterBuf));
        ImGui::SameLine();

        if (ImGui::Button("Clear")) {
            Clear();
        }
        ImGui::SameLine();

        if (ImGui::Button("Copy All")) {
            std::string allLogs;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (const auto& item : m_items) {
                    allLogs += item.text + "\n";
                }
            }
            ImGui::SetClipboardText(allLogs.c_str());
        }
        ImGui::SameLine();

        ImGui::Checkbox("Auto-scroll", &m_autoScroll);
        ImGui::Separator();

        // Log scrolling region
        const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

        std::string filterLower = m_filterBuf;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& item : m_items) {
                if (!filterLower.empty()) {
                    std::string itemLower = item.text;
                    std::transform(itemLower.begin(), itemLower.end(), itemLower.begin(), ::tolower);
                    if (itemLower.find(filterLower) == std::string::npos) {
                        continue;
                    }
                }
                ImGui::PushStyleColor(ImGuiCol_Text, item.color);
                ImGui::TextUnformatted(item.text.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (m_scrollToBottom || (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
            ImGui::SetScrollHereY(1.0f);
        }
        m_scrollToBottom = false;

        ImGui::EndChild();
        ImGui::Separator();

        // Command Input Bar
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.45f, 1.0f), ">");
        ImGui::SameLine();

        ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_CallbackCompletion |
                                         ImGuiInputTextFlags_CallbackHistory;

        ImGui::SetNextItemWidth(-80);
        if (m_focusInput) {
            ImGui::SetKeyboardFocusHere();
            m_focusInput = false;
        }

        bool reclaimFocus = false;
        if (ImGui::InputTextWithHint("##CommandInput", "Type a command ('help', 'status', 'clear', 'quit')...",
                                     m_inputBuf, sizeof(m_inputBuf), inputFlags, &DevConsole::TextEditCallbackStub, this)) {
            std::string cmd(m_inputBuf);
            m_inputBuf[0] = '\0';
            ExecuteCommand(cmd, scene, camera, cmdMgr, ui);
            reclaimFocus = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Submit", ImVec2(70, 0))) {
            std::string cmd(m_inputBuf);
            m_inputBuf[0] = '\0';
            ExecuteCommand(cmd, scene, camera, cmdMgr, ui);
            reclaimFocus = true;
        }

        if (reclaimFocus) {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void ConsoleLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    DevConsole::Get().AddLog(DevConsole::LogLevel::Info, "%s", buf);
}

void ConsoleLogWarning(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    DevConsole::Get().AddLog(DevConsole::LogLevel::Warning, "%s", buf);
}

void ConsoleLogError(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    DevConsole::Get().AddLog(DevConsole::LogLevel::Error, "%s", buf);
}

void ConsoleLogSystem(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    DevConsole::Get().AddLog(DevConsole::LogLevel::System, "%s", buf);
}
