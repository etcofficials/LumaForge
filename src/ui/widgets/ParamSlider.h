#pragma once

#include <QWidget>

class QSlider;
class QDoubleSpinBox;
class QToolButton;

namespace lf::ui {

// Slider + numeric field + reset for one numeric parameter. Emits
// valueEdited() for every user change (live preview) and gestureFinished()
// when a drag or a typed edit ends, so the owner can close the undo merge.
class ParamSlider : public QWidget {
    Q_OBJECT
public:
    ParamSlider(double min, double max, double defaultValue, int decimals, QWidget* parent = nullptr);

    [[nodiscard]] double value() const noexcept { return m_value; }
    [[nodiscard]] double defaultValue() const noexcept { return m_default; }
    void setSuffix(const QString& suffix);
    void setSingleStep(double step);
    // Range of the slider can be narrower than the spin box (e.g. scale 0..4 vs 0..100).
    void setSliderRange(double min, double max);

public slots:
    void setValue(double v);   // programmatic: no signals
    void reset();

signals:
    void valueEdited(double value);   // user change (live)
    void gestureFinished();           // end of drag / typed edit

private:
    void syncSlider();
    void syncSpin();
    [[nodiscard]] int toSlider(double v) const;
    [[nodiscard]] double fromSlider(int s) const;

    QSlider* m_slider;
    QDoubleSpinBox* m_spin;
    QToolButton* m_reset;
    double m_value;
    double m_default;
    double m_sliderMin, m_sliderMax;
    bool m_updating = false;
};

}  // namespace lf::ui
