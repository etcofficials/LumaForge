#pragma once

#include <memory>
#include <string>

namespace lf::cmd {

// Every user edit is a Command: it knows how to apply itself and how to
// reverse itself. Commands only ever mutate the document model; the UI
// observes model changes. This is what makes undo/redo a property of the
// architecture instead of a feature bolted onto each operation.
class Command {
public:
    virtual ~Command() = default;

    // Human-readable, for "Undo Move Clip" menu text.
    [[nodiscard]] virtual std::string text() const = 0;

    virtual void redo() = 0;   // apply (also used for the first execution)
    virtual void undo() = 0;

    // Optional coalescing: consecutive drags of the same clip collapse into
    // one undo step. Return true if `next` was absorbed into this command.
    [[nodiscard]] virtual bool mergeWith(const Command& /*next*/) { return false; }
    [[nodiscard]] virtual int mergeId() const { return -1; }  // -1 = never merge
};

using CommandPtr = std::unique_ptr<Command>;

}  // namespace lf::cmd
