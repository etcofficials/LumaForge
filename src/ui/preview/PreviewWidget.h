#pragma once

#include "lumaforge/core/Time.h"

#include <QWidget>

#include <memory>

namespace lf::hw { class GpuDevice; }
namespace lf::playback { class PlaybackEngine; }

namespace lf::ui {

// The video surface. A native child window whose pixels belong to the
// playback engine's render thread (Direct3D 11 swap chain). Qt never paints
// into it: paint/resize events are forwarded to the engine as requests.
//
// Without an engine (no D3D11 device at all, not even WARP) it falls back to
// a QPainter placeholder so the application stays usable.
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Hands presentation to the engine. Call once, before the widget is shown.
    void setEngine(playback::PlaybackEngine* engine, std::shared_ptr<hw::GpuDevice> device);

    [[nodiscard]] bool usingHardware() const noexcept { return m_engine != nullptr; }
    [[nodiscard]] QString backendName() const;

    QPaintEngine* paintEngine() const override;   // nullptr when the engine owns the pixels
    QSize sizeHint() const override { return {960, 540}; }

signals:
    void backendChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

private:
    void ensureSurface();
    void releaseSurface();

    playback::PlaybackEngine* m_engine = nullptr;
    std::shared_ptr<hw::GpuDevice> m_device;
    bool m_surfaceAttached = false;
    bool m_surfaceFailed = false;
};

}  // namespace lf::ui
