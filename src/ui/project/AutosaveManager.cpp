#include "ui/project/AutosaveManager.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "ui/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace lf::ui {

namespace {
constexpr const char* kLog = "autosave";

bool processRunning(qint64 pid) {
    if (pid <= 0 || pid == QCoreApplication::applicationPid()) return pid == QCoreApplication::applicationPid();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    // A recycled pid belonging to another program does not count.
    wchar_t exe[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    const bool named = QueryFullProcessImageNameW(h, 0, exe, &size) && QString::fromWCharArray(exe).endsWith(QStringLiteral("LumaForge.exe"), Qt::CaseInsensitive);
    CloseHandle(h);
    return alive && named;
}
}  // namespace

AutosaveManager::AutosaveManager(project::Document& doc, QObject* parent) : QObject(parent), m_doc(doc) {
    m_minutes = QSettings().value(QStringLiteral("autosave/minutes"), 2).toInt();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] { autosaveNow(); });
    // A new / opened project replaces the document: the old autosave no longer applies.
    m_listener = m_doc.addListener([this](const project::Change& c) { if (c.kind == project::Change::Kind::Reset) discardOwn(); });
    setIntervalMinutes(m_minutes);
}

AutosaveManager::~AutosaveManager() { m_doc.removeListener(m_listener); }

void AutosaveManager::setIntervalMinutes(int minutes) {
    m_minutes = std::clamp(minutes, 0, 60);
    QSettings().setValue(QStringLiteral("autosave/minutes"), m_minutes);
    if (m_minutes == 0) m_timer->stop();
    else m_timer->start(m_minutes * 60 * 1000);
}

QString AutosaveManager::ownBase() const {
    return QDir(AppPaths::path(AppPaths::Kind::Autosave)).filePath(QStringLiteral("session-%1").arg(QCoreApplication::applicationPid()));
}

bool AutosaveManager::autosaveNow() {
    if (!m_doc.isDirty() || m_doc.project().sequence.clips.empty() && m_doc.project().media.empty()) return false;
    const std::uint64_t serial = (static_cast<std::uint64_t>(m_doc.undoStack().index()) << 1) | 1u;
    if (serial == m_lastSavedSerial) return false;   // nothing new since the last autosave
    const QString base = ownBase();
    QDir().mkpath(QFileInfo(base).absolutePath());
    auto saved = project::serializer::saveToFile(m_doc.project(), (base + QStringLiteral(".lfproj")).toStdString());
    if (!saved) {
        LF_LOG_WARN(kLog, "autosave failed: {}", saved.error().fullText());
        return false;
    }
    QJsonObject meta;
    meta[QStringLiteral("original")] = QString::fromStdString(m_doc.filePath());
    meta[QStringLiteral("name")] = QString::fromStdString(m_doc.project().settings.name);
    meta[QStringLiteral("saved")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    meta[QStringLiteral("pid")] = QCoreApplication::applicationPid();
    QFile f(base + QStringLiteral(".json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(meta).toJson());
    m_lastSavedSerial = serial;
    LF_LOG_INFO(kLog, "autosaved to {}", (base + QStringLiteral(".lfproj")).toStdString());
    emit autosaved(base + QStringLiteral(".lfproj"));
    return true;
}

void AutosaveManager::discardOwn() {
    discard(ownBase() + QStringLiteral(".lfproj"));
    m_lastSavedSerial = 0;
}

void AutosaveManager::discard(const QString& projectFile) {
    QString base = projectFile;
    if (base.endsWith(QStringLiteral(".lfproj"))) base.chop(7);
    QFile::remove(base + QStringLiteral(".lfproj"));
    QFile::remove(base + QStringLiteral(".json"));
}

std::optional<AutosaveManager::Recoverable> AutosaveManager::findRecoverable() {
    const QDir dir(AppPaths::path(AppPaths::Kind::Autosave));
    std::optional<Recoverable> best;
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("session-*.json")}, QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject meta = QJsonDocument::fromJson(f.readAll()).object();
        const QString projectFile = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".lfproj");
        if (!QFileInfo::exists(projectFile)) continue;
        if (processRunning(meta.value(QStringLiteral("pid")).toInteger())) continue;   // that session is alive (or it is us)
        Recoverable r{projectFile, meta.value(QStringLiteral("original")).toString(), meta.value(QStringLiteral("name")).toString(),
                      QDateTime::fromString(meta.value(QStringLiteral("saved")).toString(), Qt::ISODate)};
        if (!best || r.when > best->when) best = r;
    }
    return best;
}

void AutosaveManager::backupBeforeSave(const QString& path, const QString& projectName, int keep) {
    if (!QFileInfo::exists(path)) return;
    QString folder = projectName.isEmpty() ? QFileInfo(path).completeBaseName() : projectName;
    for (QChar ch : QStringLiteral("\\/:*?\"<>|")) folder.replace(ch, QLatin1Char('_'));
    const QDir dir(QDir(AppPaths::path(AppPaths::Kind::Backups)).filePath(folder));
    QDir().mkpath(dir.absolutePath());
    const QString target = dir.filePath(QStringLiteral("%1 %2.lfproj").arg(QFileInfo(path).completeBaseName(), QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (!QFile::copy(path, target)) {
        LF_LOG_WARN(kLog, "backup of {} failed", path.toStdString());
        return;
    }
    // Keep the newest `keep`.
    auto list = dir.entryInfoList({QStringLiteral("*.lfproj")}, QDir::Files, QDir::Time);
    for (int i = keep; i < list.size(); ++i) QFile::remove(list[i].absoluteFilePath());
}

}  // namespace lf::ui
