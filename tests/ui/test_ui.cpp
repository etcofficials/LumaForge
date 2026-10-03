// UI-level tests: a real MainWindow (no GPU) driven by synthetic key events
// delivered through QWindowSystemInterface - the same path physical key
// presses take, including Qt's shortcut map. Verifies that shortcuts reach
// the intended actions and that undo/redo (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z)
// work end to end.
#include "../TestMain.h"

#include "lumaforge/project/Document.h"
#include "ui/ActionRegistry.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CommandPalette.h"
#include "ui/timeline/TimelineView.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QSettings>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include <cstdio>
#include <cstring>
#include <memory>

using namespace lf;
using namespace lf::ui;

namespace {

MainWindow* g_window = nullptr;

QAction* act(const char* objectName) { return g_window->findChild<QAction*>(QString::fromLatin1(objectName)); }
TimelineView* timeline() { return g_window->findChild<TimelineView*>(); }

void pump() {
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
}

void press(int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QWindow* w = g_window->windowHandle();
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(w, QEvent::KeyPress, key, mods);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(w, QEvent::KeyRelease, key, mods);
    pump();
}

// A fresh, clean project on the editor page (marking the old one saved avoids the save prompt).
void newProject() {
    g_window->document().markSaved();
    act("projectNew")->trigger();
    pump();
}

bool hasShortcut(const char* action, const QKeySequence& seq) { return act(action)->shortcuts().contains(seq); }

void focusTimeline() {
    g_window->activateWindow();
    timeline()->setFocus(Qt::OtherFocusReason);
    pump();
}

}  // namespace

TEST_CASE("Undo is Ctrl+Z; Redo is both Ctrl+Y and Ctrl+Shift+Z") {
    CHECK(hasShortcut("editUndo", QKeySequence(Qt::CTRL | Qt::Key_Z)));
    CHECK(hasShortcut("editRedo", QKeySequence(Qt::CTRL | Qt::Key_Y)));
    CHECK(hasShortcut("editRedo", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)));
}

TEST_CASE("default shortcuts: the spec keymap, and no two commands share a key") {
    struct M { const char* action; QKeySequence seq; };
    const M map[] = {
        {"toolSelect", QKeySequence(Qt::Key_V)}, {"toolBlade", QKeySequence(Qt::Key_B)}, {"toolText", QKeySequence(Qt::Key_T)},
        {"toolZoom", QKeySequence(Qt::Key_Z)}, {"playPause", QKeySequence(Qt::Key_Space)}, {"shuttleReverse", QKeySequence(Qt::Key_J)},
        {"pause", QKeySequence(Qt::Key_K)}, {"shuttleForward", QKeySequence(Qt::Key_L)}, {"markIn", QKeySequence(Qt::Key_I)},
        {"markOut", QKeySequence(Qt::Key_O)}, {"editCopy", QKeySequence(Qt::CTRL | Qt::Key_C)}, {"editCut", QKeySequence(Qt::CTRL | Qt::Key_X)},
        {"editPaste", QKeySequence(Qt::CTRL | Qt::Key_V)}, {"editDuplicate", QKeySequence(Qt::CTRL | Qt::Key_D)},
        {"clipDelete", QKeySequence(Qt::Key_Delete)}, {"clipDelete", QKeySequence(Qt::Key_Backspace)},
        {"clipSelectAll", QKeySequence(Qt::CTRL | Qt::Key_A)}, {"projectSave", QKeySequence(Qt::CTRL | Qt::Key_S)},
        {"projectSaveAs", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, {"projectOpen", QKeySequence(Qt::CTRL | Qt::Key_O)},
        {"projectNew", QKeySequence(Qt::CTRL | Qt::Key_N)}, {"timelineAddText", QKeySequence(Qt::CTRL | Qt::Key_T)},
        {"commandPalette", QKeySequence(Qt::CTRL | Qt::Key_K)}, {"viewerZoomFit", QKeySequence(Qt::Key_F)},
        {"viewerZoom100", QKeySequence(Qt::Key_1)}, {"viewerZoom50", QKeySequence(Qt::Key_2)}, {"viewerZoomFit", QKeySequence(Qt::Key_0)},
        {"stepBack", QKeySequence(Qt::Key_Left)}, {"stepForward", QKeySequence(Qt::Key_Right)}, {"prevEdit", QKeySequence(Qt::Key_Up)},
        {"nextEdit", QKeySequence(Qt::Key_Down)}, {"goToStart", QKeySequence(Qt::Key_Home)}, {"goToEnd", QKeySequence(Qt::Key_End)},
        {"timelineZoomIn", QKeySequence(Qt::CTRL | Qt::Key_Equal)}, {"timelineZoomOut", QKeySequence(Qt::CTRL | Qt::Key_Minus)},
        {"stepBackLarge", QKeySequence(Qt::SHIFT | Qt::Key_Left)}, {"stepForwardLarge", QKeySequence(Qt::SHIFT | Qt::Key_Right)},
    };
    for (const auto& m : map) {
        const bool ok = hasShortcut(m.action, m.seq);
        if (!ok) std::printf("    missing: %s -> %s\n", m.action, m.seq.toString().toStdString().c_str());
        CHECK(ok);
    }
    auto* registry = g_window->findChild<ActionRegistry*>();
    CHECK(registry != nullptr);
    const auto conflicts = registry->allConflicts();
    for (const auto& [a, b] : conflicts) std::printf("    conflict: %s <-> %s\n", registry->plainText(a).toStdString().c_str(), registry->plainText(b).toStdString().c_str());
    CHECK(conflicts.isEmpty());
}

TEST_CASE("keys: Ctrl+T adds text; Ctrl+Z undoes; Ctrl+Y and Ctrl+Shift+Z redo; labels follow") {
    newProject();
    focusTimeline();
    QAction* undo = act("editUndo");
    QAction* redo = act("editRedo");
    CHECK(!undo->isEnabled() && !redo->isEnabled());
    press(Qt::Key_T, Qt::ControlModifier);
    CHECK(!timeline()->sequenceDuration().isZero());
    CHECK(undo->isEnabled());
    CHECK(undo->text().contains(QStringLiteral("Add Text")));
    press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(timeline()->sequenceDuration().isZero());
    CHECK(redo->isEnabled() && redo->text().contains(QStringLiteral("Add Text")));
    press(Qt::Key_Y, Qt::ControlModifier);
    CHECK(!timeline()->sequenceDuration().isZero());
    CHECK(!redo->isEnabled());
    press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(timeline()->sequenceDuration().isZero());
    press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(!timeline()->sequenceDuration().isZero());
    // Undo -> new edit -> redo history is gone.
    press(Qt::Key_Z, Qt::ControlModifier);
    press(Qt::Key_T, Qt::ControlModifier);
    CHECK(!redo->isEnabled());
    press(Qt::Key_Y, Qt::ControlModifier);   // nothing to redo: harmless
    CHECK(!timeline()->sequenceDuration().isZero());
}

TEST_CASE("keys: V B R H Z T switch the timeline tool") {
    focusTimeline();
    struct T { int key; EditTool tool; const char* action; };
    const T tools[] = {{Qt::Key_B, EditTool::Blade, "toolBlade"}, {Qt::Key_R, EditTool::Trim, "toolTrim"}, {Qt::Key_H, EditTool::Hand, "toolHand"},
                       {Qt::Key_Z, EditTool::Zoom, "toolZoom"}, {Qt::Key_T, EditTool::Text, "toolText"}, {Qt::Key_V, EditTool::Select, "toolSelect"}};
    for (const auto& t : tools) {
        press(t.key);
        CHECK(timeline()->tool() == t.tool);
        CHECK(act(t.action)->isChecked());
    }
}

TEST_CASE("keys: Ctrl+A, Ctrl+C, Ctrl+V, Ctrl+D, Ctrl+X, Delete and Backspace edit the selection") {
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);           // one 5 s title, selected
    const Duration one = timeline()->sequenceDuration();
    CHECK(!one.isZero());
    press(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);   // deselect
    CHECK(!timeline()->hasSelection());
    press(Qt::Key_A, Qt::ControlModifier);
    CHECK(timeline()->hasSelection());
    press(Qt::Key_D, Qt::ControlModifier);           // duplicate after itself
    CHECK(timeline()->sequenceDuration() == one * 2);
    press(Qt::Key_C, Qt::ControlModifier);
    CHECK(act("editPaste")->isEnabled());
    press(Qt::Key_End);
    press(Qt::Key_V, Qt::ControlModifier);           // paste (insert) at the playhead
    press(Qt::Key_Delete);                           // deletes the pasted selection
    press(Qt::Key_A, Qt::ControlModifier);
    press(Qt::Key_X, Qt::ControlModifier);           // cut everything
    CHECK(timeline()->sequenceDuration().isZero());
    press(Qt::Key_Z, Qt::ControlModifier);           // undo the cut
    CHECK(!timeline()->sequenceDuration().isZero());
    press(Qt::Key_A, Qt::ControlModifier);
    press(Qt::Key_Backspace);
    CHECK(timeline()->sequenceDuration().isZero());
}

TEST_CASE("keys: I and O mark In/Out, Ctrl+Shift+X clears, all undoable") {
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);
    timeline()->setPlayhead(TimePoint::fromSeconds(1));
    press(Qt::Key_I);
    timeline()->setPlayhead(TimePoint::fromSeconds(3));
    press(Qt::Key_O);
    CHECK(act("clearInOut")->isEnabled());
    CHECK(act("clipLift")->isEnabled() && act("clipExtract")->isEnabled());
    press(Qt::Key_Z, Qt::ControlModifier);           // undo Mark Out
    CHECK(!act("clipLift")->isEnabled());
    press(Qt::Key_Y, Qt::ControlModifier);
    CHECK(act("clipLift")->isEnabled());
    press(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(!act("clearInOut")->isEnabled());
}

TEST_CASE("plain keys typed into a text field never trigger shortcuts") {
    focusTimeline();
    press(Qt::Key_V);
    CHECK(timeline()->tool() == EditTool::Select);
    QLineEdit* field = nullptr;
    for (auto* e : g_window->findChildren<QLineEdit*>()) if (e->isVisible()) { field = e; break; }
    CHECK(field != nullptr);
    if (!field) return;
    field->setFocus(Qt::OtherFocusReason);
    pump();
    press(Qt::Key_B, Qt::NoModifier);
    CHECK(timeline()->tool() == EditTool::Select);   // the B went into the field
    focusTimeline();
}

TEST_CASE("Ctrl+K opens the command palette; fuzzy matching ranks the obvious command first") {
    focusTimeline();
    press(Qt::Key_K, Qt::ControlModifier);
    auto* palette = g_window->findChild<CommandPalette*>();
    CHECK(palette != nullptr && palette->isVisible());
    if (palette) palette->reject();
    CHECK(CommandPalette::score(QStringLiteral("redo"), QStringLiteral("Redo")) > CommandPalette::score(QStringLiteral("redo"), QStringLiteral("Ripple Delete")));
    CHECK(CommandPalette::score(QStringLiteral("rpd"), QStringLiteral("Ripple Delete")) > 0);
    CHECK(CommandPalette::score(QStringLiteral("xyz"), QStringLiteral("Undo")) < 0);
}

TEST_CASE("shortcut overrides persist, conflicts are detected, reset restores defaults") {
    auto* registry = g_window->findChild<ActionRegistry*>();
    const QKeySequence f9(Qt::Key_F9);
    registry->setShortcuts(ActionId::ClipSplit, {f9});
    CHECK(act("clipSplit")->shortcuts() == QList<QKeySequence>{f9});
    CHECK(registry->isCustomised(ActionId::ClipSplit));
    CHECK(QSettings().contains(QStringLiteral("shortcuts/clipSplit")));
    CHECK(registry->conflictsWith(f9, ActionId::EditUndo).contains(ActionId::ClipSplit));
    registry->resetShortcuts(ActionId::ClipSplit);
    CHECK(act("clipSplit")->shortcuts().contains(QKeySequence(Qt::Key_S)));
    CHECK(!registry->isCustomised(ActionId::ClipSplit));
    // Tooltips name the command, its shortcut and what it does.
    CHECK(act("toolBlade")->toolTip().contains(QStringLiteral("Blade Tool")));
    CHECK(act("toolBlade")->toolTip().contains(QStringLiteral("B")));
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Isolated settings + data folder so the tests never touch the user's configuration.
    const QString root = QStringLiteral("G:/Caches/tmp/lf_ui_tests");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    QCoreApplication::setOrganizationName(QStringLiteral("LumaForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("LumaForgeTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root);
    QSettings().setValue(QStringLiteral("paths/dataRoot"), root + QStringLiteral("/data"));

    std::vector<char*> args(argv, argv + argc);
    char platform[] = "-platform";
    char offscreen[] = "offscreen";
    args.push_back(platform);
    args.push_back(offscreen);
    int qargc = static_cast<int>(args.size());
    QApplication app(qargc, args.data());

    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];

    int failedCases = 0;
    {
        MainWindow window(nullptr, nullptr);
        g_window = &window;
        window.show();
        window.activateWindow();
        pump();
        for (const auto& c : ::lftest::cases()) {
            if (filter && std::strstr(c.name, filter) == nullptr) continue;
            const int before = ::lftest::failures();
            c.body();
            const bool ok = ::lftest::failures() == before;
            std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
            if (!ok) ++failedCases;
        }
        g_window = nullptr;
    }
    std::printf("%zu UI test cases, %d failed\n", ::lftest::cases().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
