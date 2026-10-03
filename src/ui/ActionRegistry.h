#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace lf::ui {

// Every user-invokable operation has an ActionId. Menus, toolbars, transport
// buttons, context menus, the command palette and keyboard shortcuts all use
// the same QAction, so a label or shortcut is defined in exactly one place:
// the table in ActionRegistry.cpp. User shortcut overrides are stored in the
// settings file and applied on top of that table.
enum class ActionId {
    // File
    ProjectNew, ProjectOpen, ProjectSave, ProjectSaveAs, ProjectSettings, ProjectClose, AppQuit,
    MediaImport, MediaImportFolder, Export,
    // Edit
    EditUndo, EditRedo, EditCut, EditCopy, EditPaste, EditPasteOverwrite, EditDuplicate,
    ClipDelete, ClipRippleDelete, ClipSelectAll, ClipDeselect,
    // Tools (mutually exclusive)
    ToolSelect, ToolBlade, ToolTrim, ToolHand, ToolZoom, ToolText,
    // Playback
    PlayPause, Play, Pause, PlayStop, ShuttleReverse, ShuttleForward,
    StepBack, StepForward, StepBackLarge, StepForwardLarge, PrevEdit, NextEdit, GoToStart, GoToEnd, PlayLoop,
    MarkIn, MarkOut, ClearInOut, GoToIn, GoToOut,
    // Clip
    ClipSplit, ClipTrimStartToPlayhead, ClipTrimEndToPlayhead, ClipNudgeLeft, ClipNudgeRight, ClipNudgeLeftLarge, ClipNudgeRightLarge,
    ClipMoveToPlayhead, ClipEnable, ClipLift, ClipExtract,
    // Timeline
    TimelineZoomIn, TimelineZoomOut, TimelineZoomFit, TimelineSnap, TimelineAddText, TimelineExpand,
    TimelineAddSolid, TimelineAddAdjustment, MarkerAdd, MarkerNext, MarkerPrevious, ExportFrame,
    SubtitleAdd, SubtitlesImport, SubtitlesExport, ClipFrameHold,
    TimelineAddVideoTrack, TimelineAddAudioTrack,
    TrackAddAbove, TrackAddBelow, TrackDuplicate, TrackDelete, TrackRename, TrackMoveUp, TrackMoveDown, TrackLock, TrackMute, TrackSolo,
    ClipSpeed, ClipProperties, ClipRevealInMedia, ClipAddTransition,
    // Viewer
    ViewerZoomFit, ViewerZoom25, ViewerZoom50, ViewerZoom100, ViewerZoom200, ViewerFullscreen,
    QualityAuto, QualityFull, QualityHalf, QualityQuarter, QualityEighth,
    // Video (inspector navigation)
    VideoTransform, VideoCrop, VideoColor, VideoEffects,
    // Audio
    AudioMuteClip,
    // View / Help
    ViewDiagnostics, ViewPerformance, HelpAbout, HelpShortcuts, HelpSystemInfo, CommandPalette,
    // Workspaces
    WorkspaceEdit, WorkspaceColor, WorkspaceAudio, WorkspaceText, WorkspaceEffects, WorkspaceExport, WorkspaceReset,
};

enum class ActionCategory { File, Edit, Tools, Playback, Clip, Timeline, Viewer, Video, Audio, Text, Markers, Workspace, Help };

class ActionRegistry : public QObject {
    Q_OBJECT
public:
    explicit ActionRegistry(QObject* parent = nullptr);

    [[nodiscard]] QAction* action(ActionId id) const;
    void connect(ActionId id, const QObject* receiver, std::function<void()> slot);
    void setEnabled(ActionId id, bool enabled);
    // Changes the label (e.g. "Undo Move Clip"); the tooltip follows.
    void setText(ActionId id, const QString& text);

    [[nodiscard]] QList<ActionId> allIds() const;
    [[nodiscard]] ActionCategory category(ActionId id) const;
    [[nodiscard]] static QString categoryName(ActionCategory c);
    // Label without mnemonics ("Save As..."), description (status tip).
    [[nodiscard]] QString plainText(ActionId id) const;
    [[nodiscard]] QString description(ActionId id) const;

    // Shortcut display text of the primary shortcut ("Ctrl+S").
    [[nodiscard]] QString shortcutText(ActionId id) const;
    // All shortcuts, comma separated ("Ctrl+Y, Ctrl+Shift+Z").
    [[nodiscard]] QString allShortcutsText(ActionId id) const;
    [[nodiscard]] QList<QKeySequence> defaultShortcuts(ActionId id) const;
    // User override, persisted. An empty list means "no shortcut".
    void setShortcuts(ActionId id, const QList<QKeySequence>& shortcuts);
    void resetShortcuts(ActionId id);
    void resetAllShortcuts();
    [[nodiscard]] bool isCustomised(ActionId id) const;
    // Other actions already bound to `seq` (for conflict warnings).
    [[nodiscard]] QList<ActionId> conflictsWith(const QKeySequence& seq, ActionId except) const;
    // Every pair of actions that currently share a key sequence.
    [[nodiscard]] QList<QPair<ActionId, ActionId>> allConflicts() const;

    // True for widgets where plain keys mean typing (line edits, spin boxes...).
    static bool isTextInput(const QWidget* w);

signals:
    void shortcutsChanged();

protected:
    // Application-wide: single-key shortcuts (Space, S, Delete, arrows...) never
    // steal keystrokes from a focused text field.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyShortcuts(ActionId id, const QList<QKeySequence>& seqs);
    void updateToolTip(ActionId id);

    QHash<ActionId, QAction*> m_actions;
};

}  // namespace lf::ui
