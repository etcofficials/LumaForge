#pragma once

#include <QString>

namespace lf::ui {

// Where LumaForge keeps everything it generates: projects, caches, proxies,
// exports, logs, autosaves and recordings. Nothing large goes to the user
// profile by default:
//
//   1. the user's choice (Settings > General > Data folder), else
//   2. <drive>:\LumaForgeData when the application runs from a non-system
//      drive (e.g. G:\LumaForgeData), else
//   3. <application folder>\LumaForgeData (portable, still not AppData).
//
// Individual locations (cache, proxies, exports, projects) can be redirected
// separately. Every accessor creates its folder on first use.
class AppPaths {
public:
    enum class Kind { Projects, Cache, Thumbnails, Waveforms, Preview, Proxies, Exports, Logs, Autosave, Recordings, Backups };

    // Must be called once after QSettings is configured (main()).
    static void initialise();

    [[nodiscard]] static QString dataRoot();
    [[nodiscard]] static QString defaultDataRoot();
    static void setDataRoot(const QString& path);   // empty = default

    [[nodiscard]] static QString path(Kind kind);   // created if missing
    // User override for a kind (empty = derived from the data/cache root).
    [[nodiscard]] static QString override(Kind kind);
    static void setOverride(Kind kind, const QString& path);
    [[nodiscard]] static bool isOverridable(Kind kind) noexcept;
    [[nodiscard]] static QString displayName(Kind kind);

    // True when `path` is on the system drive (usually C:) - used to warn.
    [[nodiscard]] static bool onSystemDrive(const QString& path);
};

}  // namespace lf::ui
