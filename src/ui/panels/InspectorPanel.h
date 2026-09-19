#pragma once

#include <QWidget>

namespace lf::ui {

class PanelHeader;
class EmptyState;

// Context properties for the current selection. Placeholder in V0.1: it
// exists so the layout is final and so a new user learns where properties
// will appear.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

private:
    PanelHeader* m_header;
    EmptyState* m_empty;
};

}  // namespace lf::ui
