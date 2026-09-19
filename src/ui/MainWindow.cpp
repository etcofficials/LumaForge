#include "ui/MainWindow.h"

#include "lumaforge/core/Version.h"
#include "lumaforge/diagnostics/SystemInfo.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/hardware/HardwareDetector.h"
#include "lumaforge/playback/SequenceProgram.h"
#include "ui/ActionRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/dialogs/DiagnosticsDialog.h"
#include "ui/media/ImportController.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/playback/PlaybackController.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/preview/PreviewWidget.h"
#include "ui/timeline/TimelinePanel.h"
#include "ui/timeline/TimelineView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
constexpr const char* kLog = "ui";
}

MainWindow::MainWindow(std::shared_ptr<hw::GpuDevice> gpu, std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent)
    : QMainWindow(parent), m_gpu(std::move(gpu)), m_logSink(std::move(logSink)), m_actions(new ActionRegistry(this)),
      m_document(std::make_unique<project::Document>()), m_importer(new ImportController(*m_document, this)),
      m_playback(new PlaybackController(m_gpu, true, this)) {
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
    m_documentListener = m_document->addListener([this](const project::Change& c) { onDocumentChange(c); });
    updateUndoActions();
    updateWindowTitle();

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    if (settings.contains(QStringLiteral("window/mainSplitter"))) m_mainSplitter->restoreState(settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    if (settings.contains(QStringLiteral("window/topSplitter"))) m_topSplitter->restoreState(settings.value(QStringLiteral("window/topSplitter")).toByteArray());

    // The preview surface belongs to the playback engine's render thread.
    m_previewPanel->surface()->setEngine(m_playback->engine(), m_gpu);
    auto updatePreviewStatus = [this] {
        m_statusPreview->setText(tr("Preview: %1").arg(m_previewPanel->surface()->backendName()));
        m_statusPreview->setProperty("ok", m_previewPanel->surface()->usingHardware());
        m_statusPreview->style()->unpolish(m_statusPreview);
        m_statusPreview->style()->polish(m_statusPreview);
    };
    connect(m_previewPanel->surface(), &PreviewWidget::backendChanged, this, updatePreviewStatus);
    updatePreviewStatus();
    if (!m_playback->isAvailable()) LF_LOG_WARN(kLog, "playback unavailable: {}", m_playback->unavailableReason().toStdString());
    LF_LOG_INFO(kLog, "main window ready");
}

MainWindow::~MainWindow() {
    // Teardown order matters:
    //  1. The preview surface belongs to the engine's render thread; detach it
    //     before the playback controller (created earlier, so deleted earlier
    //     by ~QObject) takes the engine down.
    //  2. Panels observe m_document (a plain member, destroyed before the Qt
    //     children), so they must go while the document is still alive.
    //     setCentralWidget(nullptr) only deleteLater()s the old widget, which
    //     never runs once the event loop has finished - take and delete it.
    m_playback->pause();
    m_previewPanel->surface()->setEngine(nullptr, nullptr);
    m_document->removeListener(m_documentListener);
    delete m_diagnostics.data();
    delete takeCentralWidget();
    m_mediaPanel = nullptr; m_previewPanel = nullptr; m_inspectorPanel = nullptr; m_timelinePanel = nullptr;
    delete m_playback; m_playback = nullptr;
    delete m_importer; m_importer = nullptr;
    m_document.reset();
    LF_LOG_DEBUG(kLog, "main window shut down");
}

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
    m_previewPanel = new PreviewPanel(*m_actions, *m_playback, this);
    m_inspectorPanel = new InspectorPanel(this);
    m_timelinePanel = new TimelinePanel(*m_document, *m_actions, this);

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
    m_actions->connect(ActionId::ProjectNew, this, [this] { notYetAvailable(tr("New Project")); });
    m_actions->connect(ActionId::ProjectOpen, this, [this] { notYetAvailable(tr("Open Project")); });
    m_actions->connect(ActionId::ProjectSave, this, [this] { notYetAvailable(tr("Save")); });
    m_actions->connect(ActionId::ProjectSaveAs, this, [this] { notYetAvailable(tr("Save As")); });
    m_actions->connect(ActionId::ProjectSettings, this, [this] { notYetAvailable(tr("Project Settings")); });
    m_actions->connect(ActionId::Export, this, [this] { notYetAvailable(tr("Export")); });
    m_actions->connect(ActionId::AppQuit, this, [this] { close(); });

    // Media
    m_actions->connect(ActionId::MediaImport, this, [this] { m_importer->importWithDialog(this); });
    connect(m_importer, &ImportController::importStarted, this, [this](int n) { statusBar()->showMessage(tr("Importing %n file(s)…", nullptr, n)); });
    connect(m_importer, &ImportController::importFinished, this, [this](int ok, int failed) {
        statusBar()->showMessage(failed ? tr("Imported %1, %2 failed").arg(ok).arg(failed) : tr("Imported %n file(s)", nullptr, ok), 4000);
    });
    connect(m_mediaPanel, &MediaPanel::openRequested, this, [this](project::MediaId id) {
        if (const auto* item = m_document->project().findMedia(id)) m_playback->openSource(*item);
    });
    connect(m_mediaPanel, &MediaPanel::addToTimelineRequested, this, [this](const QList<project::MediaId>& ids) {
        m_timelinePanel->view()->addMedia(ids, m_timelinePanel->view()->playhead());
        showSequenceInPreview(m_timelinePanel->view()->playhead());
    });

    // Timeline <-> preview
    auto* tl = m_timelinePanel->view();
    connect(tl, &TimelineView::userSeeked, this, [this](TimePoint t, bool scrubbing) {
        if (m_playback->mode() != PlaybackController::Mode::Sequence) showSequenceInPreview(t);
        m_playback->seek(t, scrubbing);
    });
    connect(tl, &TimelineView::selectionChanged, this, [this] {
        if (m_playback->mode() == PlaybackController::Mode::Source && m_timelinePanel->view()->hasSelection()) showSequenceInPreview(m_timelinePanel->view()->playhead());
    });
    connect(tl, &TimelineView::externalFilesDropped, this, [this](const QStringList& paths, TimePoint at) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_importer, &ImportController::mediaImported, this, [this, at, conn](const QList<project::MediaId>& ids) {
            QObject::disconnect(*conn);
            m_timelinePanel->view()->addMedia(ids, at);
            showSequenceInPreview(at);
        });
        m_importer->importFiles(paths, this);
    });
    connect(m_playback, &PlaybackController::positionChanged, this, [this](TimePoint t) {
        if (m_playback->mode() == PlaybackController::Mode::Sequence) m_timelinePanel->view()->setPlayhead(t);
    });
    connect(m_playback, &PlaybackController::programChanged, this, [this] {
        if (m_playback->mode() == PlaybackController::Mode::Sequence) m_timelinePanel->view()->setPlayhead(m_playback->position());
    });

    // Edit
    m_actions->connect(ActionId::EditUndo, this, [this] { m_document->undoStack().undo(); });
    m_actions->connect(ActionId::EditRedo, this, [this] { m_document->undoStack().redo(); });

    // Transport -> playback engine
    m_actions->connect(ActionId::PlayPause, this, [this] { m_playback->togglePlay(); });
    m_actions->connect(ActionId::PlayStop, this, [this] { m_playback->stop(); });
    m_actions->connect(ActionId::StepBack, this, [this] { m_playback->stepFrames(-1); });
    m_actions->connect(ActionId::StepForward, this, [this] { m_playback->stepFrames(1); });
    m_actions->connect(ActionId::GoToStart, this, [this] { m_playback->goToStart(); });
    m_actions->connect(ActionId::GoToEnd, this, [this] { m_playback->goToEnd(); });
    connect(m_playback, &PlaybackController::statsChanged, this, [this](const playback::PlaybackStats& s) {
        m_statusFps->setText(m_playback->isPlaying() ? tr("%1 fps").arg(s.previewFps, 0, 'f', 1) : tr("— fps"));
    });
    connect(m_playback, &PlaybackController::playingChanged, this, [this](bool p) { if (!p) m_statusFps->setText(tr("— fps")); });
    connect(m_playback, &PlaybackController::errorOccurred, this, [this](const QString& m) { statusBar()->showMessage(tr("Playback: %1").arg(m), 6000); });

    m_actions->connect(ActionId::ViewDiagnostics, this, [this] { showDiagnostics(); });
    m_actions->connect(ActionId::HelpAbout, this, [this] {
        QMessageBox::about(this, tr("About LumaForge"),
                           tr("<b>LumaForge %1</b><br>Lightweight professional video editor.<br><br>"
                              "Built with Qt %2 and FFmpeg.<br>Build: %3")
                               .arg(QString::fromLatin1(version::string), QString::fromLatin1(qVersion()), QString::fromLatin1(version::buildType)));
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

void MainWindow::onDocumentChange(const project::Change& change) {
    switch (change.kind) {
        case project::Change::Kind::Reset:
        case project::Change::Kind::Settings:
        case project::Change::Kind::TracksChanged:
        case project::Change::Kind::ClipsChanged:
        case project::Change::Kind::MediaRemoved:
        case project::Change::Kind::MediaChanged:
            scheduleSequenceRefresh();
            break;
        default:
            break;
    }
    if (change.kind == project::Change::Kind::Reset || change.kind == project::Change::Kind::Settings) updateWindowTitle();
}

void MainWindow::scheduleSequenceRefresh() {
    // One edit can produce several change notifications; rebuild once per event-loop turn.
    if (m_sequenceRefreshPending) return;
    m_sequenceRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_sequenceRefreshPending = false;
        refreshSequenceProgram();
    });
}

void MainWindow::refreshSequenceProgram() {
    const auto& project = m_document->project();
    m_sequenceProgram = project.sequence.clips.empty() ? nullptr : std::make_shared<playback::SequenceProgram>(project);
    // Only the timeline view of the preview follows edits; a source preview stays put.
    if (m_playback->mode() == PlaybackController::Mode::Source) return;
    const TimePoint keep = m_timelinePanel->view()->playhead();
    if (m_sequenceProgram) m_playback->setProgram(m_sequenceProgram, PlaybackController::Mode::Sequence, tr("Timeline"), keep);
    else if (m_playback->mode() == PlaybackController::Mode::Sequence) m_playback->clearProgram();
}

void MainWindow::showSequenceInPreview(TimePoint at) {
    if (m_playback->mode() == PlaybackController::Mode::Sequence) return;
    if (!m_sequenceProgram && !m_document->project().sequence.clips.empty()) m_sequenceProgram = std::make_shared<playback::SequenceProgram>(m_document->project());
    if (!m_sequenceProgram) { m_playback->clearProgram(); return; }
    m_playback->setProgram(m_sequenceProgram, PlaybackController::Mode::Sequence, tr("Timeline"), at);
}

void MainWindow::updateWindowTitle() {
    const QString name = QString::fromStdString(m_document->project().settings.name);
    setWindowTitle(QStringLiteral("%1%2 — LumaForge").arg(name.isEmpty() ? tr("Untitled") : name, m_document->isDirty() ? QStringLiteral("*") : QString()));
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

void MainWindow::importFiles(const QStringList& paths, bool addToTimeline) {
    if (addToTimeline) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_importer, &ImportController::mediaImported, this, [this, conn](const QList<project::MediaId>& ids) {
            QObject::disconnect(*conn);
            m_timelinePanel->view()->addMedia(ids, TimePoint{-1});
            showSequenceInPreview(TimePoint::zero());
        });
    }
    m_importer->importFiles(paths, this);
}

void MainWindow::openSourceFile(const QString& path, bool autoplay) {
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    auto openIt = [this, autoplay](const project::MediaItem& item) {
        m_playback->openSource(item);
        if (autoplay) m_playback->play();
    };
    if (const auto* existing = m_document->project().findMediaByPath(native.toStdString())) {
        openIt(*existing);
        return;
    }
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_importer, &ImportController::mediaImported, this, [this, native, openIt, conn](const QList<project::MediaId>& ids) {
        for (auto id : ids) {
            const auto* item = m_document->project().findMedia(id);
            if (item && QString::fromStdString(item->path).compare(native, Qt::CaseInsensitive) == 0) {
                QObject::disconnect(*conn);
                openIt(*item);
                return;
            }
        }
    });
    m_importer->importFiles({native}, this);
}

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
    // Stop presentation before Qt tears the native window down.
    m_playback->pause();
    QMainWindow::closeEvent(event);
}

}  // namespace lf::ui
