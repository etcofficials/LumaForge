#pragma once

#include "lumaforge/core/Time.h"

#include <QWidget>

#include <vector>

namespace lf::ui {

// The timeline canvas: time ruler, track headers, track lanes and playhead.
// Everything is drawn by hand (no item views) because the timeline is the
// most performance-sensitive surface in the editor: painting must stay cheap
// at any zoom, and the playhead must move without repainting the tracks.
//
// V0.1 shell: no clip model yet. The coordinate system, zoom/scroll, ruler
// and playhead interaction are final; clips plug into paintTracks().
class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(QWidget* parent = nullptr);

    [[nodiscard]] TimePoint playhead() const noexcept { return m_playhead; }
    [[nodiscard]] Rational frameRate() const noexcept { return m_fps; }
    [[nodiscard]] Duration sequenceDuration() const noexcept { return m_duration; }
    [[nodiscard]] bool snappingEnabled() const noexcept { return m_snapping; }

    // Visible range in ticks; used by the external scrollbar.
    [[nodiscard]] Ticks scrollOffset() const noexcept { return m_scrollTicks; }
    [[nodiscard]] Ticks visibleTicks() const noexcept;
    [[nodiscard]] Ticks scrollRange() const noexcept;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setPlayhead(lf::TimePoint t);
    void setFrameRate(lf::Rational fps);
    void setSequenceDuration(lf::Duration d);
    void setSnappingEnabled(bool enabled);
    void setScrollOffset(lf::Ticks ticks);
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void stepFrames(int frames);
    void goToStart();
    void goToEnd();

signals:
    void playheadChanged(lf::TimePoint time);   // user-driven or programmatic
    void playheadScrubbed(lf::TimePoint time);  // user is dragging (preview should seek)
    void viewChanged();                         // zoom or scroll changed

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct TrackLane {
        QString name;
        bool isVideo;
        int height;
    };
    struct TickSpec {
        Ticks major;
        int minorDivisions;
        bool labelFrames;
    };

    [[nodiscard]] int headerWidth() const noexcept;
    [[nodiscard]] int rulerHeight() const noexcept;
    [[nodiscard]] QRect laneArea() const;                // tracks region (right of headers, below ruler)
    [[nodiscard]] double xFromTicks(Ticks t) const;
    [[nodiscard]] Ticks ticksFromX(double x) const;
    [[nodiscard]] TickSpec chooseTicks() const;
    [[nodiscard]] QRect playheadRect(TimePoint t) const;

    void paintRuler(QPainter& p, const QRect& r, const TickSpec& ticks);
    void paintHeaders(QPainter& p);
    void paintTracks(QPainter& p, const QRect& area, const TickSpec& ticks);
    void paintPlayhead(QPainter& p);
    void seekTo(int x, bool scrubbing);
    void setZoom(double pixelsPerSecond, int anchorX);
    void clampScroll();

    Rational m_fps = fps::f30;
    Duration m_duration = Duration::fromSeconds(60);
    TimePoint m_playhead;
    double m_pixelsPerSecond = 40.0;
    Ticks m_scrollTicks = 0;
    bool m_snapping = true;
    bool m_scrubbing = false;
    int m_hoverX = -1;
    std::vector<TrackLane> m_lanes;
};

}  // namespace lf::ui
