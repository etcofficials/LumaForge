#include "ui/timeline/TimelinePanel.h"

#include "ui/ActionRegistry.h"
#include "ui/timeline/TimelineView.h"
#include "ui/widgets/PanelHeader.h"

#include <QAction>
#include <QScrollBar>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
// The scrollbar works in "scroll units" of 1/1000 s to stay inside int range.
constexpr lf::Ticks kUnit = lf::kFlicksPerSecond / 1000;
}  // namespace

TimelinePanel::TimelinePanel(ActionRegistry& actions, QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PanelHeader(tr("Timeline"), this);
    m_header->addAction(actions.action(ActionId::TimelineSnap));
    m_header->addAction(actions.action(ActionId::TimelineZoomOut));
    m_header->addAction(actions.action(ActionId::TimelineZoomIn));
    m_header->addAction(actions.action(ActionId::TimelineZoomFit));
    layout->addWidget(m_header);

    m_view = new TimelineView(this);
    layout->addWidget(m_view, 1);

    m_scroll = new QScrollBar(Qt::Horizontal, this);
    m_scroll->setSingleStep(100);
    layout->addWidget(m_scroll);

    auto* snap = actions.action(ActionId::TimelineSnap);
    snap->setChecked(true);
    connect(snap, &QAction::toggled, m_view, &TimelineView::setSnappingEnabled);
    actions.connect(ActionId::TimelineZoomIn, this, [this] { m_view->zoomIn(); });
    actions.connect(ActionId::TimelineZoomOut, this, [this] { m_view->zoomOut(); });
    actions.connect(ActionId::TimelineZoomFit, this, [this] { m_view->zoomToFit(); });

    connect(m_view, &TimelineView::viewChanged, this, &TimelinePanel::syncScrollBar);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_syncing) return;
        m_syncing = true;
        m_view->setScrollOffset(static_cast<lf::Ticks>(v) * kUnit);
        m_syncing = false;
    });
    syncScrollBar();
}

void TimelinePanel::syncScrollBar() {
    if (m_syncing) return;
    m_syncing = true;
    m_scroll->setRange(0, static_cast<int>(m_view->scrollRange() / kUnit));
    m_scroll->setPageStep(static_cast<int>(m_view->visibleTicks() / kUnit));
    m_scroll->setValue(static_cast<int>(m_view->scrollOffset() / kUnit));
    m_syncing = false;
}

}  // namespace lf::ui
