#pragma once

#include "lumaforge/project/Document.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QImage>

namespace lf::ui {

// Table model over Document::project().media. Column 0 doubles as the
// icon-view item (decoration = thumbnail). Thumbnails are supplied
// asynchronously through setThumbnail().
class MediaModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Type, Duration, Resolution, FrameRate, VideoCodec, AudioCodec, Size, ColumnCount };
    enum Roles {
        MediaIdRole = Qt::UserRole + 1,
        MediaTypeRole,          // int(media::MediaType)
        DurationTicksRole,      // qlonglong
        ThumbnailRole,          // QImage (may be null)
        FilePathRole,
        OfflineRole,
        HasAudioRole,
        HasVideoRole,
        SortKeyRole,            // per-column sortable value
    };
    static constexpr const char* kMimeType = "application/x-lumaforge-media-ids";

    explicit MediaModel(project::Document& doc, QObject* parent = nullptr);
    ~MediaModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

    [[nodiscard]] project::MediaId idAt(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexOf(project::MediaId id) const;
    [[nodiscard]] const project::MediaItem* item(project::MediaId id) const;

    static QList<project::MediaId> decodeMime(const QMimeData* data);

public slots:
    void setThumbnail(lf::project::MediaId id, const QImage& image);

signals:
    void thumbnailNeeded(lf::project::MediaId id, const QString& path);

private:
    void onDocumentChange(const project::Change& change);
    void rebuild();

    project::Document& m_doc;
    int m_listenerToken = 0;
    QHash<project::MediaId, QImage> m_thumbnails;
    QHash<project::MediaId, bool> m_thumbnailRequested;
};

}  // namespace lf::ui
