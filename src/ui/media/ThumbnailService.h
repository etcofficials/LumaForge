#pragma once

#include "lumaforge/project/Ids.h"

#include <QImage>
#include <QObject>
#include <QSet>

namespace lf::ui {

// Generates media-bin thumbnails on the global thread pool, one request per
// media item, and delivers them on the UI thread. Never blocks the caller.
class ThumbnailService : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailService(QObject* parent = nullptr);

    static constexpr int kMaxWidth = 320;
    static constexpr int kMaxHeight = 180;

public slots:
    void request(lf::project::MediaId id, const QString& path);

signals:
    void ready(lf::project::MediaId id, const QImage& image);

private:
    QSet<project::MediaId> m_pending;
};

}  // namespace lf::ui
