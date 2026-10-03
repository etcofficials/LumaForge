#pragma once

#include "ui/inspector/InspectorSection.h"

class QCheckBox;
class QToolButton;

namespace lf::ui {

class ParamSlider;

// Position / scale / rotation / opacity / flips for visual clips.
class TransformSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit TransformSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return project::isVisualClip(kind); }
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

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Video || kind == project::ClipKind::Image; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Video; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ParamSlider* m_left;
    ParamSlider* m_right;
    ParamSlider* m_top;
    ParamSlider* m_bottom;
    ParamSlider* m_feather;
};

}  // namespace lf::ui
