#pragma once

#include "lumaforge/project/Document.h"

#include <QObject>
#include <QStringList>

class QWidget;

namespace lf::ui {

// New / Open / Save / Save As / recent files for the one open Document.
// Every entry point that would discard unsaved work asks first; a cancelled
// dialog leaves the document untouched.
class ProjectController : public QObject {
    Q_OBJECT
public:
    explicit ProjectController(project::Document& doc, QObject* parent = nullptr);

    static QString fileDialogFilter();
    static bool isProjectFile(const QString& path);

    [[nodiscard]] QStringList recentFiles() const;
    [[nodiscard]] QString displayName() const;   // project name or file stem

public slots:
    bool newProject(QWidget* parent);
    bool open(QWidget* parent);
    bool openFile(const QString& path, QWidget* parent);
    bool save(QWidget* parent);
    bool saveAs(QWidget* parent);
    // Returns false when the user cancelled (caller must abort what it was doing).
    bool maybeSave(QWidget* parent);
    void clearRecentFiles();

signals:
    void projectLoaded(const QString& path);
    void projectSaved(const QString& path);
    void recentFilesChanged();
    void statusMessage(const QString& text);

private:
    bool saveTo(const QString& path, QWidget* parent);
    void addRecent(const QString& path);
    void removeRecent(const QString& path);

    project::Document& m_doc;
};

}  // namespace lf::ui
