#pragma once

#include "ui/inspector/InspectorSection.h"

class QCheckBox;
class QComboBox;

namespace lf::ui {

class ParamSlider;

// Rectangle / ellipse mask with feather and invert, in the clip's picture
// space (percent of the picture), so it moves with the clip.
class MaskSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit MaskSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return project::isVisualClip(kind); }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Video; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    void updateEnabled();

    QComboBox* m_shape;
    ParamSlider* m_centerX;
    ParamSlider* m_centerY;
    ParamSlider* m_width;
    ParamSlider* m_height;
    ParamSlider* m_feather;
    QCheckBox* m_invert;
};

}  // namespace lf::ui
