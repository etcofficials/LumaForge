#include "ui/panels/ScopesPanel.h"

#include "lumaforge/playback/PlaybackEngine.h"
#include "ui/Theme.h"
#include "ui/playback/PlaybackController.h"

#include <QComboBox>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lf::ui {

namespace scopes {

int luma(const std::uint8_t* p) noexcept { return (54 * p[2] + 183 * p[1] + 19 * p[0]) >> 8; }   // 0.2126 R + 0.7152 G + 0.0722 B

namespace {
// Accumulates hits into a float buffer, then maps counts to brightness on a log curve.
struct Accumulator {
    int w, h;
    std::vector<float> r, g, b;
    Accumulator(int width, int height) : w(width), h(height), r(static_cast<std::size_t>(width) * height), g(r.size()), b(r.size()) {}
    void add(int x, int y, float cr, float cg, float cb) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const std::size_t i = static_cast<std::size_t>(y) * w + x;
        r[i] += cr; g[i] += cg; b[i] += cb;
    }
    QImage image(float gain) const {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(QColor(12, 13, 15));
        auto map = [gain](float v) { return std::min(255, static_cast<int>(255.0f * std::log1p(v * gain) / std::log1p(gain * 4.0f))); };
        for (int y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                if (r[i] + g[i] + b[i] <= 0.0f) continue;
                line[x] = qRgb(std::max(12, map(r[i])), std::max(13, map(g[i])), std::max(15, map(b[i])));
            }
        }
        return img;
    }
};
}  // namespace

QImage render(Mode mode, const media::Image& f, int width, int height) {
    if (f.isNull() || width < 8 || height < 8) {
        QImage empty(std::max(1, width), std::max(1, height), QImage::Format_RGB32);
        empty.fill(QColor(12, 13, 15));
        return empty;
    }
    Accumulator acc(width, height);
    const float density = static_cast<float>(width * height) / static_cast<float>(f.width * f.height);
    auto px = [&](int x, int y) { return &f.bgra[(static_cast<std::size_t>(y) * f.width + x) * 4]; };
    switch (mode) {
        case Mode::Waveform:
            for (int y = 0; y < f.height; ++y)
                for (int x = 0; x < f.width; ++x) {
                    const int l = luma(px(x, y));
                    acc.add(x * width / f.width, (255 - l) * (height - 1) / 255, 0.35f, 1.0f, 0.45f);
                }
            break;
        case Mode::Parade: {
            const int third = width / 3;
            for (int y = 0; y < f.height; ++y)
                for (int x = 0; x < f.width; ++x) {
                    const auto* p = px(x, y);
                    const int cx = x * (third - 2) / f.width;
                    acc.add(cx, (255 - p[2]) * (height - 1) / 255, 1.0f, 0.15f, 0.15f);
                    acc.add(third + cx, (255 - p[1]) * (height - 1) / 255, 0.15f, 1.0f, 0.15f);
                    acc.add(2 * third + cx, (255 - p[0]) * (height - 1) / 255, 0.2f, 0.35f, 1.0f);
                }
            break;
        }
        case Mode::Vectorscope: {
            const int size = std::min(width, height);
            const int ox = (width - size) / 2, oy = (height - size) / 2;
            for (int y = 0; y < f.height; ++y)
                for (int x = 0; x < f.width; ++x) {
                    const auto* p = px(x, y);
                    const float R = p[2] / 255.0f, G = p[1] / 255.0f, B = p[0] / 255.0f;
                    const float Y = 0.2126f * R + 0.7152f * G + 0.0722f * B;
                    const float cb = (B - Y) / 1.8556f, cr = (R - Y) / 1.5748f;   // -0.5..0.5
                    const int vx = ox + static_cast<int>((cb + 0.5f) * (size - 1));
                    const int vy = oy + static_cast<int>((0.5f - cr) * (size - 1));
                    acc.add(vx, vy, 0.4f + R * 0.6f, 0.4f + G * 0.6f, 0.4f + B * 0.6f);
                }
            break;
        }
        case Mode::Histogram: {
            std::vector<int> hr(256), hg(256), hb(256), hl(256);
            for (int y = 0; y < f.height; ++y)
                for (int x = 0; x < f.width; ++x) {
                    const auto* p = px(x, y);
                    ++hr[p[2]]; ++hg[p[1]]; ++hb[p[0]]; ++hl[luma(p)];
                }
            const int peak = std::max({*std::max_element(hr.begin() + 1, hr.end() - 1), *std::max_element(hg.begin() + 1, hg.end() - 1),
                                       *std::max_element(hb.begin() + 1, hb.end() - 1), 1});
            QImage img(width, height, QImage::Format_RGB32);
            img.fill(QColor(12, 13, 15));
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setCompositionMode(QPainter::CompositionMode_Plus);
            auto curve = [&](const std::vector<int>& h, QColor c) {
                QPolygonF poly;
                poly << QPointF(0, height);
                for (int i = 0; i < 256; ++i) poly << QPointF(i * (width - 1) / 255.0, height - 1 - std::min(1.0, static_cast<double>(h[i]) / peak) * (height - 2));
                poly << QPointF(width - 1, height);
                c.setAlpha(110);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPolygon(poly);
            };
            curve(hr, QColor(220, 60, 60));
            curve(hg, QColor(60, 200, 80));
            curve(hb, QColor(70, 110, 230));
            return img;
        }
    }
    return acc.image(std::max(0.05f, 2.5f * density));
}

}  // namespace scopes

ScopesPanel::ScopesPanel(PlaybackController& playback, QWidget* parent) : QWidget(parent), m_playback(playback) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumSize(240, 200);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("scopesMode"));
    m_mode->addItem(tr("Waveform (luma)"));
    m_mode->addItem(tr("RGB Parade"));
    m_mode->addItem(tr("Vectorscope"));
    m_mode->addItem(tr("Histogram"));
    m_mode->setCurrentIndex(std::clamp(QSettings().value(QStringLiteral("scopes/mode"), 0).toInt(), 0, 3));
    layout->addWidget(m_mode);
    layout->addStretch(1);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        QSettings().setValue(QStringLiteral("scopes/mode"), i);
        m_scope = QImage();
        update();
    });
}

ScopesPanel::~ScopesPanel() { updateTap(false); }

void ScopesPanel::updateTap(bool on) {
    auto* engine = m_playback.engine();
    if (!engine) return;
    if (!on) { engine->setFrameTap({}); return; }
    QPointer<ScopesPanel> self(this);
    engine->setFrameTap([self](const media::Image& img) {
        QMetaObject::invokeMethod(self.data(), [self, img] { if (self) self->setFrame(img); }, Qt::QueuedConnection);
    }, 320);
}

void ScopesPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    updateTap(true);
}

void ScopesPanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    updateTap(false);
}

void ScopesPanel::setFrame(const media::Image& frame) {
    m_frame = frame;
    m_scope = QImage();
    update();
}

QRect ScopesPanel::scopeRect() const {
    const int top = m_mode->geometry().bottom() + 8;
    return QRect(8 + 26, top, width() - 16 - 26, height() - top - 8);
}

void ScopesPanel::paintEvent(QPaintEvent* e) {
    QWidget::paintEvent(e);
    QPainter p(this);
    const QRect r = scopeRect();
    if (r.width() < 16 || r.height() < 16) return;
    const auto mode = static_cast<scopes::Mode>(m_mode->currentIndex());
    if (m_scope.size() != r.size()) m_scope = scopes::render(mode, m_frame, r.width(), r.height());
    p.drawImage(r.topLeft(), m_scope);
    // Graticule
    const auto& pal = Theme::palette();
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() - 1.5);
    p.setFont(f);
    QColor grid = pal.textMuted;
    grid.setAlpha(90);
    if (mode == scopes::Mode::Vectorscope) {
        const int size = std::min(r.width(), r.height());
        const QRectF circle(r.left() + (r.width() - size) / 2.0, r.top() + (r.height() - size) / 2.0, size, size);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(grid, 1));
        p.drawEllipse(circle);
        p.drawEllipse(circle.adjusted(size * 0.25, size * 0.25, -size * 0.25, -size * 0.25));
        p.drawLine(QPointF(circle.center().x(), circle.top()), QPointF(circle.center().x(), circle.bottom()));
        p.drawLine(QPointF(circle.left(), circle.center().y()), QPointF(circle.right(), circle.center().y()));
        // Skin-tone line (about 123 degrees in Cb/Cr space).
        p.setPen(QPen(QColor(230, 170, 120, 160), 1, Qt::DashLine));
        const double a = 123.0 * 3.14159265 / 180.0;
        p.drawLine(circle.center(), circle.center() + QPointF(std::cos(a) * size / 2, -std::sin(a) * size / 2));
        // Primary / secondary targets at 75% saturation.
        const std::pair<const char*, std::array<float, 3>> targets[] = {{"R", {0.75f, 0, 0}}, {"G", {0, 0.75f, 0}}, {"B", {0, 0, 0.75f}},
                                                                        {"Cy", {0, 0.75f, 0.75f}}, {"Mg", {0.75f, 0, 0.75f}}, {"Yl", {0.75f, 0.75f, 0}}};
        for (const auto& [name, rgb] : targets) {
            const float Y = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
            const float cb = (rgb[2] - Y) / 1.8556f, cr = (rgb[0] - Y) / 1.5748f;
            const QPointF pt(circle.left() + (cb + 0.5f) * size, circle.top() + (0.5f - cr) * size);
            p.setPen(QPen(pal.textSecondary, 1));
            p.drawRect(QRectF(pt.x() - 4, pt.y() - 4, 8, 8));
            p.drawText(pt + QPointF(6, -4), QString::fromLatin1(name));
        }
        return;
    }
    if (mode == scopes::Mode::Histogram) return;
    // Waveform / parade: 0, 25, 50, 75, 100 % lines with labels on the left.
    for (int pct : {0, 25, 50, 75, 100}) {
        const int y = r.bottom() - (r.height() - 1) * pct / 100;
        p.setPen(QPen(grid, 1, pct == 0 || pct == 100 ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(r.left(), y, r.right(), y);
        p.setPen(pal.textMuted);
        p.drawText(QRect(r.left() - 28, y - 7, 24, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(pct));
    }
    if (mode == scopes::Mode::Parade) {
        p.setPen(QPen(grid, 1));
        for (int i = 1; i < 3; ++i) p.drawLine(r.left() + r.width() * i / 3, r.top(), r.left() + r.width() * i / 3, r.bottom());
    }
}

}  // namespace lf::ui
