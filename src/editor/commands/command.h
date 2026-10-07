#ifndef COMMAND_H
#define COMMAND_H

#include <vector>
#include <memory>
#include <string>

class IEditCommand {
public:
    virtual ~IEditCommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual const char* GetName() const = 0;
};

class CommandManager {
public:
    CommandManager() : m_historyIndex(0), m_savedHistoryIndex(0) {}

    void ExecuteCommand(std::unique_ptr<IEditCommand> cmd) {
        if (!cmd) return;

        // Truncate any redo history if we are in the middle of the stack
        if (m_historyIndex < m_history.size()) {
            m_history.erase(m_history.begin() + m_historyIndex, m_history.end());
        }

        cmd->Execute();
        m_history.push_back(std::move(cmd));
        m_historyIndex = m_history.size();
    }

    bool CanUndo() const {
        return m_historyIndex > 0;
    }

    bool CanRedo() const {
        return m_historyIndex < m_history.size();
    }

    void Undo() {
        if (!CanUndo()) return;
        --m_historyIndex;
        m_history[m_historyIndex]->Undo();
    }

    void Redo() {
        if (!CanRedo()) return;
        m_history[m_historyIndex]->Execute();
        ++m_historyIndex;
    }

    void Clear() {
        m_history.clear();
        m_historyIndex = 0;
        m_savedHistoryIndex = 0;
    }

    void MarkSaved() {
        m_savedHistoryIndex = m_historyIndex;
    }

    bool HasUnsavedChanges() const {
        return m_historyIndex != m_savedHistoryIndex;
    }

    const char* GetUndoName() const {
        if (!CanUndo()) return "";
        return m_history[m_historyIndex - 1]->GetName();
    }

    const char* GetRedoName() const {
        if (!CanRedo()) return "";
        return m_history[m_historyIndex]->GetName();
    }

private:
    std::vector<std::unique_ptr<IEditCommand>> m_history;
    size_t m_historyIndex;
    size_t m_savedHistoryIndex;
};

#endif // COMMAND_H
