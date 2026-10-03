#pragma once

#include "lumaforge/project/Document.h"

#include <QWidget>

class QLabel;
class QFormLayout;

namespace lf::ui {

class ActionRegistry;

// "Project" tab of the browser: what the project is (name, file, frame size,
// rate, duration, media and clip counts, offline media) and where LumaForge
// keeps its generated data, with buttons to open those folders.
class ProjectInfoPanel : public QWidget {
    Q_OBJECT
public:
    ProjectInfoPanel(project::Document& doc, ActionRegistry& actions, QWidget* parent = nullptr);
    ~ProjectInfoPanel() override;

    void refreshLocations();

private:
    void refresh();

    project::Document& m_doc;
    int m_listener = 0;
    QLabel* m_name;
    QLabel* m_file;
    QLabel* m_format;
    QLabel* m_duration;
    QLabel* m_counts;
    QLabel* m_offline;
    QFormLayout* m_locations;
};

}  // namespace lf::ui
