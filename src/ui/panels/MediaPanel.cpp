#include "ui/panels/MediaPanel.h"

#include "ui/ActionRegistry.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/PanelHeader.h"

#include <QAction>
#include <QHeaderView>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace lf::ui {

MediaPanel::MediaPanel(ActionRegistry& actions, QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(220);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PanelHeader(tr("Media"), this);
    m_header->addAction(actions.action(ActionId::MediaImport));
    layout->addWidget(m_header);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    m_empty = new EmptyState(QStringLiteral("media-empty"), tr("No media yet"),
                             tr("Import video, audio or images to start editing.\nYou can also drop files here."), this);
    auto* importButton = m_empty->setAction(tr("Import Media…"));
    connect(importButton, &QPushButton::clicked, actions.action(ActionId::MediaImport), &QAction::trigger);
    m_stack->addWidget(m_empty);

    m_view = new QTreeView(this);
    m_view->setRootIsDecorated(false);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setDragEnabled(true);
    m_view->setUniformRowHeights(true);
    m_view->header()->setStretchLastSection(true);
    m_stack->addWidget(m_view);

    m_stack->setCurrentWidget(m_empty);
}

}  // namespace lf::ui
