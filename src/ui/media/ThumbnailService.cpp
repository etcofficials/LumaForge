#include "ui/media/ThumbnailService.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/media/Thumbnailer.h"

#include "ui/AppPaths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

namespace lf::ui {

namespace {
class ThumbnailTask final : public QRunnable {
public:
    ThumbnailTask(QPointer<ThumbnailService> owner, project::MediaId id, QString path) : m_owner(std::move(owner)), m_id(id), m_path(std::move(path)) {
        setAutoDelete(true);
    }
    void run() override {
        // Disk cache keyed by file, size and modification time: reopening a project
        // shows its thumbnails without decoding anything.
        const QFileInfo fi(m_path);
        const QByteArray key = QCryptographicHash::hash((m_path + QLatin1Char('|') + QString::number(fi.size()) + QLatin1Char('|') +
                                                         QString::number(fi.lastModified().toMSecsSinceEpoch())).toUtf8(),
                                                        QCryptographicHash::Sha1).toHex().left(20);
        const QString cacheFile = QDir(AppPaths::path(AppPaths::Kind::Thumbnails)).filePath(QString::fromLatin1(key) + QStringLiteral(".png"));
        QImage cached;
        if (QFileInfo::exists(cacheFile) && cached.load(cacheFile)) {
            deliver(cached.convertToFormat(QImage::Format_ARGB32));
            return;
        }
        media::Thumbnailer::Options opt;
        opt.maxWidth = ThumbnailService::kMaxWidth;
        opt.maxHeight = ThumbnailService::kMaxHeight;
        auto result = media::Thumbnailer::render(m_path.toStdString(), opt);
        QImage image;
        if (result) {
            const auto& img = result.value();
            // Deep copy: the media::Image buffer dies with this task.
            image = QImage(img.bgra.data(), img.width, img.height, img.stride(), QImage::Format_ARGB32).copy();
            image.save(cacheFile, "PNG");
        } else {
            LF_LOG_WARN("media", "thumbnail failed for {}: {}", m_path.toStdString(), result.error().fullText());
        }
        deliver(image);
    }

    void deliver(const QImage& image) {
        if (m_owner) {
            QMetaObject::invokeMethod(m_owner, [owner = m_owner, id = m_id, image] {
                if (owner) emit owner->ready(id, image);
            }, Qt::QueuedConnection);
        }
    }

private:
    QPointer<ThumbnailService> m_owner;
    project::MediaId m_id;
    QString m_path;
};
}  // namespace

ThumbnailService::ThumbnailService(QObject* parent) : QObject(parent) {
    connect(this, &ThumbnailService::ready, this, [this](project::MediaId id, const QImage&) { m_pending.remove(id); });
}

void ThumbnailService::request(project::MediaId id, const QString& path) {
    if (m_pending.contains(id)) return;
    m_pending.insert(id);
    QThreadPool::globalInstance()->start(new ThumbnailTask(this, id, path));
}

}  // namespace lf::ui
