#include "ui/MainWindow.h"

#include "lumaforge/core/Version.h"
#include "lumaforge/diagnostics/SystemInfo.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/hardware/HardwareDetector.h"
#include "ui/ActionRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/dialogs/DiagnosticsDialog.h"
#include "ui/media/ImportController.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/preview/PreviewWidget.h"
#include "ui/timeline/TimelinePanel.h"
#include "ui/timeline/TimelineView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
constexpr const char* kLog = "ui";
}

MainWindow::MainWindow(std::shared_ptr<hw::GpuDevice> gpu, std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent)
    : QMainWindow(parent), m_gpu(std::move(gpu)), m_logSink(std::move(logSink)), m_actions(new ActionRegistry(this)),
      m_document(std::make_unique<project::Document>()), m_importer(new ImportController(*m_document, this)) {
    setAcceptDrops(true);
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    setMinimumSize(1024, 640);
    resize(1440, 860);
    setDocumentMode(true);

    buildMenus();
    buildToolBar();
    buildCentralLayout();
    buildStatusBar();
    connectActions();

    m_document->undoStack().setChangeListener([this] { updateUndoActions(); updateWindowTitle(); });
    updateUndoActions();
    updateWindowTitle();
    updateTimeReadouts();

    m_playbackTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playbackTimer, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    if (settings.contains(QStringLiteral("window/mainSplitter"))) m_mainSplitter->restoreState(settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    if (settings.contains(QStringLiteral("window/topSplitter"))) m_topSplitter->restoreState(settings.value(QStringLiteral("window/topSplitter")).toByteArray());

    // The preview goes to the GPU as soon as the window exists; the surface
    // falls back to software by itself if the device is unusable.
    m_previewPanel->surface()->setGpuDevice(m_gpu);
    m_previewPanel->surface()->setFrameSize(m_projectWidth, m_projectHeight);
    connect(m_previewPanel->surface(), &PreviewWidget::backendChanged, this, [this] {
        m_statusPreview->setText(tr("Preview: %1").arg(m_previewPanel->surface()->backendName()));
        m_statusPreview->setProperty("ok", m_previewPanel->surface()->usingHardware());
        m_statusPreview->style()->unpolish(m_statusPreview);
        m_statusPreview->style()->polish(m_statusPreview);
    });
    m_statusPreview->setText(tr("Preview: %1").arg(m_previewPanel->surface()->backendName()));
    LF_LOG_INFO(kLog, "main window ready");
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- construction

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actions->action(ActionId::ProjectNew));
    file->addAction(m_actions->action(ActionId::ProjectOpen));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::ProjectSave));
    file->addAction(m_actions->action(ActionId::ProjectSaveAs));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::MediaImport));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::Export));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::ProjectSettings));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::AppQuit));

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actions->action(ActionId::EditUndo));
    edit->addAction(m_actions->action(ActionId::EditRedo));
    edit->addSeparator();
    edit->addAction(m_actions->action(ActionId::ClipSplit));
    edit->addAction(m_actions->action(ActionId::ClipDelete));
    edit->addSeparator();
    edit->addAction(m_actions->action(ActionId::ClipSelectAll));

    auto* playback = menuBar()->addMenu(tr("&Playback"));
    playback->addAction(m_actions->action(ActionId::PlayPause));
    playback->addAction(m_actions->action(ActionId::PlayStop));
    playback->addSeparator();
    playback->addAction(m_actions->action(ActionId::StepBack));
    playback->addAction(m_actions->action(ActionId::StepForward));
    playback->addAction(m_actions->action(ActionId::GoToStart));
    playback->addAction(m_actions->action(ActionId::GoToEnd));

    auto* view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_actions->action(ActionId::TimelineZoomIn));
    view->addAction(m_actions->action(ActionId::TimelineZoomOut));
    view->addAction(m_actions->action(ActionId::TimelineZoomFit));
    view->addAction(m_actions->action(ActionId::TimelineSnap));
    view->addSeparator();
    view->addAction(m_actions->action(ActionId::ViewPerformance));
    view->addAction(m_actions->action(ActionId::ViewDiagnostics));

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actions->action(ActionId::HelpAbout));
}

void MainWindow::buildToolBar() {
    auto* tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(16, 16));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setContextMenuPolicy(Qt::PreventContextMenu);

    tb->addAction(m_actions->action(ActionId::ProjectNew));
    tb->addAction(m_actions->action(ActionId::ProjectOpen));
    tb->addAction(m_actions->action(ActionId::ProjectSave));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::MediaImport));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::EditUndo));
    tb->addAction(m_actions->action(ActionId::EditRedo));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::ClipSplit));
    tb->addAction(m_actions->action(ActionId::ClipDelete));

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    // Export is the one primary action in the top bar.
    auto* exportButton = new QToolButton(tb);
    exportButton->setDefaultAction(m_actions->action(ActionId::Export));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setProperty("primary", true);
    exportButton->setIcon(Icons::icon(QStringLiteral("export"), Theme::palette().accentText, Theme::palette().textDisabled));
    tb->addWidget(exportButton);
}

void MainWindow::buildCentralLayout() {
    m_mediaPanel = new MediaPanel(*m_document, *m_actions, *m_importer, this);
    m_previewPanel = new PreviewPanel(*m_actions, this);
    m_inspectorPanel = new InspectorPanel(this);
    m_timelinePanel = new TimelinePanel(*m_actions, this);

    m_topSplitter = new QSplitter(Qt::Horizontal, this);
    m_topSplitter->setChildrenCollapsible(false);
    m_topSplitter->addWidget(m_mediaPanel);
    m_topSplitter->addWidget(m_previewPanel);
    m_topSplitter->addWidget(m_inspectorPanel);
    m_topSplitter->setStretchFactor(0, 0);
    m_topSplitter->setStretchFactor(1, 1);
    m_topSplitter->setStretchFactor(2, 0);
    m_topSplitter->setSizes({300, 860, 260});

    m_mainSplitter = new QSplitter(Qt::Vertical, this);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->addWidget(m_topSplitter);
    m_mainSplitter->addWidget(m_timelinePanel);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setSizes({520, 300});

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_mainSplitter);
    setCentralWidget(central);

    m_timelinePanel->view()->setFrameRate(m_projectFps);
}

void MainWindow::buildStatusBar() {
    auto* sb = statusBar();
    sb->setSizeGripEnabled(true);
    m_statusHardware = new QLabel(tr("Detecting hardware…"), sb);
    m_statusPreview = new QLabel(sb);
    m_statusFps = new QLabel(tr("— fps"), sb);
    m_statusFps->setToolTip(tr("Preview frame rate"));
    sb->addPermanentWidget(m_statusHardware);
    sb->addPermanentWidget(m_statusPreview);
    sb->addPermanentWidget(m_statusFps);
}

void MainWindow::connectActions() {
    auto* tl = m_timelinePanel->view();

    m_actions->connect(ActionId::ProjectNew, this, [this] { notYetAvailable(tr("New Project")); });
    m_actions->connect(ActionId::ProjectOpen, this, [this] { notYetAvailable(tr("Open Project")); });
    m_actions->connect(ActionId::ProjectSave, this, [this] { notYetAvailable(tr("Save")); });
    m_actions->connect(ActionId::ProjectSaveAs, this, [this] { notYetAvailable(tr("Save As")); });
    m_actions->connect(ActionId::ProjectSettings, this, [this] { notYetAvailable(tr("Project Settings")); });
    m_actions->connect(ActionId::MediaImport, this, [this] { m_importer->importWithDialog(this); });
    connect(m_importer, &ImportController::importStarted, this, [this](int n) { statusBar()->showMessage(tr("Importing %n file(s)…", nullptr, n)); });
    connect(m_importer, &ImportController::importFinished, this, [this](int ok, int failed) {
        statusBar()->showMessage(failed ? tr("Imported %1, %2 failed").arg(ok).arg(failed) : tr("Imported %n file(s)", nullptr, ok), 4000);
    });
    connect(m_mediaPanel, &MediaPanel::openRequested, this, [this](project::MediaId) { notYetAvailable(tr("Source preview")); });
    connect(m_mediaPanel, &MediaPanel::addToTimelineRequested, this, [this](const QList<project::MediaId>&) { notYetAvailable(tr("Add to timeline")); });
    m_actions->connect(ActionId::Export, this, [this] { notYetAvailable(tr("Export")); });
    m_actions->connect(ActionId::AppQuit, this, [this] { close(); });

    m_actions->connect(ActionId::EditUndo, this, [this] { m_document->undoStack().undo(); });
    m_actions->connect(ActionId::EditRedo, this, [this] { m_document->undoStack().redo(); });

    m_actions->connect(ActionId::PlayPause, this, [this] { togglePlayback(); });
    m_actions->connect(ActionId::PlayStop, this, [this] { stopPlayback(); });
    m_actions->connect(ActionId::StepBack, this, [tl] { tl->stepFrames(-1); });
    m_actions->connect(ActionId::StepForward, this, [tl] { tl->stepFrames(1); });
    m_actions->connect(ActionId::GoToStart, this, [tl] { tl->goToStart(); });
    m_actions->connect(ActionId::GoToEnd, this, [tl] { tl->goToEnd(); });

    m_actions->connect(ActionId::ViewDiagnostics, this, [this] { showDiagnostics(); });
    m_actions->connect(ActionId::HelpAbout, this, [this] {
        QMessageBox::about(this, tr("About LumaForge"),
                           tr("<b>LumaForge %1</b><br>Lightweight professional video editor.<br><br>"
                              "Built with Qt %2 and FFmpeg.<br>Build: %3")
                               .arg(QString::fromLatin1(version::string), QString::fromLatin1(qVersion()), QString::fromLatin1(version::buildType)));
    });

    connect(tl, &TimelineView::playheadChanged, this, [this] { updateTimeReadouts(); });
    connect(tl, &TimelineView::playheadScrubbed, this, [this] {
        // Scrubbing while playing hands control to the user.
        if (m_playing) { m_playbackOrigin = m_timelinePanel->view()->playhead(); m_playbackClock.restart(); }
    });
}

// ---------------------------------------------------------------- state

void MainWindow::updateUndoActions() {
    auto& undo = m_document->undoStack();
    const auto undoText = QString::fromStdString(undo.undoText());
    const auto redoText = QString::fromStdString(undo.redoText());
    m_actions->setEnabled(ActionId::EditUndo, undo.canUndo());
    m_actions->setEnabled(ActionId::EditRedo, undo.canRedo());
    m_actions->setText(ActionId::EditUndo, undoText.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(undoText));
    m_actions->setText(ActionId::EditRedo, redoText.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(redoText));
}

void MainWindow::updateWindowTitle() {
    const QString name = m_projectName.isEmpty() ? tr("Untitled") : m_projectName;
    setWindowTitle(QStringLiteral("%1%2 — LumaForge").arg(name, m_document->isDirty() ? QStringLiteral("*") : QString()));
}

void MainWindow::updateTimeReadouts() {
    auto* tl = m_timelinePanel->view();
    m_previewPanel->setTime(tl->playhead(), tl->sequenceDuration(), m_projectFps);
}

void MainWindow::setHardwareReport(hw::HwReport report) {
    m_hardware = std::move(report);
    const auto& r = *m_hardware;
    QString text;
    if (const auto* a = r.activeAdapter()) text = QString::fromStdString(a->name);
    else text = tr("No GPU");
    text += r.supportsDecoder("H.264") ? tr("  ·  HW decode") : tr("  ·  CPU decode");
    if (r.firstAvailableEncoder(hw::EncoderKind::Hardware)) text += tr("  ·  HW encode");
    else text += tr("  ·  CPU encode");
    m_statusHardware->setText(text);
    m_statusHardware->setToolTip(QString::fromStdString(hw::HardwareDetector::summary(r)));
    if (m_diagnostics) m_diagnostics->setHardwareReport(r);
}

void MainWindow::openDiagnostics(const QString& tab) { showDiagnostics(tab); }

void MainWindow::importFiles(const QStringList& paths) { m_importer->importFiles(paths, this); }

void MainWindow::showDiagnostics(const QString& tab) {
    if (!m_diagnostics) {
        m_diagnostics = new DiagnosticsDialog(m_logSink, this);
        m_diagnostics->setAttribute(Qt::WA_DeleteOnClose, true);
    }
    m_diagnostics->setSystemReport(diag::querySystem(), QString::fromLatin1(qVersion()), m_previewPanel->surface()->backendName());
    if (m_hardware) m_diagnostics->setHardwareReport(*m_hardware);
    else m_diagnostics->setHardwarePending();
    if (!tab.isEmpty()) m_diagnostics->selectTab(tab);
    m_diagnostics->show();
    m_diagnostics->raise();
    m_diagnostics->activateWindow();
}

void MainWindow::notYetAvailable(const QString& feature) {
    LF_LOG_INFO(kLog, "action not yet implemented: {}", feature.toStdString());
    statusBar()->showMessage(tr("%1 arrives with the next milestone.").arg(feature), 3000);
}

// ---------------------------------------------------------------- placeholder transport

void MainWindow::togglePlayback() {
    auto* tl = m_timelinePanel->view();
    if (m_playing) {
        m_playing = false;
        m_playbackTimer.stop();
    } else {
        if (tl->playhead().ticks >= tl->sequenceDuration().ticks) tl->goToStart();
        m_playing = true;
        m_playbackOrigin = tl->playhead();
        m_playbackClock.start();
        // Tick at roughly half a frame so the playhead never skips a visible step.
        const double frameMs = 1000.0 / std::max(1.0, m_projectFps.toDouble());
        m_playbackTimer.start(std::max(4, static_cast<int>(frameMs / 2)));
    }
    m_previewPanel->setPlaying(m_playing);
}

void MainWindow::stopPlayback() {
    auto* tl = m_timelinePanel->view();
    if (m_playing) {
        m_playing = false;
        m_playbackTimer.stop();
        m_previewPanel->setPlaying(false);
        tl->setPlayhead(m_playbackOrigin);
    } else {
        tl->goToStart();
    }
}

void MainWindow::onPlaybackTick() {
    auto* tl = m_timelinePanel->view();
    const auto elapsed = Duration::fromMilliseconds(m_playbackClock.elapsed());
    const TimePoint t = m_playbackOrigin + elapsed;
    if (t.ticks >= tl->sequenceDuration().ticks) {
        tl->setPlayhead(TimePoint{tl->sequenceDuration().ticks});
        m_playing = false;
        m_playbackTimer.stop();
        m_previewPanel->setPlaying(false);
        return;
    }
    tl->setPlayhead(t);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Files from Explorer dropped anywhere on the window import into the bin.
    if (event->mimeData()->hasUrls() && !ImportController::collectMediaPaths(event->mimeData()->urls()).isEmpty()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QStringList paths = ImportController::collectMediaPaths(event->mimeData()->urls());
    if (paths.isEmpty()) return;
    event->acceptProposedAction();
    m_importer->importFiles(paths, this);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/mainSplitter"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("window/topSplitter"), m_topSplitter->saveState());
    QMainWindow::closeEvent(event);
}

}  // namespace lf::ui
