#pragma once

#include <QWidget>

class QScrollBar;
class QLabel;

namespace lf::ui {

class ActionRegistry;
class PanelHeader;
class TimelineView;

// Timeline panel = header (title, snapping, zoom controls) + canvas + scrollbar.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(ActionRegistry& actions, QWidget* parent = nullptr);

    [[nodiscard]] TimelineView* view() const noexcept { return m_view; }

private:
    void syncScrollBar();

    PanelHeader* m_header;
    TimelineView* m_view;
    QScrollBar* m_scroll;
    bool m_syncing = false;
};

}  // namespace lf::ui
