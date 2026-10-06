// UI-level tests: a real MainWindow (no GPU) driven by synthetic key events
// delivered through QWindowSystemInterface - the same path physical key
// presses take, including Qt's shortcut map. Verifies that shortcuts reach
// the intended actions and that undo/redo (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z)
// work end to end.
#include "../TestMain.h"

#include "lumaforge/project/Document.h"
#include "lumaforge/project/MediaCommands.h"
#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"
#include "ui/ActionRegistry.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CommandPalette.h"
#include "ui/export/ExportController.h"
#include "ui/export/ExportSettingsPanel.h"
#include "ui/inspector/KeyframeControls.h"
#include "ui/widgets/ParamSlider.h"
#include "ui/panels/MediaPanel.h"
#include "ui/media/ProxyController.h"
#include "ui/project/AutosaveManager.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/home/HomeScreen.h"
#include "ui/AppPaths.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/media/MediaProbe.h"
#include "ui/panels/ScopesPanel.h"
#include "ui/timeline/TimelineView.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QFileInfo>
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
    CHECK(!names.contains(QStringLiteral("Audio")) && !names.contains(QStringLiteral("Speed")));
    CHECK(names.contains(QStringLiteral("Video")));         // the Mask section lives there
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

TEST_CASE("transitions: Ctrl+Shift+D adds a dissolve at the cut; inspector changes type; undo") {
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);                 // title A at 0..5 s
    press(Qt::Key_End);
    press(Qt::Key_T, Qt::ControlModifier);                 // title B at 5..10 s, selected
    const auto& seq = g_window->document().project().sequence;
    CHECK(seq.clips.size() == 2);
    if (seq.clips.size() != 2) return;
    const project::Clip* b = nullptr;
    for (const auto& c : seq.clips) if (c.start == TimePoint::fromSeconds(5)) b = &c;
    CHECK(b != nullptr);
    if (!b) return;
    const project::ClipId bid = b->id;
    // Playhead is at B's start (the cut): the transition goes across the cut, on B.
    press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
    b = seq.findClip(bid);
    CHECK(b->transitionIn.has_value() && b->transitionIn->type == project::TransitionType::CrossDissolve);
    auto* startType = g_window->findChild<QComboBox*>(QStringLiteral("transitionStartType"));
    CHECK(startType != nullptr);
    if (!startType) return;
    CHECK(startType->currentText() == QStringLiteral("Cross Dissolve"));
    const int wipe = startType->findText(QStringLiteral("Wipe Left"));
    startType->setCurrentIndex(wipe);
    emit startType->activated(wipe);
    pump();
    CHECK(seq.findClip(bid)->transitionIn->type == project::TransitionType::WipeLeft);
    focusTimeline();
    press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(seq.findClip(bid)->transitionIn->type == project::TransitionType::CrossDissolve);
    press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(!seq.findClip(bid)->transitionIn.has_value());
    press(Qt::Key_Y, Qt::ControlModifier);
    CHECK(seq.findClip(bid)->transitionIn.has_value());
}

TEST_CASE("markers (M, Shift+M, Ctrl+Shift+M, panel) and solid / adjustment clips") {
    newProject();
    focusTimeline();
    const auto& seq = g_window->document().project().sequence;
    press(Qt::Key_T, Qt::ControlModifier);                 // something to scroll over (0..5 s)
    press(Qt::Key_M);                                      // marker at 0
    timeline()->setPlayhead(TimePoint::fromSeconds(3));
    press(Qt::Key_M);                                      // marker at 3 s
    CHECK(seq.markers.size() == 2);
    auto* list = g_window->findChild<QTreeWidget*>(QStringLiteral("markersList"));
    CHECK(list && list->topLevelItemCount() == 2);
    timeline()->setPlayhead(TimePoint::zero());
    press(Qt::Key_M, Qt::ShiftModifier);                   // next marker
    CHECK(timeline()->playhead() == TimePoint::fromSeconds(3));
    press(Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);   // previous marker
    CHECK(timeline()->playhead() == TimePoint::zero());
    press(Qt::Key_Z, Qt::ControlModifier);                 // undo the second marker
    CHECK(seq.markers.size() == 1);
    CHECK(list && list->topLevelItemCount() == 1);

    timeline()->setPlayhead(TimePoint::zero());
    act("timelineAddAdjustment")->trigger();
    pump();
    const project::Clip* adj = nullptr;
    for (const auto& c : seq.clips) if (c.kind == project::ClipKind::Adjustment) adj = &c;
    CHECK(adj != nullptr);
    if (adj) {
        // The title occupies the top track at 0..5 s, so the adjustment goes on the free one.
        const project::Clip* title = nullptr;
        for (const auto& c : seq.clips) if (c.kind == project::ClipKind::Text) title = &c;
        CHECK(title && title->track != adj->track);
    }
    auto* tabs = g_window->findChild<QTabBar*>(QStringLiteral("inspectorTabs"));
    QStringList names;
    for (int i = 0; tabs && i < tabs->count(); ++i) names << tabs->tabText(i);
    CHECK(!names.contains(QStringLiteral("Transform")));    // adjustment clips always cover the frame
    CHECK(names.contains(QStringLiteral("Color")) && names.contains(QStringLiteral("Effects")));
    CHECK(g_window->findChild<QWidget*>(QStringLiteral("adjustmentMix")) != nullptr);
    act("timelineAddSolid")->trigger();
    pump();
    int solids = 0;
    for (const auto& c : seq.clips) solids += c.kind == project::ClipKind::Solid;
    CHECK(solids == 1);
}

TEST_CASE("export: Ctrl+M opens the Export workspace; the panel queues a render that completes") {
    newProject();
    focusTimeline();
    auto* exportAct = act("export");
    CHECK(!exportAct->isEnabled());                        // nothing to export yet
    press(Qt::Key_T, Qt::ControlModifier);                 // a 5 s title
    CHECK(exportAct->isEnabled());
    press(Qt::Key_M, Qt::ControlModifier);
    auto* panel = g_window->findChild<ExportSettingsPanel*>();
    auto* controller = g_window->findChild<ExportController*>();
    CHECK(panel && controller && panel->isVisible());
    if (!panel || !controller) return;
    // Small and fast for the test: 360p, ultrafast, 1 s range via In/Out.
    auto* size = g_window->findChild<QComboBox*>(QStringLiteral("exportSize"));
    CHECK(size != nullptr);
    if (size) for (int i = 0; i < size->count(); ++i) if (size->itemData(i).toSize().height() == 360) size->setCurrentIndex(i);
    timeline()->setMarkIn(TimePoint::zero());
    timeline()->setMarkOut(TimePoint::fromSeconds(1));
    pump();
    auto* range = g_window->findChild<QComboBox*>(QStringLiteral("exportRange"));
    if (range) range->setCurrentIndex(1);
    auto* path = g_window->findChild<QLineEdit*>(QStringLiteral("exportPath"));
    CHECK(path != nullptr);
    if (!path) return;
    const QString out = QStringLiteral("G:/Caches/tmp/lf_ui_tests/export test.mp4");
    QFile::remove(out);
    path->setText(QDir::toNativeSeparators(out));
    const auto s = panel->settings();
    CHECK(s.height == 360 && s.rangeEnd == TimePoint::fromSeconds(1));
    const int id = panel->startExport();
    CHECK(id > 0);
    QElapsedTimer timer;
    timer.start();
    while (controller->isBusy() && timer.elapsed() < 120000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); }
    pump();
    const ExportJob* job = controller->job(id);
    CHECK(job && job->state == ExportJob::State::Done);
    if (job && job->state != ExportJob::State::Done) std::printf("    export message: %s\n", qPrintable(job->message));
    CHECK(QFileInfo(out).size() > 1000);
    CHECK(job && job->result.frames == 30 && job->result.height == 360);
}

TEST_CASE("export panel: frame rate, custom size, audio format, metadata, lossless mode, encoder status; choices persist") {
    newProject();
    auto& doc = g_window->document();
    project::MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = std::string(LF_TEST_MEDIA_DIR_UI) + "/countdown_720p30.mp4";
    m.displayName = "countdown";
    auto info = media::MediaProbe::probe(m.path);
    CHECK(static_cast<bool>(info));
    if (!info) return;
    m.info = *info;
    doc.push(std::make_unique<project::ImportMediaCommand>(doc, std::vector<project::MediaItem>{m}));
    auto clips = project::ops::clipsForMedia(doc, m, project::ops::defaultPlacement(doc.project(), m, TimePoint::zero()));
    doc.push(std::make_unique<project::SequenceEditCommand>(doc, "Add", project::ops::insertClips(doc, std::move(clips))));
    act("export")->trigger();
    pump();
    auto* panel = g_window->findChild<ExportSettingsPanel*>();
    auto* controller = g_window->findChild<ExportController*>();
    auto* fps = g_window->findChild<QComboBox*>(QStringLiteral("exportFps"));
    auto* size = g_window->findChild<QComboBox*>(QStringLiteral("exportSize"));
    auto* customW = g_window->findChild<QSpinBox*>(QStringLiteral("exportCustomWidth"));
    auto* customH = g_window->findChild<QSpinBox*>(QStringLiteral("exportCustomHeight"));
    auto* channels = g_window->findChild<QComboBox*>(QStringLiteral("exportChannels"));
    auto* sampleRate = g_window->findChild<QComboBox*>(QStringLiteral("exportSampleRate"));
    auto* lossless = g_window->findChild<QCheckBox*>(QStringLiteral("exportLossless"));
    auto* mode = g_window->findChild<QLabel*>(QStringLiteral("exportMode"));
    auto* encoderStatus = g_window->findChild<QLabel*>(QStringLiteral("exportEncoderStatus"));
    auto* advancedToggle = g_window->findChild<QToolButton*>(QStringLiteral("exportAdvancedToggle"));
    auto* advanced = g_window->findChild<QWidget*>(QStringLiteral("exportAdvanced"));
    auto* title = g_window->findChild<QLineEdit*>(QStringLiteral("exportTitle"));
    auto* start = g_window->findChild<QPushButton*>(QStringLiteral("exportStart"));
    CHECK(panel && controller && fps && size && customW && customH && channels && sampleRate && lossless && mode && encoderStatus && advancedToggle && advanced && title && start);
    if (!panel || !controller || !fps || !size || !customW || !customH || !channels || !sampleRate || !lossless || !mode || !encoderStatus || !advancedToggle || !advanced || !title || !start) return;

    // Frame rate: "Project" by default, then any standard rate.
    CHECK(fps->currentIndex() == 0 && panel->settings().frameRate.num == 0);
    fps->setCurrentIndex(fps->findData(QSize(25, 1)));
    CHECK(panel->settings().frameRate == (Rational{25, 1}));
    // Upscaled and custom sizes (the custom fields only show for Custom).
    CHECK(size->findData(QSize(3840, 2160)) >= 0);
    CHECK(!customW->isVisibleTo(panel));
    size->setCurrentIndex(size->findData(QSize(-1, -1)));
    pump();
    CHECK(customW->isVisibleTo(panel));
    customW->setValue(801);
    customH->setValue(600);
    CHECK(panel->settings().width == 800 && panel->settings().height == 600);   // rounded to even
    // Audio format and metadata.
    channels->setCurrentIndex(channels->findData(1));
    sampleRate->setCurrentIndex(sampleRate->findData(44100));
    CHECK(panel->settings().audioChannels == 1 && panel->settings().audioSampleRate == 44100);
    CHECK(!advanced->isVisibleTo(panel));                                            // collapsed by default
    advancedToggle->click();
    CHECK(advanced->isVisibleTo(panel));
    title->setText(QStringLiteral("My film"));
    CHECK(panel->settings().title == "My film");
    // The encoder status is measured, never blank.
    CHECK(!encoderStatus->text().isEmpty());

    // Lossless: possible for the untouched clip; re-encode controls are disabled meanwhile.
    CHECK(mode->text().contains(QStringLiteral("RE-ENCODE")));
    lossless->setChecked(true);
    CHECK(mode->text().contains(QStringLiteral("LOSSLESS")) && start->isEnabled() && !fps->isEnabled() && !size->isEnabled());
    CHECK(panel->settings().losslessFastCut);
    // ...and refused, with the reason, once the clip is colour corrected.
    {
        auto& seq = doc.project().sequence;
        project::SequenceEdit e;
        for (const auto& c : seq.clips)
            if (c.kind == project::ClipKind::Video) {
                project::Clip changed = c;
                changed.color.saturation = 0.5f;
                e.modified.push_back(changed);
            }
        doc.push(std::make_unique<project::SequenceEditCommand>(doc, "Colour", std::move(e)));
        pump();
    }
    CHECK(mode->text().contains(QStringLiteral("RE-ENCODE needed")) && mode->text().contains(QStringLiteral("colour")));
    CHECK(!start->isEnabled() && panel->startExport() == 0);
    lossless->setChecked(false);
    CHECK(start->isEnabled());

    // A new panel (next session) comes back with the same choices.
    ExportSettingsPanel again(doc, *controller);
    const auto s = again.settings();
    CHECK(s.frameRate == (Rational{25, 1}) && s.width == 800 && s.height == 600 && s.audioChannels == 1 && s.audioSampleRate == 44100);
    CHECK(!s.losslessFastCut);
    CHECK(QSettings().value(QStringLiteral("export/advancedOpen")).toBool());
    // Back to defaults for the tests that follow.
    fps->setCurrentIndex(0);
    size->setCurrentIndex(0);
    channels->setCurrentIndex(0);
    sampleRate->setCurrentIndex(0);
    title->clear();
    advancedToggle->click();
    doc.undoStack().undo();
}

TEST_CASE("scopes: waveform, parade and vectorscope place known colours where they belong") {
    auto solid = [](int r, int g, int b) {
        media::Image img;
        img.width = 64;
        img.height = 36;
        img.bgra.resize(64 * 36 * 4);
        for (std::size_t i = 0; i < img.bgra.size(); i += 4) { img.bgra[i] = static_cast<std::uint8_t>(b); img.bgra[i + 1] = static_cast<std::uint8_t>(g); img.bgra[i + 2] = static_cast<std::uint8_t>(r); img.bgra[i + 3] = 255; }
        return img;
    };
    auto brightestRow = [](const QImage& img) {
        int best = -1, bestSum = -1;
        for (int y = 0; y < img.height(); ++y) {
            int sum = 0;
            for (int x = 0; x < img.width(); ++x) sum += qGreen(img.pixel(x, y));
            if (sum > bestSum) { bestSum = sum; best = y; }
        }
        return best;
    };
    // Mid grey (128): the waveform trace sits on the 50 % line.
    const QImage wf = scopes::render(scopes::Mode::Waveform, solid(128, 128, 128), 200, 101);
    CHECK(std::abs(brightestRow(wf) - 50) <= 1);
    CHECK(scopes::luma(solid(255, 255, 255).bgra.data()) >= 254 && scopes::luma(solid(0, 0, 0).bgra.data()) == 0);
    // Pure red: in the vectorscope the hits are up and to the left of centre (Cr high, Cb low).
    const QImage vs = scopes::render(scopes::Mode::Vectorscope, solid(255, 0, 0), 200, 200);
    int hx = -1, hy = -1, best = -1;
    for (int y = 0; y < vs.height(); ++y)
        for (int x = 0; x < vs.width(); ++x) if (qRed(vs.pixel(x, y)) > best) { best = qRed(vs.pixel(x, y)); hx = x; hy = y; }
    CHECK(best > 100 && hx < 100 && hy < 60);
    // Parade: a blue frame lights only the right third near the top.
    const QImage parade = scopes::render(scopes::Mode::Parade, solid(0, 0, 255), 300, 101);
    CHECK(qBlue(parade.pixel(250, 0)) > 100 && qRed(parade.pixel(50, 0)) < 40);
    CHECK(!scopes::render(scopes::Mode::Histogram, solid(10, 200, 30), 256, 100).isNull());
}

TEST_CASE("proxies: the controller makes a real proxy; the preview program uses it only when proxies are on") {
    newProject();
    auto& doc = g_window->document();
    project::MediaItem m;
    m.id = doc.allocateMediaId();
    m.path = std::string(LF_TEST_MEDIA_DIR_UI) + "/countdown_720p30.mp4";
    m.displayName = "countdown";
    auto info = media::MediaProbe::probe(m.path);
    CHECK(static_cast<bool>(info));
    if (!info) return;
    m.info = *info;
    m.fingerprintSize = 12345;   // a fresh cache key for this test run
    m.fingerprintMtime = QDateTime::currentSecsSinceEpoch();
    const auto id = m.id;
    doc.push(std::make_unique<project::ImportMediaCommand>(doc, std::vector<project::MediaItem>{m}));
    auto* proxies = g_window->findChild<ProxyController*>();
    CHECK(proxies != nullptr);
    if (!proxies) return;
    proxies->setUseProxies(false);
    CHECK(proxies->state(id) == ProxyController::State::None);
    proxies->create({id});
    CHECK(proxies->state(id) == ProxyController::State::Making);
    QElapsedTimer timer;
    timer.start();
    while (proxies->state(id) != ProxyController::State::Ready && proxies->state(id) != ProxyController::State::Failed && timer.elapsed() < 60000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    CHECK(proxies->state(id) == ProxyController::State::Ready);
    CHECK(proxies->overrides().empty());                  // off: originals
    proxies->setUseProxies(true);
    const auto o = proxies->overrides();
    CHECK(o.size() == 1 && o.count(id) == 1);
    proxies->remove({id});
    CHECK(proxies->state(id) == ProxyController::State::None && proxies->overrides().empty());
    proxies->setUseProxies(false);
}

TEST_CASE("autosave writes unsaved work; a crashed session's autosave is offered and recovered; saves keep backups") {
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);                 // unsaved work: a title
    auto& doc = g_window->document();
    auto* autosave = g_window->findChild<AutosaveManager*>();
    CHECK(autosave != nullptr);
    if (!autosave) return;
    CHECK(autosave->autosaveNow());
    CHECK(!autosave->autosaveNow());                       // nothing new since
    const QDir dir(AppPaths::path(AppPaths::Kind::Autosave));
    const QString own = dir.filePath(QStringLiteral("session-%1").arg(QCoreApplication::applicationPid()));
    CHECK(QFile::exists(own + QStringLiteral(".lfproj")) && QFile::exists(own + QStringLiteral(".json")));
    CHECK(!AutosaveManager::findRecoverable().has_value());   // our own session is alive

    // A leftover from a session that died (pid 999999).
    const QString dead = dir.filePath(QStringLiteral("session-999999"));
    QFile::remove(dead + QStringLiteral(".lfproj"));
    QFile::copy(own + QStringLiteral(".lfproj"), dead + QStringLiteral(".lfproj"));
    QFile meta(own + QStringLiteral(".json"));
    meta.open(QIODevice::ReadOnly);
    QJsonObject obj = QJsonDocument::fromJson(meta.readAll()).object();
    meta.close();
    obj[QStringLiteral("pid")] = 999999;
    obj[QStringLiteral("original")] = QStringLiteral("G:/Caches/tmp/lf_ui_tests/recovered.lfproj");
    QFile deadMeta(dead + QStringLiteral(".json"));
    deadMeta.open(QIODevice::WriteOnly | QIODevice::Truncate);
    deadMeta.write(QJsonDocument(obj).toJson());
    deadMeta.close();
    const auto rec = AutosaveManager::findRecoverable();
    CHECK(rec.has_value() && rec->projectFile == dead + QStringLiteral(".lfproj"));

    // Recover through the Home screen's banner signal.
    newProject();
    CHECK(doc.project().sequence.clips.empty());
    auto* home = g_window->findChild<HomeScreen*>();
    CHECK(home != nullptr);
    if (!home || !rec) return;
    emit home->openRecoveryRequested(rec->projectFile);
    pump();
    CHECK(doc.project().sequence.clips.size() == 1);
    CHECK(doc.isDirty());
    CHECK(QString::fromStdString(doc.filePath()).endsWith(QStringLiteral("recovered.lfproj")));
    CHECK(!QFile::exists(dead + QStringLiteral(".lfproj")));

    // Backups: the second save keeps the first version.
    const QString saveAs = QStringLiteral("G:/Caches/tmp/lf_ui_tests/backup test.lfproj");
    QFile::remove(saveAs);
    CHECK(project::serializer::saveToFile(doc.project(), saveAs.toStdString()));
    AutosaveManager::backupBeforeSave(saveAs, QStringLiteral("Backup Test"), 10);
    const QDir backups(QDir(AppPaths::path(AppPaths::Kind::Backups)).filePath(QStringLiteral("Backup Test")));
    CHECK(backups.entryList({QStringLiteral("*.lfproj")}, QDir::Files).size() >= 1);
    doc.markSaved();
}

TEST_CASE("settings: Ctrl+, opens the dialog; autosave interval and transition length take effect") {
    CHECK(hasShortcut("appSettings", QKeySequence(Qt::CTRL | Qt::Key_Comma)));
    auto* autosave = g_window->findChild<AutosaveManager*>();
    auto* proxies = g_window->findChild<ProxyController*>();
    auto* registry = g_window->findChild<ActionRegistry*>();
    CHECK(autosave && proxies && registry);
    if (!autosave || !proxies || !registry) return;
    {
        SettingsDialog dlg(*registry, *autosave, *proxies, g_window);
        auto* minutes = dlg.findChild<QSpinBox*>(QStringLiteral("settingsAutosave"));
        const auto spins = dlg.findChildren<QDoubleSpinBox*>();
        CHECK(minutes && !spins.isEmpty());
        if (!minutes || spins.isEmpty()) return;
        minutes->setValue(5);
        for (auto* sp : spins) if (sp->suffix() == QStringLiteral(" s")) sp->setValue(2.0);
        auto* box = dlg.findChild<QDialogButtonBox*>();
        box->button(QDialogButtonBox::Apply)->click();
    }
    CHECK(autosave->intervalMinutes() == 5);
    CHECK(std::abs(SettingsDialog::defaultTransitionSeconds() - 2.0) < 1e-9);
    // A new transition uses the new default length.
    newProject();
    focusTimeline();
    press(Qt::Key_T, Qt::ControlModifier);
    press(Qt::Key_End);
    press(Qt::Key_T, Qt::ControlModifier);
    press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
    const auto& seq = g_window->document().project().sequence;
    bool found = false;
    for (const auto& c : seq.clips) if (c.transitionIn) found = c.transitionIn->duration == Duration::fromSeconds(2);
    CHECK(found);
    autosave->setIntervalMinutes(2);
    QSettings().setValue(QStringLiteral("editing/transitionSeconds"), 1.0);
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
