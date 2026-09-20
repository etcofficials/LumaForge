#include "ui/widgets/CollapsibleSection.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QFormLayout>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

CollapsibleSection::CollapsibleSection(const QString& title, const QString& settingsKey, QWidget* parent)
    : QWidget(parent), m_key(QStringLiteral("inspector/section/") + settingsKey) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerRow = new QWidget(this);
    m_headerRow->setProperty("sectionHeader", true);
    m_headerRow->setAttribute(Qt::WA_StyledBackground, true);
    m_headerRow->setFixedHeight(26);
    auto* row = new QHBoxLayout(m_headerRow);
    row->setContentsMargins(4, 0, 6, 0);
    row->setSpacing(4);

    m_toggle = new QToolButton(m_headerRow);
    m_toggle->setAutoRaise(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setArrowType(Qt::DownArrow);
    m_toggle->setText(title);
    m_toggle->setProperty("sectionToggle", true);
    m_toggle->setFocusPolicy(Qt::NoFocus);
    m_toggle->setCursor(Qt::PointingHandCursor);
    QFont f = m_toggle->font();
    f.setWeight(QFont::DemiBold);
    f.setCapitalization(QFont::AllUppercase);
    f.setPointSizeF(f.pointSizeF() - 1.0);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    m_toggle->setFont(f);
    row->addWidget(m_toggle);
    m_subtitle = new QLabel(m_headerRow);
    m_subtitle->setProperty("muted", true);
    row->addWidget(m_subtitle);
    row->addStretch(1);
    layout->addWidget(m_headerRow);

    m_content = new QWidget(this);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(10, 4, 10, 10);
    m_contentLayout->setSpacing(6);
    layout->addWidget(m_content);

    connect(m_toggle, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    m_headerRow->installEventFilter(this);   // the whole strip toggles, not just the arrow
    m_expanded = QSettings().value(m_key, true).toBool();
    m_content->setVisible(m_expanded);
    updateArrow();
}

QFormLayout* CollapsibleSection::form() {
    if (!m_form) {
        m_form = new QFormLayout();
        m_form->setContentsMargins(0, 0, 0, 0);
        m_form->setHorizontalSpacing(8);
        m_form->setVerticalSpacing(4);
        m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        m_contentLayout->addLayout(m_form);
    }
    return m_form;
}

QToolButton* CollapsibleSection::addHeaderButton(const QString& text, const QString& toolTip) {
    auto* b = new QToolButton(m_headerRow);
    b->setAutoRaise(true);
    b->setText(text);
    b->setToolTip(toolTip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setProperty("sectionButton", true);
    QFont f = b->font();
    f.setPointSizeF(f.pointSizeF() - 1.0);
    b->setFont(f);
    static_cast<QHBoxLayout*>(m_headerRow->layout())->addWidget(b);
    return b;
}

void CollapsibleSection::setExpanded(bool expanded) {
    if (expanded == m_expanded) return;
    m_expanded = expanded;
    m_content->setVisible(expanded);
    QSettings().setValue(m_key, expanded);
    updateArrow();
    emit toggled(expanded);
}

void CollapsibleSection::setSubtitle(const QString& text) { m_subtitle->setText(text); }

bool CollapsibleSection::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_headerRow && event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        // Clicks on the header buttons (Reset...) are theirs; the strip itself toggles.
        if (me->button() == Qt::LeftButton && !m_headerRow->childAt(me->position().toPoint())) { setExpanded(!m_expanded); return true; }
    }
    return QWidget::eventFilter(watched, event);
}

void CollapsibleSection::updateArrow() { m_toggle->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow); }

}  // namespace lf::ui
