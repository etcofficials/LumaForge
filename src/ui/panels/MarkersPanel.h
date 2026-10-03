#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/project/TimelineCommands.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace lf::ui {

class ActionRegistry;

// The sequence's markers: time, colour and name (editable in place).
// Double-click a row to jump there; copy them as a YouTube chapter list.
class MarkersPanel : public QWidget {
    Q_OBJECT
public:
    MarkersPanel(project::Document& doc, ActionRegistry& actions, QWidget* parent = nullptr);
    ~MarkersPanel() override;

signals:
    void seekRequested(lf::TimePoint t);

private:
    void rebuild();
    void commit(const QString& text, project::SequenceEdit edit);
    [[nodiscard]] std::vector<std::uint64_t> selectedIds() const;

    project::Document& m_doc;
    int m_listenerToken = 0;
    QTreeWidget* m_list;
    QPushButton* m_delete;
    QPushButton* m_copy;
    QLabel* m_empty;
    bool m_rebuilding = false;
};

}  // namespace lf::ui
