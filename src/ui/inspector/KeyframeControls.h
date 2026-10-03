#pragma once

#include "lumaforge/project/Sequence.h"
#include "ui/inspector/InspectorSection.h"

#include <QObject>
#include <QToolButton>

#include <functional>
#include <vector>

namespace lf::ui {

class ParamSlider;

// Diamond beside an animatable parameter: hollow grey = static, hollow
// accent = animated (no key at the playhead), filled accent = key here.
class KeyframeButton : public QToolButton {
    Q_OBJECT
public:
    enum class State { Static, Animated, OnKey };
    explicit KeyframeButton(QWidget* parent = nullptr);
    void setState(State s);
    [[nodiscard]] State state() const noexcept { return m_state; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    State m_state = State::Static;
};

// Connects one ParamSlider to one or more animatable properties (several for
// uniform scale). Slider edits set the static value, or a key at the playhead
// once the property is animated; the diamond adds/removes the key at the
// playhead; its menu sets the interpolation, jumps between keys and clears.
class AnimatedParam : public QObject {
    Q_OBJECT
public:
    using ToUi = std::function<double(float)>;     // model -> slider
    using FromUi = std::function<float(double)>;   // slider -> model
    AnimatedParam(ParamSlider* slider, InspectorContext& ctx, const bool& loading, std::vector<project::AnimProp> props,
                  QString undoText, int propertyId, ToUi toUi, FromUi fromUi);

    // Model value for every property at the playhead, and the diamond state.
    void load(const project::Clip& primary);
    [[nodiscard]] KeyframeButton* button() const noexcept { return m_button; }
    // Extra fix-ups after a static edit (e.g. crop edges that may not overlap).
    void setConstraint(std::function<void(project::Clip&)> fn) { m_constraint = std::move(fn); }

    // Linear scale helpers: ui = model * factor.
    static ToUi scaled(double factor) { return [factor](float v) { return v * factor; }; }
    static FromUi unscaled(double factor) { return [factor](double v) { return static_cast<float>(v / factor); }; }

private:
    void toggleKey();
    void showMenu(const QPoint& globalPos);
    [[nodiscard]] const project::Clip* primary() const;

    ParamSlider* m_slider;
    KeyframeButton* m_button;
    InspectorContext& m_ctx;
    const bool& m_loading;
    std::vector<project::AnimProp> m_props;
    QString m_text;
    int m_propertyId;
    ToUi m_toUi;
    FromUi m_fromUi;
    std::function<void(project::Clip&)> m_constraint;
    project::ClipId m_primary = project::ClipId::Invalid;
};

}  // namespace lf::ui
