#pragma once

#include <QStyledItemDelegate>

namespace lf::ui {

// Grid tile: 16:9 thumbnail (or type glyph), duration badge, offline badge,
// file name underneath. Sized so a 300 px panel shows two columns.
class MediaTileDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MediaTileDelegate(QObject* parent = nullptr);

    static constexpr int kTileWidth = 132;
    static constexpr int kThumbHeight = 74;
    static constexpr int kTileHeight = kThumbHeight + 34;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

// Compact row for the list view's name column: small type glyph + name.
class MediaNameDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MediaNameDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

}  // namespace lf::ui
