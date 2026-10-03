#include "ui/inspector/MaskSection.h"

#include "ui/widgets/ParamSlider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QToolButton>

namespace lf::ui {

using namespace lf::project;

MaskSection::MaskSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Mask"), QStringLiteral("mask"), ctx, parent) {
    auto* f = form();
    m_shape = new QComboBox(this);
    m_shape->setObjectName(QStringLiteral("maskShape"));
    m_shape->setFocusPolicy(Qt::NoFocus);
    m_shape->addItem(tr("None"), static_cast<int>(MaskShape::None));
    m_shape->addItem(tr("Rectangle"), static_cast<int>(MaskShape::Rectangle));
    m_shape->addItem(tr("Ellipse"), static_cast<int>(MaskShape::Ellipse));
    f->addRow(tr("Shape"), m_shape);
    auto percent = [this](double def) {
        auto* s = new ParamSlider(0, 100, def, 1, this);
        s->setSuffix(QStringLiteral(" %"));
        return s;
    };
    m_centerX = percent(50);
    m_centerY = percent(50);
    m_width = percent(50);
    m_height = percent(50);
    m_feather = percent(10);
    m_feather->setToolTip(tr("Soft edge, as a share of the shape's size"));
    m_invert = new QCheckBox(tr("Invert (show outside the shape)"), this);
    m_invert->setFocusPolicy(Qt::NoFocus);
    f->addRow(tr("Centre X"), m_centerX);
    f->addRow(tr("Centre Y"), m_centerY);
    f->addRow(tr("Width"), m_width);
    f->addRow(tr("Height"), m_height);
    f->addRow(tr("Feather"), m_feather);
    f->addRow(QString(), m_invert);

    connect(m_shape, &QComboBox::activated, this, [this] {
        if (m_loading) return;
        const auto shape = static_cast<MaskShape>(m_shape->currentData().toInt());
        m_ctx.editSelection(tr("Mask Shape"), prop::MaskShape, [shape](Clip& c) { c.mask.shape = shape; });
        m_ctx.endGesture();
        updateEnabled();
    });
    auto bind = [this](ParamSlider* s, int id, float Mask::*member) {
        connect(s, &ParamSlider::valueEdited, this, [this, id, member](double v) {
            if (m_loading) return;
            const float value = static_cast<float>(v / 100.0);
            m_ctx.editSelection(tr("Mask"), id, [value, member](Clip& c) {
                if (c.mask.shape == MaskShape::None) c.mask.shape = MaskShape::Ellipse;   // editing a parameter turns the mask on
                c.mask.*member = value;
            });
        });
        connect(s, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    };
    bind(m_centerX, prop::MaskCenterX, &Mask::centerX);
    bind(m_centerY, prop::MaskCenterY, &Mask::centerY);
    bind(m_width, prop::MaskWidth, &Mask::width);
    bind(m_height, prop::MaskHeight, &Mask::height);
    bind(m_feather, prop::MaskFeather, &Mask::feather);
    connect(m_invert, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Invert Mask"), prop::MaskInvert, [on](Clip& c) { c.mask.invert = on; });
        m_ctx.endGesture();
    });
    auto* reset = addHeaderButton(tr("Reset"), tr("Remove the mask"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Mask"), prop::ResetGroup, [](Clip& c) { c.mask = Mask{}; });
        m_ctx.endGesture();
    });
}

void MaskSection::updateEnabled() {
    const bool on = m_shape->currentData().toInt() != static_cast<int>(MaskShape::None);
    for (QWidget* w : {static_cast<QWidget*>(m_invert)}) w->setEnabled(on);
    setSubtitle(on ? m_shape->currentText().toLower() : QString());
}

void MaskSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_shape->setCurrentIndex(m_shape->findData(static_cast<int>(c.mask.shape)));
    m_centerX->setValue(c.mask.centerX * 100.0);
    m_centerY->setValue(c.mask.centerY * 100.0);
    m_width->setValue(c.mask.width * 100.0);
    m_height->setValue(c.mask.height * 100.0);
    m_feather->setValue(c.mask.feather * 100.0);
    m_invert->setChecked(c.mask.invert);
    updateEnabled();
}

}  // namespace lf::ui
