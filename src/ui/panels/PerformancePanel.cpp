#include "ui/panels/PerformancePanel.h"

#include "lumaforge/core/StringUtil.h"
#include "ui/ActionRegistry.h"
#include "ui/AppPaths.h"
#include "ui/media/ProxyController.h"
#include "ui/playback/PlaybackController.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

namespace lf::ui {

namespace {
QLabel* heading(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setProperty("sectionHeading", true);
    return l;
}

std::uint64_t folderBytes(const QString& path) {
    std::uint64_t total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += static_cast<std::uint64_t>(it.fileInfo().size());
    }
    return total;
}

// Codec names the D3D11VA probe reports, by FFmpeg codec name.
QString decoderKey(const std::string& ffmpegCodec) {
    if (ffmpegCodec == "h264") return QStringLiteral("H.264");
    if (ffmpegCodec == "hevc") return QStringLiteral("HEVC Main");
    if (ffmpegCodec == "vp9") return QStringLiteral("VP9");
    if (ffmpegCodec == "av1") return QStringLiteral("AV1");
    if (ffmpegCodec == "mpeg2video") return QStringLiteral("MPEG-2");
    if (ffmpegCodec == "vc1") return QStringLiteral("VC-1");
    return {};
}
}  // namespace

PerformancePanel::PerformancePanel(project::Document& doc, PlaybackController& playback, ProxyController& proxies, ActionRegistry& actions, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_playback(playback), m_proxies(proxies) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    scroll->setWidget(body);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    layout->addWidget(heading(body, tr("Preview")));
    m_live = new QLabel(body);
    m_live->setObjectName(QStringLiteral("perfLive"));
    m_live->setTextFormat(Qt::RichText);
    m_live->setWordWrap(true);
    layout->addWidget(m_live);
    auto* toggles = new QGridLayout();
    auto* useProxies = new QCheckBox(tr("Use proxies for preview"), body);
    useProxies->setChecked(m_proxies.useProxies());
    connect(useProxies, &QCheckBox::toggled, actions.action(ActionId::PlaybackUseProxies), &QAction::setChecked);
    connect(&m_proxies, &ProxyController::useProxiesChanged, useProxies, &QCheckBox::setChecked);
    toggles->addWidget(useProxies, 0, 0, 1, 2);
    int col = 0;
    for (auto id : {ActionId::QualityAuto, ActionId::QualityHalf, ActionId::QualityQuarter}) {
        auto* b = new QToolButton(body);
        b->setDefaultAction(actions.action(id));
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        toggles->addWidget(b, 1, col++);
    }
    layout->addLayout(toggles);

    layout->addWidget(heading(body, tr("LumaForge process")));
    m_process = new QLabel(body);
    m_process->setTextFormat(Qt::RichText);
    layout->addWidget(m_process);

    layout->addWidget(heading(body, tr("Media in this project")));
    m_mediaSummary = new QLabel(body);
    m_mediaSummary->setObjectName(QStringLiteral("perfMedia"));
    m_mediaSummary->setTextFormat(Qt::RichText);
    m_mediaSummary->setWordWrap(true);
    layout->addWidget(m_mediaSummary);
    m_makeProxies = new QPushButton(body);
    m_makeProxies->setObjectName(QStringLiteral("perfMakeProxies"));
    layout->addWidget(m_makeProxies);
    connect(m_makeProxies, &QPushButton::clicked, this, [this] {
        m_proxies.create(m_heavy);
        if (!m_proxies.useProxies()) m_proxies.setUseProxies(true);
    });

    layout->addWidget(heading(body, tr("Caches and data")));
    m_caches = new QLabel(body);
    m_caches->setTextFormat(Qt::RichText);
    m_caches->setWordWrap(true);
    layout->addWidget(m_caches);
    auto* cacheButtons = new QGridLayout();
    auto* clearProxies = new QPushButton(tr("Delete All Proxies"), body);
    auto* clearCache = new QPushButton(tr("Clear Preview Cache"), body);
    auto* openData = new QPushButton(tr("Open Data Folder"), body);
    cacheButtons->addWidget(clearProxies, 0, 0);
    cacheButtons->addWidget(clearCache, 0, 1);
    cacheButtons->addWidget(openData, 1, 0, 1, 2);
    layout->addLayout(cacheButtons);
    layout->addStretch(1);
    connect(clearProxies, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Delete Proxies"), tr("Delete every proxy file? They can be made again at any time; your media is not touched.")) != QMessageBox::Yes) return;
        QList<project::MediaId> all;
        for (const auto& m : m_doc.project().media) all << m.id;
        m_proxies.remove(all);
        // Also proxies of media not in this project.
        QDir dir(AppPaths::path(AppPaths::Kind::Proxies));
        for (const QString& f : dir.entryList({QStringLiteral("*.mp4")}, QDir::Files)) dir.remove(f);
        refreshCaches();
    });
    connect(clearCache, &QPushButton::clicked, this, [this] {
        for (auto kind : {AppPaths::Kind::Thumbnails, AppPaths::Kind::Waveforms, AppPaths::Kind::Preview}) {
            QDir dir(AppPaths::path(kind));
            dir.removeRecursively();
            QDir().mkpath(dir.absolutePath());
        }
        refreshCaches();
    });
    connect(openData, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataRoot())); });

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &PerformancePanel::sample);
    connect(&m_proxies, &ProxyController::stateChanged, this, [this] { analyseMedia(); });
    m_listenerToken = m_doc.addListener([this](const project::Change& c) {
        using K = project::Change::Kind;
        if (c.kind == K::Reset || c.kind == K::MediaAdded || c.kind == K::MediaRemoved || c.kind == K::MediaChanged || c.kind == K::ClipsChanged) analyseMedia();
    });
    analyseMedia();
}

PerformancePanel::~PerformancePanel() { m_doc.removeListener(m_listenerToken); }

void PerformancePanel::setHardwareReport(const hw::HwReport& report) {
    m_report = report;
    analyseMedia();
}

void PerformancePanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    sample();
    refreshCaches();
    m_timer->start();
}

void PerformancePanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    m_timer->stop();
}

void PerformancePanel::sample() {
    const auto s = m_playback.stats();
    QString live;
    if (!m_playback.isAvailable()) {
        live = tr("Preview unavailable: %1").arg(m_playback.unavailableReason().toHtmlEscaped());
    } else {
        live = tr("Rendering <b>%1×%2</b> · decoder <b>%3</b><br>%4 · %5 frames shown, <b>%6 dropped</b>")
                   .arg(s.renderWidth).arg(s.renderHeight).arg(QString::fromStdString(s.decoder.empty() ? std::string("—") : s.decoder))
                   .arg(m_playback.isPlaying() ? tr("playing at <b>%1 fps</b>").arg(s.previewFps, 0, 'f', 1) : tr("paused"))
                   .arg(s.presentedFrames).arg(s.droppedFrames);
        if (!s.renderer.empty()) live += tr("<br>Renderer: %1").arg(QString::fromStdString(s.renderer).toHtmlEscaped());
        if (s.droppedFrames > 0) live += tr("<br><i>Dropping frames? Lower the preview quality, or use proxies for heavy clips.</i>");
    }
    m_live->setText(live);

    PROCESS_MEMORY_COUNTERS_EX mem{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof mem);
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto toU64 = [](const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    const std::uint64_t cpu = toU64(kernel) + toU64(user);
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    const std::uint64_t wall = toU64(nowFt);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    double percent = 0.0;
    if (m_lastWall && wall > m_lastWall) percent = 100.0 * static_cast<double>(cpu - m_lastCpu) / static_cast<double>(wall - m_lastWall) / std::max<DWORD>(1, si.dwNumberOfProcessors);
    m_lastCpu = cpu;
    m_lastWall = wall;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    m_process->setText(tr("CPU <b>%1%</b> of %2 cores · memory <b>%3</b> (system %4 free of %5)")
                           .arg(percent, 0, 'f', 0).arg(si.dwNumberOfProcessors)
                           .arg(QString::fromStdString(formatBytes(mem.WorkingSetSize)), QString::fromStdString(formatBytes(ms.ullAvailPhys)),
                                QString::fromStdString(formatBytes(ms.ullTotalPhys))));
}

void PerformancePanel::analyseMedia() {
    // Video this GPU cannot decode in hardware, or larger than it handles comfortably.
    m_heavy.clear();
    int videos = 0, withProxy = 0;
    QStringList reasons;
    for (const auto& m : m_doc.project().media) {
        if (m.type() != media::MediaType::Video || !m.info.video || m.offline) continue;
        ++videos;
        if (m_proxies.state(m.id) == ProxyController::State::Ready) { ++withProxy; continue; }
        const QString key = decoderKey(m.info.video->codecName);
        const bool hw = m_report && !key.isEmpty() && m_report->supportsDecoder(key.toStdString());
        const bool large = m.info.video->height > 1080;
        if (!hw || large) {
            m_heavy << m.id;
            reasons << QStringLiteral("%1 (%2%3)").arg(QString::fromStdString(m.displayName).toHtmlEscaped(),
                                                      QString::fromStdString(m.info.video->codecName),
                                                      large ? QStringLiteral(", %1p").arg(m.info.video->height) : QString());
        }
    }
    QString text = tr("%n video file(s)", nullptr, videos);
    if (withProxy) text += tr(", %1 with proxies").arg(withProxy);
    if (!m_report) text += tr("<br><i>Waiting for hardware detection…</i>");
    else if (m_heavy.isEmpty()) text += tr("<br>Everything decodes on the GPU at a comfortable size.");
    else text += tr("<br><b>Decoded on the CPU or very large:</b> %1").arg(reasons.join(QStringLiteral(", ")));
    m_mediaSummary->setText(text);
    m_makeProxies->setText(m_heavy.isEmpty() ? tr("No proxies needed") : tr("Create Proxies for These (%1)").arg(m_heavy.size()));
    m_makeProxies->setEnabled(!m_heavy.isEmpty());
}

void PerformancePanel::refreshCaches() {
    const std::pair<AppPaths::Kind, QString> folders[] = {{AppPaths::Kind::Proxies, tr("Proxies")}, {AppPaths::Kind::Thumbnails, tr("Thumbnails")},
                                                          {AppPaths::Kind::Waveforms, tr("Waveforms")}, {AppPaths::Kind::Preview, tr("Preview cache")},
                                                          {AppPaths::Kind::Exports, tr("Exports")}, {AppPaths::Kind::Recordings, tr("Recordings")},
                                                          {AppPaths::Kind::Autosave, tr("Autosave")}, {AppPaths::Kind::Logs, tr("Logs")}};
    QStringList lines;
    for (const auto& [kind, name] : folders) {
        const QString path = AppPaths::path(kind);
        lines << QStringLiteral("%1: <b>%2</b> <span style='color:gray'>%3</span>").arg(name, QString::fromStdString(formatBytes(folderBytes(path))), QDir::toNativeSeparators(path).toHtmlEscaped());
    }
    m_caches->setText(lines.join(QStringLiteral("<br>")));
}

}  // namespace lf::ui
