#pragma once

#include "lumaforge/commands/Command.h"

#include <functional>
#include <string>
#include <vector>

namespace lf::cmd {

// Linear undo history. Not thread-safe by design: all document edits happen
// on the UI thread; background workers post results as commands to it.
class UndoStack {
public:
    using ChangeListener = std::function<void()>;

    // Executes the command (redo()) and records it. Clears the redo branch.
    void push(CommandPtr command);
    void undo();
    void redo();
    void clear();

    [[nodiscard]] bool canUndo() const noexcept { return m_index > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_index < m_commands.size(); }
    [[nodiscard]] std::string undoText() const;   // "" when nothing to undo
    [[nodiscard]] std::string redoText() const;
    [[nodiscard]] std::size_t count() const noexcept { return m_commands.size(); }
    [[nodiscard]] std::size_t index() const noexcept { return m_index; }

    // "Clean" == matches the last saved state; drives the title-bar dirty flag.
    void setClean() noexcept { m_cleanIndex = static_cast<long long>(m_index); notify(); }
    [[nodiscard]] bool isClean() const noexcept { return m_cleanIndex == static_cast<long long>(m_index); }

    void setUndoLimit(std::size_t limit) noexcept { m_limit = limit; }
    void setChangeListener(ChangeListener listener) { m_listener = std::move(listener); }

    // Groups every push() until endMacro() into a single undo step.
    void beginMacro(std::string text);
    void endMacro();

private:
    void notify() { if (m_listener) m_listener(); }

    std::vector<CommandPtr> m_commands;
    std::size_t m_index = 0;           // commands[0..index) are applied
    long long m_cleanIndex = 0;        // -1 = can never be clean again (history truncated past save point)
    std::size_t m_limit = 200;
    ChangeListener m_listener;

    class MacroCommand;
    void truncateRedoBranch();

    std::vector<MacroCommand*> m_macroStack;        // open macros, outermost first
    std::unique_ptr<Command> m_pendingMacro;         // outermost macro until its first push
};

}  // namespace lf::cmd
