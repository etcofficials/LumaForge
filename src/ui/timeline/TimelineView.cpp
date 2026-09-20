#include "ui/timeline/TimelineView.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/TimelineOps.h"
#include "ui/media/WaveformService.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"
#include "ui/media/ImportController.h"
#include "ui/media/MediaModel.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lf::ui {

using namespace lf::project;

namespace {
constexpr double kMinPixelsPerSecond = 0.5;      // ~33 minutes across 1000 px
constexpr double kMaxPixelsPerSecond = 4000.0;   // sub-frame zoom
constexpr int kMinMajorSpacingPx = 90;
constexpr int kPlayheadHalfWidth = 6;
constexpr int kTrimHandlePx = 7;
constexpr int kDragThresholdPx = 4;
constexpr int kSnapPx = 8;
constexpr int kAutoScrollMarginPx = 28;

// Candidate major tick intervals, in seconds; frame-based intervals are added at runtime.
constexpr double kIntervalsSeconds[] = {0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};

QColor clipColor(ClipKind kind) {
    const auto& pal = Theme::palette();
    switch (kind) {
        case ClipKind::Video: return pal.clipVideo;
        case ClipKind::Audio: return pal.clipAudio;
        case ClipKind::Image: return QColor(0x8A, 0x5C, 0xB8);
        case ClipKind::Text: return QColor(0xC9, 0x8A, 0x2E);
    }
    return pal.clipVideo;
}

bool contains(const std::vector<ClipId>& ids, ClipId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
}  // namespace

TimelineView::TimelineView(Document& doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAcceptDrops(true);
    setCursor(Qt::ArrowCursor);
    m_fps = m_doc.project().settings.frameRate;
    rebuildLanes();
    refreshDuration();
    m_listenerToken = m_doc.addListener([this](const Change& c) { onDocumentChange(c); });
}

TimelineView::~TimelineView() { m_doc.removeListener(m_listenerToken); }

void TimelineView::setWaveformService(WaveformService* service) {
    m_waveforms = service;
    if (service) connect(service, &WaveformService::ready, this, [this](project::MediaId) { update(laneArea()); });
}

// ---------------------------------------------------------------- model sync

void TimelineView::onDocumentChange(const Change& change) {
    switch (change.kind) {
        case Change::Kind::Reset:
        case Change::Kind::Settings:
            m_fps = m_doc.project().settings.frameRate;
            [[fallthrough]];
        case Change::Kind::TracksChanged:
            rebuildLanes();
            pruneSelection();
            refreshDuration();
            updateGeometry();
            update();
            break;
        case Change::Kind::ClipsChanged:
        case Change::Kind::MediaRemoved:
        case Change::Kind::MediaChanged: {
            pruneSelection();
            const Duration before = m_sequenceDuration;
            refreshDuration();
            // Clip rows only; the ruler repaints when the sequence end moved.
            update(lanesRect());
            if (before != m_sequenceDuration) update(QRect(headerWidth(), 0, width() - headerWidth(), rulerHeight()));
            break;
        }
        default:
            break;
    }
}

void TimelineView::rebuildLanes() {
    const auto& m = Theme::metrics();
    m_lanes.clear();
    int y = rulerHeight();
    for (const auto& t : m_doc.project().sequence.tracks) {
        Lane l;
        l.id = t.id;
        l.kind = t.kind;
        l.name = QString::fromStdString(t.name);
        l.y = y;
        l.height = t.kind == TrackKind::Video ? m.videoTrackHeight : m.audioTrackHeight;
        l.muted = t.muted;
        l.locked = t.locked;
        l.solo = t.solo;
        m_lanes.push_back(l);
        y += l.height;
    }
}

void TimelineView::refreshDuration() {
    m_sequenceDuration = m_doc.project().sequence.duration();
    const Duration padded = m_sequenceDuration + Duration::fromSeconds(15);
    m_duration = std::max(padded, Duration::fromSeconds(60));
    if (m_playhead.ticks > m_duration.ticks) setPlayhead(TimePoint{m_duration.ticks});
    clampScroll();
    emit viewChanged();
}

void TimelineView::pruneSelection() {
    const auto& seq = m_doc.project().sequence;
    const auto before = m_selection.size();
    std::erase_if(m_selection, [&](ClipId id) { return seq.findClip(id) == nullptr; });
    if (m_selection.size() != before) emit selectionChanged();
}

void TimelineView::commit(const QString& text, SequenceEdit edit) {
    if (edit.empty()) return;
    m_doc.push(std::make_unique<SequenceEditCommand>(m_doc, text.toStdString(), std::move(edit)));
}

void TimelineView::toggleTrackFlag(TrackId id, TrackFlag flag) {
    const Track* t = m_doc.project().sequence.findTrack(id);
    if (!t) return;
    Track n = *t;
    const QString name = QString::fromStdString(n.name);
    QString text;
    switch (flag) {
        case TrackFlag::Lock:
            n.locked = !n.locked;
            text = n.locked ? tr("Lock %1").arg(name) : tr("Unlock %1").arg(name);
            break;
        case TrackFlag::Solo:
            n.solo = !n.solo;
            text = n.solo ? tr("Solo %1").arg(name) : tr("Unsolo %1").arg(name);
            break;
        case TrackFlag::Mute:
            n.muted = !n.muted;
            if (n.kind == TrackKind::Video) text = n.muted ? tr("Hide %1").arg(name) : tr("Show %1").arg(name);
            else text = n.muted ? tr("Mute %1").arg(name) : tr("Unmute %1").arg(name);
            break;
    }
    SequenceEdit e;
    e.tracks.push_back(n);
    commit(text, std::move(e));
}

// ---------------------------------------------------------------- geometry

int TimelineView::headerWidth() const noexcept { return Theme::metrics().trackHeaderWidth; }
int TimelineView::rulerHeight() const noexcept { return Theme::metrics().rulerHeight; }

QRect TimelineView::laneArea() const {
    return QRect(headerWidth(), rulerHeight(), std::max(0, width() - headerWidth()), std::max(0, height() - rulerHeight()));
}

double TimelineView::xFromTicks(Ticks t) const {
    return headerWidth() + static_cast<double>(t - m_scrollTicks) / kFlicksPerSecond * m_pixelsPerSecond;
}

Ticks TimelineView::ticksFromX(double x) const {
    return m_scrollTicks + static_cast<Ticks>(std::llround((x - headerWidth()) / m_pixelsPerSecond * kFlicksPerSecond));
}

Ticks TimelineView::ticksPerPixel() const noexcept { return static_cast<Ticks>(kFlicksPerSecond / m_pixelsPerSecond); }

Ticks TimelineView::visibleTicks() const noexcept {
    const int px = std::max(1, width() - headerWidth());
    return static_cast<Ticks>(px / m_pixelsPerSecond * kFlicksPerSecond);
}

Ticks TimelineView::scrollRange() const noexcept {
    const Ticks total = m_duration.ticks + visibleTicks() / 4;
    return std::max<Ticks>(0, total - visibleTicks());
}

QSize TimelineView::sizeHint() const {
    int h = rulerHeight();
    for (const auto& l : m_lanes) h += l.height;
    return {800, h + 24};
}

QSize TimelineView::minimumSizeHint() const {
    int h = rulerHeight();
    for (const auto& l : m_lanes) h += l.height;
    return {400, std::min(h, 160)};
}

QRect TimelineView::playheadRect(TimePoint t) const {
    const int x = static_cast<int>(std::floor(xFromTicks(t.ticks)));
    return QRect(x - kPlayheadHalfWidth - 1, 0, 2 * kPlayheadHalfWidth + 3, height());
}

const TimelineView::Lane* TimelineView::laneAt(int y) const {
    for (const auto& l : m_lanes) if (y >= l.y && y < l.y + l.height) return &l;
    return nullptr;
}

const TimelineView::Lane* TimelineView::laneFor(TrackId id) const {
    for (const auto& l : m_lanes) if (l.id == id) return &l;
    return nullptr;
}

int TimelineView::laneIndexAt(int y) const {
    for (std::size_t i = 0; i < m_lanes.size(); ++i) if (y >= m_lanes[i].y && y < m_lanes[i].y + m_lanes[i].height) return static_cast<int>(i);
    if (!m_lanes.empty() && y >= m_lanes.back().y + m_lanes.back().height) return static_cast<int>(m_lanes.size()) - 1;
    return 0;
}

QRectF TimelineView::clipRect(const Clip& c) const {
    const Lane* lane = laneFor(c.track);
    if (!lane) return {};
    const double x0 = xFromTicks(c.start.ticks);
    const double x1 = xFromTicks(c.end().ticks);
    return QRectF(x0, lane->y + 2, std::max(1.0, x1 - x0), lane->height - 5);
}

TimelineView::Hit TimelineView::hitTest(const QPoint& pos) const {
    Hit hit;
    const Lane* lane = laneAt(pos.y());
    if (!lane || pos.x() < headerWidth()) return hit;
    const Ticks t = ticksFromX(pos.x());
    for (const auto& c : m_doc.project().sequence.clips) {
        if (c.track != lane->id) continue;
        const QRectF r = clipRect(c);
        if (pos.x() < r.left() - 1 || pos.x() > r.right() + 1) continue;
        if (!(c.contains(TimePoint{t}) || pos.x() >= r.left() && pos.x() <= r.right())) continue;
        hit.clip = c.id;
        if (r.width() > 2 * kTrimHandlePx + 4) {
            hit.nearStart = pos.x() - r.left() <= kTrimHandlePx;
            hit.nearEnd = r.right() - pos.x() <= kTrimHandlePx;
        }
        return hit;
    }
    return hit;
}

Duration TimelineView::snapTolerance() const noexcept { return Duration{ticksPerPixel() * kSnapPx}; }

std::vector<TimelineView::LaneButton> TimelineView::laneButtons(const Lane& lane) const {
    // Right-aligned, compact: video = [eye][L], audio = [M][S][L].
    std::vector<TrackFlag> flags = lane.kind == TrackKind::Video ? std::vector<TrackFlag>{TrackFlag::Mute, TrackFlag::Lock}
                                                                 : std::vector<TrackFlag>{TrackFlag::Mute, TrackFlag::Solo, TrackFlag::Lock};
    std::vector<LaneButton> out;
    int x = headerWidth() - 8;
    for (auto it = flags.rbegin(); it != flags.rend(); ++it) {
        x -= 20;
        out.push_back({QRect(x, lane.y + (lane.height - 18) / 2, 20, 18), *it});
        x -= 4;
    }
    return out;
}

TimelineView::TickSpec TimelineView::chooseTicks() const {
    const Ticks frame = frameDuration(m_fps).ticks;
    const double frameSeconds = static_cast<double>(frame) / kFlicksPerSecond;
    for (int frames : {1, 2, 5, 10}) {
        const double px = frames * frameSeconds * m_pixelsPerSecond;
        if (px >= kMinMajorSpacingPx) return {frame * frames, frames == 1 ? 1 : (frames == 10 ? 5 : frames), true};
    }
    for (double s : kIntervalsSeconds) {
        if (s * m_pixelsPerSecond >= kMinMajorSpacingPx) {
            const int minor = (s == 0.5 || s == 1 || s == 2) ? 4 : (s == 5 || s == 10 || s == 15 || s == 30) ? 5 : (s == 60) ? 6 : 5;
            return {static_cast<Ticks>(s * kFlicksPerSecond), minor, false};
        }
    }
    return {static_cast<Ticks>(3600.0 * kFlicksPerSecond), 6, false};
}

// ---------------------------------------------------------------- state

void TimelineView::setPlayhead(TimePoint t) {
    t = snapToFrame(TimePoint{std::clamp<Ticks>(t.ticks, 0, m_duration.ticks)}, m_fps);
    if (t == m_playhead) return;
    const QRect before = playheadRect(m_playhead);
    m_playhead = t;
    update(before.united(playheadRect(m_playhead)));
    emit playheadChanged(m_playhead);
}

void TimelineView::setFrameRate(Rational fps) {
    if (fps == m_fps) return;
    m_fps = fps;
    update();
}

void TimelineView::setSnappingEnabled(bool enabled) { m_snapping = enabled; }

void TimelineView::setScrollOffset(Ticks ticks) {
    const Ticks clamped = std::clamp<Ticks>(ticks, 0, scrollRange());
    if (clamped == m_scrollTicks) return;
    m_scrollTicks = clamped;
    update();
    emit viewChanged();
}

void TimelineView::clampScroll() { m_scrollTicks = std::clamp<Ticks>(m_scrollTicks, 0, scrollRange()); }

void TimelineView::setZoom(double pixelsPerSecond, int anchorX) {
    pixelsPerSecond = std::clamp(pixelsPerSecond, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    if (pixelsPerSecond == m_pixelsPerSecond) return;
    const Ticks anchorTicks = ticksFromX(anchorX);
    m_pixelsPerSecond = pixelsPerSecond;
    m_scrollTicks = anchorTicks - static_cast<Ticks>((anchorX - headerWidth()) / m_pixelsPerSecond * kFlicksPerSecond);
    clampScroll();
    update();
    emit viewChanged();
}

void TimelineView::zoomIn() { setZoom(m_pixelsPerSecond * 1.5, static_cast<int>(xFromTicks(m_playhead.ticks))); }
void TimelineView::zoomOut() { setZoom(m_pixelsPerSecond / 1.5, static_cast<int>(xFromTicks(m_playhead.ticks))); }

void TimelineView::zoomToFit() {
    const int px = std::max(1, width() - headerWidth() - 24);
    const double seconds = std::max(1.0, (m_sequenceDuration.isZero() ? Duration::fromSeconds(60) : m_sequenceDuration).toSeconds());
    m_scrollTicks = 0;
    setZoom(px / seconds, headerWidth());
    m_scrollTicks = 0;
    update();
    emit viewChanged();
}

void TimelineView::setSelection(const std::vector<ClipId>& ids) {
    if (ids == m_selection) return;
    std::vector<ClipId> dirty = m_selection;
    dirty.insert(dirty.end(), ids.begin(), ids.end());
    m_selection = ids;
    updateClipRects(dirty);
    emit selectionChanged();
}

QRect TimelineView::lanesRect() const {
    const int bottom = m_lanes.empty() ? rulerHeight() : m_lanes.back().y + m_lanes.back().height;
    return QRect(headerWidth(), rulerHeight(), std::max(0, width() - headerWidth()), std::max(0, bottom - rulerHeight()));
}

void TimelineView::updateClipRects(const std::vector<ClipId>& ids) {
    QRegion region;
    const auto& seq = m_doc.project().sequence;
    for (auto id : ids) {
        if (const Clip* c = seq.findClip(id)) region += clipRect(*c).toAlignedRect().adjusted(-2, -2, 2, 2);
    }
    if (region.isEmpty()) return;
    update(region.intersected(lanesRect()));
}

void TimelineView::setThumbnailProvider(ThumbnailProvider provider) { m_thumbnails = std::move(provider); }
void TimelineView::thumbnailsChanged() { update(lanesRect()); }

void TimelineView::selectAll() {
    std::vector<ClipId> ids;
    for (const auto& c : m_doc.project().sequence.clips) ids.push_back(c.id);
    setSelection(ids);
}

void TimelineView::clearSelection() { setSelection({}); }

bool TimelineView::isSelected(ClipId id) const { return contains(m_selection, id); }

void TimelineView::selectClip(ClipId id, Qt::KeyboardModifiers mods) {
    const auto& seq = m_doc.project().sequence;
    const auto group = ops::withLinked(seq, {id});   // linked partners select together
    std::vector<ClipId> next = m_selection;
    if (mods & Qt::ControlModifier) {
        if (isSelected(id)) for (auto g : group) std::erase(next, g);
        else for (auto g : group) if (!contains(next, g)) next.push_back(g);
    } else if (mods & Qt::ShiftModifier) {
        for (auto g : group) if (!contains(next, g)) next.push_back(g);
    } else if (!isSelected(id)) {
        next = group;
    }
    setSelection(next);
}

bool TimelineView::canSplitAtPlayhead() const {
    const auto& seq = m_doc.project().sequence;
    for (const auto& c : seq.clips) {
        if (!(m_playhead > c.start && m_playhead < c.end())) continue;
        if (const Track* t = seq.findTrack(c.track); t && !t->locked) return true;
    }
    return false;
}

void TimelineView::setTool(EditTool tool) {
    if (tool == m_tool) return;
    if (m_gesture != Gesture::None) finishGesture(false);
    m_tool = tool;
    updateCursor(mapFromGlobal(QCursor::pos()));
    emit toolChanged(tool);
}

// ---------------------------------------------------------------- edits

// Commits `edit` and selects the clips it added (paste, duplicate, text).
void TimelineView::selectAdded(const SequenceEdit& edit, const QString& text) {
    std::vector<ClipId> ids;
    for (const auto& c : edit.added) ids.push_back(c.id);
    commit(text, edit);
    if (!ids.empty()) setSelection(ids);
}

void TimelineView::copySelection() {
    const auto& seq = m_doc.project().sequence;
    m_clipboard.clear();
    for (auto id : ops::withLinked(seq, m_selection)) if (const Clip* c = seq.findClip(id)) m_clipboard.push_back(*c);
    emit clipboardChanged();
}

void TimelineView::paste() {
    if (m_clipboard.empty()) return;
    selectAdded(ops::pasteClips(m_doc, m_clipboard, m_playhead), tr("Paste"));
}

void TimelineView::duplicateSelection() {
    if (m_selection.empty()) return;
    selectAdded(ops::duplicateClips(m_doc, ops::withLinked(m_doc.project().sequence, m_selection)), tr("Duplicate"));
}

void TimelineView::addTextClip() {
    const auto& seq = m_doc.project().sequence;
    // Topmost unlocked video track, so the title sits above the footage.
    const Track* track = nullptr;
    for (const auto& t : seq.tracks) if (t.kind == TrackKind::Video && !t.locked) { track = &t; break; }
    if (!track) return;
    Clip c;
    c.id = m_doc.allocateClipId();
    c.track = track->id;
    c.kind = ClipKind::Text;
    c.name = tr("Title").toStdString();
    c.start = m_playhead;
    c.duration = ops::kDefaultTextDuration;
    c.text.fontSize = static_cast<float>(m_doc.project().settings.height) * 0.09f;
    c.text.shadow = true;
    c.text.shadowBlur = 8.0f;
    selectAdded(ops::insertClips(m_doc, {c}), tr("Add Text"));
}

void TimelineView::splitAtPlayhead() {
    const auto& seq = m_doc.project().sequence;
    // Split the selection when it sits under the playhead, else everything under it.
    std::vector<ClipId> ids;
    for (auto id : m_selection) if (const Clip* c = seq.findClip(id); c && m_playhead > c->start && m_playhead < c->end()) ids.push_back(id);
    SequenceEdit edit = ids.empty() ? ops::splitAt(m_doc, m_playhead) : ops::splitClips(m_doc, ids, m_playhead);
    // New right-hand pieces inherit the selection of the clip they were cut from.
    std::vector<ClipId> next = m_selection;
    for (const auto& piece : edit.added) {
        for (const auto& m : edit.modified) {
            if (m.track == piece.track && m.end() == piece.start && isSelected(m.id) && !contains(next, piece.id)) next.push_back(piece.id);
        }
    }
    commit(tr("Split Clip"), std::move(edit));
    setSelection(next);
}

void TimelineView::deleteSelection(bool ripple) {
    if (m_selection.empty()) return;
    const auto ids = ops::withLinked(m_doc.project().sequence, m_selection);
    commit(ripple ? tr("Ripple Delete") : tr("Delete Clip"), ops::removeClips(m_doc, ids, ripple));
}

std::vector<ClipId> TimelineView::addMedia(const QList<MediaId>& ids, TimePoint at) {
    std::vector<Clip> clips;
    TimePoint cursor = at.ticks >= 0 ? at : TimePoint{m_sequenceDuration.ticks};
    for (auto id : ids) {
        const MediaItem* item = m_doc.project().findMedia(id);
        if (!item) continue;
        auto placement = ops::defaultPlacement(m_doc.project(), *item, cursor);
        auto made = ops::clipsForMedia(m_doc, *item, placement);
        if (made.empty()) continue;
        cursor = made.front().end();
        for (auto& c : made) clips.push_back(std::move(c));
    }
    std::vector<ClipId> added;
    for (const auto& c : clips) added.push_back(c.id);
    commit(tr("Add to Timeline"), ops::insertClips(m_doc, std::move(clips)));
    setSelection(added);
    return added;
}

// ---------------------------------------------------------------- interaction

void TimelineView::seekTo(int x) {
    const Ticks t = ticksFromX(std::max(x, headerWidth()));
    setPlayhead(TimePoint{t});
    emit userSeeked(m_playhead, true);
}

std::optional<TimePoint> TimelineView::snapped(TimePoint t, const std::vector<ClipId>& ignore) const {
    if (!m_snapping) return std::nullopt;
    return ops::snap(t, ops::snapTargets(m_doc.project().sequence, ignore, m_playhead), snapTolerance());
}

void TimelineView::autoScrollTowards(int x) {
    if (x > width() - kAutoScrollMarginPx) setScrollOffset(m_scrollTicks + visibleTicks() / 30);
    else if (x < headerWidth() + kAutoScrollMarginPx && m_scrollTicks > 0) setScrollOffset(m_scrollTicks - visibleTicks() / 30);
}

void TimelineView::updateCursor(const QPoint& pos) {
    if (m_gesture != Gesture::None) return;
    if (pos.x() < headerWidth()) { setCursor(Qt::ArrowCursor); return; }
    const Hit hit = hitTest(pos);
    if (m_tool == EditTool::Blade) {
        const Lane* lane = laneAt(pos.y());
        setCursor(isValid(hit.clip) ? (lane && lane->locked ? Qt::ForbiddenCursor : Qt::SplitHCursor) : Qt::ArrowCursor);
        return;
    }
    if (isValid(hit.clip)) {
        const Lane* lane = laneAt(pos.y());
        if (lane && lane->locked) setCursor(Qt::ForbiddenCursor);
        else if (hit.nearStart || hit.nearEnd) setCursor(Qt::SizeHorCursor);
        else setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

// Blade: split the clicked clip. When the playhead is inside it (or inside
// other selected clips) the cut lands on the playhead - that is where the
// user has been looking - otherwise at the click position.
void TimelineView::bladeClick(const QPoint& pos, ClipId clip) {
    const auto& seq = m_doc.project().sequence;
    const Clip* c = seq.findClip(clip);
    if (!c) return;
    if (m_playhead > c->start && m_playhead < c->end()) {
        std::vector<ClipId> ids{clip};
        for (auto id : m_selection) if (const Clip* s = seq.findClip(id); s && m_playhead > s->start && m_playhead < s->end()) ids.push_back(id);
        commit(tr("Split Clip"), ops::splitClips(m_doc, ids, m_playhead));
        return;
    }
    TimePoint t = snapToFrame(TimePoint{ticksFromX(pos.x())}, m_fps);
    if (auto s = snapped(t, ops::withLinked(seq, {clip}))) t = *s;
    commit(tr("Split Clip"), ops::splitClips(m_doc, {clip}, t));
}

void TimelineView::updateMarquee(const QPoint& pos) {
    const QRect area = laneArea();
    const QPoint clamped(std::clamp(pos.x(), area.left(), area.right()), std::clamp(pos.y(), area.top(), area.bottom()));
    const QRect before = m_marquee;
    m_marquee = QRect(m_pressPos, clamped).normalized();
    const Ticks t0 = ticksFromX(m_marquee.left()), t1 = ticksFromX(m_marquee.right());
    std::vector<ClipId> next = (m_pressMods & (Qt::ControlModifier | Qt::ShiftModifier)) ? m_marqueeBase : std::vector<ClipId>{};
    for (const auto& c : m_doc.project().sequence.clips) {
        const Lane* lane = laneFor(c.track);
        if (!lane) continue;
        const bool rows = lane->y < m_marquee.bottom() && lane->y + lane->height > m_marquee.top();
        const bool time = c.start.ticks <= t1 && c.end().ticks >= t0;
        if (rows && time && !contains(next, c.id)) next.push_back(c.id);
    }
    setSelection(next);
    update(before.united(m_marquee).adjusted(-2, -2, 2, 2));
}

void TimelineView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    const QPoint pos = e->position().toPoint();
    if (e->button() == Qt::LeftButton && pos.x() < headerWidth()) {
        if (const Lane* lane = laneAt(pos.y())) {
            for (const auto& b : laneButtons(*lane)) if (b.rect.contains(pos)) { toggleTrackFlag(lane->id, b.flag); break; }
        }
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_pressPos = pos;
        m_pressMods = e->modifiers();
        if (pos.y() < rulerHeight()) {
            m_gesture = Gesture::Scrub;
            seekTo(pos.x());
            e->accept();
            return;
        }
        const Hit hit = hitTest(pos);
        const Lane* lane = laneAt(pos.y());
        if (m_tool == EditTool::Blade) {
            if (isValid(hit.clip) && lane && !lane->locked) bladeClick(pos, hit.clip);
            e->accept();
            return;
        }
        if (isValid(hit.clip)) {
            m_pressClip = hit.clip;
            m_pressWasSelected = isSelected(hit.clip);
            selectClip(hit.clip, e->modifiers());
            if (lane && lane->locked) { m_gesture = Gesture::None; e->accept(); return; }
            if (!(e->modifiers() & Qt::ControlModifier) && hit.nearStart) m_gesture = Gesture::TrimStart;
            else if (!(e->modifiers() & Qt::ControlModifier) && hit.nearEnd) m_gesture = Gesture::TrimEnd;
            else m_gesture = Gesture::PendingClip;
            e->accept();
            return;
        }
        // Empty lane: a click seeks, a drag draws a selection rectangle.
        m_marqueeBase = m_selection;
        if (!(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) clearSelection();
        m_gesture = Gesture::PendingMarquee;
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton) {
        const Hit hit = hitTest(pos);
        if (isValid(hit.clip) && !isSelected(hit.clip)) selectClip(hit.clip, Qt::NoModifier);
        emit contextMenuRequested(e->globalPosition().toPoint(), hit.clip);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    switch (m_gesture) {
        case Gesture::Scrub:
            autoScrollTowards(pos.x());
            seekTo(pos.x());
            return;
        case Gesture::PendingClip:
            if ((pos - m_pressPos).manhattanLength() < kDragThresholdPx) return;
            m_gesture = Gesture::Move;
            m_previewIds = m_selection;
            setCursor(Qt::ClosedHandCursor);
            [[fallthrough]];
        case Gesture::Move:
            autoScrollTowards(pos.x());
            updateMovePreview(pos);
            return;
        case Gesture::TrimStart:
        case Gesture::TrimEnd:
            autoScrollTowards(pos.x());
            updateTrimPreview(pos);
            return;
        case Gesture::PendingMarquee:
            if ((pos - m_pressPos).manhattanLength() < kDragThresholdPx) return;
            m_gesture = Gesture::Marquee;
            [[fallthrough]];
        case Gesture::Marquee:
            autoScrollTowards(pos.x());
            updateMarquee(pos);
            return;
        case Gesture::None:
            break;
    }
    if (pos.x() != m_hoverX) {
        const int old = m_hoverX;
        m_hoverX = pos.x() >= headerWidth() ? pos.x() : -1;
        if (old >= 0) update(QRect(old - 1, 0, 3, height()));
        if (m_hoverX >= 0) update(QRect(m_hoverX - 1, 0, 3, height()));
    }
    updateCursor(pos);
}

void TimelineView::updateMovePreview(const QPoint& pos) {
    if (m_previewIds.empty()) return;
    const auto& seq = m_doc.project().sequence;
    Ticks delta = ticksFromX(pos.x()) - ticksFromX(m_pressPos.x());
    // Snap whichever moved edge is closest to a target.
    m_snapGuide.reset();
    if (m_snapping) {
        const auto targets = ops::snapTargets(seq, m_previewIds, m_playhead);
        Ticks bestAdjust = 0;
        Ticks bestDist = snapTolerance().ticks + 1;
        for (auto id : m_previewIds) {
            const Clip* c = seq.findClip(id);
            if (!c) continue;
            for (TimePoint edge : {c->start + Duration{delta}, c->end() + Duration{delta}}) {
                if (auto s = ops::snap(edge, targets, snapTolerance())) {
                    const Ticks d = std::llabs(s->ticks - edge.ticks);
                    if (d < bestDist) { bestDist = d; bestAdjust = s->ticks - edge.ticks; m_snapGuide = *s; }
                }
            }
        }
        delta += bestAdjust;
    } else {
        delta = snapToFrame(TimePoint{delta}, m_fps).ticks;
    }
    const int trackDelta = laneIndexAt(pos.y()) - laneIndexAt(m_pressPos.y());
    m_previewEdit = ops::moveClips(m_doc, m_previewIds, Duration{delta}, trackDelta);
    if (m_previewEdit) {
        // Read back the effective delta (it may have been clamped at zero).
        for (const auto& m : m_previewEdit->modified) {
            if (const Clip* c = seq.findClip(m.id); c && contains(m_previewIds, m.id)) { m_previewDelta = m.start - c->start; break; }
        }
        const Clip* first = seq.findClip(m_previewIds.front());
        m_previewText = first ? formatTimecode(first->start + m_previewDelta, m_fps) : QString();
    }
    update();
}

void TimelineView::updateTrimPreview(const QPoint& pos) {
    const auto& seq = m_doc.project().sequence;
    const Clip* c = seq.findClip(m_pressClip);
    if (!c) return;
    TimePoint t{ticksFromX(pos.x())};
    m_snapGuide.reset();
    if (auto s = snapped(t, ops::withLinked(seq, {m_pressClip}))) { t = *s; m_snapGuide = *s; }
    else t = snapToFrame(t, m_fps);
    m_previewIds = ops::withLinked(seq, {m_pressClip});
    m_previewEdit = ops::trimClip(m_doc, m_pressClip, m_gesture == Gesture::TrimStart ? ops::Edge::Start : ops::Edge::End, t);
    for (const auto& m : m_previewEdit->modified) {
        if (m.id == m_pressClip) { m_previewText = tr("%1  ·  %2").arg(formatTimecode(m_gesture == Gesture::TrimStart ? m.start : m.end(), m_fps), formatClock(m.duration)); break; }
    }
    update();
}

void TimelineView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(e); return; }
    switch (m_gesture) {
        case Gesture::Scrub:
            emit userSeeked(m_playhead, false);
            break;
        case Gesture::PendingClip:
            // Plain click on an already-selected clip narrows the selection to it.
            if (m_pressWasSelected && !(m_pressMods & (Qt::ControlModifier | Qt::ShiftModifier)))
                setSelection(ops::withLinked(m_doc.project().sequence, {m_pressClip}));
            break;
        case Gesture::Move:
        case Gesture::TrimStart:
        case Gesture::TrimEnd:
            finishGesture(true);
            updateCursor(e->position().toPoint());
            return;
        case Gesture::PendingMarquee:
            seekTo(m_pressPos.x());            // it was a click after all
            emit userSeeked(m_playhead, false);
            break;
        case Gesture::Marquee: {
            const QRect old = m_marquee;
            m_marquee = QRect();
            m_marqueeBase.clear();
            update(old.adjusted(-2, -2, 2, 2));
            break;
        }
        case Gesture::None:
            break;
    }
    m_gesture = Gesture::None;
    updateCursor(e->position().toPoint());
}

void TimelineView::finishGesture(bool commitEdit) {
    if (commitEdit && m_previewEdit && !m_previewEdit->empty()) {
        const QString text = m_gesture == Gesture::Move ? (m_previewIds.size() > 1 ? tr("Move Clips") : tr("Move Clip")) : tr("Trim Clip");
        commit(text, *m_previewEdit);
    }
    m_gesture = Gesture::None;
    m_previewEdit.reset();
    m_previewIds.clear();
    m_previewDelta = Duration::zero();
    m_previewTrackDelta = 0;
    m_snapGuide.reset();
    m_previewText.clear();
    m_marquee = QRect();
    m_marqueeBase.clear();
    update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent* e) {
    // A fast second press behaves like any other press (select / blade / seek)...
    mousePressEvent(e);
    // ...and a double-click on a clip additionally announces it.
    const Hit hit = hitTest(e->position().toPoint());
    if (e->button() == Qt::LeftButton && isValid(hit.clip) && m_tool == EditTool::Select) emit clipDoubleClicked(hit.clip);
}

void TimelineView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && m_gesture != Gesture::None) {
        finishGesture(false);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool TimelineView::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const QPoint pos = he->pos();
        QString text;
        if (pos.x() < headerWidth()) {
            if (const Lane* lane = laneAt(pos.y())) {
                for (const auto& b : laneButtons(*lane)) {
                    if (!b.rect.contains(pos)) continue;
                    switch (b.flag) {
                        case TrackFlag::Mute: text = lane->kind == TrackKind::Video ? tr("Toggle track visibility") : tr("Mute track"); break;
                        case TrackFlag::Solo: text = tr("Solo track (only soloed tracks play)"); break;
                        case TrackFlag::Lock: text = tr("Lock track (no edits)"); break;
                    }
                }
            }
        } else if (const Hit hit = hitTest(pos); isValid(hit.clip)) {
            if (const Clip* c = m_doc.project().sequence.findClip(hit.clip)) {
                text = QStringLiteral("%1\n%2 → %3  (%4)").arg(QString::fromStdString(c->name), formatTimecode(c->start, m_fps), formatTimecode(c->end(), m_fps), formatClock(c->duration));
                if (hit.nearStart || hit.nearEnd) text += tr("\nDrag to trim");
            }
        }
        if (text.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return QWidget::event(e);
}

void TimelineView::leaveEvent(QEvent*) {
    if (m_hoverX >= 0) {
        const int old = m_hoverX;
        m_hoverX = -1;
        update(QRect(old - 1, 0, 3, height()));
    }
}

void TimelineView::wheelEvent(QWheelEvent* e) {
    const QPoint deg = e->angleDelta();
    const int steps = (deg.y() != 0 ? deg.y() : deg.x()) / 120;
    if (e->modifiers() & Qt::ControlModifier) {
        setZoom(m_pixelsPerSecond * std::pow(1.25, steps), static_cast<int>(e->position().x()));
    } else {
        const Ticks delta = static_cast<Ticks>(visibleTicks() / 10) * -steps;
        setScrollOffset(m_scrollTicks + delta);
    }
    e->accept();
}

void TimelineView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    clampScroll();
    emit viewChanged();
}

// ---------------------------------------------------------------- drag & drop (media bin / Explorer)

void TimelineView::dragEnterEvent(QDragEnterEvent* e) {
    m_dropClips.clear();
    m_dropGhosts.clear();
    m_dropIsFiles = false;
    const auto ids = MediaModel::decodeMime(e->mimeData());
    if (!ids.isEmpty()) {
        // Build the clips once (ids allocated now); dragMove only repositions them.
        TimePoint cursor = TimePoint::zero();
        for (auto id : ids) {
            const MediaItem* item = m_doc.project().findMedia(id);
            if (!item) continue;
            auto made = ops::clipsForMedia(m_doc, *item, ops::defaultPlacement(m_doc.project(), *item, cursor));
            if (made.empty()) continue;
            cursor = made.front().end();
            for (auto& c : made) m_dropClips.push_back(std::move(c));
        }
        if (m_dropClips.empty()) return;
        e->acceptProposedAction();
        dragMoveEvent(static_cast<QDragMoveEvent*>(e));
        return;
    }
    if (e->mimeData()->hasUrls() && !ImportController::collectMediaPaths(e->mimeData()->urls()).isEmpty()) {
        m_dropIsFiles = true;
        e->acceptProposedAction();
        dragMoveEvent(static_cast<QDragMoveEvent*>(e));
    }
}

void TimelineView::dragMoveEvent(QDragMoveEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (pos.x() < headerWidth()) { e->ignore(); return; }
    e->acceptProposedAction();
    TimePoint t{std::max<Ticks>(0, ticksFromX(pos.x()))};
    m_snapGuide.reset();
    if (auto s = snapped(t, {})) { t = *s; m_snapGuide = *s; }
    else t = snapToFrame(t, m_fps);
    m_dropTime = t;
    m_dropGhosts.clear();

    if (m_dropIsFiles) {
        const Lane* lane = laneAt(pos.y());
        const Lane* target = lane ? lane : (m_lanes.empty() ? nullptr : &m_lanes[0]);
        if (target) {
            const double w = 5.0 * m_pixelsPerSecond;
            m_dropGhosts.push_back({QRectF(xFromTicks(t.ticks), target->y + 2, w, target->height - 5), ClipKind::Video, tr("Import…")});
        }
        update();
        return;
    }

    // Visual clips land on the hovered video lane (default V1); audio on the
    // hovered audio lane (default A1). Linked pairs keep their partner on the
    // primary track of the other kind.
    const auto& seq = m_doc.project().sequence;
    const Lane* hovered = laneAt(pos.y());
    TrackId videoTrack = seq.primaryTrack(TrackKind::Video) ? seq.primaryTrack(TrackKind::Video)->id : TrackId::Invalid;
    TrackId audioTrack = seq.primaryTrack(TrackKind::Audio) ? seq.primaryTrack(TrackKind::Audio)->id : TrackId::Invalid;
    if (hovered && !hovered->locked) {
        if (hovered->kind == TrackKind::Video) videoTrack = hovered->id;
        else audioTrack = hovered->id;
    }
    const Ticks base = m_dropClips.front().start.ticks;
    for (auto& c : m_dropClips) {
        c.start = TimePoint{t.ticks + (c.start.ticks - base)};
        c.track = isVisualClip(c.kind) ? videoTrack : audioTrack;
        if (const Lane* lane = laneFor(c.track)) {
            m_dropGhosts.push_back({clipRect(c), c.kind, QString::fromStdString(c.name)});
        }
    }
    update();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent*) {
    m_dropGhosts.clear();
    m_dropClips.clear();
    m_dropIsFiles = false;
    m_snapGuide.reset();
    update();
}

void TimelineView::dropEvent(QDropEvent* e) {
    dragMoveEvent(static_cast<QDragMoveEvent*>(e));
    e->acceptProposedAction();
    if (m_dropIsFiles) {
        const QStringList paths = ImportController::collectMediaPaths(e->mimeData()->urls());
        const TimePoint at = m_dropTime;
        dragLeaveEvent(nullptr);
        if (!paths.isEmpty()) emit externalFilesDropped(paths, at);
        return;
    }
    std::vector<Clip> clips = std::move(m_dropClips);
    std::vector<ClipId> ids;
    for (const auto& c : clips) ids.push_back(c.id);
    dragLeaveEvent(nullptr);
    commit(tr("Add to Timeline"), ops::insertClips(m_doc, std::move(clips)));
    setSelection(ids);
    setFocus(Qt::OtherFocusReason);
}

// ---------------------------------------------------------------- painting

void TimelineView::paintEvent(QPaintEvent* e) {
    QPainter p(this);
    const auto& pal = Theme::palette();
    p.fillRect(e->rect(), pal.timelineBg);

    const TickSpec ticks = chooseTicks();
    const QRect lanes = laneArea();
    const QRect ruler(headerWidth(), 0, lanes.width(), rulerHeight());

    if (e->rect().intersects(lanes)) {
        paintTracks(p, lanes, ticks);
        paintClips(p, lanes);
        paintDropGhosts(p);
    }
    if (e->rect().intersects(ruler)) paintRuler(p, ruler, ticks);
    if (e->rect().left() < headerWidth()) paintHeaders(p);

    if (m_hoverX >= headerWidth() && m_gesture == Gesture::None && m_dropGhosts.empty()) {
        p.setPen(QPen(QColor(255, 255, 255, 28), 1));
        p.drawLine(m_hoverX, rulerHeight(), m_hoverX, height());
    }
    paintSnapGuide(p);
    paintMarquee(p);
    paintPlayhead(p);
}

void TimelineView::paintMarquee(QPainter& p) {
    if (m_gesture != Gesture::Marquee || m_marquee.isNull()) return;
    const auto& pal = Theme::palette();
    QColor fill = pal.accent;
    fill.setAlpha(40);
    p.fillRect(m_marquee, fill);
    p.setPen(QPen(pal.accent, 1));
    p.drawRect(m_marquee.adjusted(0, 0, -1, -1));
}

void TimelineView::paintRuler(QPainter& p, const QRect& r, const TickSpec& ticks) {
    const auto& pal = Theme::palette();
    p.save();
    p.setClipRect(r);
    p.fillRect(r, pal.rulerBg);
    p.setPen(pal.border);
    p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());

    QFont f = font();
    f.setPointSizeF(f.pointSizeF() - 0.5);
    p.setFont(f);

    const Ticks first = (m_scrollTicks / ticks.major) * ticks.major;
    const Ticks last = m_scrollTicks + visibleTicks() + ticks.major;
    const Ticks minor = ticks.major / std::max(1, ticks.minorDivisions);
    for (Ticks t = first; t <= last; t += ticks.major) {
        const int x = static_cast<int>(std::floor(xFromTicks(t)));
        p.setPen(pal.textMuted);
        p.drawLine(x, r.bottom() - 9, x, r.bottom());
        p.setPen(t > m_sequenceDuration.ticks ? pal.textDisabled : pal.textSecondary);
        p.drawText(x + 4, r.top() + 12, formatRulerLabel(TimePoint{t}, ticks.labelFrames, m_fps));
        if (ticks.minorDivisions > 1) {
            p.setPen(pal.gridMajor);
            for (int i = 1; i < ticks.minorDivisions; ++i) {
                const int mx = static_cast<int>(std::floor(xFromTicks(t + minor * i)));
                p.drawLine(mx, r.bottom() - 4, mx, r.bottom());
            }
        }
    }
    p.restore();
}

void TimelineView::paintHeaders(QPainter& p) {
    const auto& pal = Theme::palette();
    p.save();
    const int w = headerWidth();
    p.fillRect(QRect(0, 0, w, rulerHeight()), pal.rulerBg);
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1.5);
    small.setWeight(QFont::DemiBold);
    for (const auto& lane : m_lanes) {
        const QRect r(0, lane.y, w, lane.height);
        p.fillRect(r, pal.trackHeader);
        p.setPen(pal.border);
        p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        p.fillRect(QRect(0, lane.y, 3, lane.height), lane.kind == TrackKind::Video ? pal.clipVideo : pal.clipAudio);
        p.setPen(lane.muted ? pal.textMuted : pal.textPrimary);
        p.setFont(bold);
        p.drawText(QRect(12, lane.y, w - 70, lane.height), Qt::AlignLeft | Qt::AlignVCenter, lane.name);

        // Toggle buttons: video [eye][L], audio [M][S][L]
        auto button = [&](const QRect& br, const QString& letter, bool on, const QColor& onColor, bool glyphEye = false) {
            p.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addRoundedRect(QRectF(br).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            if (on) { p.fillPath(path, onColor); p.setPen(Qt::NoPen); }
            else { p.setPen(QPen(pal.borderStrong, 1)); p.drawPath(path); }
            p.setPen(on ? pal.accentText : pal.textMuted);
            if (glyphEye) {
                // Eye: visible when off (track shown), struck through when hidden.
                const QPointF c(br.center().x() + 0.5, br.center().y() + 0.5);
                QPainterPath eye;
                eye.moveTo(c.x() - 6, c.y());
                eye.quadTo(c.x(), c.y() - 5.5, c.x() + 6, c.y());
                eye.quadTo(c.x(), c.y() + 5.5, c.x() - 6, c.y());
                p.setPen(QPen(on ? pal.accentText : pal.textSecondary, 1.2));
                p.drawPath(eye);
                p.setBrush(on ? pal.accentText : pal.textSecondary);
                p.drawEllipse(c, 1.8, 1.8);
                p.setBrush(Qt::NoBrush);
                if (on) p.drawLine(QPointF(c.x() - 6, c.y() + 5), QPointF(c.x() + 6, c.y() - 5));
            } else {
                p.setFont(small);
                p.drawText(br, Qt::AlignCenter, letter);
            }
            p.setRenderHint(QPainter::Antialiasing, false);
        };
        for (const auto& b : laneButtons(lane)) {
            switch (b.flag) {
                case TrackFlag::Mute:
                    if (lane.kind == TrackKind::Video) button(b.rect, QString(), lane.muted, pal.warning, true);
                    else button(b.rect, QStringLiteral("M"), lane.muted, pal.warning);
                    break;
                case TrackFlag::Solo: button(b.rect, QStringLiteral("S"), lane.solo, pal.success); break;
                case TrackFlag::Lock: button(b.rect, QStringLiteral("L"), lane.locked, pal.danger); break;
            }
        }
    }
    const int bottom = m_lanes.empty() ? rulerHeight() : m_lanes.back().y + m_lanes.back().height;
    p.fillRect(QRect(0, bottom, w, height() - bottom), pal.timelineBg);
    p.setPen(pal.borderStrong);
    p.drawLine(w - 1, 0, w - 1, height());
    p.restore();
}

void TimelineView::paintTracks(QPainter& p, const QRect& area, const TickSpec& ticks) {
    const auto& pal = Theme::palette();
    p.save();
    p.setClipRect(area);

    for (const auto& lane : m_lanes) {
        QColor bg = lane.kind == TrackKind::Video ? pal.trackVideo : pal.trackAudio;
        p.fillRect(QRect(area.left(), lane.y, area.width(), lane.height), bg);
        if (lane.locked) {
            // Hatched overlay signals "no edits here".
            p.setPen(QPen(QColor(255, 255, 255, 10), 1));
            for (int x = area.left() - lane.height; x < area.right(); x += 12) p.drawLine(x, lane.y + lane.height, x + lane.height, lane.y);
        }
        p.setPen(pal.border);
        p.drawLine(area.left(), lane.y + lane.height - 1, area.right(), lane.y + lane.height - 1);
    }
    const int lanesBottom = m_lanes.empty() ? area.top() : m_lanes.back().y + m_lanes.back().height;

    const Ticks first = (m_scrollTicks / ticks.major) * ticks.major;
    const Ticks last = m_scrollTicks + visibleTicks() + ticks.major;
    const Ticks minor = ticks.major / std::max(1, ticks.minorDivisions);
    const bool drawMinor = (static_cast<double>(minor) / kFlicksPerSecond * m_pixelsPerSecond) >= 24.0;
    for (Ticks t = first; t <= last; t += ticks.major) {
        const int x = static_cast<int>(std::floor(xFromTicks(t)));
        p.setPen(pal.gridMajor);
        p.drawLine(x, area.top(), x, lanesBottom);
        if (drawMinor) {
            p.setPen(pal.gridMinor);
            for (int i = 1; i < ticks.minorDivisions; ++i) {
                const int mx = static_cast<int>(std::floor(xFromTicks(t + minor * i)));
                p.drawLine(mx, area.top(), mx, lanesBottom);
            }
        }
    }

    // Beyond the sequence end
    const int endX = static_cast<int>(std::floor(xFromTicks(m_sequenceDuration.ticks)));
    if (endX < area.right()) {
        p.fillRect(QRect(std::max(endX, area.left()), area.top(), area.right() - std::max(endX, area.left()) + 1, lanesBottom - area.top()), QColor(0, 0, 0, 60));
        if (endX >= area.left() && !m_sequenceDuration.isZero()) {
            p.setPen(pal.borderStrong);
            p.drawLine(endX, area.top(), endX, lanesBottom);
        }
    }

    if (m_doc.project().sequence.clips.empty() && m_dropGhosts.empty() && !m_lanes.empty()) {
        p.setPen(pal.textMuted);
        p.drawText(QRect(area.left(), area.top(), area.width(), lanesBottom - area.top()), Qt::AlignCenter,
                   tr("Drag media here to add it to the timeline"));
    }
    p.restore();
}

void TimelineView::paintClips(QPainter& p, const QRect& area) {
    p.save();
    p.setClipRect(area);
    const auto& seq = m_doc.project().sequence;
    const std::vector<Clip>* clips = &seq.clips;
    if (m_previewEdit) {
        m_previewClipsCache = ops::applied(seq, *m_previewEdit);
        clips = &m_previewClipsCache;
    }
    const Ticks visStart = m_scrollTicks, visEnd = m_scrollTicks + visibleTicks();
    for (const auto& c : *clips) {
        if (c.end().ticks < visStart || c.start.ticks > visEnd) continue;
        const Lane* lane = laneFor(c.track);
        if (!lane) continue;
        const bool ghost = m_previewEdit && contains(m_previewIds, c.id);
        paintClip(p, c, clipRect(c), isSelected(c.id), ghost, lane->muted);
    }
    if (m_previewEdit && !m_previewText.isEmpty()) {
        // Time readout near the cursor while dragging.
        const QPoint cur = mapFromGlobal(QCursor::pos());
        QFont f = font();
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QRect tr = p.fontMetrics().boundingRect(m_previewText).adjusted(-6, -3, 6, 3);
        QRect box = tr.translated(cur + QPoint(14, -tr.height() - 6));
        box.moveLeft(std::clamp(box.left(), area.left(), std::max(area.left(), area.right() - box.width())));
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(box, 3, 3);
        p.fillPath(path, Theme::palette().bgElevated);
        p.setPen(Theme::palette().textPrimary);
        p.drawText(box, Qt::AlignCenter, m_previewText);
    }
    p.restore();
}

void TimelineView::paintClip(QPainter& p, const Clip& c, const QRectF& r, bool selected, bool ghost, bool dimmed) {
    const auto& pal = Theme::palette();
    if (r.width() < 1) return;
    QColor base = clipColor(c.kind);
    QColor fill = base.darker(selected ? 105 : 135);
    QColor border = selected ? pal.textPrimary : base.darker(170);
    int alpha = 235;
    if (dimmed) alpha = 110;
    if (ghost) alpha = 150;
    fill.setAlpha(alpha);
    border.setAlpha(std::min(255, alpha + 20));

    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.fillPath(path, fill);
    // Kind strip at the top
    p.save();
    p.setClipPath(path);
    QColor strip = base.lighter(selected ? 130 : 110);
    strip.setAlpha(alpha);
    p.fillRect(QRectF(r.left(), r.top(), r.width(), 4), strip);
    // Video / image: poster frame at the right edge when there is room for it
    // next to the label; the label is elided to stay clear of it.
    double labelReserve = 0;
    if ((c.kind == ClipKind::Video || c.kind == ClipKind::Image) && r.width() > 120 && r.height() > 30 && m_thumbnails) {
        const QImage thumb = m_thumbnails(c.media);
        if (!thumb.isNull()) {
            const double th = r.height() - 8;
            const double tw = std::min(th * thumb.width() / std::max(1, thumb.height()), r.width() * 0.45);
            QRectF tr(r.right() - tw - 4, r.top() + 6, tw, th);
            p.setOpacity(ghost || dimmed ? 0.45 : 0.85);
            p.drawImage(tr, thumb);
            p.setOpacity(1.0);
            labelReserve = tw + 8;
        }
    }
    // Audio: real peak envelope (analysed in the background), one column per pixel.
    if (c.kind == ClipKind::Audio && r.width() > 10) {
        std::shared_ptr<const media::Waveform> wave;
        if (m_waveforms) {
            if (const MediaItem* item = m_doc.project().findMedia(c.media); item && !item->offline) wave = m_waveforms->peaksFor(c.media, QString::fromStdString(item->path));
        }
        QColor col = base.lighter(160);
        col.setAlpha(std::min(255, alpha / 2 + 40));
        const double mid = r.top() + 4 + (r.height() - 4) / 2.0;
        const double amp = (r.height() - 10) / 2.0;
        p.setPen(QPen(col, 1));
        const double x0 = std::max(r.left() + 1, static_cast<double>(headerWidth()));
        const double x1 = std::min(r.right() - 1, static_cast<double>(width()));
        if (wave && !wave->empty()) {
            // Gain (volume + fades + mute) shapes the envelope so the user sees what they hear.
            for (double x = x0; x < x1; x += 1.0) {
                const TimePoint t0{ticksFromX(x)}, t1{ticksFromX(x + 1.0)};
                const TimePoint s0 = c.sourceTimeAt(t0), s1 = c.sourceTimeAt(t1);
                const float peak = wave->peakBetween(s0, std::max(s1, TimePoint{s0.ticks + 1})) / 255.0f;
                const float gain = std::min(1.0f, playback::SequenceProgram::gainAt(c, t0));
                // Perceptual-ish scale: quiet material (-20 dBFS) still reads as a waveform.
                const double h = std::max(0.5, amp * std::pow(static_cast<double>(peak * gain), 0.55));
                p.drawLine(QPointF(x + 0.5, mid - h), QPointF(x + 0.5, mid + h));
            }
        } else {
            // Not analysed yet: a quiet centre line.
            p.drawLine(QPointF(x0, mid), QPointF(x1, mid));
        }
    }
    p.restore();
    p.setPen(QPen(border, selected ? 2 : 1, ghost ? Qt::DashLine : Qt::SolidLine));
    p.drawPath(path);

    // Trim handles hint on wide clips
    if (r.width() > 40 && !ghost) {
        QColor h = base.lighter(160);
        h.setAlpha(alpha / 2);
        p.fillRect(QRectF(r.left() + 2, r.top() + 8, 2, r.height() - 12), h);
        p.fillRect(QRectF(r.right() - 4, r.top() + 8, 2, r.height() - 12), h);
    }

    // Label
    if (r.width() > 24) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() - 0.5);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        QColor text = pal.textPrimary;
        text.setAlpha(alpha);
        p.setPen(text);
        const QRectF textRect(r.left() + 8, r.top() + 5, r.width() - 16 - labelReserve, std::min(18.0, r.height() - 6));
        const QString name = p.fontMetrics().elidedText(QString::fromStdString(c.name), Qt::ElideMiddle, static_cast<int>(textRect.width()));
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, name);
        if (r.height() > 36 && r.width() > 90) {
            QFont sf = font();
            sf.setPointSizeF(font().pointSizeF() - 1.5);
            p.setFont(sf);
            QColor sub = pal.textSecondary;
            sub.setAlpha(alpha);
            p.setPen(sub);
            QString detail = formatClock(c.duration);
            if (c.speed != Rational{1, 1}) detail += QStringLiteral("  ·  %1%").arg(std::llround(c.speed.toDouble() * 100));
            p.drawText(QRectF(r.left() + 8, r.top() + 22, r.width() - 16 - labelReserve, 16), Qt::AlignLeft | Qt::AlignVCenter, detail);
        }
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void TimelineView::paintDropGhosts(QPainter& p) {
    if (m_dropGhosts.empty()) return;
    const auto& pal = Theme::palette();
    p.save();
    p.setClipRect(laneArea());
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& g : m_dropGhosts) {
        QColor fill = clipColor(g.kind);
        fill.setAlpha(90);
        QPainterPath path;
        path.addRoundedRect(g.rect.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        p.fillPath(path, fill);
        p.setPen(QPen(pal.accent, 1.5, Qt::DashLine));
        p.drawPath(path);
        if (g.rect.width() > 30) {
            p.setPen(pal.textPrimary);
            p.drawText(g.rect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(g.name, Qt::ElideMiddle, static_cast<int>(g.rect.width() - 16)));
        }
    }
    p.restore();
}

void TimelineView::paintSnapGuide(QPainter& p) {
    if (!m_snapGuide) return;
    const int x = static_cast<int>(std::floor(xFromTicks(m_snapGuide->ticks)));
    if (x < headerWidth() || x > width()) return;
    p.setPen(QPen(Theme::palette().accent, 1, Qt::DashLine));
    p.drawLine(x, rulerHeight(), x, height());
}

void TimelineView::paintPlayhead(QPainter& p) {
    const auto& pal = Theme::palette();
    const int x = static_cast<int>(std::floor(xFromTicks(m_playhead.ticks)));
    if (x < headerWidth() - kPlayheadHalfWidth || x > width() + kPlayheadHalfWidth) return;
    p.save();
    const int lanesBottom = m_lanes.empty() ? rulerHeight() : m_lanes.back().y + m_lanes.back().height;
    p.setClipRect(QRect(headerWidth(), 0, width() - headerWidth(), height()));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(pal.playhead, 1));
    p.drawLine(QPointF(x + 0.5, rulerHeight()), QPointF(x + 0.5, lanesBottom));
    QPainterPath head;
    const qreal top = rulerHeight() - 12.0;
    head.moveTo(x - kPlayheadHalfWidth + 0.5, top);
    head.lineTo(x + kPlayheadHalfWidth + 0.5, top);
    head.lineTo(x + kPlayheadHalfWidth + 0.5, top + 6);
    head.lineTo(x + 0.5, rulerHeight() - 1.0);
    head.lineTo(x - kPlayheadHalfWidth + 0.5, top + 6);
    head.closeSubpath();
    p.fillPath(head, pal.playhead);
    p.restore();
}

}  // namespace lf::ui
