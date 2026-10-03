#pragma once

#include "lumaforge/project/Document.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QImage>

#include <functional>

namespace lf::ui {

// Table model over Document::project().media. Column 0 doubles as the
// icon-view item (decoration = thumbnail). Thumbnails are supplied
// asynchronously through setThumbnail().
class MediaModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Type, Duration, Resolution, FrameRate, VideoCodec, AudioCodec, Size, Uses, ColumnCount };
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
        BinRole,                // QString, empty = unsorted
        UseCountRole,           // int: clips in the sequence that use the item
        ProxyStateRole,         // int: ProxyController::State
        ProxyProgressRole,      // double 0..1 while a proxy is being made
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
    [[nodiscard]] int useCount(project::MediaId id) const { return m_useCount.value(id, 0); }
    // Proxy status source (state as int, progress); refreshProxy() repaints one item.
    void setProxyInfo(std::function<std::pair<int, double>(project::MediaId)> info) { m_proxyInfo = std::move(info); }
    void refreshProxy(project::MediaId id);

    static QList<project::MediaId> decodeMime(const QMimeData* data);

public slots:
    void setThumbnail(lf::project::MediaId id, const QImage& image);

signals:
    void thumbnailNeeded(lf::project::MediaId id, const QString& path);

private:
    void onDocumentChange(const project::Change& change);
    void rebuild();
    void recountUses();

    project::Document& m_doc;
    int m_listenerToken = 0;
    QHash<project::MediaId, QImage> m_thumbnails;
    QHash<project::MediaId, bool> m_thumbnailRequested;
    QHash<project::MediaId, int> m_useCount;
    std::function<std::pair<int, double>(project::MediaId)> m_proxyInfo;
};

}  // namespace lf::ui
