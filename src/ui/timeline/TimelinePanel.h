#pragma once

#include "lumaforge/project/Document.h"

#include <QWidget>

class QScrollBar;
class QLabel;

namespace lf::ui {

class ActionRegistry;
class PanelHeader;
class TimelineView;
class WaveformService;

// Timeline panel = header (title, duration, snapping, zoom controls) + canvas
// + scrollbar. Owns the wiring between the clip actions (split, delete,
// select all) and the view, including their enabled state.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    TimelinePanel(project::Document& doc, ActionRegistry& actions, QWidget* parent = nullptr);
    ~TimelinePanel() override;

    [[nodiscard]] TimelineView* view() const noexcept { return m_view; }

private:
    void syncScrollBar();
    void updateActionState();
    void updateDurationLabel();
    void showContextMenu(const QPoint& globalPos, project::ClipId clip);

    project::Document& m_doc;
    ActionRegistry& m_actions;
    PanelHeader* m_header;
    QLabel* m_durationLabel;
    TimelineView* m_view;
    QScrollBar* m_scroll;
    WaveformService* m_waveforms;
    bool m_syncing = false;
    int m_listenerToken = 0;
};

}  // namespace lf::ui
