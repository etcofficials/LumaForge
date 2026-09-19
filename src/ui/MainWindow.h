#pragma once

#include "lumaforge/commands/UndoStack.h"
#include "lumaforge/core/Log.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/hardware/HwCapabilities.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>

class QLabel;
class QSplitter;

namespace lf::hw { class GpuDevice; }

namespace lf::ui {

class ActionRegistry;
class DiagnosticsDialog;
class InspectorPanel;
class MediaPanel;
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

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void buildCentralLayout();
    void connectActions();
    void updateUndoActions();
    void updateWindowTitle();
    void updateTimeReadouts();
    void showDiagnostics(const QString& tab = {});
    void notYetAvailable(const QString& feature);

    // Placeholder transport clock until the playback engine exists: advances
    // the playhead in real time so transport, timecode and repaint cost can
    // be exercised today.
    void togglePlayback();
    void stopPlayback();
    void onPlaybackTick();

    std::shared_ptr<hw::GpuDevice> m_gpu;
    std::shared_ptr<log::RingBufferSink> m_logSink;
    std::optional<hw::HwReport> m_hardware;

    ActionRegistry* m_actions;
    cmd::UndoStack m_undoStack;

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

    // Project placeholders (project module lands next milestone)
    QString m_projectName;
    Rational m_projectFps = fps::f30;
    int m_projectWidth = 1920;
    int m_projectHeight = 1080;

    bool m_playing = false;
    TimePoint m_playbackOrigin;
    QElapsedTimer m_playbackClock;
    QTimer m_playbackTimer;
};

}  // namespace lf::ui
