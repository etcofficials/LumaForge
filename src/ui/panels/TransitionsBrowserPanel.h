#pragma once

#include "lumaforge/project/Transitions.h"

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace lf::ui {

// Library of transitions. "Add" puts the chosen transition on each selected
// clip's edge nearest the playhead (a cut gets one transition); dragging one
// onto a clip puts it on the edge nearest the drop point.
class TransitionsBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionsBrowserPanel(QWidget* parent = nullptr);

    void setCanApply(bool can);
    [[nodiscard]] project::TransitionType currentType() const;
    [[nodiscard]] Duration duration() const;

signals:
    void applyRequested(lf::project::TransitionType type, lf::Duration duration);

private:
    void updateApply();

    QLineEdit* m_search;
    QTreeWidget* m_tree;
    QDoubleSpinBox* m_duration;
    QPushButton* m_apply;
    QLabel* m_hint;
    bool m_canApply = false;
};

}  // namespace lf::ui
