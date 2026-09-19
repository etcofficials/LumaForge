#include "ui/preview/PreviewWidget.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/playback/PlaybackEngine.h"
#include "lumaforge/rendering/FrameLayout.h"
#include "lumaforge/rendering/PreviewRenderer.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QPainter>
#include <QResizeEvent>

namespace lf::ui {

namespace {
constexpr const char* kLog = "preview";
}

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::ClickFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

PreviewWidget::~PreviewWidget() { releaseSurface(); }

void PreviewWidget::setEngine(playback::PlaybackEngine* engine, std::shared_ptr<hw::GpuDevice> device) {
    releaseSurface();
    m_engine = engine;
    m_device = std::move(device);
    m_surfaceFailed = false;
    if (m_engine) {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        (void)winId();
        if (isVisible()) ensureSurface();
    } else {
        setAttribute(Qt::WA_PaintOnScreen, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
    }
    emit backendChanged();
    update();
}

QString PreviewWidget::backendName() const {
    if (m_engine && m_device) return QString::fromStdString(m_device->description());
    return QStringLiteral("Software (no Direct3D)");
}

QPaintEngine* PreviewWidget::paintEngine() const { return m_engine ? nullptr : QWidget::paintEngine(); }

void PreviewWidget::ensureSurface() {
    if (!m_engine || m_surfaceAttached || m_surfaceFailed) return;
    const qreal dpr = devicePixelRatioF();
    auto surface = render::PreviewRenderer::create(m_device, reinterpret_cast<void*>(winId()), static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
    if (!surface) {
        LF_LOG_ERROR(kLog, "preview surface unavailable: {}", surface.error().fullText());
        m_surfaceFailed = true;
        m_engine = nullptr;
        setAttribute(Qt::WA_PaintOnScreen, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
        emit backendChanged();
        update();
        return;
    }
    m_engine->attachSurface(std::move(surface.value()));
    m_surfaceAttached = true;
    LF_LOG_INFO(kLog, "preview surface attached: {}", backendName().toStdString());
}

void PreviewWidget::releaseSurface() {
    if (m_engine && m_surfaceAttached) m_engine->detachSurface();
    m_surfaceAttached = false;
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    if (m_engine) {
        ensureSurface();
        if (m_surfaceAttached) {
            m_engine->requestRedraw();
            return;
        }
    }
    QPainter p(this);
    const auto& pal = Theme::palette();
    p.fillRect(rect(), pal.previewSurround);
    const auto r = render::fitFrame(width(), height(), 16, 9);
    p.fillRect(QRect(r.x, r.y, r.width, r.height), Qt::black);
    p.setPen(pal.textMuted);
    p.drawText(rect(), Qt::AlignCenter, tr("Preview unavailable: no Direct3D 11 device"));
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_engine && m_surfaceAttached) {
        const qreal dpr = devicePixelRatioF();
        m_engine->resizeSurface(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
    }
}

void PreviewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureSurface();
}

bool PreviewWidget::event(QEvent* event) {
    if (event->type() == QEvent::DevicePixelRatioChange && m_engine && m_surfaceAttached) {
        const qreal dpr = devicePixelRatioF();
        m_engine->resizeSurface(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
    }
    return QWidget::event(event);
}

}  // namespace lf::ui
