#include "ui/media/ImportController.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/media/MediaProbe.h"
#include "lumaforge/project/MediaCommands.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <QUrl>

#include <memory>
#include <mutex>

namespace lf::ui {

namespace {
constexpr const char* kLog = "media";
constexpr const char* kLastDirKey = "import/lastDirectory";

QString joinExt(const std::vector<std::string>& exts) {
    QStringList parts;
    for (const auto& e : exts) parts << QStringLiteral("*.") + QString::fromStdString(e);
    return parts.join(QLatin1Char(' '));
}
}  // namespace

ImportController::ImportController(project::Document& doc, QObject* parent) : QObject(parent), m_doc(doc) {}

ImportController::~ImportController() = default;

QString ImportController::fileDialogFilter() {
    const QString video = joinExt(media::MediaProbe::videoExtensions());
    const QString audio = joinExt(media::MediaProbe::audioExtensions());
    const QString image = joinExt(media::MediaProbe::imageExtensions());
    return tr("All media (%1 %2 %3);;Video (%1);;Audio (%2);;Images (%3);;All files (*)").arg(video, audio, image);
}

QStringList ImportController::collectMediaPaths(const QList<QUrl>& urls) {
    QStringList out;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;
        const QString path = QDir::toNativeSeparators(url.toLocalFile());
        QFileInfo fi(path);
        if (fi.isDir()) {
            // One level deep: dropping a folder of clips is common; recursing whole drives is not.
            for (const QFileInfo& child : QDir(path).entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
                if (media::MediaProbe::looksLikeMedia(child.filePath().toStdString())) out << QDir::toNativeSeparators(child.filePath());
            }
        } else if (fi.isFile()) {
            out << path;   // let the probe decide; extension lists are only hints
        }
    }
    return out;
}

void ImportController::importWithDialog(QWidget* parent) {
    QSettings settings;
    const QString start = settings.value(QString::fromLatin1(kLastDirKey), QDir::homePath()).toString();
    const QStringList files = QFileDialog::getOpenFileNames(parent, tr("Import Media"), start, fileDialogFilter());
    if (files.isEmpty()) return;
    settings.setValue(QString::fromLatin1(kLastDirKey), QFileInfo(files.first()).absolutePath());
    importFiles(files, parent);
}

void ImportController::importFiles(const QStringList& pathsIn, QWidget* parent) {
    QStringList paths;
    for (const QString& p : pathsIn) {
        const QString native = QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
        if (!paths.contains(native, Qt::CaseInsensitive)) paths << native;
    }
    if (paths.isEmpty()) return;

    emit importStarted(paths.size());
    ++m_inFlight;
    LF_LOG_INFO(kLog, "importing {} file(s)", paths.size());

    // Shared state for the batch; the last finishing task hands it to the UI thread.
    struct Batch {
        std::mutex mutex;
        std::vector<Outcome> outcomes;
        int remaining = 0;
    };
    auto batch = std::make_shared<Batch>();
    batch->outcomes.resize(static_cast<std::size_t>(paths.size()));
    batch->remaining = paths.size();
    QPointer<ImportController> self(this);
    QPointer<QWidget> parentPtr(parent);

    for (int i = 0; i < paths.size(); ++i) {
        const QString path = paths[i];
        QThreadPool::globalInstance()->start([self, parentPtr, batch, path, i] {
            Outcome out;
            out.path = path;
            auto probed = media::MediaProbe::probe(path.toStdString());
            if (probed) {
                out.ok = true;
                out.item.path = path.toStdString();
                out.item.displayName = probed->fileName;
                out.item.info = std::move(probed.value());
                out.item.fingerprintSize = out.item.info.fileSize;
                out.item.fingerprintMtime = out.item.info.modifiedTimeUnix;
                out.item.addedUnixTime = QDateTime::currentSecsSinceEpoch();
            } else {
                out.error = QString::fromStdString(probed.error().message);
                LF_LOG_WARN(kLog, "import rejected {}: {}", path.toStdString(), probed.error().fullText());
            }
            bool last = false;
            {
                std::lock_guard lock(batch->mutex);
                batch->outcomes[static_cast<std::size_t>(i)] = std::move(out);
                last = --batch->remaining == 0;
            }
            if (last && self) {
                QMetaObject::invokeMethod(self, [self, parentPtr, batch] {
                    if (self) self->finish(std::move(batch->outcomes), parentPtr);
                }, Qt::QueuedConnection);
            }
        });
    }
}

void ImportController::relink(project::MediaId id, const QString& pathIn, QWidget* parent) {
    const QString path = QDir::toNativeSeparators(QFileInfo(pathIn).absoluteFilePath());
    QPointer<ImportController> self(this);
    QPointer<QWidget> parentPtr(parent);
    QThreadPool::globalInstance()->start([self, parentPtr, id, path] {
        auto probed = media::MediaProbe::probe(path.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, parentPtr, id, path, probed = std::move(probed)]() mutable {
            if (!self) return;
            const project::MediaItem* current = self->m_doc.project().findMedia(id);
            if (!current) return;
            if (!probed) {
                QMessageBox::warning(parentPtr, tr("Relink Media"), tr("%1\n\n%2").arg(QString::fromStdString(probed.error().message), path));
                return;
            }
            project::MediaItem item = *current;
            item.path = path.toStdString();
            item.info = std::move(probed.value());
            item.fingerprintSize = item.info.fileSize;
            item.fingerprintMtime = item.info.modifiedTimeUnix;
            item.offline = false;
            if (item.displayName.empty() || item.displayName == QFileInfo(QString::fromStdString(current->path)).fileName().toStdString()) item.displayName = item.info.fileName;
            self->m_doc.push(std::make_unique<project::RelinkMediaCommand>(self->m_doc, id, std::move(item)));
            LF_LOG_INFO(kLog, "relinked media {} -> {}", project::raw(id), path.toStdString());
        }, Qt::QueuedConnection);
    });
}

void ImportController::finish(std::vector<Outcome> outcomes, QWidget* parent) {
    --m_inFlight;
    std::vector<project::MediaItem> items;
    QStringList failures;
    QStringList duplicates;
    QList<project::MediaId> ids;

    for (auto& o : outcomes) {
        if (!o.ok) {
            failures << QStringLiteral("%1 — %2").arg(QFileInfo(o.path).fileName(), o.error);
            continue;
        }
        if (m_doc.project().findMediaByPath(o.item.path)) {
            duplicates << QFileInfo(o.path).fileName();
            continue;
        }
        o.item.id = m_doc.allocateMediaId();
        ids << o.item.id;
        items.push_back(std::move(o.item));
    }

    if (!items.empty()) {
        m_doc.push(std::make_unique<project::ImportMediaCommand>(m_doc, std::move(items)));
        emit mediaImported(ids);
    }
    emit importFinished(ids.size(), failures.size());
    LF_LOG_INFO(kLog, "import finished: {} added, {} failed, {} duplicate", ids.size(), failures.size(), duplicates.size());

    if (!failures.isEmpty() || !duplicates.isEmpty()) {
        QString text;
        if (!failures.isEmpty()) {
            text += failures.size() == 1 ? tr("One file could not be imported:") : tr("%1 files could not be imported:").arg(failures.size());
            text += QStringLiteral("\n\n") + failures.mid(0, 12).join(QLatin1Char('\n'));
            if (failures.size() > 12) text += tr("\n…and %1 more").arg(failures.size() - 12);
        }
        if (!duplicates.isEmpty()) {
            if (!text.isEmpty()) text += QStringLiteral("\n\n");
            text += tr("Already in the project (skipped): %1").arg(duplicates.join(QStringLiteral(", ")));
        }
        QMessageBox box(parent);
        box.setIcon(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
        box.setWindowTitle(tr("Import Media"));
        box.setText(text);
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }
}

}  // namespace lf::ui
