#pragma once

#include "lumaforge/core/Time.h"

#include <QWidget>

class QLabel;
class QToolButton;

namespace lf::ui {

class ActionRegistry;
class PreviewWidget;

// Center panel: the video surface with the transport bar underneath.
// Transport buttons are views of the shared QActions (same shortcuts,
// same enabled state as the Playback menu).
class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(ActionRegistry& actions, QWidget* parent = nullptr);

    [[nodiscard]] PreviewWidget* surface() const noexcept { return m_surface; }

    void setTime(TimePoint current, Duration total, Rational fps);
    void setPlaying(bool playing);

private:
    QToolButton* transportButton(QAction* action, bool emphasised = false);

    ActionRegistry& m_actions;
    PreviewWidget* m_surface;
    QLabel* m_currentTime;
    QLabel* m_totalTime;
    QToolButton* m_playButton;
};

}  // namespace lf::ui
