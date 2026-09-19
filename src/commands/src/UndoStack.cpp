#include "lumaforge/commands/UndoStack.h"

#include <cassert>

namespace lf::cmd {

class UndoStack::MacroCommand final : public Command {
public:
    explicit MacroCommand(std::string text) : m_text(std::move(text)) {}
    [[nodiscard]] std::string text() const override { return m_text; }
    void redo() override { for (auto& c : m_children) c->redo(); }
    void undo() override { for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) (*it)->undo(); }
    void add(CommandPtr c) { m_children.push_back(std::move(c)); }
    [[nodiscard]] bool empty() const noexcept { return m_children.empty(); }

private:
    std::string m_text;
    std::vector<CommandPtr> m_children;
};

void UndoStack::truncateRedoBranch() {
    if (m_index < m_commands.size()) {
        m_commands.erase(m_commands.begin() + static_cast<std::ptrdiff_t>(m_index), m_commands.end());
        if (m_cleanIndex > static_cast<long long>(m_index)) m_cleanIndex = -1;
    }
}

void UndoStack::push(CommandPtr command) {
    assert(command);
    command->redo();

    if (!m_macroStack.empty()) {
        // The outermost macro enters the history only now, on its first real
        // edit, so an empty macro never disturbs the redo branch.
        if (m_pendingMacro) {
            truncateRedoBranch();
            m_commands.push_back(std::move(m_pendingMacro));
            ++m_index;
        }
        m_macroStack.back()->add(std::move(command));
        return;
    }

    truncateRedoBranch();

    // Coalesce with the previous command when both agree.
    if (m_index > 0) {
        auto& prev = m_commands[m_index - 1];
        const int id = command->mergeId();
        if (id != -1 && id == prev->mergeId() && prev->mergeWith(*command)) {
            notify();
            return;
        }
    }

    m_commands.push_back(std::move(command));
    ++m_index;

    if (m_commands.size() > m_limit) {
        m_commands.erase(m_commands.begin());
        --m_index;
        if (m_cleanIndex > 0) --m_cleanIndex;
        else if (m_cleanIndex == 0) m_cleanIndex = -1;
    }
    notify();
}

void UndoStack::undo() {
    if (!canUndo() || !m_macroStack.empty()) return;
    m_commands[--m_index]->undo();
    notify();
}

void UndoStack::redo() {
    if (!canRedo() || !m_macroStack.empty()) return;
    m_commands[m_index++]->redo();
    notify();
}

void UndoStack::clear() {
    m_commands.clear();
    m_index = 0;
    m_cleanIndex = 0;
    m_macroStack.clear();
    m_pendingMacro.reset();
    notify();
}

std::string UndoStack::undoText() const { return canUndo() ? m_commands[m_index - 1]->text() : std::string{}; }
std::string UndoStack::redoText() const { return canRedo() ? m_commands[m_index]->text() : std::string{}; }

void UndoStack::beginMacro(std::string text) {
    auto macro = std::make_unique<MacroCommand>(std::move(text));
    m_macroStack.push_back(macro.get());
    if (m_macroStack.size() == 1) m_pendingMacro = std::move(macro);
    else m_macroStack[m_macroStack.size() - 2]->add(std::move(macro));
}

void UndoStack::endMacro() {
    if (m_macroStack.empty()) return;
    m_macroStack.pop_back();
    if (m_macroStack.empty()) {
        m_pendingMacro.reset();   // still pending == nothing was pushed: drop it
        notify();
    }
}

}  // namespace lf::cmd
