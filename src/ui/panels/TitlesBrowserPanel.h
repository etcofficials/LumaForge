#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;

namespace lf::ui {

// Title presets as preview tiles. Double-click or "Add at Playhead" puts a
// 5 s title on the top video track; dragging a tile drops it where released.
class TitlesBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit TitlesBrowserPanel(QWidget* parent = nullptr);

    // Renders the preset's look into a w x h preview (also used for tests).
    [[nodiscard]] static QImage preview(int preset, int width, int height);

signals:
    void addRequested(int preset);

private:
    QListWidget* m_list;
    QPushButton* m_add;
};

}  // namespace lf::ui
