#include "ui/preview/PreviewPanel.h"

#include "ui/ActionRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"
#include "ui/playback/PlaybackController.h"
#include "ui/preview/PreviewWidget.h"
#include "ui/widgets/EmptyState.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {


PreviewPanel::PreviewPanel(ActionRegistry& actions, PlaybackController& playback, QWidget* parent)
    : QWidget(parent), m_actions(actions), m_playback(playback) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_empty = new EmptyState(QStringLiteral("media-video"), tr("No media to preview"),
                             tr("Double-click a media item to preview it,\nor add clips to the timeline."), this);
    m_surface = new PreviewWidget(this);
    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_surface);
    m_stack->setCurrentWidget(m_empty);
    layout->addWidget(m_stack, 1);

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
    m_sourceLabel = new QLabel(transport);
    m_sourceLabel->setProperty("muted", true);
    m_sourceLabel->setMinimumWidth(80);
    row->addWidget(m_sourceLabel);
    row->addStretch(1);

    row->addWidget(transportButton(actions.action(ActionId::PrevEdit)));
    row->addWidget(transportButton(actions.action(ActionId::StepBack)));
    m_playButton = transportButton(actions.action(ActionId::PlayPause), true);
    row->addWidget(m_playButton);
    row->addWidget(transportButton(actions.action(ActionId::PlayStop)));
    row->addWidget(transportButton(actions.action(ActionId::StepForward)));
    row->addWidget(transportButton(actions.action(ActionId::NextEdit)));
    row->addWidget(transportButton(actions.action(ActionId::PlayLoop)));
    m_rateLabel = new QLabel(transport);
    m_rateLabel->setProperty("timecode", true);
    m_rateLabel->setToolTip(tr("Shuttle speed (J / K / L)"));
    m_rateLabel->setFixedWidth(64);
    m_rateLabel->setAlignment(Qt::AlignCenter);
    row->addWidget(m_rateLabel);

    row->addStretch(1);
    m_statsLabel = new QLabel(transport);
    m_statsLabel->setProperty("muted", true);
    m_statsLabel->setToolTip(tr("Preview render size, decoder and frame rate"));
    row->addWidget(m_statsLabel);
    row->addSpacing(6);

    m_muteButton = new QToolButton(transport);
    m_muteButton->setCheckable(true);
    m_muteButton->setAutoRaise(true);
    m_muteButton->setFixedSize(26, 26);
    m_muteButton->setIcon(Icons::icon(QStringLiteral("audio")));
    m_muteButton->setToolTip(tr("Mute preview audio"));
    m_muteButton->setFocusPolicy(Qt::NoFocus);
    connect(m_muteButton, &QToolButton::toggled, this, [this](bool on) {
        m_playback.setMuted(on);
        m_muteButton->setIcon(Icons::icon(on ? QStringLiteral("audio-muted") : QStringLiteral("audio")));
    });
    row->addWidget(m_muteButton);

    m_zoomButton = menuButton(tr("Fit"), tr("Viewer zoom (F = fit, 1 = 100%, 2 = 50%)"),
                              {ActionId::ViewerZoomFit, ActionId::ViewerZoom25, ActionId::ViewerZoom50, ActionId::ViewerZoom100, ActionId::ViewerZoom200});
    row->addWidget(m_zoomButton);
    m_qualityButton = menuButton(tr("Auto"), tr("Preview quality: render resolution of the preview (does not affect export)"),
                                 {ActionId::QualityAuto, ActionId::QualityFull, ActionId::QualityHalf, ActionId::QualityQuarter, ActionId::QualityEighth});
    row->addWidget(m_qualityButton);
    row->addWidget(transportButton(actions.action(ActionId::ViewerFullscreen)));
    row->addSpacing(6);

    m_totalTime = new QLabel(QStringLiteral("00:00:00:00"), transport);
    m_totalTime->setProperty("timecodeMuted", true);
    m_totalTime->setToolTip(tr("Duration"));
    row->addWidget(m_totalTime);
    layout->addWidget(transport);
    // Timecodes never get squeezed: their width is that of the widest value.
    for (QLabel* l : {m_currentTime, m_totalTime}) {
        l->ensurePolished();
        l->setMinimumWidth(l->fontMetrics().horizontalAdvance(QStringLiteral("00:00:00:00")) + 12);
    }

    connect(&m_playback, &PlaybackController::positionChanged, this, [this](TimePoint) { updateTime(); });
    connect(&m_playback, &PlaybackController::playingChanged, this, [this](bool p) { updatePlaying(p); });
    connect(&m_playback, &PlaybackController::programChanged, this, [this] { updateProgram(); });
    connect(&m_playback, &PlaybackController::statsChanged, this, [this](const playback::PlaybackStats& s) { updateStats(s); });
    connect(&m_playback, &PlaybackController::rateChanged, this, &PreviewPanel::updateRate);
    updateRate(1.0);
    updatePlaying(false);
    updateProgram();
}

QToolButton* PreviewPanel::transportButton(QAction* action, bool emphasised) {
    auto* b = new QToolButton(this);
    b->setDefaultAction(action);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setFocusPolicy(Qt::NoFocus);
    b->setIconSize(emphasised ? QSize(22, 22) : QSize(16, 16));
    b->setFixedSize(emphasised ? QSize(36, 30) : QSize(28, 28));
    const QString sc = action->shortcut().toString(QKeySequence::NativeText);
    b->setToolTip(sc.isEmpty() ? action->text().remove('&') : QStringLiteral("%1  (%2)").arg(action->text().remove('&'), sc));
    return b;
}

QToolButton* PreviewPanel::menuButton(const QString& text, const QString& tip, std::initializer_list<ActionId> ids) {
    auto* b = new QToolButton(this);
    b->setText(text);
    b->setToolTip(tip);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setFocusPolicy(Qt::NoFocus);
    b->setMinimumWidth(52);
    auto* menu = new QMenu(b);
    for (auto id : ids) {
        QAction* a = m_actions.action(id);
        menu->addAction(a);
        // The button shows the checked entry of its exclusive group.
        connect(a, &QAction::toggled, b, [b, a](bool on) { if (on) b->setText(a->text().remove(QLatin1Char('&'))); });
        if (a->isChecked()) b->setText(a->text().remove(QLatin1Char('&')));
    }
    b->setMenu(menu);
    return b;
}

void PreviewPanel::updateRate(double rate) {
    if (rate == 1.0) { m_rateLabel->clear(); return; }
    m_rateLabel->setText(rate < 0 ? QStringLiteral("◀ %1×").arg(-rate) : QStringLiteral("%1× ▶").arg(rate));
}

void PreviewPanel::updateTime() {
    const Rational fps = m_playback.frameRate();
    m_currentTime->setText(formatTimecode(m_playback.position(), fps));
    m_totalTime->setText(formatTimecode(TimePoint{m_playback.duration().ticks}, fps));
}

void PreviewPanel::updatePlaying(bool playing) {
    auto* a = m_actions.action(ActionId::PlayPause);
    a->setIcon(Icons::icon(playing ? QStringLiteral("pause") : QStringLiteral("play"), Theme::palette().textPrimary));
    a->setText(playing ? tr("&Pause") : tr("&Play"));
}

void PreviewPanel::updateProgram() {
    const bool has = m_playback.mode() != PlaybackController::Mode::None && m_playback.isAvailable();
    m_stack->setCurrentWidget(has ? static_cast<QWidget*>(m_surface) : static_cast<QWidget*>(m_empty));
    if (!m_playback.isAvailable()) m_stack->setCurrentWidget(m_surface);   // shows the "unavailable" placeholder
    switch (m_playback.mode()) {
        case PlaybackController::Mode::Source: m_sourceLabel->setText(tr("Source · %1").arg(m_playback.sourceName())); break;
        case PlaybackController::Mode::Sequence: m_sourceLabel->setText(tr("Timeline")); break;
        default: m_sourceLabel->clear(); break;
    }
    const bool enabled = has;
    for (auto id : {ActionId::PlayPause, ActionId::Play, ActionId::Pause, ActionId::PlayStop, ActionId::ShuttleForward, ActionId::ShuttleReverse,
                    ActionId::StepBack, ActionId::StepForward, ActionId::StepBackLarge, ActionId::StepForwardLarge, ActionId::GoToStart, ActionId::GoToEnd})
        m_actions.setEnabled(id, enabled);
    updateTime();
}

void PreviewPanel::updateStats(const playback::PlaybackStats& s) {
    if (m_playback.mode() == PlaybackController::Mode::None) { m_statsLabel->clear(); return; }
    QString text = QStringLiteral("%1×%2 · %3").arg(s.renderWidth).arg(s.renderHeight).arg(QString::fromStdString(s.decoder));
    if (m_playback.isPlaying()) text += QStringLiteral(" · %1 fps").arg(s.previewFps, 0, 'f', 1);
    if (s.droppedFrames) text += QStringLiteral(" · %1 dropped").arg(s.droppedFrames);
    m_statsLabel->setText(text);
}

}  // namespace lf::ui
