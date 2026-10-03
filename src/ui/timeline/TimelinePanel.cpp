#include "ui/timeline/TimelinePanel.h"

#include "lumaforge/project/TimelineOps.h"
#include "ui/ActionRegistry.h"
#include "ui/Timecode.h"
#include "ui/timeline/TimelineView.h"
#include "ui/media/WaveformService.h"
#include "ui/widgets/PanelHeader.h"

#include <QAction>
#include <QHBoxLayout>
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
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_view, 1);
    m_vscroll = new QScrollBar(Qt::Vertical, this);
    m_vscroll->setSingleStep(20);
    row->addWidget(m_vscroll);
    layout->addLayout(row, 1);
    connect(m_vscroll, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_syncing) return;
        m_syncing = true;
        m_view->setScrollY(v);
        m_syncing = false;
    });

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
    actions.connect(ActionId::MarkerAdd, this, [this] { m_view->addMarker(); });
    actions.connect(ActionId::ClipFrameHold, this, [this] { m_view->addFrameHold(); });
    actions.connect(ActionId::TimelineAddSolid, this, [this] { m_view->addGeneratedClip(project::ClipKind::Solid); });
    actions.connect(ActionId::TimelineAddAdjustment, this, [this] { m_view->addGeneratedClip(project::ClipKind::Adjustment); });
    actions.connect(ActionId::ClipAddTransition, this, [this] { m_view->addTransition(project::TransitionType::CrossDissolve, project::kDefaultTransitionDuration); });
    actions.connect(ActionId::ClipNudgeLeft, this, [this] { m_view->nudgeSelection(-1); });
    actions.connect(ActionId::ClipNudgeRight, this, [this] { m_view->nudgeSelection(1); });
    actions.connect(ActionId::ClipNudgeLeftLarge, this, [this] { m_view->nudgeSelection(-10); });
    actions.connect(ActionId::ClipNudgeRightLarge, this, [this] { m_view->nudgeSelection(10); });
    actions.connect(ActionId::ClipMoveToPlayhead, this, [this] { m_view->moveSelectionToPlayhead(); });
    actions.connect(ActionId::ClipTrimStartToPlayhead, this, [this] { m_view->trimSelectionToPlayhead(true); });
    actions.connect(ActionId::ClipTrimEndToPlayhead, this, [this] { m_view->trimSelectionToPlayhead(false); });
    actions.connect(ActionId::ClipEnable, this, [this] { m_view->toggleSelectionEnabled(); updateActionState(); });
    actions.connect(ActionId::ClipLift, this, [this] { m_view->liftInOut(); });
    // Track commands act on the targeted track (last clicked header).
    actions.connect(ActionId::TimelineAddVideoTrack, this, [this] { commitTrackEdit(tr("Add Video Track"), project::ops::addTrack(m_doc, project::TrackKind::Video)); });
    actions.connect(ActionId::TimelineAddAudioTrack, this, [this] { commitTrackEdit(tr("Add Audio Track"), project::ops::addTrack(m_doc, project::TrackKind::Audio)); });
    actions.connect(ActionId::TrackAddAbove, this, [this] {
        const auto* t = m_doc.project().sequence.findTrack(m_view->selectedTrack());
        if (t) commitTrackEdit(tr("Add Track"), project::ops::addTrack(m_doc, t->kind, m_doc.project().sequence.trackIndex(t->id)));
    });
    actions.connect(ActionId::TrackAddBelow, this, [this] {
        const auto* t = m_doc.project().sequence.findTrack(m_view->selectedTrack());
        if (t) commitTrackEdit(tr("Add Track"), project::ops::addTrack(m_doc, t->kind, m_doc.project().sequence.trackIndex(t->id) + 1));
    });
    actions.connect(ActionId::TrackDuplicate, this, [this] { commitTrackEdit(tr("Duplicate Track"), project::ops::duplicateTrack(m_doc, m_view->selectedTrack())); });
    actions.connect(ActionId::TrackDelete, this, [this] { commitTrackEdit(tr("Delete Track"), project::ops::removeTrack(m_doc, m_view->selectedTrack())); });
    actions.connect(ActionId::TrackRename, this, [this] { m_view->renameTrackInteractive(m_view->selectedTrack()); });
    actions.connect(ActionId::TrackMoveUp, this, [this] { commitTrackEdit(tr("Move Track"), project::ops::moveTrack(m_doc, m_view->selectedTrack(), -1)); });
    actions.connect(ActionId::TrackMoveDown, this, [this] { commitTrackEdit(tr("Move Track"), project::ops::moveTrack(m_doc, m_view->selectedTrack(), 1)); });
    actions.connect(ActionId::TrackMute, this, [this] { m_view->toggleTrackFlagPublic(m_view->selectedTrack(), 0); });
    actions.connect(ActionId::TrackSolo, this, [this] { m_view->toggleTrackFlagPublic(m_view->selectedTrack(), 1); });
    actions.connect(ActionId::TrackLock, this, [this] { m_view->toggleTrackFlagPublic(m_view->selectedTrack(), 2); });
    connect(m_view, &TimelineView::selectedTrackChanged, this, &TimelinePanel::updateTrackActions);
    connect(m_view, &TimelineView::trackContextMenuRequested, this, [this](const QPoint& pos, project::TrackId) { showTrackMenu(pos); });
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
        if (c.kind == project::Change::Kind::TracksChanged || c.kind == project::Change::Kind::Reset) { updateTrackActions(); syncScrollBar(); }
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
    const int maxY = std::max(0, m_view->contentHeight() + 24 - m_view->lanesViewportHeight());
    m_vscroll->setRange(0, maxY);
    m_vscroll->setPageStep(std::max(1, m_view->lanesViewportHeight()));
    m_vscroll->setValue(m_view->scrollY());
    m_vscroll->setVisible(maxY > 0);
    m_syncing = false;
}

void TimelinePanel::commitTrackEdit(const QString& text, project::SequenceEdit edit) {
    if (edit.empty()) return;
    m_doc.push(std::make_unique<project::SequenceEditCommand>(m_doc, text.toStdString(), std::move(edit)));
    syncScrollBar();
}

void TimelinePanel::updateTrackActions() {
    const auto& seq = m_doc.project().sequence;
    const project::Track* t = seq.findTrack(m_view->selectedTrack());
    int sameKind = 0;
    if (t) for (const auto& o : seq.tracks) if (o.kind == t->kind) ++sameKind;
    for (auto id : {ActionId::TrackAddAbove, ActionId::TrackAddBelow, ActionId::TrackDuplicate, ActionId::TrackRename, ActionId::TrackLock, ActionId::TrackMute, ActionId::TrackSolo})
        m_actions.setEnabled(id, t != nullptr);
    m_actions.setEnabled(ActionId::TrackDelete, t && sameKind > 1);
    const int index = t ? seq.trackIndex(t->id) : -1;
    bool canUp = false, canDown = false;
    if (t) {
        canUp = index > 0 && seq.tracks[static_cast<std::size_t>(index - 1)].kind == t->kind;
        canDown = index + 1 < static_cast<int>(seq.tracks.size()) && seq.tracks[static_cast<std::size_t>(index + 1)].kind == t->kind;
    }
    m_actions.setEnabled(ActionId::TrackMoveUp, canUp);
    m_actions.setEnabled(ActionId::TrackMoveDown, canDown);
    m_actions.action(ActionId::TrackLock)->setChecked(t && t->locked);
    m_actions.action(ActionId::TrackMute)->setChecked(t && t->muted);
    m_actions.action(ActionId::TrackSolo)->setChecked(t && t->solo);
    m_actions.setText(ActionId::TrackMute, t && t->kind == project::TrackKind::Video ? tr("&Hide Track") : tr("&Mute Track"));
}

void TimelinePanel::showTrackMenu(const QPoint& globalPos) {
    updateTrackActions();
    QMenu menu(this);
    for (auto id : {ActionId::TrackRename, ActionId::TrackDuplicate, ActionId::TrackDelete}) menu.addAction(m_actions.action(id));
    menu.addSeparator();
    for (auto id : {ActionId::TrackAddAbove, ActionId::TrackAddBelow, ActionId::TrackMoveUp, ActionId::TrackMoveDown}) menu.addAction(m_actions.action(id));
    menu.addSeparator();
    for (auto id : {ActionId::TrackLock, ActionId::TrackMute, ActionId::TrackSolo}) menu.addAction(m_actions.action(id));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::TimelineAddVideoTrack));
    menu.addAction(m_actions.action(ActionId::TimelineAddAudioTrack));
    menu.exec(globalPos);
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
    for (auto id : {ActionId::VideoTransform, ActionId::VideoCrop, ActionId::VideoColor, ActionId::VideoEffects, ActionId::ClipAddTransition})
        m_actions.setEnabled(id, m_view->selectionHasVisual());
    m_actions.setEnabled(ActionId::ClipFrameHold, !m_doc.project().sequence.clips.empty());
    m_actions.setEnabled(ActionId::ClipTrimStartToPlayhead, m_view->canTrimToPlayhead());
    m_actions.setEnabled(ActionId::ClipTrimEndToPlayhead, m_view->canTrimToPlayhead());
    m_actions.setEnabled(ActionId::ClipLift, seq.hasInOut() && hasClips);
    m_actions.setEnabled(ActionId::ClipExtract, seq.hasInOut() && hasClips);
    m_actions.setEnabled(ActionId::ClearInOut, seq.markIn.ticks >= 0 || seq.markOut.ticks >= 0);
    m_actions.setEnabled(ActionId::GoToIn, seq.markIn.ticks >= 0);
    m_actions.setEnabled(ActionId::GoToOut, seq.markOut.ticks >= 0);
    bool singleMedia = false;
    if (m_view->selection().size() >= 1) {
        const auto* c = seq.findClip(m_view->selection().front());
        singleMedia = c && project::isValid(c->media);
    }
    m_actions.setEnabled(ActionId::ClipRevealInMedia, singleMedia);
    m_actions.setEnabled(ActionId::ClipProperties, hasSelection);
    bool speedable = false;
    for (auto id : m_view->selection()) if (const auto* c = seq.findClip(id); c && (c->kind == project::ClipKind::Video || c->kind == project::ClipKind::Audio)) speedable = true;
    m_actions.setEnabled(ActionId::ClipSpeed, speedable);
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
        menu.addAction(m_actions.action(ActionId::ClipSpeed));
        menu.addAction(m_actions.action(ActionId::ClipAddTransition));
        menu.addSeparator();
        menu.addAction(m_actions.action(ActionId::ClipRevealInMedia));
        menu.addAction(m_actions.action(ActionId::ClipProperties));
        menu.exec(globalPos);
        return;
    }
    menu.addAction(m_actions.action(ActionId::EditPaste));
    menu.addAction(m_actions.action(ActionId::EditPasteOverwrite));
    menu.addAction(m_actions.action(ActionId::ClipRippleDelete));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::TimelineAddVideoTrack));
    menu.addAction(m_actions.action(ActionId::TimelineAddAudioTrack));
    menu.addAction(m_actions.action(ActionId::TimelineAddText));
    menu.addAction(m_actions.action(ActionId::TimelineAddSolid));
    menu.addAction(m_actions.action(ActionId::TimelineAddAdjustment));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::ClipSelectAll));
    menu.addAction(m_actions.action(ActionId::ClipDeselect));
    menu.addSeparator();
    menu.addAction(m_actions.action(ActionId::TimelineZoomFit));
    menu.addAction(m_actions.action(ActionId::TimelineSnap));
    menu.exec(globalPos);
}

}  // namespace lf::ui
