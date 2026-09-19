#include "ui/timeline/TimelinePanel.h"

#include "ui/ActionRegistry.h"
#include "ui/Timecode.h"
#include "ui/timeline/TimelineView.h"
#include "ui/widgets/PanelHeader.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
// The scrollbar works in "scroll units" of 1/1000 s to stay inside int range.
constexpr lf::Ticks kUnit = lf::kFlicksPerSecond / 1000;
}  // namespace

TimelinePanel::TimelinePanel(project::Document& doc, ActionRegistry& actions, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_actions(actions) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PanelHeader(tr("Timeline"), this);
    m_durationLabel = new QLabel(this);
    m_durationLabel->setProperty("muted", true);
    m_durationLabel->setToolTip(tr("Sequence duration"));
    m_header->addWidget(m_durationLabel);
    m_header->addAction(actions.action(ActionId::ClipSplit));
    m_header->addAction(actions.action(ActionId::ClipDelete));
    m_header->addAction(actions.action(ActionId::TimelineSnap));
    m_header->addAction(actions.action(ActionId::TimelineZoomOut));
    m_header->addAction(actions.action(ActionId::TimelineZoomIn));
    m_header->addAction(actions.action(ActionId::TimelineZoomFit));
    layout->addWidget(m_header);

    m_view = new TimelineView(doc, this);
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
    actions.connect(ActionId::ClipSplit, this, [this] { m_view->splitAtPlayhead(); });
    actions.connect(ActionId::ClipDelete, this, [this] { m_view->deleteSelection(false); });
    actions.connect(ActionId::ClipRippleDelete, this, [this] { m_view->deleteSelection(true); });
    actions.connect(ActionId::ClipSelectAll, this, [this] { m_view->selectAll(); });

    connect(m_view, &TimelineView::viewChanged, this, &TimelinePanel::syncScrollBar);
    connect(m_view, &TimelineView::selectionChanged, this, &TimelinePanel::updateActionState);
    connect(m_view, &TimelineView::playheadChanged, this, [this](TimePoint) { updateActionState(); });
    connect(m_view, &TimelineView::contextMenuRequested, this, &TimelinePanel::showContextMenu);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_syncing) return;
        m_syncing = true;
        m_view->setScrollOffset(static_cast<lf::Ticks>(v) * kUnit);
        m_syncing = false;
    });
    m_listenerToken = m_doc.addListener([this](const project::Change&) { updateActionState(); updateDurationLabel(); });
    syncScrollBar();
    updateActionState();
    updateDurationLabel();
}

TimelinePanel::~TimelinePanel() { m_doc.removeListener(m_listenerToken); }

void TimelinePanel::syncScrollBar() {
    if (m_syncing) return;
    m_syncing = true;
    m_scroll->setRange(0, static_cast<int>(m_view->scrollRange() / kUnit));
    m_scroll->setPageStep(static_cast<int>(m_view->visibleTicks() / kUnit));
    m_scroll->setValue(static_cast<int>(m_view->scrollOffset() / kUnit));
    m_syncing = false;
}

void TimelinePanel::updateActionState() {
    const bool hasSelection = m_view->hasSelection();
    const bool hasClips = !m_doc.project().sequence.clips.empty();
    m_actions.setEnabled(ActionId::ClipSplit, m_view->canSplitAtPlayhead());
    m_actions.setEnabled(ActionId::ClipDelete, hasSelection);
    m_actions.setEnabled(ActionId::ClipRippleDelete, hasSelection);
    m_actions.setEnabled(ActionId::ClipSelectAll, hasClips);
}

void TimelinePanel::updateDurationLabel() {
    const auto d = m_doc.project().sequence.duration();
    m_durationLabel->setText(d.isZero() ? QString() : formatTimecode(TimePoint{d.ticks}, m_doc.project().settings.frameRate));
}

void TimelinePanel::showContextMenu(const QPoint& globalPos, project::ClipId clip) {
    QMenu menu(this);
    if (project::isValid(clip)) {
        menu.addAction(m_actions.action(ActionId::ClipSplit));
        menu.addSeparator();
        menu.addAction(m_actions.action(ActionId::ClipDelete));
        menu.addAction(m_actions.action(ActionId::ClipRippleDelete));
        menu.addSeparator();
    }
    menu.addAction(m_actions.action(ActionId::ClipSelectAll));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::TimelineZoomFit));
    menu.addAction(m_actions.action(ActionId::TimelineSnap));
    menu.exec(globalPos);
}

}  // namespace lf::ui
