#pragma once

#include "ui/inspector/InspectorSection.h"

class QLabel;

namespace lf::ui {

class ColorButton;
class ParamSlider;

// Colour of a Solid clip.
class SolidSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit SolidSection(InspectorContext& ctx, QWidget* parent = nullptr);
    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Solid; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Video; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ColorButton* m_color;
};

// How strongly an Adjustment clip applies (its opacity), with a short
// explanation; its colour, effects, crop and mask live in their own sections.
class AdjustmentSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit AdjustmentSection(InspectorContext& ctx, QWidget* parent = nullptr);
    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Adjustment; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Video; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ParamSlider* m_mix;
};

}  // namespace lf::ui
