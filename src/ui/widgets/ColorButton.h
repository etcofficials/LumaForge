#pragma once

#include "lumaforge/core/VisualParams.h"

#include <QToolButton>

namespace lf::ui {

// A colour swatch that opens a colour dialog. Alpha is left to separate
// opacity controls so the dialog stays simple.
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);

    [[nodiscard]] ColorRgba color() const noexcept { return m_color; }
    void setColor(const ColorRgba& c);   // programmatic; no signal

signals:
    void colorEdited(const lf::ColorRgba& color);

private:
    void updateSwatch();
    ColorRgba m_color;
};

}  // namespace lf::ui
