#pragma once

#include "lumaforge/core/VisualParams.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace lf::ui {

// Library of clip effects grouped by category. Double-click or "Apply" adds
// the effect to the selected timeline clips; dragging an effect onto a clip
// adds it to that clip (or to the whole selection when the clip is selected).
class EffectsBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsBrowserPanel(QWidget* parent = nullptr);

    // Whether the timeline selection has a clip an effect can go on.
    void setCanApply(bool can);
    [[nodiscard]] EffectType currentEffect() const;

signals:
    void applyRequested(lf::EffectType type);

private:
    void filter(const QString& text);
    void updateApply();

    QLineEdit* m_search;
    QTreeWidget* m_tree;
    QPushButton* m_apply;
    QLabel* m_hint;
    bool m_canApply = false;
};

}  // namespace lf::ui
