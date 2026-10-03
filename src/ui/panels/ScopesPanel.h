#pragma once

#include "lumaforge/media/Image.h"

#include <QImage>
#include <QWidget>

class QComboBox;

namespace lf::ui {

class PlaybackController;

// Pure scope analysis of a BGRA frame (testable without a GPU).
namespace scopes {
enum class Mode { Waveform, Parade, Vectorscope, Histogram };
// Renders the scope for `frame` into an image of the given size (dark
// background, additive traces, no graticule).
[[nodiscard]] QImage render(Mode mode, const media::Image& frame, int width, int height);
// BT.709 luma 0..255 of a BGRA pixel.
[[nodiscard]] int luma(const std::uint8_t* bgra) noexcept;
}  // namespace scopes

// Color workspace scopes for the composited preview frame. Frames are
// tapped from the playback engine only while this panel is visible.
class ScopesPanel : public QWidget {
    Q_OBJECT
public:
    ScopesPanel(PlaybackController& playback, QWidget* parent = nullptr);
    ~ScopesPanel() override;

public slots:
    void setFrame(const lf::media::Image& frame);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateTap(bool on);
    QRect scopeRect() const;

    PlaybackController& m_playback;
    QComboBox* m_mode;
    media::Image m_frame;
    QImage m_scope;
};

}  // namespace lf::ui
