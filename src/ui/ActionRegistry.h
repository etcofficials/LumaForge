#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

#include <functional>

namespace lf::ui {

// Every user-invokable operation has an ActionId. Menus, toolbars, transport
// buttons and keyboard shortcuts all reference the same QAction, so a
// shortcut or label is defined in exactly one place: the table in
// ActionRegistry.cpp. Nothing else in the codebase hard-codes a shortcut.
enum class ActionId {
    // Project
    ProjectNew, ProjectOpen, ProjectSave, ProjectSaveAs, ProjectSettings, AppQuit,
    // Media
    MediaImport,
    // Edit
    EditUndo, EditRedo, ClipSplit, ClipDelete, ClipSelectAll,
    // Playback
    PlayPause, PlayStop, StepBack, StepForward, GoToStart, GoToEnd,
    // Timeline
    TimelineZoomIn, TimelineZoomOut, TimelineZoomFit, TimelineSnap,
    // Output
    Export,
    // View / Help
    ViewDiagnostics, ViewPerformance, HelpAbout,
};

class ActionRegistry : public QObject {
    Q_OBJECT
public:
    explicit ActionRegistry(QObject* parent = nullptr);

    [[nodiscard]] QAction* action(ActionId id) const;
    void connect(ActionId id, const QObject* receiver, std::function<void()> slot);
    void setEnabled(ActionId id, bool enabled);
    void setText(ActionId id, const QString& text);

    // Shortcut display text ("Ctrl+S") for tooltips/hints.
    [[nodiscard]] QString shortcutText(ActionId id) const;

private:
    QHash<ActionId, QAction*> m_actions;
};

}  // namespace lf::ui
