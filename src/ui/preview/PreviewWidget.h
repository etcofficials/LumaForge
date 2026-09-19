#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/rendering/PreviewRenderer.h"

#include <QWidget>

#include <memory>

namespace lf::hw { class GpuDevice; }

namespace lf::ui {

// The video surface. A native child window owned by Qt but painted by
// Direct3D 11 (PreviewRenderer). Qt never touches these pixels: the widget
// opts out of Qt's backing store so there is no compositing copy per frame.
//
// If no D3D11 device is available the widget falls back to QPainter and
// draws the same layout, so the application is fully usable without a GPU.
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Supplying a device switches to the D3D11 path; nullptr uses software.
    void setGpuDevice(std::shared_ptr<hw::GpuDevice> device);
    void setFrameSize(int width, int height);

    [[nodiscard]] bool usingHardware() const noexcept { return m_renderer != nullptr; }
    [[nodiscard]] QString backendName() const;

    QPaintEngine* paintEngine() const override;   // nullptr on the D3D path
    QSize sizeHint() const override { return {960, 540}; }

signals:
    void backendChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

private:
    void ensureRenderer();
    void renderNow();
    void dropRenderer(const QString& reason);

    std::shared_ptr<hw::GpuDevice> m_device;
    std::unique_ptr<render::PreviewRenderer> m_renderer;
    render::PreviewScene m_scene;
    bool m_hardwareFailed = false;
};

}  // namespace lf::ui
