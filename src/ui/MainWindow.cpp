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
#include "lumaforge/project/TimelineOps.h"
#include "lumaforge/project/Subtitles.h"
#include "ui/dialogs/CommandPalette.h"
#include "ui/dialogs/DiagnosticsDialog.h"
#include "ui/dialogs/ShortcutEditor.h"
#include "ui/dialogs/VoiceOverDialog.h"
#include "ui/dialogs/ProjectSettingsDialog.h"
#include "ui/home/HomeScreen.h"
#include "ui/media/ImportController.h"
#include "ui/media/ProxyController.h"
#include "ui/panels/BrowserPanel.h"
#include "ui/panels/EffectsBrowserPanel.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/MediaPanel.h"
#include "ui/panels/ProjectInfoPanel.h"
#include "ui/panels/TitlesBrowserPanel.h"
#include "ui/panels/TransitionsBrowserPanel.h"
#include "ui/panels/MarkersPanel.h"
#include "ui/panels/MixerPanel.h"
#include "ui/panels/ScopesPanel.h"
#include "ui/panels/PerformancePanel.h"
#include "ui/export/ExportController.h"
#include "ui/export/ExportQueuePanel.h"
#include "ui/export/ExportSettingsPanel.h"
#include "ui/playback/PlaybackController.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/preview/PreviewWidget.h"
#include "ui/project/AutosaveManager.h"
#include "ui/project/ProjectController.h"
#include "ui/timeline/TimelinePanel.h"
#include "ui/timeline/TimelineView.h"
#include "ui/widgets/PanelHeader.h"
#include "ui/widgets/ToastHost.h"
#include "ui/workspace/WorkspaceManager.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
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

    // Startup cost per stage goes to the log (the window should appear in a few seconds on the target PC).
    QElapsedTimer stage;
    stage.start();
    auto mark = [&stage](const char* what) { LF_LOG_DEBUG(kLog, "startup: {} {} ms", what, stage.restart()); };
    mark("pages");
    buildMenus();
    mark("menus");
    buildToolBar();
    mark("toolbar");
    buildWorkspaceBar();
    buildPanels();
    mark("panels");
    buildStatusBar();
    connectActions();
    mark("status + actions");
    // Every action also lives on the window itself, so its shortcut works whatever
    // is visible (e.g. the menu bar is hidden in the full-screen viewer).
    for (ActionId id : m_actions->allIds()) addAction(m_actions->action(id));

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
    // Helpers that observe the document or run workers go before it.
    delete m_autosave; m_autosave = nullptr;
    delete m_exportController; m_exportController = nullptr;
    delete m_proxies; m_proxies = nullptr;
    m_document.reset();
    LF_LOG_DEBUG(kLog, "main window shut down");
}

// ---------------------------------------------------------------- construction

void MainWindow::buildMenus() {
    auto add = [this](QMenu* m, std::initializer_list<ActionId> ids) {
        for (auto id : ids) m->addAction(m_actions->action(id));
    };
    auto* file = menuBar()->addMenu(tr("&File"));
    add(file, {ActionId::ProjectNew, ActionId::ProjectOpen});
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addSeparator();
    add(file, {ActionId::ProjectSave, ActionId::ProjectSaveAs, ActionId::ProjectSettings});
    file->addSeparator();
    add(file, {ActionId::MediaImport, ActionId::MediaImportFolder});
    file->addSeparator();
    add(file, {ActionId::Export, ActionId::ExportFrame});
    file->addSeparator();
    add(file, {ActionId::ProjectClose, ActionId::AppQuit});

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    add(edit, {ActionId::EditUndo, ActionId::EditRedo});
    edit->addSeparator();
    add(edit, {ActionId::EditCut, ActionId::EditCopy, ActionId::EditPaste, ActionId::EditPasteOverwrite, ActionId::EditDuplicate});
    edit->addSeparator();
    add(edit, {ActionId::ClipDelete, ActionId::ClipRippleDelete});
    edit->addSeparator();
    add(edit, {ActionId::ClipSelectAll, ActionId::ClipDeselect});
    edit->addSeparator();
    add(edit, {ActionId::CommandPalette});

    auto* view = menuBar()->addMenu(tr("&View"));
    auto* workspaceSub = view->addMenu(tr("&Workspace"));
    add(workspaceSub, {ActionId::WorkspaceEdit, ActionId::WorkspaceColor, ActionId::WorkspaceAudio, ActionId::WorkspaceText, ActionId::WorkspaceEffects, ActionId::WorkspaceExport});
    workspaceSub->addSeparator();
    add(workspaceSub, {ActionId::WorkspaceReset});
    m_panelsMenu = view->addMenu(tr("&Panels"));
    view->addSeparator();
    add(view, {ActionId::ViewerFullscreen});
    auto* zoom = view->addMenu(tr("Viewer &Zoom"));
    add(zoom, {ActionId::ViewerZoomFit, ActionId::ViewerZoom25, ActionId::ViewerZoom50, ActionId::ViewerZoom100, ActionId::ViewerZoom200});
    auto* quality = view->addMenu(tr("Preview &Quality"));
    add(quality, {ActionId::QualityAuto, ActionId::QualityFull, ActionId::QualityHalf, ActionId::QualityQuarter, ActionId::QualityEighth});
    view->addSeparator();
    add(view, {ActionId::TimelineZoomIn, ActionId::TimelineZoomOut, ActionId::TimelineZoomFit});

    auto* playback = menuBar()->addMenu(tr("&Playback"));
    add(playback, {ActionId::PlayPause, ActionId::Play, ActionId::Pause, ActionId::PlayStop});
    auto* speed = playback->addMenu(tr("Playback &Speed"));
    add(speed, {ActionId::ShuttleReverse, ActionId::Pause, ActionId::ShuttleForward});
    playback->addSeparator();
    add(playback, {ActionId::StepBack, ActionId::StepForward, ActionId::StepBackLarge, ActionId::StepForwardLarge});
    playback->addSeparator();
    add(playback, {ActionId::PrevEdit, ActionId::NextEdit, ActionId::GoToStart, ActionId::GoToEnd});
    playback->addSeparator();
    add(playback, {ActionId::PlayLoop});
    playback->addSeparator();
    add(playback, {ActionId::MarkIn, ActionId::MarkOut, ActionId::GoToIn, ActionId::GoToOut, ActionId::ClearInOut});
    playback->addSeparator();
    add(playback, {ActionId::MarkerNext, ActionId::MarkerPrevious});
    playback->addSeparator();
    add(playback, {ActionId::PlaybackUseProxies});

    auto* clip = menuBar()->addMenu(tr("&Clip"));
    add(clip, {ActionId::EditCut, ActionId::ClipSplit, ActionId::ClipTrimStartToPlayhead, ActionId::ClipTrimEndToPlayhead, ActionId::ClipRippleDelete});
    clip->addSeparator();
    add(clip, {ActionId::ClipSpeed, ActionId::ClipFrameHold, ActionId::ClipAddTransition, ActionId::ClipProperties, ActionId::ClipRevealInMedia});
    clip->addSeparator();
    auto* nudge = clip->addMenu(tr("&Nudge"));
    add(nudge, {ActionId::ClipNudgeLeft, ActionId::ClipNudgeRight, ActionId::ClipNudgeLeftLarge, ActionId::ClipNudgeRightLarge});
    add(clip, {ActionId::ClipMoveToPlayhead});
    clip->addSeparator();
    add(clip, {ActionId::ClipLift, ActionId::ClipExtract});
    clip->addSeparator();
    add(clip, {ActionId::ClipEnable});

    auto* timeline = menuBar()->addMenu(tr("&Timeline"));
    add(timeline, {ActionId::TimelineAddVideoTrack, ActionId::TimelineAddAudioTrack});
    auto* track = timeline->addMenu(tr("T&rack"));
    add(track, {ActionId::TrackRename, ActionId::TrackDuplicate, ActionId::TrackDelete});
    track->addSeparator();
    add(track, {ActionId::TrackAddAbove, ActionId::TrackAddBelow, ActionId::TrackMoveUp, ActionId::TrackMoveDown});
    track->addSeparator();
    add(track, {ActionId::TrackLock, ActionId::TrackMute, ActionId::TrackSolo});
    timeline->addSeparator();
    auto* tools = timeline->addMenu(tr("&Tools"));
    add(tools, {ActionId::ToolSelect, ActionId::ToolBlade, ActionId::ToolTrim, ActionId::ToolHand, ActionId::ToolZoom, ActionId::ToolText});
    timeline->addSeparator();
    add(timeline, {ActionId::TimelineSnap});
    timeline->addSeparator();
    add(timeline, {ActionId::TimelineZoomIn, ActionId::TimelineZoomOut, ActionId::TimelineZoomFit, ActionId::TimelineExpand});
    timeline->addSeparator();
    add(timeline, {ActionId::TimelineAddText, ActionId::TimelineAddSolid, ActionId::TimelineAddAdjustment});
    timeline->addSeparator();
    add(timeline, {ActionId::MarkerAdd});

    auto* audio = menuBar()->addMenu(tr("&Audio"));
    add(audio, {ActionId::AudioRecordVoiceOver});
    audio->addSeparator();
    add(audio, {ActionId::AudioMuteClip});

    auto* video = menuBar()->addMenu(tr("&Video"));
    add(video, {ActionId::VideoTransform, ActionId::VideoCrop, ActionId::VideoColor, ActionId::VideoEffects});

    auto* text = menuBar()->addMenu(tr("Te&xt"));
    add(text, {ActionId::TimelineAddText, ActionId::ToolText});
    text->addSeparator();
    add(text, {ActionId::SubtitleAdd, ActionId::SubtitlesImport, ActionId::SubtitlesExport});

    auto* workspace = menuBar()->addMenu(tr("&Workspace"));
    add(workspace, {ActionId::WorkspaceEdit, ActionId::WorkspaceColor, ActionId::WorkspaceAudio, ActionId::WorkspaceText, ActionId::WorkspaceEffects, ActionId::WorkspaceExport});
    workspace->addSeparator();
    add(workspace, {ActionId::WorkspaceReset});

    auto* help = menuBar()->addMenu(tr("&Help"));
    add(help, {ActionId::HelpShortcuts, ActionId::CommandPalette});
    help->addSeparator();
    add(help, {ActionId::ViewDiagnostics, ActionId::HelpSystemInfo});
    help->addSeparator();
    add(help, {ActionId::HelpAbout});
}

void MainWindow::buildToolBar() {
    auto* tb = m_editor->addToolBar(tr("Main Toolbar"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(16, 16));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_mainToolBar = tb;

    // Groups can be hidden from the toolbar's context menu (customisation).
    auto group = [this, tb](const QString& key, const QString& name, std::initializer_list<ActionId> ids) {
        ToolbarGroup g{key, name, {}};
        for (auto id : ids) { tb->addAction(m_actions->action(id)); g.items << m_actions->action(id); }
        g.items << tb->addSeparator();
        m_toolbarGroups.push_back(g);
    };
    group(QStringLiteral("file"), tr("Project"), {ActionId::ProjectNew, ActionId::ProjectOpen, ActionId::ProjectSave, ActionId::ProjectSaveAs});
    group(QStringLiteral("history"), tr("Undo / Redo"), {ActionId::EditUndo, ActionId::EditRedo});
    group(QStringLiteral("tools"), tr("Editing Tools"), {ActionId::ToolSelect, ActionId::ToolBlade, ActionId::ToolTrim, ActionId::ToolHand, ActionId::ToolZoom, ActionId::ToolText});
    group(QStringLiteral("playback"), tr("Playback"), {ActionId::PrevEdit, ActionId::Play, ActionId::Pause, ActionId::PlayStop, ActionId::NextEdit, ActionId::PlayLoop});
    group(QStringLiteral("viewer"), tr("Viewer"), {ActionId::ViewerZoomFit, ActionId::ViewerZoom50, ActionId::ViewerZoom100, ActionId::ViewerFullscreen});
    {
        // Preview quality as a compact drop-down sharing the View menu's actions.
        auto* q = new QToolButton(tb);
        q->setPopupMode(QToolButton::InstantPopup);
        q->setToolTip(tr("<b>Preview quality</b><br>Render resolution of the preview; export always renders at full quality"));
        auto* menu = new QMenu(q);
        for (auto id : {ActionId::QualityAuto, ActionId::QualityFull, ActionId::QualityHalf, ActionId::QualityQuarter, ActionId::QualityEighth}) {
            QAction* a = m_actions->action(id);
            menu->addAction(a);
            connect(a, &QAction::toggled, q, [q, a](bool on) { if (on) q->setText(tr("Quality: %1").arg(a->text())); });
            if (a->isChecked()) q->setText(tr("Quality: %1").arg(a->text()));
        }
        q->setMenu(menu);
        ToolbarGroup g{QStringLiteral("quality"), tr("Preview Quality"), {}};
        g.items << tb->addWidget(q) << tb->addSeparator();
        m_toolbarGroups.push_back(g);
    }
    {
        // Performance: what is really accelerated, and how playback is keeping up.
        m_toolbarHw = new QLabel(tb);
        m_toolbarHw->setProperty("chip", true);
        m_toolbarHw->setText(tr("Detecting…"));
        m_toolbarFps = new QLabel(tb);
        m_toolbarFps->setProperty("chip", true);
        m_toolbarFps->setText(tr("— fps"));
        m_toolbarFps->setToolTip(tr("Preview frames per second and dropped frames while playing · click for the Performance panel"));
        m_toolbarHw->installEventFilter(this);
        m_toolbarFps->installEventFilter(this);
        m_toolbarHw->setCursor(Qt::PointingHandCursor);
        m_toolbarFps->setCursor(Qt::PointingHandCursor);
        ToolbarGroup g{QStringLiteral("performance"), tr("Performance"), {}};
        g.items << tb->addWidget(m_toolbarHw) << tb->addWidget(m_toolbarFps);
        m_toolbarGroups.push_back(g);
    }

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    auto* exportButton = new QToolButton(tb);
    exportButton->setDefaultAction(m_actions->action(ActionId::Export));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setProperty("primary", true);
    exportButton->setIcon(Icons::icon(QStringLiteral("export"), Theme::palette().accentText, Theme::palette().textDisabled));
    tb->addWidget(exportButton);

    tb->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tb, &QToolBar::customContextMenuRequested, this, [this, tb](const QPoint& pos) {
        QMenu menu(this);
        menu.addSection(tr("Show in toolbar"));
        for (auto& g : m_toolbarGroups) {
            auto* a = menu.addAction(g.name);
            a->setCheckable(true);
            a->setChecked(g.items.isEmpty() || g.items.first()->isVisible());
            connect(a, &QAction::toggled, this, [this, key = g.key](bool on) { setToolbarGroupVisible(key, on); });
        }
        menu.exec(tb->mapToGlobal(pos));
    });
    const QStringList hidden = QSettings().value(QStringLiteral("toolbar/hiddenGroups")).toStringList();
    for (const auto& key : hidden) setToolbarGroupVisible(key, false);
}

void MainWindow::setToolbarGroupVisible(const QString& key, bool visible) {
    QStringList hidden = QSettings().value(QStringLiteral("toolbar/hiddenGroups")).toStringList();
    for (auto& g : m_toolbarGroups) {
        if (g.key != key) continue;
        for (QAction* a : g.items) a->setVisible(visible);
    }
    hidden.removeAll(key);
    if (!visible) hidden << key;
    QSettings().setValue(QStringLiteral("toolbar/hiddenGroups"), hidden);
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
    QElapsedTimer panelTimer;
    panelTimer.start();
    auto mark = [&panelTimer](const char* what) { LF_LOG_DEBUG(kLog, "startup:   {} {} ms", what, panelTimer.restart()); };
    m_mediaPanel = new MediaPanel(*m_document, *m_actions, *m_importer, this);
    m_proxies = new ProxyController(*m_document, this);
    m_mediaPanel->setProxyController(m_proxies);
    mark("media");
    m_previewPanel = new PreviewPanel(*m_actions, *m_playback, this);
    mark("preview");
    m_inspectorPanel = new InspectorPanel(*m_document, this);
    mark("inspector");
    m_timelinePanel = new TimelinePanel(*m_document, *m_actions, this);
    m_projectInfo = new ProjectInfoPanel(*m_document, *m_actions, this);
    mark("timeline + project");
    m_effectsBrowser = new EffectsBrowserPanel(this);
    mark("effects");
    m_titlesBrowser = new TitlesBrowserPanel(this);
    m_transitionsBrowser = new TransitionsBrowserPanel(this);
    mark("titles + transitions");
    m_markersPanel = new MarkersPanel(*m_document, *m_actions, this);
    mark("markers");
    m_mixerPanel = new MixerPanel(*m_document, *m_playback, this);
    mark("mixer");
    m_scopesPanel = new ScopesPanel(*m_playback, this);
    m_performancePanel = new PerformancePanel(*m_document, *m_playback, *m_proxies, *m_actions, this);
    mark("scopes + performance");
    m_exportController = new ExportController(m_gpu, this);
    m_exportSettings = new ExportSettingsPanel(*m_document, *m_exportController, this);
    mark("export settings");
    m_exportQueue = new ExportQueuePanel(*m_exportController, this);
    mark("export queue");

    m_browser = new BrowserPanel(this);
    m_browser->addPage(m_mediaPanel, tr("Media"), QStringLiteral("media"), tr("Imported media and bins"));
    m_browser->addPage(m_effectsBrowser, tr("Effects"), QStringLiteral("effects"), tr("Clip effects: drag onto a clip or apply to the selection"));
    m_browser->addPage(m_transitionsBrowser, tr("Transitions"), QStringLiteral("transitions"), tr("Transitions: drag onto a clip edge or add to the selection"));
    m_browser->addPage(m_titlesBrowser, tr("Titles"), QStringLiteral("titles"), tr("Title presets: drag onto the timeline or add at the playhead"));
    m_browser->addPage(m_markersPanel, tr("Markers"), QStringLiteral("markers"), tr("Sequence markers: jump, rename, colour, copy as chapters"));
    m_browser->addPage(m_projectInfo, tr("Project"), QStringLiteral("project"), tr("Project information and data locations"));

    m_editor->setCentralWidget(m_previewPanel);

    // The browser's tabs are its header; an empty title widget keeps it compact.
    auto* browserDock = makeDock(tr("Browser"), QStringLiteral("browserDock"), m_browser, new QWidget());
    auto* inspectorDock = makeDock(tr("Inspector"), QStringLiteral("inspectorDock"), m_inspectorPanel, m_inspectorPanel->header());
    auto* timelineDock = makeDock(tr("Timeline"), QStringLiteral("timelineDock"), m_timelinePanel, m_timelinePanel->header());
    m_workspaces->registerDock(DockId::Browser, browserDock);
    m_workspaces->registerDock(DockId::Inspector, inspectorDock);
    m_workspaces->registerDock(DockId::Timeline, timelineDock);
    auto* exportSettingsDock = makeDock(tr("Export Settings"), QStringLiteral("exportSettingsDock"), m_exportSettings, nullptr);
    auto* exportQueueDock = makeDock(tr("Render Queue"), QStringLiteral("exportQueueDock"), m_exportQueue, nullptr);
    auto* mixerDock = makeDock(tr("Mixer"), QStringLiteral("mixerDock"), m_mixerPanel, nullptr);
    m_workspaces->registerDock(DockId::Mixer, mixerDock);
    auto* scopesDock = makeDock(tr("Scopes"), QStringLiteral("scopesDock"), m_scopesPanel, nullptr);
    m_workspaces->registerDock(DockId::Scopes, scopesDock);
    m_performanceDock = makeDock(tr("Performance"), QStringLiteral("performanceDock"), m_performancePanel, nullptr);
    m_workspaces->registerDock(DockId::Performance, m_performanceDock);
    m_workspaces->registerDock(DockId::ExportSettings, exportSettingsDock);
    m_workspaces->registerDock(DockId::ExportQueue, exportQueueDock);
    m_workspaces->setAvailable(Workspace::Export, true);
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
    m_statusExport = make(tr("Export progress (open the Export workspace for the queue)"));
    m_statusExport->hide();
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
    m_autosave = new AutosaveManager(*m_document, this);
    connect(m_project, &ProjectController::projectSaved, m_autosave, [this] { m_autosave->discardOwn(); });
    connect(m_home, &HomeScreen::openRecoveryRequested, this, [this](const QString& path) {
        const auto rec = AutosaveManager::findRecoverable();
        const QString original = rec && rec->projectFile == path ? rec->originalPath : QString();
        if (!m_project->openRecovered(path, original, this)) return;
        AutosaveManager::discard(path);   // this session's own autosave takes over from here
        m_home->clearRecovery();
        showEditor();
        m_toasts->show(tr("Recovered your unsaved work. Save it to keep it."), ToastHost::Kind::Success, 6000);
    });
    connect(m_home, &HomeScreen::discardRecoveryRequested, this, [this](const QString& path) {
        AutosaveManager::discard(path);
        m_home->clearRecovery();
        if (const auto rec = AutosaveManager::findRecoverable()) m_home->setRecovery(rec->projectFile, rec->name.isEmpty() ? tr("Untitled") : rec->name, rec->when);
    });

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
    connect(m_effectsBrowser, &EffectsBrowserPanel::applyRequested, this, [this](EffectType type) {
        auto* view = m_timelinePanel->view();
        if (view->addEffect(view->selection(), type) == 0) {
            m_toasts->show(tr("Select a video, image or title clip on an unlocked track first."), ToastHost::Kind::Warning);
            return;
        }
        m_inspectorPanel->revealSection(QStringLiteral("effects"));
    });
    connect(m_transitionsBrowser, &TransitionsBrowserPanel::applyRequested, this, [this](project::TransitionType type, Duration d) {
        if (m_timelinePanel->view()->addTransition(type, d) == 0) {
            m_toasts->show(tr("Select a video, image or title clip on an unlocked track first."), ToastHost::Kind::Warning);
            return;
        }
        m_inspectorPanel->showTab(InspectorTab::Transition);
    });
    connect(m_markersPanel, &MarkersPanel::seekRequested, this, [this](TimePoint t) { seekTimeline(t); });
    connect(tl, &TimelineView::transitionClicked, this, [this](project::ClipId) { m_inspectorPanel->showTab(InspectorTab::Transition); });
    connect(m_titlesBrowser, &TitlesBrowserPanel::addRequested, this, [this](int preset) {
        m_timelinePanel->view()->addTextClip(preset);
        m_timelinePanel->view()->setFocus(Qt::OtherFocusReason);
    });
    connect(tl, &TimelineView::selectionChanged, this, [this] {
        m_effectsBrowser->setCanApply(m_timelinePanel->view()->selectionHasVisual());
        m_transitionsBrowser->setCanApply(m_timelinePanel->view()->selectionHasVisual());
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
    connect(m_inspectorPanel, &InspectorPanel::seekRequested, this, [this](TimePoint t) { seekTimeline(t); });
    connect(tl, &TimelineView::playheadChanged, this, [this](TimePoint t) {
        m_inspectorPanel->setPlayhead(t);
        m_statusTimecode->setText(formatTimecode(t, m_document->project().settings.frameRate));
    });
    // The engine's clock drives the playhead only when there is an engine; without
    // one (no Direct3D device) the timeline playhead is the only position.
    connect(m_playback, &PlaybackController::positionChanged, this, [this](TimePoint t) {
        if (m_playback->isAvailable() && m_playback->mode() == PlaybackController::Mode::Sequence) m_timelinePanel->view()->setPlayhead(t);
    });
    connect(m_playback, &PlaybackController::programChanged, this, [this] {
        if (m_playback->isAvailable() && m_playback->mode() == PlaybackController::Mode::Sequence) m_timelinePanel->view()->setPlayhead(m_playback->position());
    });

    // Edit
    m_actions->connect(ActionId::EditUndo, this, [this] { m_document->undoStack().undo(); });
    m_actions->connect(ActionId::EditRedo, this, [this] { m_document->undoStack().redo(); });

    // Transport -> playback engine
    m_actions->connect(ActionId::PlayPause, this, [this] { ensurePreviewProgram(); m_playback->togglePlay(); });
    m_actions->connect(ActionId::Play, this, [this] { ensurePreviewProgram(); m_playback->play(); });
    m_actions->connect(ActionId::Pause, this, [this] { m_playback->pause(); });
    m_actions->connect(ActionId::PlayStop, this, [this] { m_playback->stop(); });
    m_actions->connect(ActionId::ShuttleForward, this, [this] { ensurePreviewProgram(); m_playback->shuttleForward(); });
    m_actions->connect(ActionId::ShuttleReverse, this, [this] { ensurePreviewProgram(); m_playback->shuttleReverse(); });
    m_actions->connect(ActionId::StepBack, this, [this] { stepFrames(-1); });
    m_actions->connect(ActionId::StepForward, this, [this] { stepFrames(1); });
    m_actions->connect(ActionId::StepBackLarge, this, [this] { stepFrames(-10); });
    m_actions->connect(ActionId::StepForwardLarge, this, [this] { stepFrames(10); });
    m_actions->connect(ActionId::GoToStart, this, [this] {
        if (m_playback->mode() == PlaybackController::Mode::Source) m_playback->goToStart();
        else seekTimeline(TimePoint::zero());
    });
    m_actions->connect(ActionId::GoToEnd, this, [this] {
        if (m_playback->mode() == PlaybackController::Mode::Source) m_playback->goToEnd();
        else seekTimeline(TimePoint{m_document->project().sequence.duration().ticks});
    });
    m_actions->connect(ActionId::Export, this, [this] { m_workspaces->switchTo(Workspace::Export); });
    m_actions->connect(ActionId::AudioRecordVoiceOver, this, [this] { recordVoiceOver(); });
    m_actions->action(ActionId::PlaybackUseProxies)->setChecked(m_proxies->useProxies());
    connect(m_actions->action(ActionId::PlaybackUseProxies), &QAction::toggled, m_proxies, &ProxyController::setUseProxies);
    connect(m_proxies, &ProxyController::useProxiesChanged, this, [this](bool on) {
        m_actions->action(ActionId::PlaybackUseProxies)->setChecked(on);
        m_toasts->show(on ? tr("Preview uses proxies where available") : tr("Preview uses the original media"), ToastHost::Kind::Info);
    });
    connect(m_proxies, &ProxyController::proxiesChanged, this, [this] { scheduleSequenceRefresh(); });
    m_actions->connect(ActionId::SubtitleAdd, this, [this] {
        project::SequenceEdit edit = project::addSubtitle(*m_document, m_timelinePanel->view()->playhead());
        if (edit.empty()) return;
        std::vector<project::ClipId> ids;
        for (const auto& c : edit.added) ids.push_back(c.id);
        m_document->push(std::make_unique<project::SequenceEditCommand>(*m_document, tr("Add Subtitle").toStdString(), std::move(edit)));
        m_timelinePanel->view()->setSelection(ids);
        m_inspectorPanel->showTab(InspectorTab::Text);
    });
    m_actions->connect(ActionId::SubtitlesImport, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Import Subtitles"), QString(), tr("Subtitles (*.srt *.vtt);;All files (*)"));
        if (file.isEmpty()) return;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) { m_toasts->show(tr("Could not open %1").arg(QFileInfo(file).fileName()), ToastHost::Kind::Error); return; }
        auto cues = project::parseSubtitles(f.readAll().toStdString());
        if (!cues) { m_toasts->show(QString::fromStdString(cues.error().message), ToastHost::Kind::Error, 6000); return; }
        project::SequenceEdit edit = project::importSubtitles(*m_document, *cues);
        m_document->push(std::make_unique<project::SequenceEditCommand>(*m_document, tr("Import Subtitles").toStdString(), std::move(edit)));
        m_toasts->show(tr("Imported %n subtitle(s)", nullptr, static_cast<int>(cues->size())), ToastHost::Kind::Success);
    });
    m_actions->connect(ActionId::SubtitlesExport, this, [this] {
        const auto cues = project::subtitleCues(m_document->project().sequence);
        if (cues.empty()) return;
        QString base = QString::fromStdString(m_document->project().settings.name);
        if (base.isEmpty()) base = tr("Subtitles");
        const QString suggested = QDir(AppPaths::path(AppPaths::Kind::Exports)).filePath(base + QStringLiteral(".srt"));
        QString selected;
        QString file = QFileDialog::getSaveFileName(this, tr("Export Subtitles"), QDir::toNativeSeparators(suggested), tr("SubRip (*.srt);;WebVTT (*.vtt)"), &selected);
        if (file.isEmpty()) return;
        const bool vtt = file.endsWith(QStringLiteral(".vtt"), Qt::CaseInsensitive) || selected.contains(QStringLiteral("vtt"));
        if (!file.endsWith(vtt ? QStringLiteral(".vtt") : QStringLiteral(".srt"), Qt::CaseInsensitive)) file += vtt ? QStringLiteral(".vtt") : QStringLiteral(".srt");
        QFile f(file);
        const std::string data = vtt ? project::writeVtt(cues) : project::writeSrt(cues);
        if (!f.open(QIODevice::WriteOnly) || f.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size())) {
            m_toasts->show(tr("Could not write %1").arg(QFileInfo(file).fileName()), ToastHost::Kind::Error);
            return;
        }
        m_toasts->show(tr("Wrote %n subtitle(s) to %1", nullptr, static_cast<int>(cues.size())).arg(QFileInfo(file).fileName()), ToastHost::Kind::Success);
    });
    m_actions->connect(ActionId::ExportFrame, this, [this] {
        const TimePoint t = m_timelinePanel->view()->playhead();
        QDir().mkpath(AppPaths::path(AppPaths::Kind::Exports));
        QString base = QString::fromStdString(m_document->project().settings.name);
        if (base.isEmpty()) base = tr("Frame");
        const QString suggested = QDir(AppPaths::path(AppPaths::Kind::Exports))
                                      .filePath(QStringLiteral("%1 %2.png").arg(base, formatTimecode(t, m_document->project().settings.frameRate).replace(QLatin1Char(':'), QLatin1Char('-'))));
        QString file = QFileDialog::getSaveFileName(this, tr("Export Frame"), QDir::toNativeSeparators(suggested), tr("PNG image (*.png)"));
        if (file.isEmpty()) return;
        if (!file.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) file += QStringLiteral(".png");
        m_exportController->saveFrame(m_document->project(), t, file);
    });
    connect(m_exportSettings, &ExportSettingsPanel::exportFrameRequested, m_actions->action(ActionId::ExportFrame), &QAction::trigger);
    connect(m_exportController, &ExportController::frameSaved, this, [this](const QString& path, bool ok, const QString& error) {
        if (ok) m_toasts->show(tr("Frame saved: %1").arg(QFileInfo(path).fileName()), ToastHost::Kind::Success);
        else m_toasts->show(tr("Could not save the frame: %1").arg(error), ToastHost::Kind::Error, 6000);
    });
    connect(m_exportSettings, &ExportSettingsPanel::exportQueued, this, [this](int) {
        m_toasts->show(m_exportController->isBusy() ? tr("Export started") : tr("Export queued"), ToastHost::Kind::Info);
    });
    connect(m_exportController, &ExportController::jobProgress, this, [this](int id) {
        if (const ExportJob* j = m_exportController->job(id)) {
            m_statusExport->setText(tr("Exporting %1%").arg(std::lround(j->progress.fraction() * 100)));
            m_statusExport->show();
        }
    });
    connect(m_exportController, &ExportController::jobFinished, this, [this](int id) {
        if (!m_exportController->isBusy()) m_statusExport->hide();
        const ExportJob* j = m_exportController->job(id);
        if (!j) return;
        if (j->state == ExportJob::State::Done) {
            m_toasts->show(tr("Exported %1 (%2)").arg(j->name, QString::fromStdString(formatBytes(j->result.bytes))), ToastHost::Kind::Success, 6000);
            if (!j->result.note.empty()) m_toasts->show(QString::fromStdString(j->result.note), ToastHost::Kind::Warning, 8000);
        }
        else if (j->state == ExportJob::State::Failed)
            m_toasts->show(tr("Export failed: %1").arg(j->message), ToastHost::Kind::Error, 8000);
    });
    m_actions->connect(ActionId::MarkerNext, this, [this] {
        if (auto t = project::ops::nextMarker(m_document->project().sequence, m_timelinePanel->view()->playhead())) seekTimeline(*t);
    });
    m_actions->connect(ActionId::MarkerPrevious, this, [this] {
        if (auto t = project::ops::previousMarker(m_document->project().sequence, m_timelinePanel->view()->playhead())) seekTimeline(*t);
    });
    m_actions->connect(ActionId::PrevEdit, this, [this] {
        if (auto t = project::ops::previousEditPoint(m_document->project().sequence, m_timelinePanel->view()->playhead())) seekTimeline(*t);
    });
    m_actions->connect(ActionId::NextEdit, this, [this] {
        if (auto t = project::ops::nextEditPoint(m_document->project().sequence, m_timelinePanel->view()->playhead())) seekTimeline(*t);
    });
    m_actions->connect(ActionId::GoToIn, this, [this] { if (m_document->project().sequence.markIn.ticks >= 0) seekTimeline(m_document->project().sequence.markIn); });
    m_actions->connect(ActionId::GoToOut, this, [this] { if (m_document->project().sequence.markOut.ticks >= 0) seekTimeline(m_document->project().sequence.markOut); });
    connect(m_actions->action(ActionId::PlayLoop), &QAction::toggled, this, [this] { applyLoop(); });

    // Viewer zoom + preview quality (exclusive action groups; persisted)
    const std::pair<ActionId, double> zooms[] = {{ActionId::ViewerZoomFit, 0.0}, {ActionId::ViewerZoom25, 0.25}, {ActionId::ViewerZoom50, 0.5},
                                                 {ActionId::ViewerZoom100, 1.0}, {ActionId::ViewerZoom200, 2.0}};
    for (const auto& [id, z] : zooms) {
        const double zoom = z;
        m_actions->connect(id, this, [this, zoom] { m_playback->setViewZoom(zoom); });
    }
    const std::pair<ActionId, playback::PreviewQuality> qualities[] = {
        {ActionId::QualityAuto, playback::PreviewQuality::Auto}, {ActionId::QualityFull, playback::PreviewQuality::Full},
        {ActionId::QualityHalf, playback::PreviewQuality::Half}, {ActionId::QualityQuarter, playback::PreviewQuality::Quarter},
        {ActionId::QualityEighth, playback::PreviewQuality::Eighth}};
    const int savedQuality = QSettings().value(QStringLiteral("preview/quality2"), 0).toInt();
    for (const auto& [id, q] : qualities) {
        const auto quality = q;
        m_actions->connect(id, this, [this, quality] {
            m_playback->setQuality(quality);
            QSettings().setValue(QStringLiteral("preview/quality2"), static_cast<int>(quality));
        });
        if (static_cast<int>(q) == savedQuality) { m_actions->action(id)->setChecked(true); m_playback->setQuality(q); }
    }
    m_actions->connect(ActionId::ViewerFullscreen, this, [this] { setViewerFullscreen(m_actions->action(ActionId::ViewerFullscreen)->isChecked()); });

    // Inspector navigation
    const std::pair<ActionId, const char*> sections[] = {{ActionId::VideoTransform, "transform"}, {ActionId::VideoCrop, "crop"},
                                                         {ActionId::VideoColor, "color"}, {ActionId::VideoEffects, "effects"}};
    for (const auto& [id, key] : sections) {
        const QString k = QString::fromLatin1(key);
        m_actions->connect(id, this, [this, k] {
            if (auto* dock = m_workspaces->dock(DockId::Inspector)) { dock->show(); dock->raise(); }
            m_inspectorPanel->revealSection(k);
        });
    }

    m_actions->connect(ActionId::ClipSpeed, this, [this] {
        if (auto* dock = m_workspaces->dock(DockId::Inspector)) { dock->show(); dock->raise(); }
        m_inspectorPanel->revealSection(QStringLiteral("speed"));
    });
    m_actions->connect(ActionId::ClipProperties, this, [this] {
        if (auto* dock = m_workspaces->dock(DockId::Inspector)) { dock->show(); dock->raise(); }
        m_inspectorPanel->revealSection(QStringLiteral("clip"));
    });
    m_actions->connect(ActionId::ClipRevealInMedia, this, [this] {
        const auto& sel = m_timelinePanel->view()->selection();
        if (sel.empty()) return;
        const auto* c = m_document->project().sequence.findClip(sel.front());
        if (!c || !project::isValid(c->media)) return;
        if (auto* dock = m_workspaces->dock(DockId::Browser)) { dock->show(); dock->raise(); }
        m_browser->showPage(QStringLiteral("media"));
        m_mediaPanel->revealMedia(c->media);
    });
    m_actions->connect(ActionId::TimelineExpand, this, [this] {
        auto* dock = m_workspaces->dock(DockId::Timeline);
        if (!dock) return;
        if (m_actions->action(ActionId::TimelineExpand)->isChecked()) {
            m_timelineHeightBeforeExpand = dock->height();
            dock->show();
            m_editor->resizeDocks({dock}, {std::max(200, m_editor->height() - 160)}, Qt::Vertical);
        } else {
            m_editor->resizeDocks({dock}, {m_timelineHeightBeforeExpand > 0 ? m_timelineHeightBeforeExpand : m_editor->height() * 2 / 5}, Qt::Vertical);
        }
    });

    // Import folder, help, palette
    m_actions->connect(ActionId::MediaImportFolder, this, [this] { importFolderWithDialog(); });
    m_actions->connect(ActionId::HelpShortcuts, this, [this] { ShortcutsDialog dlg(*m_actions, this); dlg.exec(); });
    m_actions->connect(ActionId::HelpSystemInfo, this, [this] { showDiagnostics(QStringLiteral("system")); });
    m_actions->connect(ActionId::CommandPalette, this, [this] {
        if (!m_palette) m_palette = new CommandPalette(*m_actions, this);
        m_palette->show();
    });
    connect(m_playback, &PlaybackController::statsChanged, this, &MainWindow::updateStatusStats);
    connect(m_playback, &PlaybackController::playingChanged, this, [this](bool p) {
        if (!p) { m_statusPlayback->setText(tr("— fps")); m_toolbarFps->setText(tr("— fps")); }
        m_actions->setEnabled(ActionId::Play, !p);
        m_actions->setEnabled(ActionId::Pause, p);
    });
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
    if (const auto rec = AutosaveManager::findRecoverable()) m_home->setRecovery(rec->projectFile, rec->name.isEmpty() ? tr("Untitled") : rec->name, rec->when);
    else m_home->clearRecovery();
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
    m_autosave->discardOwn();
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
        case Workspace::Color: m_inspectorPanel->showTab(InspectorTab::Color); break;
        case Workspace::Audio: m_inspectorPanel->showTab(InspectorTab::Audio); break;
        case Workspace::Text: m_browser->showPage(QStringLiteral("titles")); m_inspectorPanel->showTab(InspectorTab::Text); break;
        case Workspace::Effects: m_browser->showPage(QStringLiteral("effects")); m_inspectorPanel->showTab(InspectorTab::Effects); break;
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
    const bool hasClips = !m_document->project().sequence.clips.empty();
    m_actions->setEnabled(ActionId::Export, hasClips);
    m_actions->setEnabled(ActionId::ExportFrame, hasClips);
    m_actions->setEnabled(ActionId::SubtitlesExport, !project::subtitleCues(m_document->project().sequence).empty());
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
    if (change.kind == project::Change::Kind::MarksChanged || change.kind == project::Change::Kind::Reset) applyLoop();
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
    m_sequenceProgram = project.sequence.clips.empty() ? nullptr : std::make_shared<playback::SequenceProgram>(project, m_proxies->overrides());
    // Only the timeline view of the preview follows edits; a source preview stays put.
    if (m_playback->mode() == PlaybackController::Mode::Source) return;
    const TimePoint keep = m_timelinePanel->view()->playhead();
    if (m_sequenceProgram) m_playback->setProgram(m_sequenceProgram, PlaybackController::Mode::Sequence, tr("Timeline"), keep);
    else if (m_playback->mode() == PlaybackController::Mode::Sequence) m_playback->clearProgram();
}

void MainWindow::showSequenceInPreview(TimePoint at) {
    if (m_playback->mode() == PlaybackController::Mode::Sequence) return;
    if (!m_sequenceProgram && !m_document->project().sequence.clips.empty()) m_sequenceProgram = std::make_shared<playback::SequenceProgram>(m_document->project(), m_proxies->overrides());
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
        m_toolbarFps->setText(text);
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
    m_exportSettings->setEncoderProbes(r.encoders);
    m_performancePanel->setHardwareReport(r);
    m_toolbarHw->setText(parts.join(QStringLiteral(" · ")));
    QString tip;
    if (const auto* a = r.activeAdapter()) tip = QString::fromStdString(a->name) + QLatin1Char('\n');
    tip += QString::fromStdString(hw::HardwareDetector::summary(r));
    m_statusHardware->setToolTip(tip);
    m_toolbarHw->setToolTip(tip);
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

void MainWindow::ensurePreviewProgram() {
    // Transport keys act on the timeline unless the source monitor is showing media.
    if (m_playback->mode() == PlaybackController::Mode::None) showSequenceInPreview(m_timelinePanel->view()->playhead());
}

void MainWindow::recordVoiceOver() {
    showEditor();
    m_playback->pause();
    const TimePoint at = m_timelinePanel->view()->playhead();
    const QDir dir(AppPaths::path(AppPaths::Kind::Recordings));
    QDir().mkpath(dir.absolutePath());
    QString base = QString::fromStdString(m_document->project().settings.name);
    if (base.isEmpty()) base = tr("Voice-over");
    for (QChar ch : QStringLiteral("\\/:*?\"<>|")) base.replace(ch, QLatin1Char('_'));
    const QString path = dir.filePath(QStringLiteral("%1 voice %2.wav").arg(base, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    VoiceOverDialog dlg(path, this);
    connect(&dlg, &VoiceOverDialog::recordingStarted, this, [this, &dlg, at] {
        if (!dlg.playTimeline() || m_document->project().sequence.clips.empty()) return;
        seekTimeline(at);
        m_playback->play();
    });
    connect(&dlg, &VoiceOverDialog::recordingStopped, this, [this] { m_playback->pause(); });
    if (dlg.exec() != QDialog::Accepted || dlg.recordedFile().isEmpty()) return;
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_importer, &ImportController::mediaImported, this, [this, at, conn](const QList<project::MediaId>& ids) {
        QObject::disconnect(*conn);
        if (!ids.isEmpty()) placeAudioOnFreeTrack(ids.front(), at);
    });
    m_importer->importFiles({dlg.recordedFile()}, this);
}

void MainWindow::placeAudioOnFreeTrack(project::MediaId media, TimePoint at) {
    const project::MediaItem* item = m_document->project().findMedia(media);
    if (!item) return;
    const auto& seq = m_document->project().sequence;
    const TimeRange range{at, item->info.duration};
    project::SequenceEdit edit;
    project::TrackId target = project::TrackId::Invalid;
    for (const auto* t : seq.audioTracks()) {
        if (t->locked) continue;
        bool free = true;
        for (const auto* c : seq.clipsOnTrack(t->id)) free &= !(c->start < range.end() && range.start < c->end());
        if (free) { target = t->id; break; }
    }
    if (!project::isValid(target)) {
        edit = project::ops::addTrack(*m_document, project::TrackKind::Audio, -1);
        if (edit.tracksAdded.empty()) return;
        target = edit.tracksAdded.front().second.id;
    }
    project::ops::Placement placement;
    placement.audioTrack = target;
    placement.start = at;
    auto clips = project::ops::clipsForMedia(*m_document, *item, placement);
    std::vector<project::ClipId> ids;
    for (auto& c : clips) { ids.push_back(c.id); edit.add(std::move(c)); }
    if (ids.empty()) return;
    m_document->push(std::make_unique<project::SequenceEditCommand>(*m_document, tr("Add Voice-over").toStdString(), std::move(edit)));
    m_timelinePanel->view()->setSelection(ids);
    m_toasts->show(tr("Voice-over added (%1)").arg(formatClock(item->info.duration)), ToastHost::Kind::Success);
}

void MainWindow::stepFrames(int frames) {
    // The source monitor steps through its media; otherwise the timeline playhead moves
    // (with or without a playback engine) and the preview follows.
    if (m_playback->mode() == PlaybackController::Mode::Source) { m_playback->stepFrames(frames); return; }
    m_playback->pause();
    const Rational fps = m_document->project().settings.frameRate;
    const std::int64_t frame = frameIndexAt(m_timelinePanel->view()->playhead(), fps) + frames;
    seekTimeline(timeOfFrame(std::max<std::int64_t>(0, frame), fps));
}

void MainWindow::seekTimeline(TimePoint t) {
    m_timelinePanel->view()->setPlayhead(t);
    if (m_playback->mode() != PlaybackController::Mode::Sequence) showSequenceInPreview(t);
    m_playback->seek(m_timelinePanel->view()->playhead());
}

void MainWindow::applyLoop() {
    const auto& seq = m_document->project().sequence;
    const bool on = m_actions->action(ActionId::PlayLoop)->isChecked();
    if (seq.hasInOut()) m_playback->setLoop(on, seq.markIn, seq.markOut);
    else m_playback->setLoop(on, TimePoint{-1}, TimePoint{-1});
}

void MainWindow::importFolderWithDialog() {
    QSettings settings;
    const QString start = settings.value(QStringLiteral("media/lastFolder"), QDir::homePath()).toString();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Import Folder"), start);
    if (dir.isEmpty()) return;
    settings.setValue(QStringLiteral("media/lastFolder"), dir);
    QList<QUrl> urls;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) urls << QUrl::fromLocalFile(it.next());
    const QStringList paths = ImportController::collectMediaPaths(urls);
    if (paths.isEmpty()) {
        m_toasts->show(tr("No media files found in %1").arg(QDir::toNativeSeparators(dir)), ToastHost::Kind::Warning);
        return;
    }
    showEditor();
    m_importer->importFiles(paths, this);
}

void MainWindow::setViewerFullscreen(bool on) {
    if (on == m_viewerFullscreen) return;
    m_viewerFullscreen = on;
    m_actions->action(ActionId::ViewerFullscreen)->setChecked(on);
    if (on) {
        showEditor();
        m_preFullscreenState = m_editor->saveState();
        m_wasMaximized = isMaximized();
        for (QDockWidget* d : m_editor->findChildren<QDockWidget*>()) d->hide();
        for (QToolBar* t : m_editor->findChildren<QToolBar*>()) t->hide();
        menuBar()->hide();
        statusBar()->hide();
        showFullScreen();
        qApp->installEventFilter(this);
        m_toasts->show(tr("Press Esc or F11 to leave full screen"), ToastHost::Kind::Info, 2500);
    } else {
        qApp->removeEventFilter(this);
        menuBar()->show();
        statusBar()->show();
        for (QToolBar* t : m_editor->findChildren<QToolBar*>()) t->show();
        m_editor->restoreState(m_preFullscreenState);
        if (m_wasMaximized) showMaximized();
        else showNormal();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == m_toolbarHw || watched == m_toolbarFps) && event->type() == QEvent::MouseButtonRelease && m_performanceDock) {
        m_performanceDock->show();
        m_performanceDock->raise();
        return true;
    }
    if (m_viewerFullscreen && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        setViewerFullscreen(false);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
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
    if (m_exportController && m_exportController->isBusy()) {
        const auto answer = QMessageBox::question(this, tr("Export in Progress"), tr("A video is still being exported. Stop the export and quit?"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) { event->ignore(); return; }
        for (const auto& j : m_exportController->jobs()) m_exportController->cancel(j.id);
    }
    if (!m_project->maybeSave(this)) {
        event->ignore();
        return;
    }
    // The user chose to save or discard: this session's autosave is no longer needed.
    if (m_autosave) m_autosave->discardOwn();
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (m_layoutRestored) m_workspaces->saveSession();
    // Stop presentation before Qt tears the native window down.
    m_playback->pause();
    QMainWindow::closeEvent(event);
}

}  // namespace lf::ui
