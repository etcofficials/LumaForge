#pragma once

#include "lumaforge/project/Document.h"

#include <QObject>
#include <QStringList>

namespace lf::ui {

// Turns file paths into media-bin items: probes on worker threads, then
// pushes one ImportMediaCommand on the UI thread and reports failures in a
// single dialog. Duplicates (same path) are skipped and reported.
class ImportController : public QObject {
    Q_OBJECT
public:
    explicit ImportController(project::Document& doc, QObject* parent = nullptr);
    ~ImportController() override;

    // Native file dialog, then importFiles().
    void importWithDialog(QWidget* parent);
    void importFiles(const QStringList& paths, QWidget* parent = nullptr);
    // Probes `path` off the UI thread and points the media item at it (undoable).
    void relink(project::MediaId id, const QString& path, QWidget* parent = nullptr);

    // Filter string for QFileDialog built from MediaProbe's extension lists.
    static QString fileDialogFilter();
    // Keeps only files (expanding dropped folders one level) that look like media.
    static QStringList collectMediaPaths(const QList<QUrl>& urls);

signals:
    void importStarted(int count);
    void importFinished(int imported, int failed);
    void mediaImported(const QList<lf::project::MediaId>& ids);

private:
    struct Outcome {
        QString path;
        bool ok = false;
        project::MediaItem item;
        QString error;
    };
    void finish(std::vector<Outcome> outcomes, QWidget* parent);

    project::Document& m_doc;
    int m_inFlight = 0;
};

}  // namespace lf::ui
