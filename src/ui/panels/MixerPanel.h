#pragma once

#include "lumaforge/project/TimelineCommands.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QSlider;
class QTimer;
class QToolButton;

namespace lf::ui {

class PlaybackController;

// Vertical peak meter (dBFS scale, -60..0) with a falling peak-hold line.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void setLevel(float linearPeak);   // newest block peak; the meter falls back smoothly
    [[nodiscard]] QSize sizeHint() const override { return {14, 160}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_level = 0.0f;   // displayed (dB-mapped 0..1)
    float m_hold = 0.0f;
    int m_holdAge = 0;
};

// The Audio workspace mixer: a strip per audio track (volume fader, pan,
// mute, solo, meter) and the master strip (volume, limiter, meter). Every
// change is one undoable edit; meters follow the preview mix live.
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    MixerPanel(project::Document& doc, PlaybackController& playback, QWidget* parent = nullptr);
    ~MixerPanel() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Strip {
        project::TrackId track;
        QWidget* widget = nullptr;
        QLabel* name = nullptr;
        QSlider* fader = nullptr;
        QLabel* db = nullptr;
        QSlider* pan = nullptr;
        QToolButton* mute = nullptr;
        QToolButton* solo = nullptr;
        LevelMeter* meter = nullptr;
    };
    void rebuild();
    void refreshValues();
    void pollMeters();
    void commitTrack(const project::Track& track, const QString& text);
    void commitMaster(project::MasterBus bus, const QString& text);

    project::Document& m_doc;
    PlaybackController& m_playback;
    int m_listenerToken = 0;
    QHBoxLayout* m_strips;
    std::vector<Strip> m_stripList;
    QSlider* m_masterFader;
    QLabel* m_masterDb;
    QCheckBox* m_limiter;
    LevelMeter* m_masterMeter;
    QTimer* m_meterTimer;
    bool m_updating = false;
};

}  // namespace lf::ui
