#include "ui/widgets/PanelHeader.h"

#include "ui/Theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace lf::ui {

PanelHeader::PanelHeader(const QString& title, QWidget* parent) : QWidget(parent) {
    setProperty("panelHeader", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(Theme::metrics().panelHeaderHeight);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 4, 0);
    m_layout->setSpacing(2);

    m_title = new QLabel(title, this);
    m_title->setProperty("panelTitle", true);
    m_layout->addWidget(m_title);
    m_layout->addStretch(1);
}

QToolButton* PanelHeader::addAction(QAction* action) {
    auto* b = new QToolButton(this);
    b->setDefaultAction(action);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(24, 24);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setFocusPolicy(Qt::NoFocus);
    m_layout->addWidget(b);
    return b;
}

void PanelHeader::addWidget(QWidget* widget) { m_layout->addWidget(widget); }

void PanelHeader::setTitle(const QString& title) { m_title->setText(title); }

}  // namespace lf::ui
