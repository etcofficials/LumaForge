#pragma once

#include "ui/inspector/InspectorSection.h"

class QComboBox;
class QLabel;

namespace lf::ui {

class ParamSlider;

// The transitions at the selected clip's start and end: type (or none) and
// duration. A transition across a cut is shared with the neighbouring clip.
class TransitionSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit TransitionSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return project::isVisualClip(kind); }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Transition; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    struct Edge {
        QComboBox* type = nullptr;
        ParamSlider* duration = nullptr;
        QLabel* note = nullptr;
    };
    void buildEdge(Edge& edge, bool atStart, const QString& title);
    void apply(bool atStart);

    Edge m_start;
    Edge m_end;
};

}  // namespace lf::ui
