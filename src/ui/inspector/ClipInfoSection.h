#pragma once

#include "ui/inspector/InspectorSection.h"

class QLabel;
class QLineEdit;

namespace lf::ui {

// Identity and timing of the selected clip: editable name, kind, track,
// in/out/duration and the source media it refers to.
class ClipInfoSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit ClipInfoSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind) const override { return true; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Video; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    QLineEdit* m_name;
    QLabel* m_kind;
    QLabel* m_range;
    QLabel* m_source;
    QLabel* m_media;
};

}  // namespace lf::ui
