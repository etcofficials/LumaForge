#include "ui/media/MediaTileDelegate.h"

#include "lumaforge/media/MediaInfo.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"
#include "ui/media/MediaModel.h"

#include <QPainter>
#include <QPainterPath>

namespace lf::ui {

namespace {
const char* glyphFor(int type) {
    switch (static_cast<media::MediaType>(type)) {
        case media::MediaType::Audio: return "media-audio";
        case media::MediaType::Image: return "media-image";
        default: return "media-video";
    }
}

void drawSelectionFrame(QPainter* p, const QRect& r, const QStyleOptionViewItem& opt) {
    const auto& pal = Theme::palette();
    if (opt.state & QStyle::State_Selected) {
        p->setPen(QPen(pal.accent, 2));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 4, 4);
    } else if (opt.state & QStyle::State_MouseOver) {
        p->fillRect(r, QColor(255, 255, 255, 10));
    }
}
}  // namespace

// ---------------------------------------------------------------- tiles

MediaTileDelegate::MediaTileDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize MediaTileDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return {kTileWidth, kTileHeight}; }

void MediaTileDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const {
    const auto& pal = Theme::palette();
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect tile = opt.rect.adjusted(3, 3, -3, -3);
    const QRect thumbRect(tile.left(), tile.top(), tile.width(), kThumbHeight);

    // Thumbnail area
    QPainterPath clip;
    clip.addRoundedRect(thumbRect, 3, 3);
    p->fillPath(clip, pal.bgInput);
    const QImage thumb = index.data(MediaModel::ThumbnailRole).value<QImage>();
    const int type = index.data(MediaModel::MediaTypeRole).toInt();
    if (!thumb.isNull()) {
        p->setClipPath(clip);
        const QSize scaled = thumb.size().scaled(thumbRect.size(), Qt::KeepAspectRatio);
        const QRect dst(thumbRect.center().x() - scaled.width() / 2, thumbRect.center().y() - scaled.height() / 2, scaled.width(), scaled.height());
        p->drawImage(dst, thumb);
        p->setClipping(false);
    } else {
        const QPixmap glyph = Icons::pixmap(QString::fromLatin1(glyphFor(type)), 28, pal.textMuted, p->device()->devicePixelRatioF());
        p->drawPixmap(thumbRect.center().x() - 14, thumbRect.center().y() - 14, glyph);
    }

    // Duration badge
    const qlonglong ticks = index.data(MediaModel::DurationTicksRole).toLongLong();
    if (ticks > 0) {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() - 1);
        p->setFont(f);
        const QString text = formatClock(Duration{ticks}).mid(3, 5);   // mm:ss
        const QRect tr = p->fontMetrics().boundingRect(text).adjusted(-4, -1, 4, 1);
        const QRect badge(thumbRect.right() - tr.width() - 4, thumbRect.bottom() - tr.height() - 4, tr.width(), tr.height());
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 170));
        p->drawRoundedRect(badge, 2, 2);
        p->setPen(pal.textPrimary);
        p->drawText(badge, Qt::AlignCenter, text);
    }
    if (index.data(MediaModel::OfflineRole).toBool()) {
        p->setPen(Qt::NoPen);
        p->setBrush(pal.danger);
        p->drawEllipse(QPoint(thumbRect.left() + 9, thumbRect.top() + 9), 4, 4);
    }

    // Name
    p->setFont(opt.font);
    p->setPen((opt.state & QStyle::State_Selected) ? pal.textPrimary : pal.textSecondary);
    const QRect nameRect(tile.left() + 2, thumbRect.bottom() + 4, tile.width() - 4, tile.bottom() - thumbRect.bottom() - 4);
    const QString name = p->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, nameRect.width());
    p->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, name);

    drawSelectionFrame(p, opt.rect.adjusted(1, 1, -1, -1), opt);
    p->restore();
}

// ---------------------------------------------------------------- list rows

MediaNameDelegate::MediaNameDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize MediaNameDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& index) const {
    QSize s = QStyledItemDelegate::sizeHint(opt, index);
    s.setHeight(std::max(s.height(), 24));
    return s;
}

void MediaNameDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const auto& pal = Theme::palette();
    p->save();
    if (opt.state & QStyle::State_Selected) p->fillRect(opt.rect, pal.selection);
    else if (opt.state & QStyle::State_MouseOver) p->fillRect(opt.rect, pal.bgHover);
    const int type = index.data(MediaModel::MediaTypeRole).toInt();
    const bool offline = index.data(MediaModel::OfflineRole).toBool();
    const QPixmap glyph = Icons::pixmap(QString::fromLatin1(glyphFor(type)), 16, offline ? pal.danger : pal.textMuted, p->device()->devicePixelRatioF());
    p->drawPixmap(opt.rect.left() + 6, opt.rect.center().y() - 8, glyph);
    p->setPen(offline ? pal.danger : pal.textPrimary);
    const QRect textRect = opt.rect.adjusted(28, 0, -4, 0);
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, opt.fontMetrics.elidedText(opt.text, Qt::ElideMiddle, textRect.width()));
    p->restore();
}

}  // namespace lf::ui
