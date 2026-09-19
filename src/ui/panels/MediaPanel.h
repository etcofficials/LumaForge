#pragma once

#include <QWidget>

class QStackedWidget;
class QTreeView;

namespace lf::ui {

class ActionRegistry;
class EmptyState;
class PanelHeader;

// Project media bin. Until the media engine lands it shows the empty state
// with the import call-to-action; the tree view is already in place so the
// model can be plugged in without touching the layout.
class MediaPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaPanel(ActionRegistry& actions, QWidget* parent = nullptr);

private:
    PanelHeader* m_header;
    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QTreeView* m_view;
};

}  // namespace lf::ui
