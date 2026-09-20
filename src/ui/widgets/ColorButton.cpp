#include "ui/widgets/ColorButton.h"

#include "ui/Theme.h"

#include <QColorDialog>
#include <QPainter>
#include <QPixmap>

namespace lf::ui {

ColorButton::ColorButton(QWidget* parent) : QToolButton(parent) {
    setFixedSize(44, 22);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(tr("Choose colour"));
    setCursor(Qt::PointingHandCursor);
    updateSwatch();
    connect(this, &QToolButton::clicked, this, [this] {
        const QColor initial = QColor::fromRgbF(m_color.r, m_color.g, m_color.b);
        const QColor picked = QColorDialog::getColor(initial, window(), tr("Colour"), QColorDialog::DontUseNativeDialog);
        if (!picked.isValid()) return;
        m_color.r = static_cast<float>(picked.redF());
        m_color.g = static_cast<float>(picked.greenF());
        m_color.b = static_cast<float>(picked.blueF());
        updateSwatch();
        emit colorEdited(m_color);
    });
}

void ColorButton::setColor(const ColorRgba& c) {
    m_color = c;
    updateSwatch();
}

void ColorButton::updateSwatch() {
    QPixmap pm(36, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Theme::palette().borderStrong, 1));
    p.setBrush(QColor::fromRgbF(m_color.r, m_color.g, m_color.b));
    p.drawRoundedRect(QRectF(0.5, 0.5, 35, 13), 3, 3);
    setIcon(QIcon(pm));
    setIconSize(pm.size());
}

}  // namespace lf::ui
