// UI-level tests: a real MainWindow (no GPU) driven by synthetic key events
// delivered through QWindowSystemInterface - the same path physical key
// presses take, including Qt's shortcut map. Verifies that shortcuts reach
// the intended actions and that undo/redo (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z)
// work end to end.
#include "../TestMain.h"

#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "ui/ActionRegistry.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CommandPalette.h"
#include "ui/inspector/KeyframeControls.h"
#include "ui/widgets/ParamSlider.h"
#include "ui/panels/MediaPanel.h"
#include "ui/timeline/TimelineView.h"

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QTabBar>
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

TEST_CASE("inspector: only applicable tabs; multi-selection banner and shared properties") {
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);           // title 1 (5 s)
    press(Qt::Key_End);                              // playhead to the end of the timeline
    CHECK(timeline()->playhead() == TimePoint{timeline()->sequenceDuration().ticks});
    press(Qt::Key_T, Qt::ControlModifier);           // title 2 after it
    press(Qt::Key_Left);
    CHECK(timeline()->playhead() < TimePoint{timeline()->sequenceDuration().ticks});
    auto* tabs = g_window->findChild<QTabBar*>(QStringLiteral("inspectorTabs"));
    auto* banner = g_window->findChild<QLabel*>(QStringLiteral("inspectorMultiBanner"));
    CHECK(tabs && banner);
    if (!tabs || !banner) return;
    auto offered = [tabs](const QString& name) {
        for (int i = 0; i < tabs->count(); ++i) if (tabs->tabText(i) == name) return tabs->isTabVisible(i);
        return false;
    };
    CHECK(!banner->isVisible());                     // one title selected
    CHECK(offered(QStringLiteral("Text")) && offered(QStringLiteral("Transform")));
    CHECK(!offered(QStringLiteral("Color")) && !offered(QStringLiteral("Audio")) && !offered(QStringLiteral("Speed")));
    press(Qt::Key_A, Qt::ControlModifier);           // both titles
    CHECK(banner->isVisible());
    CHECK(banner->text().contains(QStringLiteral("2 items")));
    CHECK(offered(QStringLiteral("Text")));
}

TEST_CASE("titles and effects browsers: add a preset title, apply an effect, undo each") {
    newProject();
    focusTimeline();
    const auto& seq = g_window->document().project().sequence;
    auto* titles = g_window->findChild<QListWidget*>(QStringLiteral("titlesList"));
    auto* addTitle = g_window->findChild<QPushButton*>(QStringLiteral("titlesAdd"));
    auto* tree = g_window->findChild<QTreeWidget*>(QStringLiteral("effectsTree"));
    auto* applyFx = g_window->findChild<QPushButton*>(QStringLiteral("effectsApply"));
    CHECK(titles && addTitle && tree && applyFx);
    if (!titles || !addTitle || !tree || !applyFx) return;
    CHECK(titles->count() >= 7);
    CHECK(!addTitle->isEnabled());                         // nothing chosen yet
    titles->setCurrentRow(2);                              // Lower Third
    CHECK(addTitle->isEnabled());
    addTitle->click();
    pump();
    CHECK(seq.clips.size() == 1);
    if (seq.clips.size() != 1) return;
    CHECK(seq.clips[0].name == "Lower Third" && seq.clips[0].text.align == TextAlign::Left);
    CHECK(timeline()->selection().size() == 1);            // the new title is selected

    QTreeWidgetItem* pixelate = nullptr;
    for (auto* item : tree->findItems(QStringLiteral("Pixelate"), Qt::MatchExactly | Qt::MatchRecursive)) pixelate = item;
    CHECK(pixelate != nullptr);
    if (!pixelate) return;
    tree->setCurrentItem(pixelate);
    CHECK(applyFx->isEnabled());
    applyFx->click();
    pump();
    CHECK(seq.clips[0].effects.size() == 1 && seq.clips[0].effects[0].type == EffectType::Pixelate);
    focusTimeline();
    press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(seq.clips[0].effects.empty());
    press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(seq.clips.empty());
    // With nothing selected the effect cannot be applied.
    CHECK(!applyFx->isEnabled());
}

TEST_CASE("media panel: type and usage filters, bins, rename") {
    newProject();
    auto& doc = g_window->document();
    auto make = [&doc](const char* name, media::MediaType type) {
        project::MediaItem m;
        m.id = doc.allocateMediaId();
        m.path = std::string("G:/Caches/tmp/lf_ui_tests/") + name;
        m.displayName = name;
        m.info.type = type;
        m.info.duration = Duration::fromSeconds(3);
        if (type != media::MediaType::Audio) { media::VideoStreamInfo v; v.width = 640; v.height = 360; v.frameRate = fps::f30; m.info.video = v; }
        if (type != media::MediaType::Image) { media::AudioStreamInfo a; a.channels = 2; a.sampleRate = 48000; m.info.audio = a; }
        return m;
    };
    std::vector<project::MediaItem> items{make("clip.mp4", media::MediaType::Video), make("song.wav", media::MediaType::Audio),
                                          make("logo.png", media::MediaType::Image)};
    const project::MediaId video = items[0].id, audio = items[1].id;
    doc.push(std::make_unique<project::ImportMediaCommand>(doc, std::move(items)));
    pump();
    auto* panel = g_window->findChild<MediaPanel*>();
    auto* grid = panel ? panel->findChild<QListView*>(QStringLiteral("mediaGrid")) : nullptr;
    auto* typeFilter = g_window->findChild<QComboBox*>(QStringLiteral("mediaTypeFilter"));
    auto* binFilter = g_window->findChild<QComboBox*>(QStringLiteral("mediaBinFilter"));
    CHECK(grid && typeFilter && binFilter);
    if (!grid || !typeFilter || !binFilter) return;
    auto shown = [grid] { return grid->model()->rowCount(); };
    typeFilter->setCurrentIndex(0);
    CHECK(shown() == 3);
    typeFilter->setCurrentIndex(2);                        // Audio
    CHECK(shown() == 1);
    typeFilter->setCurrentIndex(4);                        // Unused: nothing is on the timeline yet
    CHECK(shown() == 3);
    timeline()->addMedia({video}, TimePoint::zero());
    pump();
    CHECK(shown() == 2);                                   // the video is used now
    typeFilter->setCurrentIndex(0);

    // Bins: moving into a new bin creates it; the bin filter shows just its items.
    doc.push(project::OrganizeMediaCommand::moveToBin(doc, {audio}, "Music"));
    pump();
    CHECK(binFilter->findText(QStringLiteral("Music")) >= 0);
    panel->showBin(QStringLiteral("Music"));
    CHECK(shown() == 1);
    panel->showBin(QString());                             // unsorted
    CHECK(shown() == 2);
    panel->clearBinFilter();
    CHECK(shown() == 3);
    press(Qt::Key_Z, Qt::ControlModifier);                 // undo the move: the bin goes away
    pump();
    CHECK(binFilter->findText(QStringLiteral("Music")) < 0);
    CHECK(panel->removeUnused() == 2);                     // song + logo
    CHECK(doc.project().media.size() == 1);
}

TEST_CASE("inspector tabs re-laid out when the selection changes kind (video -> title)") {
    newProject();
    auto& doc = g_window->document();
    project::MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = "G:/Caches/tmp/lf_ui_tests/v.mp4";
    m.displayName = "v.mp4";
    m.info.type = media::MediaType::Video;
    m.info.duration = Duration::fromSeconds(6);
    { media::VideoStreamInfo v; v.width = 640; v.height = 360; v.frameRate = fps::f30; m.info.video = v; }
    { media::AudioStreamInfo a; a.channels = 2; a.sampleRate = 48000; m.info.audio = a; }
    const auto id = m.id;
    doc.push(std::make_unique<project::ImportMediaCommand>(doc, std::vector<project::MediaItem>{m}));
    g_window->resize(1442, 892);
    g_window->show();
    pump();
    timeline()->addMedia({id}, TimePoint::zero());
    pump();
    auto* tabs = g_window->findChild<QTabBar*>(QStringLiteral("inspectorTabs"));
    for (int i = 0; i < tabs->count(); ++i) if (tabs->tabText(i) == QStringLiteral("Effects")) tabs->setCurrentIndex(i);
    pump();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);
    pump();
    CHECK(tabs->isVisible());
    QStringList names;
    for (int i = 0; i < tabs->count(); ++i) {
        names << tabs->tabText(i);
        CHECK(tabs->tabRect(i).width() > 0);           // every offered tab is laid out
    }
    CHECK(names.contains(QStringLiteral("Text")) && names.contains(QStringLiteral("Effects")) && names.contains(QStringLiteral("Transform")));
    CHECK(!names.contains(QStringLiteral("Audio")) && !names.contains(QStringLiteral("Video")));
}

TEST_CASE("keyframes from the inspector: diamond adds a key, edits at another time add more, undo removes") {
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);                 // a 5 s title, selected, playhead at 0
    const auto& seq = g_window->document().project().sequence;
    CHECK(seq.clips.size() == 1);
    if (seq.clips.empty()) return;
    auto* opacity = g_window->findChild<ParamSlider*>(QStringLiteral("param.opacity"));
    CHECK(opacity != nullptr);
    if (!opacity) return;
    auto* diamond = opacity->findChild<KeyframeButton*>();
    auto* spin = opacity->findChild<QDoubleSpinBox*>();
    CHECK(diamond && spin);
    if (!diamond || !spin) return;
    CHECK(diamond->isEnabled() && diamond->state() == KeyframeButton::State::Static);
    diamond->click();
    pump();
    CHECK(project::isAnimated(seq.clips[0], project::AnimProp::Opacity));
    CHECK(diamond->state() == KeyframeButton::State::OnKey);

    timeline()->setPlayhead(TimePoint::fromSeconds(2));    // inspector follows the playhead
    pump();
    CHECK(diamond->state() == KeyframeButton::State::Animated);
    spin->setValue(40);                                    // a typed edit at 2 s makes a second key
    pump();
    const auto* track = project::findKeyTrack(seq.clips[0], project::AnimProp::Opacity);
    CHECK(track && track->keys.size() == 2);
    CHECK(diamond->state() == KeyframeButton::State::OnKey);
    CHECK(std::abs(project::valueAt(seq.clips[0], project::AnimProp::Opacity, TimePoint::fromSeconds(1)) - 0.7f) < 1e-3f);

    timeline()->setPlayhead(TimePoint::fromSeconds(1));
    pump();
    CHECK(std::abs(spin->value() - 70.0) < 0.6);          // the field shows the animated value

    focusTimeline();
    press(Qt::Key_Z, Qt::ControlModifier);                 // undo the second key
    track = project::findKeyTrack(seq.clips[0], project::AnimProp::Opacity);
    CHECK(track && track->keys.size() == 1);
    press(Qt::Key_Z, Qt::ControlModifier);                 // undo the first: static again
    CHECK(!project::isAnimated(seq.clips[0], project::AnimProp::Opacity));
    timeline()->setPlayhead(TimePoint::fromSeconds(30));   // outside the clip: no keyframing
    pump();
    CHECK(!diamond->isEnabled());
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
