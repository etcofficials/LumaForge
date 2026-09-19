#pragma once

#include "lumaforge/core/Log.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/hardware/HwCapabilities.h"
#include "lumaforge/project/Document.h"

#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <optional>

class QLabel;
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
class TimelinePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::shared_ptr<hw::GpuDevice> gpu, std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Called once background hardware detection completes.
    void setHardwareReport(hw::HwReport report);
    void openDiagnostics(const QString& tab = {});
    // Imports media files (e.g. passed on the command line or via 'Open with').
    void importFiles(const QStringList& paths);
    // Imports one file and opens it in the source preview (optionally playing).
    void openSourceFile(const QString& path, bool autoplay);

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

    std::shared_ptr<hw::GpuDevice> m_gpu;
    std::shared_ptr<log::RingBufferSink> m_logSink;
    std::optional<hw::HwReport> m_hardware;

    ActionRegistry* m_actions;
    std::unique_ptr<project::Document> m_document;
    ImportController* m_importer = nullptr;
    PlaybackController* m_playback = nullptr;

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
