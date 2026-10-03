#pragma once

#include "ui/inspector/InspectorSection.h"

#include <optional>
#include <vector>

class QToolButton;

namespace lf::ui {

class ParamSlider;

// Colour correction sliders bound one-to-one to ColorAdjust (GPU shader).
class ColorSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit ColorSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override {
        return kind == project::ClipKind::Video || kind == project::ClipKind::Image || kind == project::ClipKind::Adjustment;
    }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Color; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    struct Binding {
        ParamSlider* slider;
        float ColorAdjust::*member;
    };
    std::vector<Binding> m_bindings;
    QToolButton* m_paste = nullptr;
    ColorAdjust m_current;
    static std::optional<ColorAdjust>& clipboard();
};

}  // namespace lf::ui
