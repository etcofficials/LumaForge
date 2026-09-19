#include "ui/project/ProjectController.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/project/ProjectSerializer.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace lf::ui {

namespace {
constexpr const char* kLog = "project";
constexpr const char* kRecentKey = "project/recent";
constexpr const char* kLastDirKey = "project/lastDir";
constexpr int kMaxRecent = 10;

QString toNative(const QString& p) { return QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath()); }
}  // namespace

ProjectController::ProjectController(project::Document& doc, QObject* parent) : QObject(parent), m_doc(doc) {}

QString ProjectController::fileDialogFilter() {
    return tr("LumaForge project (*%1);;All files (*)").arg(QString::fromLatin1(project::serializer::kFileExtension));
}

bool ProjectController::isProjectFile(const QString& path) {
    return path.endsWith(QString::fromLatin1(project::serializer::kFileExtension), Qt::CaseInsensitive);
}

QStringList ProjectController::recentFiles() const { return QSettings().value(QString::fromLatin1(kRecentKey)).toStringList(); }

QString ProjectController::displayName() const {
    const QString name = QString::fromStdString(m_doc.project().settings.name);
    if (!name.isEmpty()) return name;
    if (!m_doc.filePath().empty()) return QFileInfo(QString::fromStdString(m_doc.filePath())).completeBaseName();
    return tr("Untitled");
}

// ---------------------------------------------------------------- new / open

bool ProjectController::maybeSave(QWidget* parent) {
    if (!m_doc.isDirty()) return true;
    const auto answer = QMessageBox::question(parent, tr("Unsaved Changes"),
                                              tr("\"%1\" has unsaved changes.\nDo you want to save them?").arg(displayName()),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save(parent);
    return true;
}

bool ProjectController::newProject(QWidget* parent) {
    if (!maybeSave(parent)) return false;
    m_doc.setFilePath({});
    m_doc.reset(project::Project{});
    emit projectLoaded(QString());
    emit statusMessage(tr("New project"));
    return true;
}

bool ProjectController::open(QWidget* parent) {
    if (!maybeSave(parent)) return false;
    QSettings settings;
    const QString start = settings.value(QString::fromLatin1(kLastDirKey), QDir::homePath()).toString();
    const QString file = QFileDialog::getOpenFileName(parent, tr("Open Project"), start, fileDialogFilter());
    if (file.isEmpty()) return false;
    settings.setValue(QString::fromLatin1(kLastDirKey), QFileInfo(file).absolutePath());
    return openFile(file, parent);
}

bool ProjectController::openFile(const QString& pathIn, QWidget* parent) {
    const QString path = toNative(pathIn);
    auto loaded = project::serializer::loadFromFile(path.toStdString());
    if (!loaded) {
        removeRecent(path);
        QMessageBox::warning(parent, tr("Open Project"), tr("%1\n\n%2").arg(QString::fromStdString(loaded.error().message), path));
        return false;
    }
    m_doc.setFilePath(path.toStdString());
    m_doc.reset(std::move(loaded->project));
    addRecent(path);
    emit projectLoaded(path);
    if (!loaded->offlineMedia.empty()) {
        QStringList names;
        for (const auto& n : loaded->offlineMedia) names << QString::fromStdString(n);
        QMessageBox::information(parent, tr("Missing Media"),
                                 tr("%n media file(s) could not be found and are shown as offline:\n\n%1\n\n"
                                    "Right-click an offline item in the Media panel and choose Relink to point it at the file.",
                                    nullptr, names.size())
                                     .arg(names.mid(0, 12).join(QLatin1Char('\n')) + (names.size() > 12 ? tr("\n…") : QString())));
    }
    emit statusMessage(tr("Opened %1").arg(QFileInfo(path).fileName()));
    return true;
}

// ---------------------------------------------------------------- save

bool ProjectController::save(QWidget* parent) {
    if (m_doc.filePath().empty()) return saveAs(parent);
    return saveTo(QString::fromStdString(m_doc.filePath()), parent);
}

bool ProjectController::saveAs(QWidget* parent) {
    QSettings settings;
    QString start = settings.value(QString::fromLatin1(kLastDirKey), QDir::homePath()).toString();
    if (!m_doc.filePath().empty()) start = QString::fromStdString(m_doc.filePath());
    else start = QDir(start).filePath(displayName() + QString::fromLatin1(project::serializer::kFileExtension));
    QString file = QFileDialog::getSaveFileName(parent, tr("Save Project As"), start, fileDialogFilter());
    if (file.isEmpty()) return false;
    if (!isProjectFile(file)) file += QString::fromLatin1(project::serializer::kFileExtension);
    settings.setValue(QString::fromLatin1(kLastDirKey), QFileInfo(file).absolutePath());
    return saveTo(file, parent);
}

bool ProjectController::saveTo(const QString& pathIn, QWidget* parent) {
    const QString path = toNative(pathIn);
    // An unnamed project takes the file's name so the title bar and the file agree.
    if (m_doc.project().settings.name.empty() || m_doc.project().settings.name == "Untitled") {
        m_doc.project().settings.name = QFileInfo(path).completeBaseName().toStdString();
        m_doc.notify(project::Change{project::Change::Kind::Settings, {}, {}});
    }
    auto saved = project::serializer::saveToFile(m_doc.project(), path.toStdString());
    if (!saved) {
        LF_LOG_ERROR(kLog, "save failed: {}", saved.error().fullText());
        QMessageBox::warning(parent, tr("Save Project"), tr("%1\n\n%2").arg(QString::fromStdString(saved.error().message), path));
        return false;
    }
    m_doc.setFilePath(path.toStdString());
    m_doc.markSaved();
    addRecent(path);
    emit projectSaved(path);
    emit statusMessage(tr("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

// ---------------------------------------------------------------- recent files

void ProjectController::addRecent(const QString& path) {
    QStringList recent = recentFiles();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kMaxRecent) recent.removeLast();
    QSettings().setValue(QString::fromLatin1(kRecentKey), recent);
    emit recentFilesChanged();
}

void ProjectController::removeRecent(const QString& path) {
    QStringList recent = recentFiles();
    if (recent.removeAll(path) > 0) {
        QSettings().setValue(QString::fromLatin1(kRecentKey), recent);
        emit recentFilesChanged();
    }
}

void ProjectController::clearRecentFiles() {
    QSettings().remove(QString::fromLatin1(kRecentKey));
    emit recentFilesChanged();
}

}  // namespace lf::ui
