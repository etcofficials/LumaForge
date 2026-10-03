#pragma once

#include "lumaforge/core/Time.h"

#include <QDialog>

#include <memory>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;

namespace lf::audio { class AudioRecorder; }

namespace lf::ui {

class LevelMeter;

// Records a voice-over from the default microphone. Recording starts at the
// timeline playhead (optionally playing the timeline so the speaker can
// follow the picture); the result is a WAV in the data folder's Recordings.
class VoiceOverDialog : public QDialog {
    Q_OBJECT
public:
    VoiceOverDialog(const QString& outputPath, QWidget* parent = nullptr);
    ~VoiceOverDialog() override;

    [[nodiscard]] bool playTimeline() const;
    [[nodiscard]] QString recordedFile() const { return m_recordedFile; }

signals:
    void recordingStarted();    // the owner starts playback if wanted
    void recordingStopped();

private:
    void toggle();
    void tick();

    QString m_path;
    QString m_recordedFile;
    std::unique_ptr<audio::AudioRecorder> m_recorder;
    QLabel* m_device;
    QLabel* m_time;
    LevelMeter* m_meter;
    QCheckBox* m_play;
    QPushButton* m_record;
    QTimer* m_timer;
};

}  // namespace lf::ui
