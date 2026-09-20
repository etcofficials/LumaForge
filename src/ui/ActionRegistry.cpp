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

    [[nodiscard]] QKeySequence toSequence() const {
        if (standard != QKeySequence::UnknownKey) return QKeySequence(standard);
        return keys ? QKeySequence(keys) : QKeySequence();
    }
};

struct ActionDef {
    ActionId id;
    const char* objectName;
    const char* text;
    const char* icon;         // resource icon name or nullptr
    Shortcut shortcut;
    const char* statusTip;
    bool checkable = false;
    bool enabledInitially = true;
};

// The single source of truth for labels and shortcuts.
const ActionDef kActions[] = {
    {ActionId::ProjectNew,      "projectNew",      "&New Project",        "new",    QKeySequence::New,    "Create a new project"},
    {ActionId::ProjectOpen,     "projectOpen",     "&Open Project...",    "open",   QKeySequence::Open,   "Open an existing project"},
    {ActionId::ProjectSave,     "projectSave",     "&Save",               "save",   QKeySequence::Save,   "Save the project"},
    {ActionId::ProjectSaveAs,   "projectSaveAs",   "Save &As...",         nullptr,  QKeySequence::SaveAs, "Save the project to a new file"},
    {ActionId::ProjectSettings, "projectSettings", "Project S&ettings...", nullptr, Shortcut{},      "Change resolution and frame rate"},
    {ActionId::AppQuit,         "appQuit",         "E&xit",               nullptr,  Shortcut{Qt::CTRL | Qt::Key_Q}, "Quit LumaForge"},

    {ActionId::MediaImport,     "mediaImport",     "&Import Media...",    "import", Shortcut{Qt::CTRL | Qt::Key_I}, "Import video, audio or images"},

    {ActionId::EditUndo,        "editUndo",        "&Undo",               "undo",   QKeySequence::Undo,   "Undo the last edit", false, false},
    {ActionId::EditRedo,        "editRedo",        "&Redo",               "redo",   Shortcut{Qt::CTRL | Qt::SHIFT | Qt::Key_Z}, "Redo the last undone edit", false, false},
    {ActionId::ClipSplit,       "clipSplit",       "&Split at Playhead",  "split",  Shortcut{Qt::Key_S}, "Split the clip under the playhead", false, false},
    {ActionId::ClipDelete,      "clipDelete",      "&Delete",             "delete", QKeySequence::Delete, "Delete the selected clips", false, false},
    {ActionId::ClipRippleDelete,"clipRippleDelete","&Ripple Delete",      nullptr,  Shortcut{Qt::SHIFT | Qt::Key_Delete}, "Delete the selected clips and close the gap", false, false},
    {ActionId::ClipSelectAll,   "clipSelectAll",   "Select &All",         nullptr,  QKeySequence::SelectAll, "Select all clips", false, false},
    {ActionId::EditCopy,        "editCopy",        "&Copy",               nullptr,  QKeySequence::Copy,   "Copy the selected clips", false, false},
    {ActionId::EditPaste,       "editPaste",       "&Paste",              nullptr,  QKeySequence::Paste,  "Paste clips at the playhead", false, false},
    {ActionId::EditDuplicate,   "editDuplicate",   "D&uplicate",          nullptr,  Shortcut{Qt::CTRL | Qt::Key_D}, "Duplicate the selected clips after themselves", false, false},

    {ActionId::ToolSelect,      "toolSelect",      "&Select Tool",        "tool-select", Shortcut{Qt::Key_V}, "Select, move and trim clips", true},
    {ActionId::ToolBlade,       "toolBlade",       "&Blade Tool",         "tool-blade",  Shortcut{Qt::Key_B}, "Click a clip to split it", true},

    {ActionId::PlayPause,       "playPause",       "&Play/Pause",         "play",   Shortcut{Qt::Key_Space}, "Play or pause"},
    {ActionId::PlayStop,        "playStop",        "S&top",               "stop",   Shortcut{Qt::Key_K}, "Stop and return to the start of playback"},
    {ActionId::StepBack,        "stepBack",        "Step &Back",          "step-back", Shortcut{Qt::Key_Left}, "Move one frame back"},
    {ActionId::StepForward,     "stepForward",     "Step &Forward",       "step-forward", Shortcut{Qt::Key_Right}, "Move one frame forward"},
    {ActionId::GoToStart,       "goToStart",       "Go to &Start",        "go-start", Shortcut{Qt::Key_Home}, "Move to the start of the sequence"},
    {ActionId::GoToEnd,         "goToEnd",         "Go to &End",          "go-end", Shortcut{Qt::Key_End}, "Move to the end of the sequence"},

    {ActionId::TimelineZoomIn,  "timelineZoomIn",  "Zoom &In",            "zoom-in",  Shortcut{Qt::Key_Equal}, "Zoom in on the timeline"},
    {ActionId::TimelineZoomOut, "timelineZoomOut", "Zoom &Out",           "zoom-out", Shortcut{Qt::Key_Minus}, "Zoom out on the timeline"},
    {ActionId::TimelineZoomFit, "timelineZoomFit", "Zoom to &Fit",        "zoom-fit", Shortcut{Qt::SHIFT | Qt::Key_Z}, "Fit the whole sequence in view"},
    {ActionId::TimelineSnap,    "timelineSnap",    "S&napping",           "magnet",   Shortcut{Qt::Key_N}, "Snap clips and the playhead to edges", true},
    {ActionId::TimelineAddText, "timelineAddText", "Add &Text",           "text",     Shortcut{Qt::CTRL | Qt::Key_T}, "Add a text clip at the playhead"},

    {ActionId::Export,          "export",          "&Export...",          "export", Shortcut{Qt::CTRL | Qt::Key_M}, "Export the sequence to a video file", false, false},

    {ActionId::ViewDiagnostics, "viewDiagnostics", "&Diagnostics...",     "diagnostics", Shortcut{}, "System information, hardware acceleration and logs"},
    {ActionId::ViewPerformance, "viewPerformance", "&Performance Monitor", nullptr, Shortcut{}, "Show live performance counters", true, false},
    {ActionId::HelpAbout,       "helpAbout",       "&About LumaForge",    nullptr,  Shortcut{}, "Version and licence information"},
};
}  // namespace

ActionRegistry::ActionRegistry(QObject* parent) : QObject(parent) {
    for (const auto& def : kActions) {
        auto* a = new QAction(QString::fromUtf8(def.text), this);
        a->setObjectName(QString::fromLatin1(def.objectName));
        if (def.icon) a->setIcon(Icons::icon(QString::fromLatin1(def.icon)));
        if (const QKeySequence seq = def.shortcut.toSequence(); !seq.isEmpty()) a->setShortcut(seq);
        a->setStatusTip(QString::fromUtf8(def.statusTip));
        a->setCheckable(def.checkable);
        a->setEnabled(def.enabledInitially);
        // Shortcuts belong to the whole window, not the focused widget, so
        // Space plays whether the timeline or the media list has focus.
        a->setShortcutContext(Qt::WindowShortcut);
        m_actions.insert(def.id, a);
    }
    auto* tools = new QActionGroup(this);
    tools->setExclusive(true);
    tools->addAction(action(ActionId::ToolSelect));
    tools->addAction(action(ActionId::ToolBlade));
    action(ActionId::ToolSelect)->setChecked(true);
    qApp->installEventFilter(this);
}

bool ActionRegistry::isTextInput(const QWidget* w) {
    if (!w) return false;
    if (qobject_cast<const QLineEdit*>(w) || qobject_cast<const QTextEdit*>(w) || qobject_cast<const QPlainTextEdit*>(w) ||
        qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QKeySequenceEdit*>(w))
        return true;
    if (const auto* combo = qobject_cast<const QComboBox*>(w)) return combo->isEditable();
    // Editors embedded in item views / spin boxes report through their parent.
    if (const QWidget* parent = w->parentWidget()) {
        if (qobject_cast<const QAbstractSpinBox*>(parent) || qobject_cast<const QComboBox*>(parent)) return true;
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
    if (auto* a = action(id)) a->setText(text);
}

QString ActionRegistry::shortcutText(ActionId id) const {
    auto* a = action(id);
    return a ? a->shortcut().toString(QKeySequence::NativeText) : QString();
}

}  // namespace lf::ui
