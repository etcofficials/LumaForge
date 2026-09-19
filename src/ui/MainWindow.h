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

class QLabel;
class QMenu;
class QSplitter;

namespace lf::hw { class GpuDevice; }

namespace lf::ui {

class ActionRegistry;
class DiagnosticsDialog;
class ImportController;
class InspectorPanel;
class MediaPanel;
class PlaybackController;
class PreviewPanel;
class ProjectController;
class TimelinePanel;

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

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void buildCentralLayout();
    void connectActions();
    void updateUndoActions();
    void updateWindowTitle();
    void showDiagnostics(const QString& tab = {});
    void notYetAvailable(const QString& feature);
    void onDocumentChange(const project::Change& change);
    void rebuildRecentMenu();
    void showProjectSettings();
    void scheduleSequenceRefresh();
    void refreshSequenceProgram();
    // Switches the preview to the timeline (no-op when already there).
    void showSequenceInPreview(TimePoint at);

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
    std::shared_ptr<const playback::Program> m_sequenceProgram;
    bool m_sequenceRefreshPending = false;

    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_topSplitter = nullptr;
    MediaPanel* m_mediaPanel = nullptr;
    PreviewPanel* m_previewPanel = nullptr;
    InspectorPanel* m_inspectorPanel = nullptr;
    TimelinePanel* m_timelinePanel = nullptr;
    QPointer<DiagnosticsDialog> m_diagnostics;

    QLabel* m_statusHardware = nullptr;
    QLabel* m_statusPreview = nullptr;
    QLabel* m_statusFps = nullptr;
};

}  // namespace lf::ui
