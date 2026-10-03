#include "ui/inspector/GeneratedSections.h"

#include "ui/widgets/ColorButton.h"
#include "ui/widgets/ParamSlider.h"

#include <QFormLayout>
#include <QLabel>

#include <cmath>

namespace lf::ui {

using namespace lf::project;

SolidSection::SolidSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Solid"), QStringLiteral("solid"), ctx, parent) {
    m_color = new ColorButton(this);
    m_color->setObjectName(QStringLiteral("solidColor"));
    form()->addRow(tr("Colour"), m_color);
    connect(m_color, &ColorButton::colorEdited, this, [this](const ColorRgba& c) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Solid Colour"), prop::SolidColor, [c](Clip& clip) { clip.solidColor = ColorRgba{c.r, c.g, c.b, 1.0f}; });
        m_ctx.endGesture();
    });
}

void SolidSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_color->setColor(c.solidColor);
}

AdjustmentSection::AdjustmentSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Adjustment"), QStringLiteral("adjustment"), ctx, parent) {
    auto* note = new QLabel(tr("Applies its Color, Effects, Crop and Mask settings to everything below it on lower tracks."), this);
    note->setWordWrap(true);
    note->setProperty("muted", true);
    form()->addRow(note);
    m_mix = new ParamSlider(0, 100, 100, 0, this);
    m_mix->setObjectName(QStringLiteral("adjustmentMix"));
    m_mix->setSuffix(QStringLiteral(" %"));
    m_mix->setToolTip(tr("How much of the adjusted picture replaces the original"));
    form()->addRow(tr("Mix"), m_mix);
    connect(m_mix, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float o = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Adjustment Mix"), prop::Opacity, [o](Clip& c) { c.transform.opacity = o; });
    });
    connect(m_mix, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
}

void AdjustmentSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_mix->setValue(std::round(c.transform.opacity * 100.0));
}

}  // namespace lf::ui
