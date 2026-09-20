#include "ui/inspector/ColorSection.h"

#include "ui/widgets/ParamSlider.h"

#include <QFormLayout>
#include <QToolButton>

namespace lf::ui {

using namespace lf::project;

ColorSection::ColorSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Color"), QStringLiteral("color"), ctx, parent) {
    auto* f = form();
    struct Def {
        const char* label;
        float ColorAdjust::*member;
        double min, max, def;
        int decimals;
        int propertyId;
        const char* suffix;
    };
    const Def defs[] = {
        {"Exposure", &ColorAdjust::exposure, -3, 3, 0, 2, prop::Exposure, " EV"},
        {"Brightness", &ColorAdjust::brightness, -1, 1, 0, 2, prop::Brightness, ""},
        {"Contrast", &ColorAdjust::contrast, 0, 2, 1, 2, prop::Contrast, ""},
        {"Saturation", &ColorAdjust::saturation, 0, 2, 1, 2, prop::Saturation, ""},
        {"Temperature", &ColorAdjust::temperature, -1, 1, 0, 2, prop::Temperature, ""},
        {"Tint", &ColorAdjust::tint, -1, 1, 0, 2, prop::Tint, ""},
        {"Highlights", &ColorAdjust::highlights, -1, 1, 0, 2, prop::Highlights, ""},
        {"Shadows", &ColorAdjust::shadows, -1, 1, 0, 2, prop::Shadows, ""},
        {"Vibrance", &ColorAdjust::vibrance, -1, 1, 0, 2, prop::Vibrance, ""},
        {"Gamma", &ColorAdjust::gamma, 0.2, 3, 1, 2, prop::Gamma, ""},
        {"Hue", &ColorAdjust::hue, -180, 180, 0, 0, prop::Hue, "°"},
    };
    for (const Def& d : defs) {
        auto* s = new ParamSlider(d.min, d.max, d.def, d.decimals, this);
        if (*d.suffix) s->setSuffix(QString::fromUtf8(d.suffix));
        f->addRow(tr(d.label), s);
        float ColorAdjust::* const member = d.member;
        const int propertyId = d.propertyId;
        const QString text = tr(d.label);
        connect(s, &ParamSlider::valueEdited, this, [this, member, propertyId, text](double v) {
            if (m_loading) return;
            m_ctx.editSelection(text, propertyId, [v, member](Clip& c) { c.color.*member = static_cast<float>(v); });
        });
        connect(s, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
        m_bindings.push_back({s, d.member});
    }
    auto* reset = addHeaderButton(tr("Reset"), tr("Reset all colour adjustments"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Color"), prop::ResetGroup, [](Clip& c) { c.color = ColorAdjust{}; });
        m_ctx.endGesture();
    });
}

void ColorSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    for (const auto& b : m_bindings) b.slider->setValue(c.color.*(b.member));
    setSubtitle(c.color.isIdentity() ? QString() : tr("adjusted"));
}

}  // namespace lf::ui
