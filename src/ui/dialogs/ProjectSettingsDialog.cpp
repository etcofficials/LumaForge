#include "ui/dialogs/ProjectSettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
struct Preset {
    const char* name;
    int width, height;
    Rational fps;
};
const Preset kPresets[] = {
    {"Custom", 0, 0, {0, 1}},
    {"1080p 30 (1920×1080)", 1920, 1080, fps::f30},
    {"1080p 25 (1920×1080)", 1920, 1080, fps::f25},
    {"1080p 60 (1920×1080)", 1920, 1080, fps::f60},
    {"1080p 24 (1920×1080)", 1920, 1080, fps::f24},
    {"720p 30 (1280×720)", 1280, 720, fps::f30},
    {"720p 60 (1280×720)", 1280, 720, fps::f60},
    {"4K UHD 30 (3840×2160)", 3840, 2160, fps::f30},
    {"Vertical 1080×1920 30", 1080, 1920, fps::f30},
    {"Square 1080×1080 30", 1080, 1080, fps::f30},
};
}  // namespace

const std::vector<std::pair<QString, Rational>>& ProjectSettingsDialog::frameRates() {
    static const std::vector<std::pair<QString, Rational>> rates = {
        {QStringLiteral("23.976"), fps::f23_976}, {QStringLiteral("24"), fps::f24}, {QStringLiteral("25"), fps::f25},
        {QStringLiteral("29.97"), fps::f29_97},   {QStringLiteral("30"), fps::f30}, {QStringLiteral("50"), fps::f50},
        {QStringLiteral("59.94"), fps::f59_94},   {QStringLiteral("60"), fps::f60},
    };
    return rates;
}

ProjectSettingsDialog::ProjectSettingsDialog(const project::ProjectSettings& current, QWidget* parent) : QDialog(parent), m_originalRate(current.frameRate) {
    setWindowTitle(tr("Project Settings"));
    setModal(true);
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_name = new QLineEdit(QString::fromStdString(current.name), this);
    m_name->setMaxLength(120);
    form->addRow(tr("Name"), m_name);

    m_preset = new QComboBox(this);
    for (const auto& p : kPresets) m_preset->addItem(QString::fromUtf8(p.name));
    form->addRow(tr("Preset"), m_preset);

    auto* sizeRow = new QWidget(this);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    m_width = new QSpinBox(sizeRow);
    m_width->setRange(16, 8192);
    m_width->setSingleStep(2);
    m_width->setValue(current.width);
    m_height = new QSpinBox(sizeRow);
    m_height->setRange(16, 8192);
    m_height->setSingleStep(2);
    m_height->setValue(current.height);
    sizeLayout->addWidget(m_width);
    sizeLayout->addWidget(new QLabel(QStringLiteral("×"), sizeRow));
    sizeLayout->addWidget(m_height);
    sizeLayout->addStretch(1);
    form->addRow(tr("Frame size"), sizeRow);

    m_frameRate = new QComboBox(this);
    int fpsIndex = -1;
    for (const auto& [label, rate] : frameRates()) {
        m_frameRate->addItem(label + tr(" fps"));
        if (rate == current.frameRate) fpsIndex = m_frameRate->count() - 1;
    }
    if (fpsIndex < 0) {
        m_frameRate->addItem(tr("%1 fps").arg(current.frameRate.toDouble(), 0, 'f', 3));
        fpsIndex = m_frameRate->count() - 1;
    }
    m_frameRate->setCurrentIndex(fpsIndex);
    form->addRow(tr("Frame rate"), m_frameRate);

    m_sampleRate = new QComboBox(this);
    m_sampleRate->addItem(QStringLiteral("48000 Hz"), 48000);
    m_sampleRate->addItem(QStringLiteral("44100 Hz"), 44100);
    m_sampleRate->setCurrentIndex(current.audioSampleRate == 44100 ? 1 : 0);
    form->addRow(tr("Audio"), m_sampleRate);

    layout->addLayout(form);
    m_summary = new QLabel(this);
    m_summary->setProperty("muted", true);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Select the preset matching the current settings, if any.
    int presetIndex = 0;
    for (int i = 1; i < static_cast<int>(std::size(kPresets)); ++i) {
        if (kPresets[i].width == current.width && kPresets[i].height == current.height && kPresets[i].fps == current.frameRate) { presetIndex = i; break; }
    }
    m_preset->setCurrentIndex(presetIndex);
    connect(m_preset, &QComboBox::currentIndexChanged, this, &ProjectSettingsDialog::applyPreset);
    auto customise = [this] { if (m_preset->currentIndex() != 0) { QSignalBlocker b(m_preset); m_preset->setCurrentIndex(0); } updateSummary(); };
    connect(m_width, &QSpinBox::valueChanged, this, customise);
    connect(m_height, &QSpinBox::valueChanged, this, customise);
    connect(m_frameRate, &QComboBox::currentIndexChanged, this, customise);
    updateSummary();
}

void ProjectSettingsDialog::applyPreset(int index) {
    if (index <= 0 || index >= static_cast<int>(std::size(kPresets))) return;
    const Preset& p = kPresets[index];
    QSignalBlocker bw(m_width), bh(m_height), bf(m_frameRate);
    m_width->setValue(p.width);
    m_height->setValue(p.height);
    for (int i = 0; i < static_cast<int>(frameRates().size()); ++i) if (frameRates()[static_cast<std::size_t>(i)].second == p.fps) m_frameRate->setCurrentIndex(i);
    updateSummary();
}

void ProjectSettingsDialog::updateSummary() {
    const project::ProjectSettings s = settings();
    const bool exact = isExactFrameRate(s.frameRate);
    m_summary->setText(tr("%1 × %2 at %3 fps%4. Existing clips keep their timing; only frame stepping and the ruler change.")
                           .arg(s.width).arg(s.height).arg(s.frameRate.toDouble(), 0, 'g', 6)
                           .arg(exact ? QString() : tr(" (not frame-exact)")));
}

project::ProjectSettings ProjectSettingsDialog::settings() const {
    project::ProjectSettings s;
    s.name = m_name->text().trimmed().toStdString();
    if (s.name.empty()) s.name = "Untitled";
    s.width = m_width->value() & ~1;    // encoders want even dimensions
    s.height = m_height->value() & ~1;
    const int fi = m_frameRate->currentIndex();
    s.frameRate = fi >= 0 && fi < static_cast<int>(frameRates().size()) ? frameRates()[static_cast<std::size_t>(fi)].second : m_originalRate;
    s.audioSampleRate = m_sampleRate->currentData().toInt();
    return s;
}

}  // namespace lf::ui
