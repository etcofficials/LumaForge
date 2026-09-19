#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace lf::ui {

// Loads monochrome SVG icons from resources and recolours them with theme
// colours for each QIcon state, at 1x and 2x device pixel ratios. Icons are
// cached by (name, colour) so repeated lookups are free.
class Icons {
public:
    // Standard UI icon: normal = secondary text, active/selected = primary, disabled = muted.
    static QIcon icon(const QString& name);
    // Icon in an explicit colour (e.g. the play button in accent colour).
    static QIcon icon(const QString& name, const QColor& normal, const QColor& disabled = {});
    // Renders one state to a pixmap (for painting inside custom widgets).
    static QPixmap pixmap(const QString& name, int size, const QColor& color, qreal devicePixelRatio = 1.0);

private:
    static QByteArray svgData(const QString& name);
};

}  // namespace lf::ui
