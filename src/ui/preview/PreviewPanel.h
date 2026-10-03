#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/playback/PlaybackEngine.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class QToolButton;

namespace lf::ui {

class ActionRegistry;
enum class ActionId;
class EmptyState;
class PlaybackController;
class PreviewWidget;

// Center panel: the video surface (or the empty state) with the transport bar
// underneath. Transport buttons are views of the shared QActions.
class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    PreviewPanel(ActionRegistry& actions, PlaybackController& playback, QWidget* parent = nullptr);

    [[nodiscard]] PreviewWidget* surface() const noexcept { return m_surface; }

private:
    QToolButton* transportButton(QAction* action, bool emphasised = false);
    void updateTime();
    void updatePlaying(bool playing);
    void updateProgram();
    void updateStats(const playback::PlaybackStats& stats);
    void updateRate(double rate);
    QToolButton* menuButton(const QString& text, const QString& tip, std::initializer_list<ActionId> ids);

    ActionRegistry& m_actions;
    PlaybackController& m_playback;
    QStackedWidget* m_stack;
    EmptyState* m_empty;
    PreviewWidget* m_surface;
    QLabel* m_sourceLabel;
    QLabel* m_currentTime;
    QLabel* m_totalTime;
    QLabel* m_statsLabel;
    QLabel* m_rateLabel;
    QToolButton* m_qualityButton;
    QToolButton* m_zoomButton;
    QToolButton* m_playButton;
    QToolButton* m_muteButton;
};

}  // namespace lf::ui
