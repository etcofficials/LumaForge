#include "ui/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStorageInfo>

namespace lf::ui {

namespace {
constexpr const char* kRootKey = "paths/dataRoot";

const char* overrideKey(AppPaths::Kind k) {
    switch (k) {
        case AppPaths::Kind::Projects: return "paths/projects";
        case AppPaths::Kind::Cache: return "paths/cache";
        case AppPaths::Kind::Proxies: return "paths/proxies";
        case AppPaths::Kind::Exports: return "paths/exports";
        default: return nullptr;
    }
}

QString subdir(AppPaths::Kind k) {
    switch (k) {
        case AppPaths::Kind::Projects: return QStringLiteral("Projects");
        case AppPaths::Kind::Cache: return QString();
        case AppPaths::Kind::Thumbnails: return QStringLiteral("Thumbnails");
        case AppPaths::Kind::Waveforms: return QStringLiteral("Waveforms");
        case AppPaths::Kind::Preview: return QStringLiteral("Preview");
        case AppPaths::Kind::Proxies: return QStringLiteral("Proxies");
        case AppPaths::Kind::Exports: return QStringLiteral("Exports");
        case AppPaths::Kind::Logs: return QStringLiteral("Logs");
        case AppPaths::Kind::Autosave: return QStringLiteral("Autosave");
        case AppPaths::Kind::Recordings: return QStringLiteral("Recordings");
        case AppPaths::Kind::Backups: return QStringLiteral("Backups");
    }
    return QString();
}

QString ensure(const QString& path) {
    QDir().mkpath(path);
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString systemDriveRoot() {
    const QString sys = qEnvironmentVariable("SystemDrive", QStringLiteral("C:"));
    return sys.left(2).toUpper();
}
}  // namespace

void AppPaths::initialise() { ensure(dataRoot()); }

QString AppPaths::defaultDataRoot() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString drive = QDir::toNativeSeparators(appDir).left(2).toUpper();
    if (drive.size() == 2 && drive[1] == QLatin1Char(':') && drive != systemDriveRoot()) return drive + QStringLiteral("\\LumaForgeData");
    return QDir::toNativeSeparators(appDir + QStringLiteral("/LumaForgeData"));
}

QString AppPaths::dataRoot() {
    const QString chosen = QSettings().value(QString::fromLatin1(kRootKey)).toString();
    return QDir::toNativeSeparators(chosen.isEmpty() ? defaultDataRoot() : chosen);
}

void AppPaths::setDataRoot(const QString& path) {
    QSettings s;
    if (path.isEmpty() || QDir::toNativeSeparators(path) == defaultDataRoot()) s.remove(QString::fromLatin1(kRootKey));
    else s.setValue(QString::fromLatin1(kRootKey), QDir::toNativeSeparators(path));
}

bool AppPaths::isOverridable(Kind kind) noexcept { return overrideKey(kind) != nullptr; }

QString AppPaths::override(Kind kind) {
    const char* key = overrideKey(kind);
    return key ? QSettings().value(QString::fromLatin1(key)).toString() : QString();
}

void AppPaths::setOverride(Kind kind, const QString& path) {
    const char* key = overrideKey(kind);
    if (!key) return;
    QSettings s;
    if (path.isEmpty()) s.remove(QString::fromLatin1(key));
    else s.setValue(QString::fromLatin1(key), QDir::toNativeSeparators(path));
}

QString AppPaths::path(Kind kind) {
    if (const QString o = override(kind); !o.isEmpty()) return ensure(o);
    switch (kind) {
        case Kind::Cache: return ensure(dataRoot());
        case Kind::Thumbnails:
        case Kind::Waveforms:
        case Kind::Preview: {
            // Generated caches live under the cache root (= data root unless redirected).
            const QString cacheRoot = override(Kind::Cache).isEmpty() ? dataRoot() : override(Kind::Cache);
            return ensure(cacheRoot + QLatin1Char('/') + subdir(kind));
        }
        default: return ensure(dataRoot() + QLatin1Char('/') + subdir(kind));
    }
}

QString AppPaths::displayName(Kind kind) {
    switch (kind) {
        case Kind::Projects: return QCoreApplication::translate("AppPaths", "Projects");
        case Kind::Cache: return QCoreApplication::translate("AppPaths", "Cache (thumbnails, waveforms, previews)");
        case Kind::Thumbnails: return QCoreApplication::translate("AppPaths", "Thumbnails");
        case Kind::Waveforms: return QCoreApplication::translate("AppPaths", "Waveforms");
        case Kind::Preview: return QCoreApplication::translate("AppPaths", "Preview renders");
        case Kind::Proxies: return QCoreApplication::translate("AppPaths", "Proxies");
        case Kind::Exports: return QCoreApplication::translate("AppPaths", "Exports");
        case Kind::Logs: return QCoreApplication::translate("AppPaths", "Logs");
        case Kind::Autosave: return QCoreApplication::translate("AppPaths", "Autosave");
        case Kind::Recordings: return QCoreApplication::translate("AppPaths", "Recordings");
        case Kind::Backups: return QCoreApplication::translate("AppPaths", "Backups");
    }
    return QString();
}

bool AppPaths::onSystemDrive(const QString& path) {
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).left(2).toUpper() == systemDriveRoot();
}

}  // namespace lf::ui
