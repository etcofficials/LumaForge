#pragma once

#include "ui/inspector/InspectorSection.h"

#include <vector>

class QCheckBox;
class QToolButton;
class QVBoxLayout;
class QComboBox;

namespace lf::ui {

class ParamSlider;

// The clip's effect stack: one row per effect (enable, name, amount, move,
// remove) and an "Add effect" chooser. Rows are rebuilt when the stack's
// shape changes and merely refreshed when only amounts change.
class EffectsSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit EffectsSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return project::isVisualClip(kind); }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Effects; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    struct Row {
        QWidget* widget;
        QCheckBox* enabled;
        ParamSlider* amount;
        EffectType type;
    };
    void rebuildRows(const std::vector<Effect>& effects);
    void editEffects(const QString& text, int propertyId, const std::function<void(std::vector<Effect>&)>& fn);

    QVBoxLayout* m_rows;
    QComboBox* m_add;
    std::vector<Row> m_rowWidgets;
    std::vector<EffectType> m_shape;   // types in order, to know when to rebuild
};

}  // namespace lf::ui
