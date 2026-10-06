#pragma once

#include "lumaforge/project/Document.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <optional>

class QTimer;

namespace lf::ui {

// Crash safety for the open project.
//  - Autosave: while there are unsaved changes, the project is written every
//    few minutes to the data folder's Autosave as <session>.lfproj with a
//    <session>.json describing it (original file, name, time, process id).
//    A clean exit or a save of the real file removes it.
//  - Recovery: an autosave left behind by a process that is no longer
//    running means LumaForge did not exit cleanly; it is offered on start.
//  - Backups: before a save replaces a project file, the previous version is
//    copied to Backups/<project>/ (the newest 10 are kept).
class AutosaveManager : public QObject {
    Q_OBJECT
public:
    explicit AutosaveManager(project::Document& doc, QObject* parent = nullptr);
    ~AutosaveManager() override;

    void setIntervalMinutes(int minutes);   // 0 disables autosave
    [[nodiscard]] int intervalMinutes() const noexcept { return m_minutes; }

    // Writes the autosave now if there is something unsaved; returns true when written.
    bool autosaveNow();
    // Removes this session's autosave (after a save or on a clean exit).
    void discardOwn();

    struct Recoverable {
        QString projectFile;    // the autosave (a normal project file)
        QString originalPath;   // where the project was saved, empty if never
        QString name;
        QDateTime when;
    };
    // The newest autosave from a session that is no longer running.
    [[nodiscard]] static std::optional<Recoverable> findRecoverable();
    static void discard(const QString& autosaveProjectFile);

    // Copies `path` (if it exists) to the backups folder before it is overwritten.
    static void backupBeforeSave(const QString& path, const QString& projectName, int keep = 10);

signals:
    void autosaved(const QString& path);

private:
    [[nodiscard]] QString ownBase() const;

    project::Document& m_doc;
    QTimer* m_timer;
    int m_minutes = 2;
    std::uint64_t m_lastSavedSerial = 0;   // undo index + dirty state when last written
    int m_listener = 0;
};

}  // namespace lf::ui
