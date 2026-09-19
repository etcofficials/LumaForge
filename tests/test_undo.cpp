#include "TestMain.h"

#include "lumaforge/commands/UndoStack.h"

#include <memory>

using namespace lf::cmd;

namespace {
struct Counter {
    int value = 0;
};

class AddCommand final : public Command {
public:
    AddCommand(Counter& c, int delta, bool mergeable = false) : m_c(c), m_delta(delta), m_mergeable(mergeable) {}
    std::string text() const override { return "Add"; }
    void redo() override { m_c.value += m_delta; }
    void undo() override { m_c.value -= m_delta; }
    int mergeId() const override { return m_mergeable ? 1 : -1; }
    bool mergeWith(const Command& next) override {
        m_delta += static_cast<const AddCommand&>(next).m_delta;
        return true;
    }

private:
    Counter& m_c;
    int m_delta;
    bool m_mergeable;
};
}  // namespace

TEST_CASE("undo/redo applies and reverses in order") {
    Counter c;
    UndoStack s;
    s.push(std::make_unique<AddCommand>(c, 1));
    s.push(std::make_unique<AddCommand>(c, 10));
    CHECK(c.value == 11);
    CHECK(s.canUndo() && !s.canRedo());
    s.undo();
    CHECK(c.value == 1);
    s.undo();
    CHECK(c.value == 0);
    CHECK(!s.canUndo() && s.canRedo());
    s.redo();
    CHECK(c.value == 1);
}

TEST_CASE("push after undo drops the redo branch") {
    Counter c;
    UndoStack s;
    s.push(std::make_unique<AddCommand>(c, 1));
    s.push(std::make_unique<AddCommand>(c, 2));
    s.undo();
    s.push(std::make_unique<AddCommand>(c, 5));
    CHECK(!s.canRedo());
    CHECK(c.value == 6);
    CHECK(s.count() == 2);
}

TEST_CASE("mergeable commands coalesce into one step") {
    Counter c;
    UndoStack s;
    s.push(std::make_unique<AddCommand>(c, 1, true));
    s.push(std::make_unique<AddCommand>(c, 1, true));
    s.push(std::make_unique<AddCommand>(c, 1, true));
    CHECK(c.value == 3);
    CHECK(s.count() == 1);
    s.undo();
    CHECK(c.value == 0);
}

TEST_CASE("macros group commands and drop empty groups") {
    Counter c;
    UndoStack s;
    s.beginMacro("Group");
    s.push(std::make_unique<AddCommand>(c, 2));
    s.push(std::make_unique<AddCommand>(c, 3));
    s.endMacro();
    CHECK(c.value == 5);
    CHECK(s.count() == 1);
    s.undo();
    CHECK(c.value == 0);
    s.beginMacro("Empty");
    s.endMacro();
    CHECK(s.count() == 1);   // the empty macro left nothing behind...
    CHECK(s.canRedo());      // ...and did not destroy the redo branch
    s.redo();
    CHECK(c.value == 5);
}

TEST_CASE("clean state tracks the save point") {
    Counter c;
    UndoStack s;
    CHECK(s.isClean());
    s.push(std::make_unique<AddCommand>(c, 1));
    CHECK(!s.isClean());
    s.setClean();
    CHECK(s.isClean());
    s.undo();
    CHECK(!s.isClean());
    s.redo();
    CHECK(s.isClean());
}
