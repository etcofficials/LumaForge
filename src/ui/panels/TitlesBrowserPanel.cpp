#include "ui/panels/TitlesBrowserPanel.h"

#include "lumaforge/project/TitlePresets.h"
#include "ui/DragMime.h"

#include <QFontMetricsF>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lf::ui {

namespace {
constexpr int kPresetRole = Qt::UserRole + 1;
constexpr int kTileW = 104, kTileH = 58;

QColor toColor(const ColorRgba& c, float alphaScale = 1.0f) {
    return QColor::fromRgbF(c.r, c.g, c.b, std::clamp(c.a * alphaScale, 0.0f, 1.0f));
}

class TitleList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(mime::kTitle)}; }
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        if (items.isEmpty()) return nullptr;
        auto* data = new QMimeData();
        data->setData(QString::fromLatin1(mime::kTitle), QByteArray::number(items.front()->data(kPresetRole).toInt()));
        return data;
    }
};
}  // namespace

QImage TitlesBrowserPanel::preview(int preset, int width, int height) {
    // A stand-in for the real text rasteriser: same font, colours, box and
    // placement on a neutral backdrop. Text is drawn 2.2x larger than strict
    // scale so it stays legible on a small tile, and kept inside the tile.
    constexpr int kFrameW = 1920, kFrameH = 1080;
    const project::Clip c = project::makeTitleClip(preset, kFrameW, kFrameH);
    const double place = static_cast<double>(height) / kFrameH;   // positions
    const double s = place * 2.2;                                  // text metrics
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QLinearGradient bg(0, 0, width, height);
    bg.setColorAt(0, QColor(0x3a, 0x45, 0x55));
    bg.setColorAt(1, QColor(0x1d, 0x22, 0x2b));
    p.fillRect(img.rect(), bg);

    const TextParams& t = c.text;
    QFont f(QString::fromStdString(t.fontFamily));
    f.setPixelSize(std::max(4, static_cast<int>(t.fontSize * s)));
    f.setBold(t.bold);
    f.setItalic(t.italic);
    f.setLetterSpacing(QFont::AbsoluteSpacing, t.letterSpacing * s);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const QStringList lines = QString::fromStdString(t.text).split(QLatin1Char('\n'));
    double blockW = 0;
    for (const auto& l : lines) blockW = std::max(blockW, fm.horizontalAdvance(l));
    const double lineH = fm.height() * t.lineSpacing;
    const double blockH = lineH * lines.size();
    const double pad = t.backgroundOpacity > 0.0f ? t.backgroundPadding * s : 0.0;
    const double margin = 3.0 + pad;
    const double cx = std::clamp(width / 2.0 + c.transform.positionX * place, margin + blockW / 2, std::max(margin + blockW / 2, width - margin - blockW / 2));
    const double cy = std::clamp(height / 2.0 + c.transform.positionY * place, margin + blockH / 2, std::max(margin + blockH / 2, height - margin - blockH / 2));
    const QRectF block(cx - blockW / 2, cy - blockH / 2, blockW, blockH);
    if (t.backgroundOpacity > 0.0f) {
        p.setPen(Qt::NoPen);
        p.setBrush(toColor(t.backgroundColor, t.backgroundOpacity));
        p.drawRect(block.adjusted(-pad, -pad, pad, pad));
    }
    const int align = t.align == TextAlign::Left ? Qt::AlignLeft : t.align == TextAlign::Right ? Qt::AlignRight : Qt::AlignHCenter;
    for (int i = 0; i < lines.size(); ++i) {
        const QRectF r(block.left(), block.top() + i * lineH, blockW, lineH);
        if (t.shadow) {
            p.setPen(toColor(t.shadowColor));
            p.drawText(r.translated(t.shadowOffsetX * s, t.shadowOffsetY * s), align | Qt::AlignVCenter, lines[i]);
        }
        p.setPen(toColor(t.color));
        p.drawText(r, align | Qt::AlignVCenter, lines[i]);
    }
    return img;
}

TitlesBrowserPanel::TitlesBrowserPanel(QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    m_list = new TitleList(this);
    m_list->setObjectName(QStringLiteral("titlesList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(QSize(kTileW, kTileH));
    m_list->setGridSize(QSize(kTileW + 8, kTileH + 26));
    m_list->setUniformItemSizes(true);
    m_list->setWordWrap(false);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFocusPolicy(Qt::ClickFocus);
    layout->addWidget(m_list, 1);

    const auto presets = project::titlePresets();
    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        QImage img = preview(i, static_cast<int>(kTileW * dpr), static_cast<int>(kTileH * dpr));
        img.setDevicePixelRatio(dpr);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(img)), tr(presets[static_cast<std::size_t>(i)].name), m_list);
        item->setData(kPresetRole, i);
        item->setToolTip(tr("<b>%1</b><br>%2<br><i>Drag onto the timeline, or double-click to add at the playhead.</i>")
                             .arg(tr(presets[static_cast<std::size_t>(i)].name), tr(presets[static_cast<std::size_t>(i)].description)));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }

    m_add = new QPushButton(tr("Add at Playhead"), this);
    m_add->setObjectName(QStringLiteral("titlesAdd"));
    m_add->setFocusPolicy(Qt::NoFocus);
    m_add->setEnabled(false);
    layout->addWidget(m_add);

    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] { m_add->setEnabled(!m_list->selectedItems().isEmpty()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { if (item) emit addRequested(item->data(kPresetRole).toInt()); });
    connect(m_add, &QPushButton::clicked, this, [this] {
        const auto items = m_list->selectedItems();
        if (!items.isEmpty()) emit addRequested(items.front()->data(kPresetRole).toInt());
    });
}

}  // namespace lf::ui
