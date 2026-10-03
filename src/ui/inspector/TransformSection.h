#pragma once

#include "ui/inspector/InspectorSection.h"

class QCheckBox;
class QToolButton;

namespace lf::ui {

class AnimatedParam;
class ParamSlider;

// Position / scale / rotation / opacity / flips for visual clips.
class TransformSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit TransformSection(InspectorContext& ctx, QWidget* parent = nullptr);

    // Adjustment clips always cover the frame: only their mix (opacity) applies, in their own section.
    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return project::isVisualClip(kind) && kind != project::ClipKind::Adjustment; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Transform; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ParamSlider* m_posX;
    ParamSlider* m_posY;
    ParamSlider* m_scale;      // uniform
    ParamSlider* m_scaleX;
    ParamSlider* m_scaleY;
    ParamSlider* m_rotation;
    ParamSlider* m_opacity;
    QCheckBox* m_uniform;
    QToolButton* m_flipH;
    QToolButton* m_flipV;
    std::vector<AnimatedParam*> m_params;

    enum class Fit { Fit, Fill, Original };
    enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight, Center };
    void applyFit(Fit fit);
    void applyPip(Corner corner);
};

// Crop from each edge, as a percentage of the source.
class CropSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit CropSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return project::isVisualClip(kind) && kind != project::ClipKind::Text; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Video; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ParamSlider* m_left;
    ParamSlider* m_right;
    ParamSlider* m_top;
    ParamSlider* m_bottom;
    ParamSlider* m_feather;
    std::vector<AnimatedParam*> m_params;
};

}  // namespace lf::ui
