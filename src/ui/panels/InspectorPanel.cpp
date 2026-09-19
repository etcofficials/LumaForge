#include "ui/panels/InspectorPanel.h"

#include "ui/widgets/EmptyState.h"
#include "ui/widgets/PanelHeader.h"

#include <QVBoxLayout>

namespace lf::ui {

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PanelHeader(tr("Inspector"), this);
    layout->addWidget(m_header);

    m_empty = new EmptyState(QStringLiteral("inspector-empty"), tr("Nothing selected"),
                             tr("Select a clip in the timeline or an item in the media panel to see its properties."), this);
    layout->addWidget(m_empty, 1);
}

}  // namespace lf::ui
