#include "ui/media/MediaModel.h"

#include "lumaforge/core/StringUtil.h"
#include "ui/Timecode.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QSet>
#include <QUrl>

namespace lf::ui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

QString durationText(const project::MediaItem& m) {
    if (m.type() == media::MediaType::Image) return QStringLiteral("—");
    if (m.info.video && m.info.video->frameRate.isValid() && !m.info.video->frameRate.isZero())
        return formatTimecode(TimePoint{m.info.duration.ticks}, m.info.video->frameRate);
    return formatClock(m.info.duration);
}

QString fpsText(const project::MediaItem& m) {
    if (!m.info.video || !m.info.video->frameRate.isValid() || m.info.video->frameRate.isZero()) return QStringLiteral("—");
    const double f = m.info.video->frameRate.toDouble();
    return (std::abs(f - std::round(f)) < 0.001) ? QString::number(static_cast<int>(std::round(f))) : QString::number(f, 'f', 3);
}
}  // namespace

MediaModel::MediaModel(project::Document& doc, QObject* parent) : QAbstractTableModel(parent), m_doc(doc) {
    m_listenerToken = m_doc.addListener([this](const project::Change& c) { onDocumentChange(c); });
}

MediaModel::~MediaModel() { m_doc.removeListener(m_listenerToken); }

int MediaModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_doc.project().media.size()); }
int MediaModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant MediaModel::data(const QModelIndex& index, int role) const {
    const auto& media = m_doc.project().media;
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(media.size())) return {};
    const project::MediaItem& m = media[static_cast<std::size_t>(index.row())];

    switch (role) {
        case MediaIdRole: return QVariant::fromValue(m.id);
        case MediaTypeRole: return static_cast<int>(m.type());
        case DurationTicksRole: return static_cast<qlonglong>(m.info.duration.ticks);
        case FilePathRole: return qs(m.path);
        case OfflineRole: return m.offline;
        case HasAudioRole: return m.info.hasAudio();
        case HasVideoRole: return m.info.hasVideo();
        case ThumbnailRole: {
            auto it = m_thumbnails.find(m.id);
            if (it != m_thumbnails.end()) return *it;
            if (m.info.hasVideo() && !m.offline && !m_thumbnailRequested.value(m.id, false)) {
                // const-cast is confined to lazy request bookkeeping.
                auto* self = const_cast<MediaModel*>(this);
                self->m_thumbnailRequested.insert(m.id, true);
                emit self->thumbnailNeeded(m.id, qs(m.path));
            }
            return QImage();
        }
        case Qt::ToolTipRole: {
            QStringList lines;
            lines << QStringLiteral("<b>%1</b>").arg(qs(m.displayName).toHtmlEscaped());
            lines << qs(m.path).toHtmlEscaped();
            if (m.info.video) lines << QStringLiteral("%1×%2 · %3 fps · %4").arg(m.info.video->width).arg(m.info.video->height).arg(fpsText(m), qs(m.info.video->codecName));
            if (m.info.audio) lines << QStringLiteral("%1 · %2 ch · %3 Hz").arg(qs(m.info.audio->codecName)).arg(m.info.audio->channels).arg(m.info.audio->sampleRate);
            if (m.type() != media::MediaType::Image) lines << durationText(m);
            lines << qs(formatBytes(m.info.fileSize));
            if (m.offline) lines << QStringLiteral("<span style='color:#E5533D'>File is missing</span>");
            return lines.join(QStringLiteral("<br>"));
        }
        default: break;
    }

    if (role == Qt::DisplayRole || role == SortKeyRole) {
        const bool sort = role == SortKeyRole;
        switch (index.column()) {
            case Name: return qs(m.displayName);
            case Type: return sort ? QVariant(static_cast<int>(m.type())) : QVariant(QString::fromLatin1(media::mediaTypeName(m.type())));
            case Duration: return sort ? QVariant(static_cast<qlonglong>(m.info.duration.ticks)) : QVariant(durationText(m));
            case Resolution:
                if (!m.info.video) return sort ? QVariant(0) : QVariant(QStringLiteral("—"));
                return sort ? QVariant(static_cast<qlonglong>(m.info.video->width) * m.info.video->height)
                            : QVariant(QStringLiteral("%1×%2").arg(m.info.video->width).arg(m.info.video->height));
            case FrameRate: return sort ? QVariant(m.info.video ? m.info.video->frameRate.toDouble() : 0.0) : QVariant(fpsText(m));
            case VideoCodec: return m.info.video ? qs(m.info.video->codecName) : QStringLiteral("—");
            case AudioCodec:
                if (!m.info.audio) return QStringLiteral("—");
                return sort ? QVariant(qs(m.info.audio->codecName)) : QVariant(QStringLiteral("%1 · %2ch").arg(qs(m.info.audio->codecName)).arg(m.info.audio->channels));
            case Size: return sort ? QVariant(static_cast<qulonglong>(m.info.fileSize)) : QVariant(qs(formatBytes(m.info.fileSize)));
            default: return {};
        }
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == Duration || index.column() == FrameRate || index.column() == Size) return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return {};
}

QVariant MediaModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case Name: return tr("Name");
        case Type: return tr("Type");
        case Duration: return tr("Duration");
        case Resolution: return tr("Resolution");
        case FrameRate: return tr("FPS");
        case VideoCodec: return tr("Video");
        case AudioCodec: return tr("Audio");
        case Size: return tr("Size");
        default: return {};
    }
}

Qt::ItemFlags MediaModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QStringList MediaModel::mimeTypes() const { return {QString::fromLatin1(kMimeType), QStringLiteral("text/uri-list")}; }

QMimeData* MediaModel::mimeData(const QModelIndexList& indexes) const {
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    QList<QUrl> urls;
    QSet<int> rows;
    for (const auto& idx : indexes) {
        if (!idx.isValid() || rows.contains(idx.row())) continue;
        rows.insert(idx.row());
        const auto id = idAt(idx);
        out << static_cast<quint64>(id);
        urls << QUrl::fromLocalFile(data(idx.siblingAtColumn(0), FilePathRole).toString());
    }
    auto* mime = new QMimeData();
    mime->setData(QString::fromLatin1(kMimeType), payload);
    mime->setUrls(urls);
    return mime;
}

QList<project::MediaId> MediaModel::decodeMime(const QMimeData* data) {
    QList<project::MediaId> ids;
    if (!data || !data->hasFormat(QString::fromLatin1(kMimeType))) return ids;
    QByteArray payload = data->data(QString::fromLatin1(kMimeType));
    QDataStream in(&payload, QIODevice::ReadOnly);
    while (!in.atEnd()) {
        quint64 v = 0;
        in >> v;
        ids << static_cast<project::MediaId>(v);
    }
    return ids;
}

project::MediaId MediaModel::idAt(const QModelIndex& index) const {
    return index.isValid() ? index.data(MediaIdRole).value<project::MediaId>() : project::MediaId::Invalid;
}

QModelIndex MediaModel::indexOf(project::MediaId id) const {
    const auto& media = m_doc.project().media;
    for (std::size_t i = 0; i < media.size(); ++i) if (media[i].id == id) return index(static_cast<int>(i), 0);
    return {};
}

const project::MediaItem* MediaModel::item(project::MediaId id) const { return m_doc.project().findMedia(id); }

void MediaModel::setThumbnail(project::MediaId id, const QImage& image) {
    m_thumbnails.insert(id, image);
    const QModelIndex idx = indexOf(id);
    if (idx.isValid()) emit dataChanged(idx, idx, {ThumbnailRole, Qt::DecorationRole});
}

void MediaModel::onDocumentChange(const project::Change& change) {
    using K = project::Change::Kind;
    switch (change.kind) {
        case K::Reset:
        case K::MediaAdded:
        case K::MediaRemoved:
            rebuild();
            break;
        case K::MediaChanged:
            for (auto id : change.mediaIds) {
                // A relink changes the file: the thumbnail must be produced again.
                m_thumbnails.remove(id);
                m_thumbnailRequested.remove(id);
                const QModelIndex idx = indexOf(id);
                if (idx.isValid()) emit dataChanged(idx, index(idx.row(), ColumnCount - 1));
            }
            break;
        default: break;
    }
}

void MediaModel::rebuild() {
    // Media lists are small (tens to hundreds); a full reset keeps the
    // model trivially correct. Thumbnails persist across resets by id.
    beginResetModel();
    for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();) {
        if (!m_doc.project().findMedia(it.key())) { m_thumbnailRequested.remove(it.key()); it = m_thumbnails.erase(it); }
        else ++it;
    }
    endResetModel();
}

}  // namespace lf::ui
