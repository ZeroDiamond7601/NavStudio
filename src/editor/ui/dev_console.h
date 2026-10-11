#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

#include <string>
#include <vector>
#include <mutex>
#include <imgui.h>

class EditorScene;
class Camera;
class CommandManager;
class EditorUI;

class DevConsole {
public:
    enum class LogLevel {
        Info,
        Warning,
        Error,
        System,
        Command
    };

    struct LogItem {
        std::string text;
        LogLevel level;
        ImVec4 color;
    };

    static DevConsole& Get();

    DevConsole();
    ~DevConsole();

    void AddLog(const char* fmt, ...);
    void AddLog(LogLevel level, const char* fmt, ...);
    void Clear();

    void Toggle() {
        m_isOpen = !m_isOpen;
        if (m_isOpen) {
            m_focusInput = true;
        }
    }
    bool IsOpen() const { return m_isOpen; }
    void SetOpen(bool open) {
        m_isOpen = open;
        if (m_isOpen) {
            m_focusInput = true;
        }
    }

    void Render(EditorScene& scene, Camera& camera, CommandManager& cmdMgr, EditorUI& ui);
    void ExecuteCommand(const std::string& cmdLine, EditorScene& scene, Camera& camera, CommandManager& cmdMgr, EditorUI& ui);

private:
    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data);
    int TextEditCallback(ImGuiInputTextCallbackData* data);

    std::vector<LogItem> m_items;
    std::vector<std::string> m_history;
    int m_historyPos{-1};
    char m_inputBuf[512]{""};
    char m_filterBuf[128]{""};
    bool m_autoScroll{true};
    bool m_scrollToBottom{false};
    bool m_isOpen{false};
    bool m_focusInput{false};
    mutable std::mutex m_mutex;
    static constexpr size_t kMaxLogLines = 3000;
};

// Global log functions
void ConsoleLog(const char* fmt, ...);
void ConsoleLogWarning(const char* fmt, ...);
void ConsoleLogError(const char* fmt, ...);
void ConsoleLogSystem(const char* fmt, ...);

#endif // DEV_CONSOLE_H
