#include "ui/MainWindow.h"

#include "lumaforge/core/StringUtil.h"
#include "lumaforge/core/Version.h"
#include "lumaforge/diagnostics/SystemInfo.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/hardware/HardwareDetector.h"
#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/MediaCommands.h"
#include "ui/ActionRegistry.h"
#include "ui/AppPaths.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"
#include "ui/dialogs/DiagnosticsDialog.h"
#include "ui/dialogs/ProjectSettingsDialog.h"
#include "ui/home/HomeScreen.h"
#include "ui/media/ImportController.h"
#include "ui/panels/BrowserPanel.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ProjectInfoPanel.h"
#include "ui/playback/PlaybackController.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/preview/PreviewWidget.h"
#include "ui/project/ProjectController.h"
#include "ui/timeline/TimelinePanel.h"
#include "ui/timeline/TimelineView.h"
#include "ui/widgets/PanelHeader.h"
#include "ui/widgets/ToastHost.h"
#include "ui/workspace/WorkspaceManager.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <windows.h>
#include <psapi.h>

namespace lf::ui {

namespace {
constexpr const char* kLog = "ui";

void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}
}  // namespace

MainWindow::MainWindow(std::shared_ptr<hw::GpuDevice> gpu, std::shared_ptr<log::RingBufferSink> logSink, QWidget* parent)
    : QMainWindow(parent), m_gpu(std::move(gpu)), m_logSink(std::move(logSink)), m_actions(new ActionRegistry(this)),
      m_document(std::make_unique<project::Document>()), m_importer(new ImportController(*m_document, this)),
      m_playback(new PlaybackController(m_gpu, true, this)), m_project(new ProjectController(*m_document, this)) {
    setAcceptDrops(true);
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    setMinimumSize(1024, 640);
    resize(1440, 860);
    setDocumentMode(true);

    m_pages = new QStackedWidget(this);
    setCentralWidget(m_pages);
    m_home = new HomeScreen(m_pages);
    m_pages->addWidget(m_home);
    // The editor is a nested main window: its docks are what workspaces arrange.
    m_editor = new QMainWindow(nullptr, Qt::Widget);
    m_editor->setObjectName(QStringLiteral("editor"));
    m_editor->setDockNestingEnabled(true);
    m_editor->setAnimated(false);   // dock animations cost frames on the target hardware
    m_editor->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    m_editor->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    m_pages->addWidget(m_editor);
    m_toasts = new ToastHost(this);

    buildMenus();
    buildToolBar();
    buildWorkspaceBar();
    buildPanels();
    buildStatusBar();
    connectActions();

    m_document->undoStack().setChangeListener([this] { updateUndoActions(); updateWindowTitle(); });
    m_documentListener = m_document->addListener([this](const project::Change& c) { onDocumentChange(c); });
    updateUndoActions();
    updateWindowTitle();
    updateStatusProject();

    restoreGeometry(QSettings().value(QStringLiteral("window/geometry")).toByteArray());

    // The preview surface belongs to the playback engine's render thread.
    m_previewPanel->surface()->setEngine(m_playback->engine(), m_gpu);
    auto updatePreviewStatus = [this] {
        m_statusHardware->setProperty("ok", m_previewPanel->surface()->usingHardware());
        repolish(m_statusHardware);
    };
    connect(m_previewPanel->surface(), &PreviewWidget::backendChanged, this, updatePreviewStatus);
    updatePreviewStatus();
    if (!m_playback->isAvailable()) LF_LOG_WARN(kLog, "playback unavailable: {}", m_playback->unavailableReason().toStdString());

    showHome();
    LF_LOG_INFO(kLog, "main window ready (data folder {})", AppPaths::dataRoot().toStdString());
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
    delete m_workspaces;
    m_workspaces = nullptr;
    delete takeCentralWidget();
    m_mediaPanel = nullptr; m_previewPanel = nullptr; m_inspectorPanel = nullptr; m_timelinePanel = nullptr;
    m_browser = nullptr; m_projectInfo = nullptr; m_editor = nullptr; m_home = nullptr;
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
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::ProjectSave));
    file->addAction(m_actions->action(ActionId::ProjectSaveAs));
    file->addAction(m_actions->action(ActionId::ProjectSettings));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::MediaImport));
    file->addAction(m_actions->action(ActionId::Export));
    file->addSeparator();
    file->addAction(m_actions->action(ActionId::ProjectClose));
    file->addAction(m_actions->action(ActionId::AppQuit));

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actions->action(ActionId::EditUndo));
    edit->addAction(m_actions->action(ActionId::EditRedo));
    edit->addSeparator();
    edit->addAction(m_actions->action(ActionId::EditCopy));
    edit->addAction(m_actions->action(ActionId::EditPaste));
    edit->addAction(m_actions->action(ActionId::EditDuplicate));
    edit->addSeparator();
    edit->addAction(m_actions->action(ActionId::ClipSplit));
    edit->addAction(m_actions->action(ActionId::ClipDelete));
    edit->addAction(m_actions->action(ActionId::ClipRippleDelete));
    edit->addSeparator();
    edit->addAction(m_actions->action(ActionId::ClipSelectAll));
    edit->addSeparator();
    edit->addAction(m_actions->action(ActionId::ToolSelect));
    edit->addAction(m_actions->action(ActionId::ToolBlade));

    auto* view = menuBar()->addMenu(tr("&View"));
    auto* workspaceSub = view->addMenu(tr("&Workspace"));
    for (auto id : {ActionId::WorkspaceEdit, ActionId::WorkspaceColor, ActionId::WorkspaceAudio, ActionId::WorkspaceText, ActionId::WorkspaceEffects, ActionId::WorkspaceExport})
        workspaceSub->addAction(m_actions->action(id));
    workspaceSub->addSeparator();
    workspaceSub->addAction(m_actions->action(ActionId::WorkspaceReset));
    m_panelsMenu = view->addMenu(tr("&Panels"));
    view->addSeparator();
    view->addAction(m_actions->action(ActionId::TimelineZoomIn));
    view->addAction(m_actions->action(ActionId::TimelineZoomOut));
    view->addAction(m_actions->action(ActionId::TimelineZoomFit));
    view->addAction(m_actions->action(ActionId::TimelineSnap));
    view->addSeparator();
    view->addAction(m_actions->action(ActionId::ViewDiagnostics));

    auto* playback = menuBar()->addMenu(tr("&Playback"));
    playback->addAction(m_actions->action(ActionId::PlayPause));
    playback->addAction(m_actions->action(ActionId::PlayStop));
    playback->addSeparator();
    playback->addAction(m_actions->action(ActionId::StepBack));
    playback->addAction(m_actions->action(ActionId::StepForward));
    playback->addAction(m_actions->action(ActionId::GoToStart));
    playback->addAction(m_actions->action(ActionId::GoToEnd));

    auto* timeline = menuBar()->addMenu(tr("&Timeline"));
    timeline->addAction(m_actions->action(ActionId::TimelineAddText));

    auto* workspace = menuBar()->addMenu(tr("&Workspace"));
    for (auto id : {ActionId::WorkspaceEdit, ActionId::WorkspaceColor, ActionId::WorkspaceAudio, ActionId::WorkspaceText, ActionId::WorkspaceEffects, ActionId::WorkspaceExport})
        workspace->addAction(m_actions->action(id));
    workspace->addSeparator();
    workspace->addAction(m_actions->action(ActionId::WorkspaceReset));

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actions->action(ActionId::ViewDiagnostics));
    help->addSeparator();
    help->addAction(m_actions->action(ActionId::HelpAbout));
}

void MainWindow::buildToolBar() {
    auto* tb = m_editor->addToolBar(tr("Main Toolbar"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(16, 16));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    tb->addAction(m_actions->action(ActionId::ProjectNew));
    tb->addAction(m_actions->action(ActionId::ProjectOpen));
    tb->addAction(m_actions->action(ActionId::ProjectSave));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::MediaImport));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::EditUndo));
    tb->addAction(m_actions->action(ActionId::EditRedo));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::ToolSelect));
    tb->addAction(m_actions->action(ActionId::ToolBlade));
    tb->addAction(m_actions->action(ActionId::TimelineAddText));
    tb->addSeparator();
    tb->addAction(m_actions->action(ActionId::ClipSplit));
    tb->addAction(m_actions->action(ActionId::ClipDelete));

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    auto* exportButton = new QToolButton(tb);
    exportButton->setDefaultAction(m_actions->action(ActionId::Export));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setProperty("primary", true);
    exportButton->setIcon(Icons::icon(QStringLiteral("export"), Theme::palette().accentText, Theme::palette().textDisabled));
    tb->addWidget(exportButton);
}

void MainWindow::buildWorkspaceBar() {
    auto* bar = new QToolBar(tr("Workspaces"), m_editor);
    bar->setObjectName(QStringLiteral("workspaceBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    m_editor->addToolBarBreak();
    m_editor->addToolBar(bar);
    m_workspaceBar = new QTabBar(bar);
    m_workspaceBar->setObjectName(QStringLiteral("workspaceTabs"));
    m_workspaceBar->setDrawBase(false);
    m_workspaceBar->setExpanding(false);
    m_workspaceBar->setFocusPolicy(Qt::NoFocus);
    bar->addWidget(m_workspaceBar);
    m_workspaces = new WorkspaceManager(m_editor, m_workspaceBar, this);
    connect(m_workspaces, &WorkspaceManager::workspaceChanged, this, &MainWindow::onWorkspaceChanged);
}

QDockWidget* MainWindow::makeDock(const QString& title, const QString& objectName, QWidget* content, QWidget* titleBar) {
    auto* dock = new QDockWidget(title, m_editor);
    dock->setObjectName(objectName);
    dock->setWidget(content);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
    if (titleBar) dock->setTitleBarWidget(titleBar);
    m_panelsMenu->addAction(dock->toggleViewAction());
    return dock;
}

void MainWindow::buildPanels() {
    m_mediaPanel = new MediaPanel(*m_document, *m_actions, *m_importer, this);
    m_previewPanel = new PreviewPanel(*m_actions, *m_playback, this);
    m_inspectorPanel = new InspectorPanel(*m_document, this);
    m_timelinePanel = new TimelinePanel(*m_document, *m_actions, this);
    m_projectInfo = new ProjectInfoPanel(*m_document, *m_actions, this);

    m_browser = new BrowserPanel(this);
    m_browser->addPage(m_mediaPanel, tr("Media"), QStringLiteral("media"), tr("Imported media and bins"));
    m_browser->addPage(m_projectInfo, tr("Project"), QStringLiteral("project"), tr("Project information and data locations"));

    m_editor->setCentralWidget(m_previewPanel);

    // The browser's tabs are its header; an empty title widget keeps it compact.
    auto* browserDock = makeDock(tr("Browser"), QStringLiteral("browserDock"), m_browser, new QWidget());
    auto* inspectorDock = makeDock(tr("Inspector"), QStringLiteral("inspectorDock"), m_inspectorPanel, m_inspectorPanel->header());
    auto* timelineDock = makeDock(tr("Timeline"), QStringLiteral("timelineDock"), m_timelinePanel, m_timelinePanel->header());
    m_workspaces->registerDock(DockId::Browser, browserDock);
    m_workspaces->registerDock(DockId::Inspector, inspectorDock);
    m_workspaces->registerDock(DockId::Timeline, timelineDock);
    m_workspaces->setAvailable(Workspace::Edit, true);
    m_workspaces->setAvailable(Workspace::Color, true);
    m_workspaces->setAvailable(Workspace::Audio, true);
}

void MainWindow::buildStatusBar() {
    auto* sb = statusBar();
    sb->setSizeGripEnabled(true);
    auto make = [&](const QString& tip) {
        auto* l = new QLabel(sb);
        l->setToolTip(tip);
        l->setProperty("statusField", true);
        sb->addPermanentWidget(l);
        return l;
    };
    m_statusProject = make(tr("Sequence frame size and frame rate"));
    m_statusTimecode = make(tr("Playhead position"));
    m_statusTimecode->setProperty("timecode", true);
    m_statusPlayback = make(tr("Preview frame rate and dropped frames while playing"));
    m_statusDecoder = make(tr("Video decoder used for the current frame"));
    m_statusHardware = make(tr("Detecting hardware…"));
    m_statusHardware->setText(tr("Detecting hardware…"));
    m_statusMemory = make(tr("Memory used by LumaForge (working set)"));
    m_memoryTimer = new QTimer(this);
    m_memoryTimer->setInterval(2000);
    connect(m_memoryTimer, &QTimer::timeout, this, &MainWindow::sampleMemory);
    m_memoryTimer->start();
    sampleMemory();
    m_statusPlayback->setText(tr("— fps"));
    m_statusDecoder->setText(tr("Decoder —"));
}

void MainWindow::connectActions() {
    m_actions->connect(ActionId::ProjectNew, this, [this] { m_playback->pause(); if (m_project->newProject(this)) showEditor(); });
    m_actions->connect(ActionId::ProjectOpen, this, [this] { m_playback->pause(); m_project->open(this); });
    m_actions->connect(ActionId::ProjectSave, this, [this] { if (m_project->save(this)) m_toasts->show(tr("Project saved"), ToastHost::Kind::Success); });
    m_actions->connect(ActionId::ProjectSaveAs, this, [this] { if (m_project->saveAs(this)) m_toasts->show(tr("Project saved"), ToastHost::Kind::Success); });
    m_actions->connect(ActionId::ProjectSettings, this, [this] { showProjectSettings(); });
    m_actions->connect(ActionId::ProjectClose, this, [this] { closeProject(); });
    m_actions->connect(ActionId::AppQuit, this, [this] { close(); });
    connect(m_project, &ProjectController::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 4000); });
    connect(m_project, &ProjectController::recentFilesChanged, this, &MainWindow::rebuildRecentMenu);
    connect(m_project, &ProjectController::projectLoaded, this, [this](const QString&) {
        // A fresh document: nothing selected, playhead at zero, preview on the timeline.
        m_timelinePanel->view()->clearSelection();
        m_timelinePanel->view()->setPlayhead(TimePoint::zero());
        m_timelinePanel->view()->zoomToFit();
        m_playback->clearProgram();
        refreshSequenceProgram();
        updateWindowTitle();
        showEditor();
    });
    connect(m_project, &ProjectController::projectSaved, this, [this](const QString&) { updateWindowTitle(); });
    rebuildRecentMenu();

    // Workspaces
    const std::pair<ActionId, Workspace> workspaceActions[] = {
        {ActionId::WorkspaceEdit, Workspace::Edit}, {ActionId::WorkspaceColor, Workspace::Color}, {ActionId::WorkspaceAudio, Workspace::Audio},
        {ActionId::WorkspaceText, Workspace::Text}, {ActionId::WorkspaceEffects, Workspace::Effects}, {ActionId::WorkspaceExport, Workspace::Export}};
    for (const auto& [id, ws] : workspaceActions) {
        const Workspace w = ws;
        m_actions->connect(id, this, [this, w] { showEditor(); m_workspaces->switchTo(w); });
        m_actions->setEnabled(id, m_workspaces->isAvailable(w));
    }
    m_actions->connect(ActionId::WorkspaceReset, this, [this] { m_workspaces->resetCurrent(); });

    // Home screen
    connect(m_home, &HomeScreen::newProjectRequested, this, &MainWindow::newProjectFromHome);
    connect(m_home, &HomeScreen::openProjectRequested, m_actions->action(ActionId::ProjectOpen), &QAction::trigger);
    connect(m_home, &HomeScreen::openRecentRequested, this, [this](const QString& path) {
        if (!QFileInfo::exists(path)) {
            m_toasts->show(tr("%1 can no longer be found").arg(QFileInfo(path).fileName()), ToastHost::Kind::Warning);
            return;
        }
        openProjectFile(path);
    });
    connect(m_home, &HomeScreen::importMediaRequested, this, [this] {
        showEditor();
        m_importer->importWithDialog(this);
    });
    connect(m_home, &HomeScreen::clearRecentRequested, m_project, &ProjectController::clearRecentFiles);

    // Media
    m_actions->connect(ActionId::MediaImport, this, [this] { showEditor(); m_importer->importWithDialog(this); });
    connect(m_importer, &ImportController::importStarted, this, [this](int n) { statusBar()->showMessage(tr("Importing %n file(s)…", nullptr, n)); });
    connect(m_importer, &ImportController::importFinished, this, [this](int ok, int failed) {
        statusBar()->clearMessage();
        if (failed) m_toasts->show(tr("Imported %1, %2 failed").arg(ok).arg(failed), ToastHost::Kind::Warning);
        else if (ok) m_toasts->show(tr("Imported %n file(s)", nullptr, ok), ToastHost::Kind::Success);
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
    tl->setThumbnailProvider([this](project::MediaId id) { return m_mediaPanel->thumbnail(id); });
    connect(m_mediaPanel, &MediaPanel::thumbnailsChanged, tl, &TimelineView::thumbnailsChanged);
    connect(tl, &TimelineView::userSeeked, this, [this](TimePoint t, bool scrubbing) {
        if (m_playback->mode() != PlaybackController::Mode::Sequence) showSequenceInPreview(t);
        m_playback->seek(t, scrubbing);
    });
    connect(tl, &TimelineView::selectionChanged, this, [this] {
        m_inspectorPanel->setSelection(m_timelinePanel->view()->selection());
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
    connect(tl, &TimelineView::playheadChanged, this, [this](TimePoint t) {
        m_statusTimecode->setText(formatTimecode(t, m_document->project().settings.frameRate));
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
    connect(m_playback, &PlaybackController::statsChanged, this, &MainWindow::updateStatusStats);
    connect(m_playback, &PlaybackController::playingChanged, this, [this](bool p) { if (!p) m_statusPlayback->setText(tr("— fps")); });
    connect(m_playback, &PlaybackController::errorOccurred, this, [this](const QString& m) { m_toasts->show(tr("Playback: %1").arg(m), ToastHost::Kind::Error, 6000); });

    m_actions->connect(ActionId::ViewDiagnostics, this, [this] { showDiagnostics(); });
    m_actions->connect(ActionId::HelpAbout, this, [this] {
        QMessageBox::about(this, tr("About LumaForge"),
                           tr("<b>LumaForge %1</b><br>Lightweight professional video editor.<br><br>"
                              "Built with Qt %2 and FFmpeg.<br>Build: %3<br>Data folder: %4")
                               .arg(QString::fromLatin1(version::string), QString::fromLatin1(qVersion()), QString::fromLatin1(version::buildType),
                                    AppPaths::dataRoot().toHtmlEscaped()));
    });
}

// ---------------------------------------------------------------- pages

void MainWindow::showHome() {
    m_playback->pause();
    m_home->setRecentProjects(m_project->recentFiles());
    m_pages->setCurrentWidget(m_home);
    updateWindowTitle();
}

void MainWindow::showEditor() {
    if (m_pages->currentWidget() == m_editor) return;
    m_pages->setCurrentWidget(m_editor);
    updateWindowTitle();
    if (!m_layoutRestored) {
        // Dock sizes are relative to the editor's real size: lay out once it is visible.
        m_layoutRestored = true;
        QTimer::singleShot(0, this, [this] {
            m_workspaces->restoreSession();
            QTimer::singleShot(0, m_timelinePanel->view(), &TimelineView::zoomToFit);
        });
    }
}

bool MainWindow::editorVisible() const { return m_pages->currentWidget() == m_editor; }

void MainWindow::newProjectFromHome() {
    project::ProjectSettings defaults;
    ProjectSettingsDialog dialog(defaults, this);
    dialog.setWindowTitle(tr("New Project"));
    if (dialog.exec() != QDialog::Accepted) return;
    if (!m_project->newProject(this)) return;
    // Settings chosen before the project existed are part of its initial state, not an undoable edit.
    m_document->project().settings = dialog.settings();
    m_document->notify(project::Change{project::Change::Kind::Settings, {}, {}});
    showEditor();
}

void MainWindow::closeProject() {
    m_playback->pause();
    if (!m_project->maybeSave(this)) return;
    m_document->setFilePath({});
    m_document->reset(project::Project{});
    m_playback->clearProgram();
    showHome();
}

void MainWindow::onWorkspaceChanged(Workspace w) {
    const std::pair<ActionId, Workspace> map[] = {
        {ActionId::WorkspaceEdit, Workspace::Edit}, {ActionId::WorkspaceColor, Workspace::Color}, {ActionId::WorkspaceAudio, Workspace::Audio},
        {ActionId::WorkspaceText, Workspace::Text}, {ActionId::WorkspaceEffects, Workspace::Effects}, {ActionId::WorkspaceExport, Workspace::Export}};
    for (const auto& [id, ws] : map) if (ws == w) m_actions->action(id)->setChecked(true);
    switch (w) {
        case Workspace::Edit: m_browser->showPage(QStringLiteral("media")); break;
        default: break;
    }
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
    if (change.kind == project::Change::Kind::Reset || change.kind == project::Change::Kind::Settings) {
        updateWindowTitle();
        updateStatusProject();
    }
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
    if (!editorVisible()) {
        setWindowTitle(QStringLiteral("LumaForge"));
        setWindowFilePath(QString());
        setWindowModified(false);
        return;
    }
    const QString name = m_project->displayName();
    QString title = QStringLiteral("%1%2 — LumaForge").arg(name, m_document->isDirty() ? QStringLiteral("*") : QString());
    setWindowFilePath(QString::fromStdString(m_document->filePath()));
    setWindowModified(m_document->isDirty());
    setWindowTitle(title);   // setWindowFilePath resets the title; ours wins
}

void MainWindow::updateStatusProject() {
    const auto& s = m_document->project().settings;
    m_statusProject->setText(tr("%1×%2 · %3 fps").arg(s.width).arg(s.height).arg(s.frameRate.toDouble(), 0, 'f', s.frameRate.den == 1 ? 0 : 3));
    m_statusTimecode->setText(formatTimecode(m_timelinePanel ? m_timelinePanel->view()->playhead() : TimePoint::zero(), s.frameRate));
}

void MainWindow::updateStatusStats(const playback::PlaybackStats& s) {
    if (m_playback->isPlaying()) {
        QString text = tr("%1 fps").arg(s.previewFps, 0, 'f', 1);
        if (s.droppedFrames) text += tr(" · %1 dropped").arg(s.droppedFrames);
        m_statusPlayback->setText(text);
    }
    m_statusDecoder->setText(tr("Decoder %1").arg(s.decoder.empty() || s.decoder == "-" ? QStringLiteral("—") : QString::fromStdString(s.decoder)));
}

void MainWindow::sampleMemory() {
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
        m_statusMemory->setText(tr("RAM %1").arg(QString::fromStdString(formatBytes(pmc.WorkingSetSize))));
    }
}

void MainWindow::rebuildRecentMenu() {
    m_recentMenu->clear();
    const QStringList recent = m_project->recentFiles();
    for (int i = 0; i < recent.size(); ++i) {
        const QString& path = recent[i];
        auto* a = m_recentMenu->addAction(QStringLiteral("&%1  %2").arg(i + 1).arg(QFileInfo(path).fileName()));
        a->setToolTip(path);
        a->setStatusTip(path);
        connect(a, &QAction::triggered, this, [this, path] { openProjectFile(path); });
    }
    if (!recent.isEmpty()) {
        m_recentMenu->addSeparator();
        auto* clear = m_recentMenu->addAction(tr("&Clear List"));
        connect(clear, &QAction::triggered, m_project, &ProjectController::clearRecentFiles);
    }
    m_recentMenu->setEnabled(!recent.isEmpty());
    if (m_home && !editorVisible()) m_home->setRecentProjects(recent);
}

void MainWindow::showProjectSettings() {
    ProjectSettingsDialog dialog(m_document->project().settings, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto next = dialog.settings();
    if (next == m_document->project().settings) return;
    m_document->push(std::make_unique<project::ProjectSettingsCommand>(*m_document, next));
}

void MainWindow::openProjectFile(const QString& path) {
    m_playback->pause();
    if (m_project->maybeSave(this)) m_project->openFile(path, this);
}

void MainWindow::setHardwareReport(hw::HwReport report) {
    m_hardware = std::move(report);
    const auto& r = *m_hardware;
    QStringList parts;
    parts << (m_previewPanel->surface()->usingHardware() ? (m_gpu && m_gpu->isWarp() ? tr("WARP (CPU)") : tr("D3D11")) : tr("Software preview"));
    parts << (r.supportsDecoder("H.264") ? tr("D3D11VA") : tr("CPU decode"));
    if (const auto* hwEnc = r.firstAvailableEncoder(hw::EncoderKind::Hardware)) parts << QString::fromStdString(hwEnc->name);
    else parts << tr("CPU encode");
    m_statusHardware->setText(parts.join(QStringLiteral(" · ")));
    QString tip;
    if (const auto* a = r.activeAdapter()) tip = QString::fromStdString(a->name) + QLatin1Char('\n');
    tip += QString::fromStdString(hw::HardwareDetector::summary(r));
    m_statusHardware->setToolTip(tip);
    if (m_diagnostics) m_diagnostics->setHardwareReport(r);
}

void MainWindow::openDiagnostics(const QString& tab) { showDiagnostics(tab); }

void MainWindow::importFiles(const QStringList& paths, bool addToTimeline) {
    showEditor();
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
    showEditor();
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

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Files from Explorer dropped anywhere on the window import into the bin;
    // a project file opens.
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& u : event->mimeData()->urls()) if (u.isLocalFile() && ProjectController::isProjectFile(u.toLocalFile())) { event->acceptProposedAction(); return; }
    if (!ImportController::collectMediaPaths(event->mimeData()->urls()).isEmpty()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.isLocalFile() && ProjectController::isProjectFile(u.toLocalFile())) {
            event->acceptProposedAction();
            openProjectFile(u.toLocalFile());
            return;
        }
    }
    const QStringList paths = ImportController::collectMediaPaths(event->mimeData()->urls());
    if (paths.isEmpty()) return;
    event->acceptProposedAction();
    showEditor();
    m_importer->importFiles(paths, this);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    m_playback->pause();
    if (!m_project->maybeSave(this)) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (m_layoutRestored) m_workspaces->saveSession();
    // Stop presentation before Qt tears the native window down.
    m_playback->pause();
    QMainWindow::closeEvent(event);
}

}  // namespace lf::ui
