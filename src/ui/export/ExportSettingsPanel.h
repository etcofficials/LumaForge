#pragma once

#include "lumaforge/export/ExportSettings.h"
#include "lumaforge/hardware/HwCapabilities.h"
#include "lumaforge/project/Document.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace lf::ui {

class ExportController;

// The Export workspace's settings: preset, file, format, codec, size,
// quality, audio and range, with a live summary (size, length, estimated
// file size, free space) and every problem that would stop the export.
class ExportSettingsPanel : public QWidget {
    Q_OBJECT
public:
    ExportSettingsPanel(project::Document& doc, ExportController& controller, QWidget* parent = nullptr);
    ~ExportSettingsPanel() override;

    [[nodiscard]] exporting::ExportSettings settings() const;
    // Hardware encoders the probe confirmed (Media Foundation etc.).
    void setEncoderProbes(const std::vector<hw::EncoderProbe>& probes);
    // Resets the file name for the current project (new/open).
    void resetOutputName();
    // Queues the export if the settings are valid; returns the job id or 0.
    int startExport();

signals:
    void exportQueued(int jobId);
    void exportFrameRequested();

private:
    void applyPresetIndex(int index);
    void markCustom();
    void updateEncoders();
    void updateSummary();
    void browse();
    [[nodiscard]] QString defaultOutputPath() const;

    project::Document& m_doc;
    ExportController& m_controller;
    int m_listenerToken = 0;
    bool m_updating = false;
    std::vector<hw::EncoderProbe> m_probes;

    QComboBox* m_preset;
    QLineEdit* m_path;
    QComboBox* m_container;
    QComboBox* m_codec;
    QComboBox* m_encoder;
    QComboBox* m_size;
    QComboBox* m_rate;
    QSlider* m_quality;
    QLabel* m_qualityLabel;
    QSpinBox* m_bitrate;
    QComboBox* m_speed;
    QCheckBox* m_audio;
    QComboBox* m_audioBitrate;
    QComboBox* m_range;
    QCheckBox* m_fastStart;
    QCheckBox* m_burnSubtitles;
    QCheckBox* m_subtitleSidecar;
    QLabel* m_summary;
    QLabel* m_problems;
    QPushButton* m_export;
};

}  // namespace lf::ui
