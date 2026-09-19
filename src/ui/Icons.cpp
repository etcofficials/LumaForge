#include "ui/Icons.h"

#include "lumaforge/core/Log.h"
#include "ui/Theme.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace lf::ui {

namespace {
constexpr int kBaseSize = 16;
constexpr std::initializer_list<qreal> kRatios = {1.0, 1.5, 2.0};

QHash<QString, QByteArray>& svgCache() {
    static QHash<QString, QByteArray> cache;
    return cache;
}
QHash<QString, QIcon>& iconCache() {
    static QHash<QString, QIcon> cache;
    return cache;
}
}  // namespace

QByteArray Icons::svgData(const QString& name) {
    auto& cache = svgCache();
    if (auto it = cache.find(name); it != cache.end()) return *it;
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    QByteArray data;
    if (f.open(QIODevice::ReadOnly)) data = f.readAll();
    else LF_LOG_WARN("ui", "missing icon resource: {}", name.toStdString());
    cache.insert(name, data);
    return data;
}

QPixmap Icons::pixmap(const QString& name, int size, const QColor& color, qreal dpr) {
    QByteArray data = svgData(name);
    data.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QSvgRenderer renderer(data);
    const int px = static_cast<int>(size * dpr);
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, px, px));
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QIcon Icons::icon(const QString& name) {
    const auto& pal = Theme::palette();
    return icon(name, pal.textSecondary, pal.textDisabled);
}

QIcon Icons::icon(const QString& name, const QColor& normal, const QColor& disabledIn) {
    const auto& pal = Theme::palette();
    const QColor disabled = disabledIn.isValid() ? disabledIn : pal.textDisabled;
    const QString key = name + '|' + normal.name() + '|' + disabled.name();
    auto& cache = iconCache();
    if (auto it = cache.find(key); it != cache.end()) return *it;

    QIcon icon;
    for (qreal dpr : kRatios) {
        icon.addPixmap(pixmap(name, kBaseSize, normal, dpr), QIcon::Normal, QIcon::Off);
        icon.addPixmap(pixmap(name, kBaseSize, pal.textPrimary, dpr), QIcon::Active, QIcon::Off);
        icon.addPixmap(pixmap(name, kBaseSize, pal.textPrimary, dpr), QIcon::Selected, QIcon::Off);
        icon.addPixmap(pixmap(name, kBaseSize, disabled, dpr), QIcon::Disabled, QIcon::Off);
    }
    cache.insert(key, icon);
    return icon;
}

}  // namespace lf::ui
