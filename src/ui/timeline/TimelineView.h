#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/TimelineCommands.h"

#include <QImage>
#include <QWidget>

#include <functional>

#include <optional>
#include <vector>

namespace lf::ui {

class WaveformService;

// The timeline canvas: time ruler, track headers, track lanes, clips and
// playhead. Everything is drawn by hand (no item views) because the timeline
// is the most performance-sensitive surface in the editor: painting must stay
// cheap at any zoom, and the playhead must move without repainting the tracks.
//
// The view observes the Document and turns gestures into SequenceEdits
// (through ops::*) which it pushes as commands. It never mutates the project
// directly, so every gesture is undoable.
// The editing tool decides what a press in the lane area does. Tools are a
// property of the view (set from the ActionRegistry's tool actions), never
// implemented as separate widgets.
enum class EditTool { Select, Blade };

class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(project::Document& doc, QWidget* parent = nullptr);
    ~TimelineView() override;

    [[nodiscard]] EditTool tool() const noexcept { return m_tool; }
    // Shared peak cache (owned by the panel); repaints when peaks arrive.
    void setWaveformService(WaveformService* service);
    // Poster frames for video clips come from the media bin's thumbnail cache.
    using ThumbnailProvider = std::function<QImage(project::MediaId)>;
    void setThumbnailProvider(ThumbnailProvider provider);
    void thumbnailsChanged();   // repaint clip rows after new thumbnails arrived
    [[nodiscard]] bool canPaste() const noexcept { return !m_clipboard.empty(); }

    [[nodiscard]] TimePoint playhead() const noexcept { return m_playhead; }
    [[nodiscard]] Rational frameRate() const noexcept { return m_fps; }
    [[nodiscard]] Duration sequenceDuration() const noexcept { return m_sequenceDuration; }
    [[nodiscard]] bool snappingEnabled() const noexcept { return m_snapping; }
    [[nodiscard]] const std::vector<project::ClipId>& selection() const noexcept { return m_selection; }
    [[nodiscard]] bool hasSelection() const noexcept { return !m_selection.empty(); }
    // Clip under the playhead (strictly inside) on any unlocked track?
    [[nodiscard]] bool canSplitAtPlayhead() const;

    // Visible range in ticks; used by the external scrollbar.
    [[nodiscard]] Ticks scrollOffset() const noexcept { return m_scrollTicks; }
    [[nodiscard]] Ticks visibleTicks() const noexcept;
    [[nodiscard]] Ticks scrollRange() const noexcept;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setPlayhead(lf::TimePoint t);          // programmatic (playback); no seek is requested
    void setFrameRate(lf::Rational fps);
    void setSnappingEnabled(bool enabled);
    void setScrollOffset(lf::Ticks ticks);
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void setSelection(const std::vector<lf::project::ClipId>& ids);
    void selectAll();
    void clearSelection();

    void setTool(lf::ui::EditTool tool);

    // Edits (each is one undo step)
    void splitAtPlayhead();
    void deleteSelection(bool ripple);
    void copySelection();
    void paste();              // at the playhead
    void duplicateSelection();
    void addTextClip();        // 5 s title at the playhead on the top video track
    // Adds media at the given time on the default tracks (V1/A1), or at the
    // sequence end when time < 0. Returns the created visual/audio clip ids.
    std::vector<lf::project::ClipId> addMedia(const QList<lf::project::MediaId>& ids, lf::TimePoint at);

signals:
    void playheadChanged(lf::TimePoint time);   // any change
    void userSeeked(lf::TimePoint time, bool scrubbing);   // the user moved the playhead (preview must seek)
    void viewChanged();                         // zoom or scroll changed
    void selectionChanged();
    void clipboardChanged();
    void toolChanged(lf::ui::EditTool tool);
    void clipDoubleClicked(lf::project::ClipId id);
    void contextMenuRequested(const QPoint& globalPos, lf::project::ClipId clipUnderCursor);
    // Files from Explorer dropped on the timeline; the owner imports them and calls addMediaAt().
    void externalFilesDropped(const QStringList& paths, lf::TimePoint at);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Lane {
        project::TrackId id;
        project::TrackKind kind;
        QString name;
        int y = 0;
        int height = 0;
        bool muted = false;
        bool locked = false;
        bool solo = false;
    };
    struct TickSpec {
        Ticks major;
        int minorDivisions;
        bool labelFrames;
    };
    enum class Gesture { None, Scrub, PendingClip, Move, TrimStart, TrimEnd, PendingMarquee, Marquee };
    struct Hit {
        project::ClipId clip = project::ClipId::Invalid;
        bool nearStart = false;
        bool nearEnd = false;
    };

    // Geometry
    [[nodiscard]] int headerWidth() const noexcept;
    [[nodiscard]] int rulerHeight() const noexcept;
    [[nodiscard]] QRect laneArea() const;
    [[nodiscard]] double xFromTicks(Ticks t) const;
    [[nodiscard]] Ticks ticksFromX(double x) const;
    [[nodiscard]] Ticks ticksPerPixel() const noexcept;
    [[nodiscard]] TickSpec chooseTicks() const;
    [[nodiscard]] QRect playheadRect(TimePoint t) const;
    [[nodiscard]] const Lane* laneAt(int y) const;
    [[nodiscard]] const Lane* laneFor(project::TrackId id) const;
    [[nodiscard]] int laneIndexAt(int y) const;
    [[nodiscard]] QRectF clipRect(const project::Clip& c) const;
    [[nodiscard]] Hit hitTest(const QPoint& pos) const;
    [[nodiscard]] Duration snapTolerance() const noexcept;
    enum class TrackFlag { Mute, Solo, Lock };   // Mute on a video track = hidden
    struct LaneButton { QRect rect; TrackFlag flag; };
    [[nodiscard]] std::vector<LaneButton> laneButtons(const Lane& lane) const;

    // Model sync
    void onDocumentChange(const project::Change& change);
    void rebuildLanes();
    void refreshDuration();
    void pruneSelection();
    void commit(const QString& text, project::SequenceEdit edit);
    void toggleTrackFlag(project::TrackId id, TrackFlag flag);

    // Painting
    void paintRuler(QPainter& p, const QRect& r, const TickSpec& ticks);
    void paintHeaders(QPainter& p);
    void paintTracks(QPainter& p, const QRect& area, const TickSpec& ticks);
    void paintClips(QPainter& p, const QRect& area);
    // Repaints only the rows/rects that can have changed.
    void updateClipRects(const std::vector<project::ClipId>& ids);
    [[nodiscard]] QRect lanesRect() const;
    void paintClip(QPainter& p, const project::Clip& c, const QRectF& r, bool selected, bool ghost, bool dimmed);
    void paintDropGhosts(QPainter& p);
    void paintMarquee(QPainter& p);
    void paintSnapGuide(QPainter& p);
    void paintPlayhead(QPainter& p);

    // Interaction
    void seekTo(int x);
    void setZoom(double pixelsPerSecond, int anchorX);
    void clampScroll();
    void updateCursor(const QPoint& pos);
    void selectClip(project::ClipId id, Qt::KeyboardModifiers mods);
    [[nodiscard]] bool isSelected(project::ClipId id) const;
    void updateMovePreview(const QPoint& pos);
    void updateMarquee(const QPoint& pos);
    void bladeClick(const QPoint& pos, project::ClipId clip);
    void selectAdded(const project::SequenceEdit& edit, const QString& text);
    void updateTrimPreview(const QPoint& pos);
    void finishGesture(bool commitEdit);
    [[nodiscard]] std::optional<TimePoint> snapped(TimePoint t, const std::vector<project::ClipId>& ignore) const;
    void autoScrollTowards(int x);

    project::Document& m_doc;
    int m_listenerToken = 0;
    WaveformService* m_waveforms = nullptr;
    ThumbnailProvider m_thumbnails;

    Rational m_fps = fps::f30;
    Duration m_sequenceDuration;           // end of the last clip
    Duration m_duration = Duration::fromSeconds(60);   // scrollable extent
    TimePoint m_playhead;
    double m_pixelsPerSecond = 40.0;
    Ticks m_scrollTicks = 0;
    bool m_snapping = true;
    int m_hoverX = -1;
    std::vector<Lane> m_lanes;
    std::vector<project::ClipId> m_selection;
    EditTool m_tool = EditTool::Select;
    std::vector<project::Clip> m_clipboard;   // copies; ids are reassigned on paste

    // Gesture state
    Gesture m_gesture = Gesture::None;
    QPoint m_pressPos;
    Qt::KeyboardModifiers m_pressMods;
    project::ClipId m_pressClip = project::ClipId::Invalid;
    bool m_pressWasSelected = false;
    std::optional<project::SequenceEdit> m_previewEdit;   // move/trim result shown live
    std::vector<project::ClipId> m_previewIds;            // clips being moved/trimmed (drawn as ghosts)
    Duration m_previewDelta;
    int m_previewTrackDelta = 0;
    std::optional<TimePoint> m_snapGuide;
    QString m_previewText;
    std::vector<project::Clip> m_previewClipsCache;
    QRect m_marquee;                                  // current rubber band (widget coords)
    std::vector<project::ClipId> m_marqueeBase;       // selection when the band started (Ctrl/Shift add to it)

    // External drag (media bin / Explorer)
    struct DropGhost { QRectF rect; project::ClipKind kind; QString name; };
    std::vector<DropGhost> m_dropGhosts;
    std::vector<project::Clip> m_dropClips;   // pre-built clips with ids, repositioned on drag move
    bool m_dropIsFiles = false;
    TimePoint m_dropTime;
};

}  // namespace lf::ui
