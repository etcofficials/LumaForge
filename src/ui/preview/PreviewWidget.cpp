#include "ui/preview/PreviewWidget.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWindow>

namespace lf::ui {

namespace {
constexpr const char* kLog = "preview";

render::Color toColor(const QColor& c) {
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), 1.0f};
}
}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    m_scene.surround = toColor(Theme::palette().previewSurround);
    setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::ClickFocus);
    // Software mode until a device arrives; nothing behind us needs painting.
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

PreviewWidget::~PreviewWidget() = default;

void PreviewWidget::setGpuDevice(std::shared_ptr<hw::GpuDevice> device) {
    m_device = std::move(device);
    m_renderer.reset();
    m_hardwareFailed = false;
    if (m_device) {
        // Native handle + no Qt backing store: D3D owns this window's pixels.
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        (void)winId();
        ensureRenderer();
    }
    emit backendChanged();
    update();
}

void PreviewWidget::setFrameSize(int width, int height) {
    if (width == m_scene.frameWidth && height == m_scene.frameHeight) return;
    m_scene.frameWidth = width;
    m_scene.frameHeight = height;
    if (m_renderer) renderNow();
    else update();
}

QString PreviewWidget::backendName() const {
    if (m_renderer && m_device) return QStringLiteral("Direct3D 11 (FL %1)").arg(QString::fromStdString(m_device->featureLevelString()));
    return QStringLiteral("Software");
}

QPaintEngine* PreviewWidget::paintEngine() const {
    return m_renderer ? nullptr : QWidget::paintEngine();
}

void PreviewWidget::ensureRenderer() {
    if (m_renderer || !m_device || m_hardwareFailed || !isVisible()) return;
    const qreal dpr = devicePixelRatioF();
    const int w = static_cast<int>(width() * dpr);
    const int h = static_cast<int>(height() * dpr);
    auto created = render::PreviewRenderer::create(m_device, reinterpret_cast<void*>(winId()), w, h);
    if (!created) {
        dropRenderer(QString::fromStdString(created.error().fullText()));
        return;
    }
    m_renderer = std::move(created.value());
    LF_LOG_INFO(kLog, "preview surface: {}", backendName().toStdString());
    emit backendChanged();
}

void PreviewWidget::dropRenderer(const QString& reason) {
    LF_LOG_WARN(kLog, "hardware preview unavailable, using software: {}", reason.toStdString());
    m_renderer.reset();
    m_hardwareFailed = true;
    setAttribute(Qt::WA_PaintOnScreen, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    emit backendChanged();
    update();
}

void PreviewWidget::renderNow() {
    if (!m_renderer) return;
    const qreal dpr = devicePixelRatioF();
    if (auto s = m_renderer->resize(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr)); !s) {
        dropRenderer(QString::fromStdString(s.error().fullText()));
        return;
    }
    if (auto s = m_renderer->render(m_scene); !s) {
        const bool lost = s.error().code == static_cast<int>(DXGI_ERROR_DEVICE_REMOVED) || s.error().code == static_cast<int>(DXGI_ERROR_DEVICE_RESET);
        if (lost) {
            // Device loss is recoverable: recreate the swap chain on the next paint.
            LF_LOG_WARN(kLog, "device lost during present: {}", s.error().detail);
            m_renderer.reset();
            update();
        } else {
            dropRenderer(QString::fromStdString(s.error().fullText()));
        }
    }
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    if (m_device && !m_hardwareFailed) {
        ensureRenderer();
        if (m_renderer) {
            renderNow();
            return;
        }
    }
    // Software fallback: identical layout via QPainter.
    QPainter p(this);
    const auto& pal = Theme::palette();
    p.fillRect(rect(), pal.previewSurround);
    const auto r = render::fitFrame(width(), height(), m_scene.frameWidth, m_scene.frameHeight);
    p.fillRect(QRect(r.x, r.y, r.width, r.height), Qt::black);
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_renderer) renderNow();
}

void PreviewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureRenderer();
}

bool PreviewWidget::event(QEvent* event) {
    // DPI change moves the widget between monitors: the swap chain must follow.
    if (event->type() == QEvent::DevicePixelRatioChange && m_renderer) renderNow();
    return QWidget::event(event);
}

}  // namespace lf::ui
