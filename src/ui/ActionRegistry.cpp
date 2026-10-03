#include "ui/ActionRegistry.h"

#include "ui/Icons.h"

#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextEdit>

namespace lf::ui {

namespace {
// Shortcuts are stored as plain data and turned into QKeySequence objects at
// runtime: QKeySequence(StandardKey) needs the platform theme, which does not
// exist during static initialisation.
struct Shortcut {
    QKeySequence::StandardKey standard = QKeySequence::UnknownKey;
    int keys = 0;   // Qt::Key | modifiers, when no standard key applies

    constexpr Shortcut() = default;
    constexpr Shortcut(QKeySequence::StandardKey k) : standard(k) {}   // NOLINT(google-explicit-constructor)
    constexpr Shortcut(int k) : keys(k) {}                              // NOLINT(google-explicit-constructor)
    constexpr Shortcut(QKeyCombination k) : keys(k.toCombined()) {}     // NOLINT(google-explicit-constructor)
    constexpr Shortcut(Qt::Key k) : keys(static_cast<int>(k)) {}        // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool isEmpty() const { return standard == QKeySequence::UnknownKey && keys == 0; }
    [[nodiscard]] QKeySequence toSequence() const {
        if (standard != QKeySequence::UnknownKey) return QKeySequence(standard);
        return keys ? QKeySequence(keys) : QKeySequence();
    }
};

using C = ActionCategory;
constexpr int CTRL = Qt::CTRL;
constexpr int SHIFT = Qt::SHIFT;
constexpr int ALT = Qt::ALT;

struct ActionDef {
    ActionId id;
    const char* objectName;
    const char* text;
    const char* icon;          // resource icon name or nullptr
    ActionCategory category;
    Shortcut primary;
    Shortcut secondary;
    const char* statusTip;
    bool checkable = false;
    bool enabledInitially = true;
};

// The single source of truth for labels and default shortcuts.
const ActionDef kActions[] = {
    // ---- File
    {ActionId::ProjectNew,      "projectNew",      "&New Project",         "new",     C::File, Shortcut{CTRL | Qt::Key_N}, {}, "Create a new project"},
    {ActionId::ProjectOpen,     "projectOpen",     "&Open Project…",       "open",    C::File, Shortcut{CTRL | Qt::Key_O}, {}, "Open an existing project"},
    {ActionId::ProjectSave,     "projectSave",     "&Save",                "save",    C::File, Shortcut{CTRL | Qt::Key_S}, {}, "Save the project"},
    {ActionId::ProjectSaveAs,   "projectSaveAs",   "Save &As…",            "save-as", C::File, Shortcut{CTRL | SHIFT | Qt::Key_S}, {}, "Save the project to a new file"},
    {ActionId::ProjectSettings, "projectSettings", "Project S&ettings…",   nullptr,   C::File, {}, {}, "Change name, frame size and frame rate"},
    {ActionId::ProjectClose,    "projectClose",    "&Close Project",       nullptr,   C::File, Shortcut{CTRL | Qt::Key_W}, {}, "Close the project and return to the Home screen"},
    {ActionId::AppQuit,         "appQuit",         "E&xit",                nullptr,   C::File, Shortcut{CTRL | Qt::Key_Q}, {}, "Quit LumaForge"},
    {ActionId::MediaImport,     "mediaImport",     "&Import Media…",       "import",  C::File, Shortcut{CTRL | Qt::Key_I}, {}, "Import video, audio or image files"},
    {ActionId::MediaImportFolder, "mediaImportFolder", "Import &Folder…",  nullptr,   C::File, Shortcut{CTRL | SHIFT | Qt::Key_I}, {}, "Import every media file in a folder and its subfolders"},
    {ActionId::Export,          "export",          "&Export…",             "export",  C::File, Shortcut{CTRL | Qt::Key_M}, {}, "Render the timeline to a video file", false, false},

    // ---- Edit
    {ActionId::EditUndo,        "editUndo",        "&Undo",                "undo",    C::Edit, Shortcut{CTRL | Qt::Key_Z}, {}, "Undo the last edit", false, false},
    {ActionId::EditRedo,        "editRedo",        "&Redo",                "redo",    C::Edit, Shortcut{CTRL | Qt::Key_Y}, Shortcut{CTRL | SHIFT | Qt::Key_Z}, "Redo the last undone edit", false, false},
    {ActionId::EditCut,         "editCut",         "Cu&t",                 "cut",     C::Edit, Shortcut{CTRL | Qt::Key_X}, {}, "Copy the selected clips and remove them", false, false},
    {ActionId::EditCopy,        "editCopy",        "&Copy",                "copy",    C::Edit, Shortcut{CTRL | Qt::Key_C}, {}, "Copy the selected clips", false, false},
    {ActionId::EditPaste,       "editPaste",       "&Paste",               "paste",   C::Edit, Shortcut{CTRL | Qt::Key_V}, {}, "Insert the copied clips at the playhead, moving later clips right", false, false},
    {ActionId::EditPasteOverwrite, "editPasteOverwrite", "Paste &Overwrite", nullptr, C::Edit, Shortcut{CTRL | SHIFT | Qt::Key_V}, {}, "Paste the copied clips at the playhead over whatever is there", false, false},
    {ActionId::EditDuplicate,   "editDuplicate",   "D&uplicate",           nullptr,   C::Edit, Shortcut{CTRL | Qt::Key_D}, {}, "Insert copies of the selected clips right after them", false, false},
    {ActionId::ClipDelete,      "clipDelete",      "&Delete",              "delete",  C::Edit, Shortcut{Qt::Key_Delete}, Shortcut{Qt::Key_Backspace}, "Delete the selected clips, leaving a gap", false, false},
    {ActionId::ClipRippleDelete,"clipRippleDelete","&Ripple Delete",       nullptr,   C::Edit, Shortcut{SHIFT | Qt::Key_Delete}, Shortcut{SHIFT | Qt::Key_Backspace}, "Delete the selected clips and close the gap", false, false},
    {ActionId::ClipSelectAll,   "clipSelectAll",   "Select &All",          nullptr,   C::Edit, Shortcut{CTRL | Qt::Key_A}, {}, "Select every clip in the timeline", false, false},
    {ActionId::ClipDeselect,    "clipDeselect",    "Dese&lect All",        nullptr,   C::Edit, Shortcut{CTRL | SHIFT | Qt::Key_A}, {}, "Clear the clip selection", false, false},

    // ---- Tools
    {ActionId::ToolSelect,      "toolSelect",      "&Select Tool",         "tool-select", C::Tools, Shortcut{Qt::Key_V}, {}, "Select, move and trim clips", true},
    {ActionId::ToolBlade,       "toolBlade",       "&Blade Tool",          "tool-blade",  C::Tools, Shortcut{Qt::Key_B}, {}, "Click a clip to split it (at the playhead when it is inside the clip)", true},
    {ActionId::ToolTrim,        "toolTrim",        "&Trim Tool",           "tool-trim",   C::Tools, Shortcut{Qt::Key_R}, {}, "Drag an edit point: ripple trim one side, or roll a cut between two clips", true},
    {ActionId::ToolHand,        "toolHand",        "&Hand Tool",           "tool-hand",   C::Tools, Shortcut{Qt::Key_H}, {}, "Drag to scroll the timeline", true},
    {ActionId::ToolZoom,        "toolZoom",        "&Zoom Tool",           "tool-zoom",   C::Tools, Shortcut{Qt::Key_Z}, {}, "Click to zoom in, Alt+click to zoom out, drag to zoom to a range", true},
    {ActionId::ToolText,        "toolText",        "Te&xt Tool",           "text",        C::Tools, Shortcut{Qt::Key_T}, {}, "Click a video track to place a text clip there", true},

    // ---- Playback
    {ActionId::PlayPause,       "playPause",       "&Play/Pause",          "play",        C::Playback, Shortcut{Qt::Key_Space}, {}, "Play or pause at normal speed"},
    {ActionId::Play,            "play",            "Pla&y",                "play",        C::Playback, {}, {}, "Play at normal speed"},
    {ActionId::Pause,           "pause",           "P&ause",               "pause",       C::Playback, Shortcut{Qt::Key_K}, {}, "Pause playback"},
    {ActionId::PlayStop,        "playStop",        "S&top",                "stop",        C::Playback, {}, {}, "Stop and return to where playback started"},
    {ActionId::ShuttleReverse,  "shuttleReverse",  "Play &Reverse",        nullptr,       C::Playback, Shortcut{Qt::Key_J}, {}, "Play backwards; press again to go faster (1x, 2x, 4x, 8x)"},
    {ActionId::ShuttleForward,  "shuttleForward",  "Play &Forward",        nullptr,       C::Playback, Shortcut{Qt::Key_L}, {}, "Play forwards; press again to go faster (1x, 2x, 4x, 8x)"},
    {ActionId::StepBack,        "stepBack",        "Previous Frame",       "step-back",   C::Playback, Shortcut{Qt::Key_Left}, {}, "Move one frame back"},
    {ActionId::StepForward,     "stepForward",     "Next Frame",           "step-forward",C::Playback, Shortcut{Qt::Key_Right}, {}, "Move one frame forward"},
    {ActionId::StepBackLarge,   "stepBackLarge",   "Back 10 Frames",       nullptr,       C::Playback, Shortcut{SHIFT | Qt::Key_Left}, {}, "Move ten frames back"},
    {ActionId::StepForwardLarge,"stepForwardLarge","Forward 10 Frames",    nullptr,       C::Playback, Shortcut{SHIFT | Qt::Key_Right}, {}, "Move ten frames forward"},
    {ActionId::PrevEdit,        "prevEdit",        "Previous &Edit",       "edit-prev",   C::Playback, Shortcut{Qt::Key_Up}, {}, "Move the playhead to the previous cut or clip edge"},
    {ActionId::NextEdit,        "nextEdit",        "&Next Edit",           "edit-next",   C::Playback, Shortcut{Qt::Key_Down}, {}, "Move the playhead to the next cut or clip edge"},
    {ActionId::GoToStart,       "goToStart",       "Go to &Start",         "go-start",    C::Playback, Shortcut{Qt::Key_Home}, {}, "Move to the start of the timeline"},
    {ActionId::GoToEnd,         "goToEnd",         "Go to E&nd",           "go-end",      C::Playback, Shortcut{Qt::Key_End}, {}, "Move to the end of the timeline"},
    {ActionId::PlayLoop,        "playLoop",        "&Loop Playback",       "loop",        C::Playback, Shortcut{CTRL | Qt::Key_L}, {}, "Repeat playback (between In and Out when both are set)", true},
    {ActionId::MarkIn,          "markIn",          "Mark &In",             "mark-in",     C::Playback, Shortcut{Qt::Key_I}, {}, "Set the In point at the playhead"},
    {ActionId::MarkOut,         "markOut",         "Mark &Out",            "mark-out",    C::Playback, Shortcut{Qt::Key_O}, {}, "Set the Out point at the playhead"},
    {ActionId::ClearInOut,      "clearInOut",      "&Clear In/Out",        nullptr,       C::Playback, Shortcut{CTRL | SHIFT | Qt::Key_X}, {}, "Remove the In and Out points"},
    {ActionId::GoToIn,          "goToIn",          "Go to In",             nullptr,       C::Playback, Shortcut{SHIFT | Qt::Key_I}, {}, "Move the playhead to the In point"},
    {ActionId::GoToOut,         "goToOut",         "Go to Out",            nullptr,       C::Playback, Shortcut{SHIFT | Qt::Key_O}, {}, "Move the playhead to the Out point"},

    // ---- Clip
    {ActionId::ClipSplit,       "clipSplit",       "&Split at Playhead",   "split",       C::Clip, Shortcut{Qt::Key_S}, {}, "Split the selected clips (or all clips) under the playhead", false, false},
    {ActionId::ClipTrimStartToPlayhead, "clipTrimStart", "Trim Start to Playhead", nullptr, C::Clip, Shortcut{Qt::Key_Q}, {}, "Move the start of the selected clips to the playhead", false, false},
    {ActionId::ClipTrimEndToPlayhead,   "clipTrimEnd",   "Trim End to Playhead",   nullptr, C::Clip, Shortcut{Qt::Key_W}, {}, "Move the end of the selected clips to the playhead", false, false},
    {ActionId::ClipNudgeLeft,   "clipNudgeLeft",   "Nudge Left",           nullptr,       C::Clip, Shortcut{ALT | Qt::Key_Left}, {}, "Move the selected clips one frame earlier", false, false},
    {ActionId::ClipNudgeRight,  "clipNudgeRight",  "Nudge Right",          nullptr,       C::Clip, Shortcut{ALT | Qt::Key_Right}, {}, "Move the selected clips one frame later", false, false},
    {ActionId::ClipNudgeLeftLarge,  "clipNudgeLeftLarge",  "Nudge Left 10 Frames",  nullptr, C::Clip, Shortcut{ALT | SHIFT | Qt::Key_Left}, {}, "Move the selected clips ten frames earlier", false, false},
    {ActionId::ClipNudgeRightLarge, "clipNudgeRightLarge", "Nudge Right 10 Frames", nullptr, C::Clip, Shortcut{ALT | SHIFT | Qt::Key_Right}, {}, "Move the selected clips ten frames later", false, false},
    {ActionId::ClipMoveToPlayhead, "clipMoveToPlayhead", "&Move to Playhead", nullptr,     C::Clip, Shortcut{CTRL | SHIFT | Qt::Key_P}, {}, "Move the selected clips so they start at the playhead", false, false},
    {ActionId::ClipEnable,      "clipEnable",      "&Enable",              nullptr,       C::Clip, Shortcut{SHIFT | Qt::Key_E}, {}, "Enable or disable the selected clips (disabled clips are not shown or heard)", true, false},
    {ActionId::ClipLift,        "clipLift",        "&Lift",                nullptr,       C::Clip, Shortcut{Qt::Key_Semicolon}, {}, "Remove everything between In and Out on unlocked tracks, leaving a gap", false, false},
    {ActionId::ClipExtract,     "clipExtract",     "E&xtract",             nullptr,       C::Clip, Shortcut{Qt::Key_Apostrophe}, {}, "Remove everything between In and Out and close the gap", false, false},

    // ---- Timeline
    {ActionId::TimelineZoomIn,  "timelineZoomIn",  "Zoom &In",             "zoom-in",     C::Timeline, Shortcut{CTRL | Qt::Key_Equal}, Shortcut{CTRL | Qt::Key_Plus}, "Zoom in on the timeline"},
    {ActionId::TimelineZoomOut, "timelineZoomOut", "Zoom &Out",            "zoom-out",    C::Timeline, Shortcut{CTRL | Qt::Key_Minus}, {}, "Zoom out on the timeline"},
    {ActionId::TimelineZoomFit, "timelineZoomFit", "Zoom to &Fit",         "zoom-fit",    C::Timeline, Shortcut{Qt::Key_Backslash}, {}, "Fit the whole timeline in view"},
    {ActionId::TimelineSnap,    "timelineSnap",    "S&napping",            "magnet",      C::Timeline, Shortcut{Qt::Key_N}, {}, "Snap clips and the playhead to edges", true},
    {ActionId::TimelineExpand,  "timelineExpand",  "&Expand Timeline",    nullptr,       C::Timeline, Shortcut{Qt::Key_QuoteLeft}, {}, "Give the timeline most of the window; press again to restore", true},
    {ActionId::TimelineAddVideoTrack, "timelineAddVideoTrack", "Add &Video Track", nullptr, C::Timeline, {}, {}, "Add a video track above the top video track"},
    {ActionId::TimelineAddAudioTrack, "timelineAddAudioTrack", "Add &Audio Track", nullptr, C::Timeline, {}, {}, "Add an audio track below the last audio track"},
    {ActionId::TrackAddAbove,   "trackAddAbove",   "Add Track &Above",     nullptr,       C::Timeline, {}, {}, "Insert a track of the same kind above the targeted track", false, false},
    {ActionId::TrackAddBelow,   "trackAddBelow",   "Add Track &Below",     nullptr,       C::Timeline, {}, {}, "Insert a track of the same kind below the targeted track", false, false},
    {ActionId::TrackDuplicate,  "trackDuplicate",  "&Duplicate Track",     nullptr,       C::Timeline, {}, {}, "Copy the targeted track and its clips", false, false},
    {ActionId::TrackDelete,     "trackDelete",     "De&lete Track",        nullptr,       C::Timeline, {}, {}, "Remove the targeted track and its clips (one track of each kind always stays)", false, false},
    {ActionId::TrackRename,     "trackRename",     "&Rename Track…",       nullptr,       C::Timeline, {}, {}, "Rename the targeted track", false, false},
    {ActionId::TrackMoveUp,     "trackMoveUp",     "Move Track &Up",       nullptr,       C::Timeline, {}, {}, "Move the targeted track up among tracks of its kind", false, false},
    {ActionId::TrackMoveDown,   "trackMoveDown",   "Move Track Do&wn",     nullptr,       C::Timeline, {}, {}, "Move the targeted track down among tracks of its kind", false, false},
    {ActionId::TrackLock,       "trackLock",       "Loc&k Track",          nullptr,       C::Timeline, {}, {}, "Prevent edits on the targeted track", true, false},
    {ActionId::TrackMute,       "trackMute",       "&Mute / Hide Track",   nullptr,       C::Timeline, {}, {}, "Silence an audio track or hide a video track", true, false},
    {ActionId::TrackSolo,       "trackSolo",       "&Solo Track",          nullptr,       C::Timeline, {}, {}, "Play only soloed tracks of this kind", true, false},
    {ActionId::ClipSpeed,       "clipSpeed",       "S&peed…",              nullptr,       C::Clip, Shortcut{CTRL | Qt::Key_R}, {}, "Show the speed controls of the selected clip", false, false},
    {ActionId::ClipProperties,  "clipProperties",  "P&roperties",          nullptr,       C::Clip, {}, {}, "Show the selected clip in the Inspector", false, false},
    {ActionId::ClipRevealInMedia, "clipRevealInMedia", "Reveal in &Media", nullptr,       C::Clip, {}, {}, "Select the clip's source file in the Media panel", false, false},
    {ActionId::TimelineAddText, "timelineAddText", "Add &Text",            "text",        C::Text, Shortcut{CTRL | Qt::Key_T}, {}, "Add a text clip at the playhead on the top video track"},

    // ---- Viewer
    {ActionId::ViewerZoomFit,   "viewerZoomFit",   "&Fit",                 "zoom-fit",    C::Viewer, Shortcut{Qt::Key_F}, Shortcut{Qt::Key_0}, "Fit the picture in the viewer", true},
    {ActionId::ViewerZoom25,    "viewerZoom25",    "25%",                  nullptr,       C::Viewer, {}, {}, "Show the picture at 25%", true},
    {ActionId::ViewerZoom50,    "viewerZoom50",    "50%",                  nullptr,       C::Viewer, Shortcut{Qt::Key_2}, {}, "Show the picture at 50%", true},
    {ActionId::ViewerZoom100,   "viewerZoom100",   "100%",                 nullptr,       C::Viewer, Shortcut{Qt::Key_1}, {}, "Show the picture at 100% (one frame pixel per screen pixel)", true},
    {ActionId::ViewerZoom200,   "viewerZoom200",   "200%",                 nullptr,       C::Viewer, {}, {}, "Show the picture at 200%", true},
    {ActionId::ViewerFullscreen,"viewerFullscreen","Full&screen Viewer",   "fullscreen",  C::Viewer, Shortcut{Qt::Key_F11}, {}, "Show only the viewer, full screen (Esc to leave)", true},
    {ActionId::QualityAuto,     "qualityAuto",     "Auto",                 nullptr,       C::Viewer, {}, {}, "Render the preview at the viewer's size, capped at 720p while playing", true},
    {ActionId::QualityFull,     "qualityFull",     "Full",                 nullptr,       C::Viewer, {}, {}, "Render the preview at the project resolution", true},
    {ActionId::QualityHalf,     "qualityHalf",     "1/2",                  nullptr,       C::Viewer, {}, {}, "Render the preview at half the project resolution", true},
    {ActionId::QualityQuarter,  "qualityQuarter",  "1/4",                  nullptr,       C::Viewer, {}, {}, "Render the preview at a quarter of the project resolution", true},
    {ActionId::QualityEighth,   "qualityEighth",   "1/8",                  nullptr,       C::Viewer, {}, {}, "Render the preview at an eighth of the project resolution", true},

    // ---- Video / Audio (inspector navigation, clip toggles)
    {ActionId::VideoTransform,  "videoTransform",  "&Transform",           nullptr,       C::Video, {}, {}, "Show the transform controls of the selected clip", false, false},
    {ActionId::VideoCrop,       "videoCrop",       "&Crop",                nullptr,       C::Video, {}, {}, "Show the crop controls of the selected clip", false, false},
    {ActionId::VideoColor,      "videoColor",      "C&olor",               nullptr,       C::Video, {}, {}, "Show the colour controls of the selected clip", false, false},
    {ActionId::VideoEffects,    "videoEffects",    "&Effects",             nullptr,       C::Video, {}, {}, "Show the effect stack of the selected clip", false, false},
    {ActionId::AudioMuteClip,   "audioMuteClip",   "&Mute Clip",           "audio-muted", C::Audio, {}, {}, "Mute or unmute the selected audio clips", true, false},

    // ---- View / Help
    {ActionId::ViewDiagnostics, "viewDiagnostics", "&Diagnostics…",        "diagnostics", C::Help, {}, {}, "System information, hardware acceleration and logs"},
    {ActionId::ViewPerformance, "viewPerformance", "&Performance Monitor", nullptr,       C::Help, {}, {}, "Show live performance counters", true, false},
    {ActionId::HelpAbout,       "helpAbout",       "&About LumaForge",     nullptr,       C::Help, {}, {}, "Version and licence information"},
    {ActionId::HelpShortcuts,   "helpShortcuts",   "&Keyboard Shortcuts…", "keyboard",    C::Help, Shortcut{Qt::Key_F1}, {}, "List every command and its keyboard shortcut"},
    {ActionId::CommandPalette,  "commandPalette",  "&Command Palette…",   "keyboard",    C::Help, Shortcut{CTRL | Qt::Key_K}, {}, "Find and run any command by typing its name"},
    {ActionId::HelpSystemInfo,  "helpSystemInfo",  "&System Information…", nullptr,       C::Help, {}, {}, "Processor, memory, graphics and FFmpeg details"},

    // ---- Workspaces
    {ActionId::WorkspaceEdit,    "workspaceEdit",    "&Edit",            nullptr, C::Workspace, Shortcut{ALT | SHIFT | Qt::Key_1}, {}, "Editing layout: browser, viewer, inspector, timeline", true},
    {ActionId::WorkspaceColor,   "workspaceColor",   "&Color",           nullptr, C::Workspace, Shortcut{ALT | SHIFT | Qt::Key_2}, {}, "Colour layout: large viewer and colour controls", true},
    {ActionId::WorkspaceAudio,   "workspaceAudio",   "&Audio",           nullptr, C::Workspace, Shortcut{ALT | SHIFT | Qt::Key_3}, {}, "Audio layout: tall timeline and audio controls", true},
    {ActionId::WorkspaceText,    "workspaceText",    "&Text",            nullptr, C::Workspace, Shortcut{ALT | SHIFT | Qt::Key_4}, {}, "Titles layout: title presets and text controls", true},
    {ActionId::WorkspaceEffects, "workspaceEffects", "E&ffects",         nullptr, C::Workspace, Shortcut{ALT | SHIFT | Qt::Key_5}, {}, "Effects layout: effects browser and effect controls", true},
    {ActionId::WorkspaceExport,  "workspaceExport",  "E&xport",          nullptr, C::Workspace, Shortcut{ALT | SHIFT | Qt::Key_6}, {}, "Export layout: export settings and queue", true},
    {ActionId::WorkspaceReset,   "workspaceReset",   "&Reset Layout",    nullptr, C::Workspace, {}, {}, "Restore the default panel layout of the current workspace"},
};

const ActionDef* defFor(ActionId id) {
    for (const auto& d : kActions) if (d.id == id) return &d;
    return nullptr;
}

QString settingsKey(const QAction* a) { return QStringLiteral("shortcuts/") + a->objectName(); }
}  // namespace

ActionRegistry::ActionRegistry(QObject* parent) : QObject(parent) {
    QSettings settings;
    for (const auto& def : kActions) {
        auto* a = new QAction(QString::fromUtf8(def.text), this);
        a->setObjectName(QString::fromLatin1(def.objectName));
        if (def.icon) a->setIcon(Icons::icon(QString::fromLatin1(def.icon)));
        a->setStatusTip(QString::fromUtf8(def.statusTip));
        a->setCheckable(def.checkable);
        a->setEnabled(def.enabledInitially);
        // Shortcuts belong to the whole window, not the focused widget, so
        // Space plays whether the timeline or the media list has focus.
        a->setShortcutContext(Qt::WindowShortcut);
        a->setAutoRepeat(def.category == C::Playback || def.category == C::Clip);
        m_actions.insert(def.id, a);
        // A user override (possibly empty = "no shortcut") wins over the default.
        if (settings.contains(settingsKey(a))) {
            QList<QKeySequence> seqs;
            for (const QString& s : settings.value(settingsKey(a)).toStringList()) {
                const QKeySequence k = QKeySequence::fromString(s, QKeySequence::PortableText);
                if (!k.isEmpty()) seqs << k;
            }
            applyShortcuts(def.id, seqs);
        } else {
            applyShortcuts(def.id, defaultShortcuts(def.id));
        }
    }
    auto group = [this](std::initializer_list<ActionId> ids, ActionId checked) {
        auto* g = new QActionGroup(this);
        g->setExclusive(true);
        for (auto id : ids) g->addAction(action(id));
        action(checked)->setChecked(true);
    };
    group({ActionId::ToolSelect, ActionId::ToolBlade, ActionId::ToolTrim, ActionId::ToolHand, ActionId::ToolZoom, ActionId::ToolText}, ActionId::ToolSelect);
    group({ActionId::WorkspaceEdit, ActionId::WorkspaceColor, ActionId::WorkspaceAudio, ActionId::WorkspaceText, ActionId::WorkspaceEffects, ActionId::WorkspaceExport}, ActionId::WorkspaceEdit);
    group({ActionId::ViewerZoomFit, ActionId::ViewerZoom25, ActionId::ViewerZoom50, ActionId::ViewerZoom100, ActionId::ViewerZoom200}, ActionId::ViewerZoomFit);
    group({ActionId::QualityAuto, ActionId::QualityFull, ActionId::QualityHalf, ActionId::QualityQuarter, ActionId::QualityEighth}, ActionId::QualityAuto);
    qApp->installEventFilter(this);
}

QList<QKeySequence> ActionRegistry::defaultShortcuts(ActionId id) const {
    QList<QKeySequence> out;
    if (const ActionDef* d = defFor(id)) {
        if (!d->primary.isEmpty()) out << d->primary.toSequence();
        if (!d->secondary.isEmpty()) out << d->secondary.toSequence();
    }
    return out;
}

void ActionRegistry::applyShortcuts(ActionId id, const QList<QKeySequence>& seqs) {
    QAction* a = action(id);
    if (!a) return;
    a->setShortcuts(seqs);
    updateToolTip(id);
}

void ActionRegistry::setShortcuts(ActionId id, const QList<QKeySequence>& shortcuts) {
    QAction* a = action(id);
    if (!a) return;
    QStringList stored;
    for (const auto& k : shortcuts) if (!k.isEmpty()) stored << k.toString(QKeySequence::PortableText);
    QSettings settings;
    if (shortcuts == defaultShortcuts(id)) settings.remove(settingsKey(a));
    else settings.setValue(settingsKey(a), stored);
    applyShortcuts(id, shortcuts);
    emit shortcutsChanged();
}

void ActionRegistry::resetShortcuts(ActionId id) { setShortcuts(id, defaultShortcuts(id)); }

void ActionRegistry::resetAllShortcuts() {
    QSettings settings;
    settings.remove(QStringLiteral("shortcuts"));
    for (const auto& def : kActions) applyShortcuts(def.id, defaultShortcuts(def.id));
    emit shortcutsChanged();
}

bool ActionRegistry::isCustomised(ActionId id) const {
    QAction* a = action(id);
    return a && QSettings().contains(settingsKey(a));
}

QList<ActionId> ActionRegistry::conflictsWith(const QKeySequence& seq, ActionId except) const {
    QList<ActionId> out;
    if (seq.isEmpty()) return out;
    for (const auto& def : kActions) {
        if (def.id == except) continue;
        if (action(def.id)->shortcuts().contains(seq)) out << def.id;
    }
    return out;
}

QList<QPair<ActionId, ActionId>> ActionRegistry::allConflicts() const {
    QList<QPair<ActionId, ActionId>> out;
    const int n = static_cast<int>(std::size(kActions));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const auto a = action(kActions[i].id)->shortcuts();
            for (const auto& k : action(kActions[j].id)->shortcuts()) {
                if (a.contains(k)) { out.append({kActions[i].id, kActions[j].id}); break; }
            }
        }
    }
    return out;
}

void ActionRegistry::updateToolTip(ActionId id) {
    QAction* a = action(id);
    if (!a) return;
    // "Name  (Shortcut)" on the first line, what it does on the second.
    QString tip = QStringLiteral("<b>%1</b>").arg(plainText(id).toHtmlEscaped());
    const QString keys = allShortcutsText(id);
    if (!keys.isEmpty()) tip += QStringLiteral("&nbsp;&nbsp;<span style='color:#9BA3AE'>%1</span>").arg(keys.toHtmlEscaped());
    if (!a->statusTip().isEmpty()) tip += QStringLiteral("<br>") + a->statusTip().toHtmlEscaped();
    a->setToolTip(tip);
}

bool ActionRegistry::isTextInput(const QWidget* w) {
    if (!w) return false;
    if (qobject_cast<const QLineEdit*>(w) || qobject_cast<const QTextEdit*>(w) || qobject_cast<const QPlainTextEdit*>(w) ||
        qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QKeySequenceEdit*>(w))
        return true;
    if (const auto* combo = qobject_cast<const QComboBox*>(w)) return combo->isEditable();
    // Editors embedded in item views / spin boxes report through their parent.
    if (const QWidget* parent = w->parentWidget()) {
        if (qobject_cast<const QAbstractSpinBox*>(parent) || qobject_cast<const QComboBox*>(parent) || qobject_cast<const QKeySequenceEdit*>(parent)) return true;
    }
    return false;
}

bool ActionRegistry::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const bool plainKey = !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (plainKey && isTextInput(qobject_cast<QWidget*>(watched))) {
            event->accept();   // the widget gets the key press; the shortcut does not fire
            return false;
        }
    }
    return QObject::eventFilter(watched, event);
}

QAction* ActionRegistry::action(ActionId id) const { return m_actions.value(id, nullptr); }

void ActionRegistry::connect(ActionId id, const QObject* receiver, std::function<void()> slot) {
    if (auto* a = action(id)) QObject::connect(a, &QAction::triggered, receiver, [slot = std::move(slot)] { slot(); });
}

void ActionRegistry::setEnabled(ActionId id, bool enabled) {
    if (auto* a = action(id)) a->setEnabled(enabled);
}

void ActionRegistry::setText(ActionId id, const QString& text) {
    if (auto* a = action(id)) {
        if (a->text() == text) return;
        a->setText(text);
        updateToolTip(id);
    }
}

QList<ActionId> ActionRegistry::allIds() const {
    QList<ActionId> out;
    for (const auto& def : kActions) out << def.id;
    return out;
}

ActionCategory ActionRegistry::category(ActionId id) const {
    const ActionDef* d = defFor(id);
    return d ? d->category : C::Edit;
}

QString ActionRegistry::categoryName(ActionCategory c) {
    switch (c) {
        case C::File: return tr("File");
        case C::Edit: return tr("Edit");
        case C::Tools: return tr("Tools");
        case C::Playback: return tr("Playback");
        case C::Clip: return tr("Clip");
        case C::Timeline: return tr("Timeline");
        case C::Viewer: return tr("Viewer");
        case C::Video: return tr("Video");
        case C::Audio: return tr("Audio");
        case C::Text: return tr("Text");
        case C::Markers: return tr("Markers");
        case C::Workspace: return tr("Workspace");
        case C::Help: return tr("Help");
    }
    return {};
}

QString ActionRegistry::plainText(ActionId id) const {
    QAction* a = action(id);
    if (!a) return {};
    QString t = a->text();
    t.remove(QLatin1Char('&'));
    return t;
}

QString ActionRegistry::description(ActionId id) const {
    QAction* a = action(id);
    return a ? a->statusTip() : QString();
}

QString ActionRegistry::shortcutText(ActionId id) const {
    auto* a = action(id);
    return a ? a->shortcut().toString(QKeySequence::NativeText) : QString();
}

QString ActionRegistry::allShortcutsText(ActionId id) const {
    auto* a = action(id);
    if (!a) return {};
    QStringList parts;
    for (const auto& k : a->shortcuts()) parts << k.toString(QKeySequence::NativeText);
    return parts.join(QStringLiteral(", "));
}

}  // namespace lf::ui
