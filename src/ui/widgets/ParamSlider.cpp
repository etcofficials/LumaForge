#include "ui/widgets/ParamSlider.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSlider>
#include <QToolButton>

#include <cmath>

namespace lf::ui {

namespace {
constexpr int kSliderSteps = 1000;
}

ParamSlider::ParamSlider(double min, double max, double defaultValue, int decimals, QWidget* parent)
    : QWidget(parent), m_value(defaultValue), m_default(defaultValue), m_sliderMin(min), m_sliderMax(max) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, kSliderSteps);
    m_slider->setFocusPolicy(Qt::NoFocus);   // arrow keys stay with the transport
    m_slider->setMinimumWidth(50);
    row->addWidget(m_slider, 1);

    m_spin = new QDoubleSpinBox(this);
    m_spin->setDecimals(decimals);
    m_spin->setRange(min, max);
    m_spin->setValue(defaultValue);
    m_spin->setKeyboardTracking(false);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(60);
    row->addWidget(m_spin);

    m_reset = new QToolButton(this);
    m_reset->setAutoRaise(true);
    m_reset->setFixedSize(18, 18);
    m_reset->setIconSize(QSize(12, 12));
    m_reset->setIcon(Icons::icon(QStringLiteral("undo")));
    m_reset->setToolTip(tr("Reset"));
    m_reset->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_reset);

    syncSlider();
    connect(m_slider, &QSlider::valueChanged, this, [this](int s) {
        if (m_updating) return;
        m_value = fromSlider(s);
        syncSpin();
        emit valueEdited(m_value);
        if (!m_slider->isSliderDown()) emit gestureFinished();   // click on the groove / wheel
    });
    connect(m_slider, &QSlider::sliderReleased, this, [this] { emit gestureFinished(); });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating) return;
        m_value = v;
        syncSlider();
        emit valueEdited(m_value);
        emit gestureFinished();
    });
    connect(m_reset, &QToolButton::clicked, this, [this] { reset(); });
}

void ParamSlider::setSuffix(const QString& suffix) { m_spin->setSuffix(suffix); }
void ParamSlider::setSingleStep(double step) { m_spin->setSingleStep(step); }

void ParamSlider::setSliderRange(double min, double max) {
    m_sliderMin = min;
    m_sliderMax = max;
    syncSlider();
}

int ParamSlider::toSlider(double v) const {
    const double t = (v - m_sliderMin) / (m_sliderMax - m_sliderMin);
    return static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * kSliderSteps));
}

double ParamSlider::fromSlider(int s) const {
    const double v = m_sliderMin + (m_sliderMax - m_sliderMin) * (static_cast<double>(s) / kSliderSteps);
    // Round to the spin box precision so the two never disagree.
    const double scale = std::pow(10.0, m_spin->decimals());
    return std::round(v * scale) / scale;
}

void ParamSlider::syncSlider() {
    m_updating = true;
    m_slider->setValue(toSlider(m_value));
    m_reset->setEnabled(std::fabs(m_value - m_default) > 1e-9);
    m_updating = false;
}

void ParamSlider::syncSpin() {
    m_updating = true;
    m_spin->setValue(m_value);
    m_reset->setEnabled(std::fabs(m_value - m_default) > 1e-9);
    m_updating = false;
}

void ParamSlider::setValue(double v) {
    m_value = v;
    syncSlider();
    syncSpin();
}

void ParamSlider::reset() {
    if (std::fabs(m_value - m_default) <= 1e-9) return;
    setValue(m_default);
    emit valueEdited(m_value);
    emit gestureFinished();
}

}  // namespace lf::ui
