#include "ui/dialogs/VoiceOverDialog.h"

#include "lumaforge/audio/AudioRecorder.h"
#include "ui/panels/MixerPanel.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace lf::ui {

VoiceOverDialog::VoiceOverDialog(const QString& outputPath, QWidget* parent) : QDialog(parent), m_path(outputPath) {
    setWindowTitle(tr("Record Voice-over"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    m_device = new QLabel(this);
    m_device->setWordWrap(true);
    layout->addWidget(m_device);
    auto* row = new QHBoxLayout();
    m_meter = new LevelMeter(this);
    m_meter->setMinimumHeight(120);
    m_time = new QLabel(QStringLiteral("0:00.0"), this);
    m_time->setProperty("timecode", true);
    QFont f = m_time->font();
    f.setPointSizeF(f.pointSizeF() * 2.0);
    m_time->setFont(f);
    row->addWidget(m_meter);
    row->addSpacing(12);
    row->addWidget(m_time, 1);
    layout->addLayout(row);
    m_play = new QCheckBox(tr("Play the timeline while recording (use headphones to avoid echo)"), this);
    m_play->setChecked(true);
    layout->addWidget(m_play);
    auto* note = new QLabel(tr("Recording starts at the playhead. When you stop, the recording is added on a free audio track at that point."), this);
    note->setWordWrap(true);
    note->setProperty("muted", true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(this);
    m_record = buttons->addButton(tr("Record"), QDialogButtonBox::ActionRole);
    m_record->setObjectName(QStringLiteral("voiceOverRecord"));
    m_record->setProperty("primary", true);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_record, &QPushButton::clicked, this, &VoiceOverDialog::toggle);

    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &VoiceOverDialog::tick);

    auto rec = audio::AudioRecorder::create();
    if (!rec) {
        m_device->setText(tr("<b>No microphone available.</b><br>%1").arg(QString::fromStdString(rec.error().message)));
        m_record->setEnabled(false);
    } else {
        m_recorder = std::move(rec.value());
        m_device->setText(tr("Microphone: <b>%1</b>").arg(QString::fromStdString(m_recorder->deviceName()).toHtmlEscaped()));
    }
}

VoiceOverDialog::~VoiceOverDialog() {
    if (m_recorder && m_recorder->isRecording()) (void)m_recorder->stop();
}

bool VoiceOverDialog::playTimeline() const { return m_play->isChecked(); }

void VoiceOverDialog::toggle() {
    if (!m_recorder) return;
    if (!m_recorder->isRecording()) {
        if (auto s = m_recorder->start(m_path.toStdString()); !s) {
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(s.error().message));
            return;
        }
        m_record->setText(tr("Stop"));
        m_play->setEnabled(false);
        m_timer->start();
        emit recordingStarted();
        return;
    }
    auto length = m_recorder->stop();
    m_timer->stop();
    emit recordingStopped();
    if (!length) {
        QMessageBox::warning(this, windowTitle(), QString::fromStdString(length.error().message));
        m_record->setText(tr("Record"));
        m_play->setEnabled(true);
        return;
    }
    m_recordedFile = m_path;
    accept();
}

void VoiceOverDialog::tick() {
    m_meter->setLevel(m_recorder->takePeak());
    const double s = m_recorder->recorded().toSeconds();
    m_time->setText(QStringLiteral("%1:%2").arg(static_cast<int>(s) / 60).arg(std::fmod(s, 60.0), 4, 'f', 1, QLatin1Char('0')));
}

}  // namespace lf::ui
