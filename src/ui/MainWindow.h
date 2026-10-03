#pragma once

#include "lumaforge/core/Log.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/hardware/HwCapabilities.h"
#include "lumaforge/playback/Program.h"
#include "lumaforge/project/Document.h"

#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <optional>

class QDockWidget;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabBar;
class QTimer;
class QToolBar;

namespace lf::hw { class GpuDevice; }
namespace lf::playback { struct PlaybackStats; }

namespace lf::ui {

class ActionRegistry;
class BrowserPanel;
class CommandPalette;
class DiagnosticsDialog;
class EffectsBrowserPanel;
class HomeScreen;
class ImportController;
class InspectorPanel;
class MediaPanel;
class PlaybackController;
class PreviewPanel;
class ProjectController;
class ProjectInfoPanel;
class TimelinePanel;
class TitlesBrowserPanel;
class TransitionsBrowserPanel;
class MarkersPanel;
class MixerPanel;
class ProxyController;
class ScopesPanel;
class PerformancePanel;
class ExportController;
class ExportSettingsPanel;
class ExportQueuePanel;
class ToastHost;
class WorkspaceManager;
enum class Workspace;

// Top-level window. Two pages: the Home screen (no project open) and the
// editor, a nested QMainWindow whose docks (browser, inspector, timeline,
// mixer, scopes, export...) are arranged by the WorkspaceManager around the
// central viewer. Menus and the status bar belong to this outer window.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::shared_ptr<hw::GpuDevice> gpu, std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Called once background hardware detection completes.
    void setHardwareReport(hw::HwReport report);
    void openDiagnostics(const QString& tab = {});
    // Imports media files (e.g. passed on the command line or via 'Open with');
    // optionally appends them to the timeline in the given order.
    void importFiles(const QStringList& paths, bool addToTimeline = false);
    // Imports one file and opens it in the source preview (optionally playing).
    void openSourceFile(const QString& path, bool autoplay);
    // Opens a .lfproj (asks about unsaved changes first).
    void openProjectFile(const QString& path);

    [[nodiscard]] ToastHost* toasts() const noexcept { return m_toasts; }
    [[nodiscard]] project::Document& document() const noexcept { return *m_document; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    void buildToolBar();
    void buildWorkspaceBar();
    void buildPanels();
    void buildStatusBar();
    void connectActions();
    QDockWidget* makeDock(const QString& title, const QString& objectName, QWidget* content, QWidget* titleBar);
    void updateUndoActions();
    void updateWindowTitle();
    void updateStatusProject();
    void updateStatusStats(const playback::PlaybackStats& stats);
    void sampleMemory();
    void showDiagnostics(const QString& tab = {});
    void onDocumentChange(const project::Change& change);
    void rebuildRecentMenu();
    void showProjectSettings();
    void scheduleSequenceRefresh();
    void refreshSequenceProgram();
    // Switches the preview to the timeline (no-op when already there).
    void showSequenceInPreview(TimePoint at);
    void onWorkspaceChanged(Workspace w);
    void setToolbarGroupVisible(const QString& key, bool visible);
    void ensurePreviewProgram();
    void seekTimeline(TimePoint t);
    void stepFrames(int frames);
    void recordVoiceOver();
    // Puts an audio media item at `at` on the first audio track that is free there (adds one if needed).
    void placeAudioOnFreeTrack(project::MediaId media, TimePoint at);
    void applyLoop();
    void importFolderWithDialog();
    void setViewerFullscreen(bool on);

    // Pages
    void showHome();
    void showEditor();
    [[nodiscard]] bool editorVisible() const;
    void newProjectFromHome();
    void closeProject();

    std::shared_ptr<hw::GpuDevice> m_gpu;
    std::shared_ptr<log::RingBufferSink> m_logSink;
    std::optional<hw::HwReport> m_hardware;

    ActionRegistry* m_actions;
    std::unique_ptr<project::Document> m_document;
    int m_documentListener = 0;
    ImportController* m_importer = nullptr;
    PlaybackController* m_playback = nullptr;
    ProjectController* m_project = nullptr;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_panelsMenu = nullptr;
    std::shared_ptr<const playback::Program> m_sequenceProgram;
    bool m_sequenceRefreshPending = false;

    QStackedWidget* m_pages = nullptr;
    HomeScreen* m_home = nullptr;
    QMainWindow* m_editor = nullptr;
    QTabBar* m_workspaceBar = nullptr;
    WorkspaceManager* m_workspaces = nullptr;
    ToastHost* m_toasts = nullptr;
    bool m_layoutRestored = false;

    BrowserPanel* m_browser = nullptr;
    MediaPanel* m_mediaPanel = nullptr;
    ProjectInfoPanel* m_projectInfo = nullptr;
    EffectsBrowserPanel* m_effectsBrowser = nullptr;
    TitlesBrowserPanel* m_titlesBrowser = nullptr;
    TransitionsBrowserPanel* m_transitionsBrowser = nullptr;
    MarkersPanel* m_markersPanel = nullptr;
    MixerPanel* m_mixerPanel = nullptr;
    ProxyController* m_proxies = nullptr;
    ScopesPanel* m_scopesPanel = nullptr;
    PerformancePanel* m_performancePanel = nullptr;
    QDockWidget* m_performanceDock = nullptr;
    ExportController* m_exportController = nullptr;
    ExportSettingsPanel* m_exportSettings = nullptr;
    ExportQueuePanel* m_exportQueue = nullptr;
    QLabel* m_statusExport = nullptr;
    PreviewPanel* m_previewPanel = nullptr;
    InspectorPanel* m_inspectorPanel = nullptr;
    TimelinePanel* m_timelinePanel = nullptr;
    QPointer<DiagnosticsDialog> m_diagnostics;

    struct ToolbarGroup {
        QString key;
        QString name;
        QList<QAction*> items;   // actions + widget actions + trailing separator
    };
    QToolBar* m_mainToolBar = nullptr;
    std::vector<ToolbarGroup> m_toolbarGroups;
    QLabel* m_toolbarHw = nullptr;
    QLabel* m_toolbarFps = nullptr;
    CommandPalette* m_palette = nullptr;
    bool m_viewerFullscreen = false;
    int m_timelineHeightBeforeExpand = 0;
    bool m_wasMaximized = false;
    QByteArray m_preFullscreenState;

    QLabel* m_statusProject = nullptr;
    QLabel* m_statusTimecode = nullptr;
    QLabel* m_statusPlayback = nullptr;
    QLabel* m_statusDecoder = nullptr;
    QLabel* m_statusHardware = nullptr;
    QLabel* m_statusMemory = nullptr;
    QTimer* m_memoryTimer = nullptr;
};

}  // namespace lf::ui
