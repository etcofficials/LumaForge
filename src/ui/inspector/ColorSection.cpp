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
    // Copy / paste the whole correction between clips (kept for the session).
    auto* copy = addHeaderButton(tr("Copy"), tr("Copy this clip's colour correction"));
    m_paste = addHeaderButton(tr("Paste"), tr("Apply the copied colour correction to the selected clips"));
    m_paste->setEnabled(clipboard().has_value());
    connect(copy, &QToolButton::clicked, this, [this] {
        clipboard() = m_current;
        m_paste->setEnabled(true);
    });
    connect(m_paste, &QToolButton::clicked, this, [this] {
        if (!clipboard()) return;
        const ColorAdjust value = *clipboard();
        m_ctx.editSelection(tr("Paste Color"), prop::ResetGroup, [value](Clip& c) { c.color = value; });
        m_ctx.endGesture();
    });
    auto* reset = addHeaderButton(tr("Reset"), tr("Reset all colour adjustments"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Color"), prop::ResetGroup, [](Clip& c) { c.color = ColorAdjust{}; });
        m_ctx.endGesture();
    });
}

std::optional<ColorAdjust>& ColorSection::clipboard() {
    static std::optional<ColorAdjust> value;
    return value;
}

void ColorSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_current = c.color;
    m_paste->setEnabled(clipboard().has_value());
    for (const auto& b : m_bindings) b.slider->setValue(c.color.*(b.member));
    setSubtitle(c.color.isIdentity() ? QString() : tr("adjusted"));
}

}  // namespace lf::ui
