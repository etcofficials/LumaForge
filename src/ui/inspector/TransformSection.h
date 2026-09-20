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
};

// Crop from each edge, as a percentage of the source.
class CropSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit CropSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Video || kind == project::ClipKind::Image; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ParamSlider* m_left;
    ParamSlider* m_right;
    ParamSlider* m_top;
    ParamSlider* m_bottom;
};

}  // namespace lf::ui
