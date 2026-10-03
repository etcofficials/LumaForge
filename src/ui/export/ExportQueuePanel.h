#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace lf::ui {

class ExportController;
struct ExportJob;

// The render queue: one row per job with its state, progress, speed and
// time remaining; cancel while queued or rendering; open the folder or play
// the file when done.
class ExportQueuePanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportQueuePanel(ExportController& controller, QWidget* parent = nullptr);

private:
    struct Row {
        QWidget* widget = nullptr;
        QLabel* name = nullptr;
        QLabel* status = nullptr;
        QProgressBar* bar = nullptr;
        QPushButton* cancel = nullptr;
        QPushButton* folder = nullptr;
        QPushButton* play = nullptr;
        QPushButton* remove = nullptr;
    };
    void rebuild();
    void refresh(int id);
    void fill(Row& row, const ExportJob& job);

    ExportController& m_controller;
    QVBoxLayout* m_rows;
    QLabel* m_empty;
    QPushButton* m_clear;
    QHash<int, Row> m_rowById;
};

}  // namespace lf::ui
