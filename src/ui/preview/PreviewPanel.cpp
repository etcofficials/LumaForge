#include "ui/preview/PreviewPanel.h"

#include "ui/ActionRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"
#include "ui/preview/PreviewWidget.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

PreviewPanel::PreviewPanel(ActionRegistry& actions, QWidget* parent) : QWidget(parent), m_actions(actions) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_surface = new PreviewWidget(this);
    layout->addWidget(m_surface, 1);

    auto* transport = new QWidget(this);
    transport->setProperty("transport", true);
    transport->setAttribute(Qt::WA_StyledBackground, true);
    transport->setFixedHeight(Theme::metrics().transportHeight);
    auto* row = new QHBoxLayout(transport);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(2);

    m_currentTime = new QLabel(QStringLiteral("00:00:00:00"), transport);
    m_currentTime->setProperty("timecode", true);
    m_currentTime->setToolTip(tr("Current time"));
    row->addWidget(m_currentTime);
    row->addStretch(1);

    row->addWidget(transportButton(actions.action(ActionId::GoToStart)));
    row->addWidget(transportButton(actions.action(ActionId::StepBack)));
    m_playButton = transportButton(actions.action(ActionId::PlayPause), true);
    row->addWidget(m_playButton);
    row->addWidget(transportButton(actions.action(ActionId::PlayStop)));
    row->addWidget(transportButton(actions.action(ActionId::StepForward)));
    row->addWidget(transportButton(actions.action(ActionId::GoToEnd)));

    row->addStretch(1);
    m_totalTime = new QLabel(QStringLiteral("00:00:00:00"), transport);
    m_totalTime->setProperty("timecodeMuted", true);
    m_totalTime->setToolTip(tr("Sequence duration"));
    row->addWidget(m_totalTime);

    layout->addWidget(transport);
    setPlaying(false);
}

QToolButton* PreviewPanel::transportButton(QAction* action, bool emphasised) {
    auto* b = new QToolButton(this);
    b->setDefaultAction(action);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setFocusPolicy(Qt::NoFocus);
    b->setIconSize(emphasised ? QSize(22, 22) : QSize(16, 16));
    b->setFixedSize(emphasised ? QSize(36, 30) : QSize(28, 28));
    // Tooltip shows the shortcut so the keyboard workflow is discoverable.
    const QString sc = action->shortcut().toString(QKeySequence::NativeText);
    b->setToolTip(sc.isEmpty() ? action->text().remove('&') : QStringLiteral("%1  (%2)").arg(action->text().remove('&'), sc));
    return b;
}

void PreviewPanel::setTime(TimePoint current, Duration total, Rational fps) {
    m_currentTime->setText(formatTimecode(current, fps));
    m_totalTime->setText(formatTimecode(TimePoint{total.ticks}, fps));
}

void PreviewPanel::setPlaying(bool playing) {
    auto* a = m_actions.action(ActionId::PlayPause);
    a->setIcon(Icons::icon(playing ? QStringLiteral("pause") : QStringLiteral("play"), Theme::palette().textPrimary));
    a->setText(playing ? tr("&Pause") : tr("&Play"));
}

}  // namespace lf::ui
