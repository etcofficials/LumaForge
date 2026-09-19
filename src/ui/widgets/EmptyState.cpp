#include "ui/widgets/EmptyState.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lf::ui {

EmptyState::EmptyState(const QString& iconName, const QString& title, const QString& hint, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(6);
    layout->addStretch(2);

    m_icon = new QLabel(this);
    m_icon->setPixmap(Icons::pixmap(iconName, 40, Theme::palette().textMuted, devicePixelRatioF()));
    m_icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_icon);
    layout->addSpacing(6);

    m_title = new QLabel(title, this);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setProperty("secondary", true);
    QFont f = m_title->font();
    f.setWeight(QFont::DemiBold);
    m_title->setFont(f);
    layout->addWidget(m_title);

    m_hint = new QLabel(hint, this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_hint->setProperty("muted", true);
    layout->addWidget(m_hint);

    layout->addStretch(3);
}

QPushButton* EmptyState::setAction(const QString& text) {
    if (!m_button) {
        m_button = new QPushButton(text, this);
        m_button->setCursor(Qt::PointingHandCursor);
        auto* layout = static_cast<QVBoxLayout*>(this->layout());
        auto* row = new QHBoxLayout();
        row->addStretch(1);
        row->addWidget(m_button);
        row->addStretch(1);
        layout->insertLayout(layout->count() - 1, row);
        layout->insertSpacing(layout->count() - 1, 4);
    }
    m_button->setText(text);
    return m_button;
}

}  // namespace lf::ui
