#include "ui/timeline/TimelineView.h"

#include "ui/Theme.h"
#include "ui/Timecode.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lf::ui {

namespace {
constexpr double kMinPixelsPerSecond = 0.5;      // ~33 minutes across 1000 px
constexpr double kMaxPixelsPerSecond = 4000.0;   // sub-frame zoom
constexpr int kMinMajorSpacingPx = 90;
constexpr int kPlayheadHalfWidth = 6;

// Candidate major tick intervals, in seconds; frame-based intervals are added at runtime.
constexpr double kIntervalsSeconds[] = {0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
}  // namespace

TimelineView::TimelineView(QWidget* parent) : QWidget(parent) {
    const auto& m = Theme::metrics();
    m_lanes = {{tr("V1"), true, m.videoTrackHeight}, {tr("A1"), false, m.audioTrackHeight}};
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::ArrowCursor);
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

Ticks TimelineView::visibleTicks() const noexcept {
    const int px = std::max(1, width() - headerWidth());
    return static_cast<Ticks>(px / m_pixelsPerSecond * kFlicksPerSecond);
}

Ticks TimelineView::scrollRange() const noexcept {
    // Allow scrolling a little past the end so the last frame is never glued to the edge.
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
    return {400, h};
}

QRect TimelineView::playheadRect(TimePoint t) const {
    const int x = static_cast<int>(std::floor(xFromTicks(t.ticks)));
    return QRect(x - kPlayheadHalfWidth - 1, 0, 2 * kPlayheadHalfWidth + 3, height());
}

TimelineView::TickSpec TimelineView::chooseTicks() const {
    const Ticks frame = frameDuration(m_fps).ticks;
    const double frameSeconds = static_cast<double>(frame) / kFlicksPerSecond;

    // Frame-based candidates first (1, 2, 5, 10 frames), then seconds.
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
    update(before.united(playheadRect(m_playhead)));   // only the two playhead columns repaint
    emit playheadChanged(m_playhead);
}

void TimelineView::setFrameRate(Rational fps) {
    if (fps == m_fps) return;
    m_fps = fps;
    update();
}

void TimelineView::setSequenceDuration(Duration d) {
    m_duration = std::max(d, Duration::fromSeconds(1));
    clampScroll();
    update();
    emit viewChanged();
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
    // Keep the time under the anchor pixel stationary.
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
    m_scrollTicks = 0;
    setZoom(px / std::max(0.001, m_duration.toSeconds()), headerWidth());
    m_scrollTicks = 0;
    update();
    emit viewChanged();
}

void TimelineView::stepFrames(int frames) {
    setPlayhead(TimePoint{m_playhead.ticks + frames * frameDuration(m_fps).ticks});
    // Keep the playhead in view while stepping.
    const double x = xFromTicks(m_playhead.ticks);
    if (x < headerWidth() || x > width() - 8) setScrollOffset(m_playhead.ticks - visibleTicks() / 2);
}

void TimelineView::goToStart() { setPlayhead(TimePoint::zero()); setScrollOffset(0); }
void TimelineView::goToEnd() { setPlayhead(TimePoint{m_duration.ticks}); setScrollOffset(m_duration.ticks - visibleTicks() * 3 / 4); }

// ---------------------------------------------------------------- interaction

void TimelineView::seekTo(int x, bool scrubbing) {
    const Ticks t = ticksFromX(std::max(x, headerWidth()));
    setPlayhead(TimePoint{t});
    if (scrubbing) emit playheadScrubbed(m_playhead);
}

void TimelineView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && e->position().x() >= headerWidth()) {
        m_scrubbing = true;
        seekTo(static_cast<int>(e->position().x()), true);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const int x = static_cast<int>(e->position().x());
    if (m_scrubbing) {
        seekTo(x, true);
        return;
    }
    if (x != m_hoverX) {
        const int old = m_hoverX;
        m_hoverX = x >= headerWidth() ? x : -1;
        // Repaint only the hover guide columns.
        if (old >= 0) update(QRect(old - 1, 0, 3, height()));
        if (m_hoverX >= 0) update(QRect(m_hoverX - 1, 0, 3, height()));
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_scrubbing && e->button() == Qt::LeftButton) {
        m_scrubbing = false;
        emit playheadScrubbed(m_playhead);
    }
    QWidget::mouseReleaseEvent(e);
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
        // Plain wheel scrolls horizontally: a timeline has one long axis.
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

// ---------------------------------------------------------------- painting

void TimelineView::paintEvent(QPaintEvent* e) {
    QPainter p(this);
    const auto& pal = Theme::palette();
    p.fillRect(e->rect(), pal.timelineBg);

    const TickSpec ticks = chooseTicks();
    const QRect lanes = laneArea();
    const QRect ruler(headerWidth(), 0, lanes.width(), rulerHeight());

    if (e->rect().intersects(lanes)) paintTracks(p, lanes, ticks);
    if (e->rect().intersects(ruler)) paintRuler(p, ruler, ticks);
    if (e->rect().left() < headerWidth()) paintHeaders(p);

    // Hover guide
    if (m_hoverX >= headerWidth() && !m_scrubbing) {
        p.setPen(QPen(QColor(255, 255, 255, 28), 1));
        p.drawLine(m_hoverX, rulerHeight(), m_hoverX, height());
    }
    paintPlayhead(p);
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
        p.setPen(t > m_duration.ticks ? pal.textDisabled : pal.textSecondary);
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
    int y = rulerHeight();
    for (const auto& lane : m_lanes) {
        const QRect r(0, y, w, lane.height);
        p.fillRect(r, pal.trackHeader);
        p.setPen(pal.border);
        p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        // Track type strip
        p.fillRect(QRect(0, y, 3, lane.height), lane.isVideo ? pal.clipVideo : pal.clipAudio);
        p.setPen(pal.textPrimary);
        QFont f = font();
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.drawText(QRect(12, y, w - 16, lane.height), Qt::AlignLeft | Qt::AlignVCenter, lane.name);
        p.setPen(pal.textMuted);
        p.setFont(font());
        p.drawText(QRect(12, y, w - 16, lane.height), Qt::AlignRight | Qt::AlignVCenter, lane.isVideo ? tr("Video") : tr("Audio"));
        y += lane.height;
    }
    p.fillRect(QRect(0, y, w, height() - y), pal.timelineBg);
    p.setPen(pal.borderStrong);
    p.drawLine(w - 1, 0, w - 1, height());
    p.restore();
}

void TimelineView::paintTracks(QPainter& p, const QRect& area, const TickSpec& ticks) {
    const auto& pal = Theme::palette();
    p.save();
    p.setClipRect(area);

    int y = area.top();
    for (const auto& lane : m_lanes) {
        p.fillRect(QRect(area.left(), y, area.width(), lane.height), lane.isVideo ? pal.trackVideo : pal.trackAudio);
        p.setPen(pal.border);
        p.drawLine(area.left(), y + lane.height - 1, area.right(), y + lane.height - 1);
        y += lane.height;
    }
    const int lanesBottom = y;

    // Grid at major ticks, and a fainter line at minor ticks when zoomed in.
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
    const int endX = static_cast<int>(std::floor(xFromTicks(m_duration.ticks)));
    if (endX < area.right()) {
        p.fillRect(QRect(endX, area.top(), area.right() - endX + 1, lanesBottom - area.top()), QColor(0, 0, 0, 70));
        p.setPen(pal.borderStrong);
        p.drawLine(endX, area.top(), endX, lanesBottom);
    }

    // Empty-state hint inside V1 (no clip model yet)
    if (!m_lanes.empty()) {
        p.setPen(pal.textMuted);
        p.drawText(QRect(area.left(), area.top(), area.width(), m_lanes.front().height), Qt::AlignCenter,
                   tr("Drag media here to add it to the timeline"));
    }
    p.restore();
}

void TimelineView::paintPlayhead(QPainter& p) {
    const auto& pal = Theme::palette();
    const int x = static_cast<int>(std::floor(xFromTicks(m_playhead.ticks)));
    if (x < headerWidth() - kPlayheadHalfWidth || x > width() + kPlayheadHalfWidth) return;
    p.save();
    int lanesBottom = rulerHeight();
    for (const auto& lane : m_lanes) lanesBottom += lane.height;
    p.setClipRect(QRect(headerWidth(), 0, width() - headerWidth(), height()));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(pal.playhead, 1));
    p.drawLine(QPointF(x + 0.5, rulerHeight()), QPointF(x + 0.5, lanesBottom));
    // Head marker in the ruler
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
