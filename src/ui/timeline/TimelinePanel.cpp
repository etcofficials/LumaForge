#include "ui/timeline/TimelinePanel.h"

#include "ui/ActionRegistry.h"
#include "ui/Timecode.h"
#include "ui/timeline/TimelineView.h"
#include "ui/media/WaveformService.h"
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
    m_header->addAction(actions.action(ActionId::ToolSelect));
    m_header->addAction(actions.action(ActionId::ToolBlade));
    m_header->addAction(actions.action(ActionId::ToolTrim));
    m_header->addAction(actions.action(ActionId::ToolHand));
    m_header->addAction(actions.action(ActionId::ToolZoom));
    m_header->addAction(actions.action(ActionId::ToolText));
    m_header->addAction(actions.action(ActionId::ClipSplit));
    m_header->addAction(actions.action(ActionId::ClipDelete));
    m_header->addAction(actions.action(ActionId::TimelineSnap));
    m_header->addAction(actions.action(ActionId::TimelineZoomOut));
    m_header->addAction(actions.action(ActionId::TimelineZoomIn));
    m_header->addAction(actions.action(ActionId::TimelineZoomFit));
    layout->addWidget(m_header);

    m_view = new TimelineView(doc, this);
    m_waveforms = new WaveformService(this);
    m_view->setWaveformService(m_waveforms);
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
    actions.connect(ActionId::ClipDeselect, this, [this] { m_view->clearSelection(); });
    actions.connect(ActionId::EditCopy, this, [this] { m_view->copySelection(); });
    actions.connect(ActionId::EditCut, this, [this] { m_view->cutSelection(); });
    actions.connect(ActionId::EditPaste, this, [this] { m_view->paste(); });
    actions.connect(ActionId::EditPasteOverwrite, this, [this] { m_view->pasteOverwrite(); });
    actions.connect(ActionId::EditDuplicate, this, [this] { m_view->duplicateSelection(); });
    actions.connect(ActionId::TimelineAddText, this, [this] { m_view->addTextClip(); });
    actions.connect(ActionId::ClipNudgeLeft, this, [this] { m_view->nudgeSelection(-1); });
    actions.connect(ActionId::ClipNudgeRight, this, [this] { m_view->nudgeSelection(1); });
    actions.connect(ActionId::ClipNudgeLeftLarge, this, [this] { m_view->nudgeSelection(-10); });
    actions.connect(ActionId::ClipNudgeRightLarge, this, [this] { m_view->nudgeSelection(10); });
    actions.connect(ActionId::ClipMoveToPlayhead, this, [this] { m_view->moveSelectionToPlayhead(); });
    actions.connect(ActionId::ClipTrimStartToPlayhead, this, [this] { m_view->trimSelectionToPlayhead(true); });
    actions.connect(ActionId::ClipTrimEndToPlayhead, this, [this] { m_view->trimSelectionToPlayhead(false); });
    actions.connect(ActionId::ClipEnable, this, [this] { m_view->toggleSelectionEnabled(); updateActionState(); });
    actions.connect(ActionId::ClipLift, this, [this] { m_view->liftInOut(); });
    actions.connect(ActionId::AudioMuteClip, this, [this] { m_view->toggleSelectionAudioMute(); updateActionState(); });
    actions.connect(ActionId::ClipExtract, this, [this] { m_view->extractInOut(); });
    actions.connect(ActionId::MarkIn, this, [this] { m_view->setMarkIn(m_view->playhead()); });
    actions.connect(ActionId::MarkOut, this, [this] { m_view->setMarkOut(m_view->playhead()); });
    actions.connect(ActionId::ClearInOut, this, [this] { m_view->clearInOut(); });
    const std::pair<ActionId, EditTool> tools[] = {
        {ActionId::ToolSelect, EditTool::Select}, {ActionId::ToolBlade, EditTool::Blade}, {ActionId::ToolTrim, EditTool::Trim},
        {ActionId::ToolHand, EditTool::Hand}, {ActionId::ToolZoom, EditTool::Zoom}, {ActionId::ToolText, EditTool::Text}};
    for (const auto& [id, tool] : tools) {
        const EditTool t = tool;
        actions.connect(id, this, [this, t] { m_view->setTool(t); });
    }
    connect(m_view, &TimelineView::toolChanged, this, [this](EditTool t) {
        const std::pair<ActionId, EditTool> map[] = {
            {ActionId::ToolSelect, EditTool::Select}, {ActionId::ToolBlade, EditTool::Blade}, {ActionId::ToolTrim, EditTool::Trim},
            {ActionId::ToolHand, EditTool::Hand}, {ActionId::ToolZoom, EditTool::Zoom}, {ActionId::ToolText, EditTool::Text}};
        for (const auto& [id, tool] : map) if (tool == t) m_actions.action(id)->setChecked(true);
    });
    connect(m_view, &TimelineView::clipboardChanged, this, &TimelinePanel::updateActionState);

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
    m_listenerToken = m_doc.addListener([this](const project::Change& c) {
        if (c.kind == project::Change::Kind::MediaChanged || c.kind == project::Change::Kind::MediaRemoved) for (auto id : c.mediaIds) m_waveforms->forget(id);
        updateActionState();
        updateDurationLabel();
    });
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
    const auto& seq = m_doc.project().sequence;
    m_actions.setEnabled(ActionId::ClipSelectAll, hasClips);
    m_actions.setEnabled(ActionId::ClipDeselect, hasSelection);
    m_actions.setEnabled(ActionId::EditCopy, hasSelection);
    m_actions.setEnabled(ActionId::EditCut, hasSelection);
    m_actions.setEnabled(ActionId::EditDuplicate, hasSelection);
    m_actions.setEnabled(ActionId::EditPaste, m_view->canPaste());
    m_actions.setEnabled(ActionId::EditPasteOverwrite, m_view->canPaste());
    for (auto id : {ActionId::ClipNudgeLeft, ActionId::ClipNudgeRight, ActionId::ClipNudgeLeftLarge, ActionId::ClipNudgeRightLarge, ActionId::ClipMoveToPlayhead, ActionId::ClipEnable})
        m_actions.setEnabled(id, hasSelection);
    m_actions.action(ActionId::ClipEnable)->setChecked(hasSelection && m_view->selectionEnabled());
    m_actions.setEnabled(ActionId::AudioMuteClip, m_view->selectionHasAudio());
    m_actions.action(ActionId::AudioMuteClip)->setChecked(m_view->selectionAudioMuted());
    for (auto id : {ActionId::VideoTransform, ActionId::VideoCrop, ActionId::VideoColor, ActionId::VideoEffects}) m_actions.setEnabled(id, m_view->selectionHasVisual());
    m_actions.setEnabled(ActionId::ClipTrimStartToPlayhead, m_view->canTrimToPlayhead());
    m_actions.setEnabled(ActionId::ClipTrimEndToPlayhead, m_view->canTrimToPlayhead());
    m_actions.setEnabled(ActionId::ClipLift, seq.hasInOut() && hasClips);
    m_actions.setEnabled(ActionId::ClipExtract, seq.hasInOut() && hasClips);
    m_actions.setEnabled(ActionId::ClearInOut, seq.markIn.ticks >= 0 || seq.markOut.ticks >= 0);
    m_actions.setEnabled(ActionId::GoToIn, seq.markIn.ticks >= 0);
    m_actions.setEnabled(ActionId::GoToOut, seq.markOut.ticks >= 0);
}

void TimelinePanel::updateDurationLabel() {
    const auto d = m_doc.project().sequence.duration();
    m_durationLabel->setText(d.isZero() ? QString() : formatTimecode(TimePoint{d.ticks}, m_doc.project().settings.frameRate));
}

void TimelinePanel::showContextMenu(const QPoint& globalPos, project::ClipId clip) {
    QMenu menu(this);
    if (project::isValid(clip)) {
        menu.addAction(m_actions.action(ActionId::EditCut));
        menu.addAction(m_actions.action(ActionId::EditCopy));
        menu.addAction(m_actions.action(ActionId::EditPaste));
        menu.addAction(m_actions.action(ActionId::EditDuplicate));
        menu.addSeparator();
        menu.addAction(m_actions.action(ActionId::ClipDelete));
        menu.addAction(m_actions.action(ActionId::ClipRippleDelete));
        menu.addSeparator();
        menu.addAction(m_actions.action(ActionId::ClipSplit));
        menu.addAction(m_actions.action(ActionId::ClipTrimStartToPlayhead));
        menu.addAction(m_actions.action(ActionId::ClipTrimEndToPlayhead));
        menu.addAction(m_actions.action(ActionId::ClipEnable));
        menu.addSeparator();
    }
    menu.addAction(m_actions.action(ActionId::EditPaste));
    menu.addAction(m_actions.action(ActionId::EditPasteOverwrite));
    menu.addAction(m_actions.action(ActionId::TimelineAddText));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::ClipSelectAll));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::TimelineZoomFit));
    menu.addAction(m_actions.action(ActionId::TimelineSnap));
    menu.exec(globalPos);
}

}  // namespace lf::ui
