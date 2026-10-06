#include "ui/export/ExportSettingsPanel.h"

#include "lumaforge/core/StringUtil.h"
#include "lumaforge/export/FastCut.h"
#include "lumaforge/project/Subtitles.h"
#include "ui/AppPaths.h"
#include "ui/Timecode.h"
#include "ui/export/ExportController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStorageInfo>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

using namespace lf::exporting;

namespace {
constexpr const char* kSpeeds[] = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower"};

QString qualityText(int crf) {
    QString word = crf <= 16 ? QObject::tr("Visually lossless") : crf <= 19 ? QObject::tr("Very high") : crf <= 22 ? QObject::tr("High")
                 : crf <= 25 ? QObject::tr("Good") : crf <= 28 ? QObject::tr("Medium") : QObject::tr("Low");
    return QStringLiteral("%1 (CRF %2)").arg(word).arg(crf);
}

QString fpsText(Rational fps) {
    const double v = fps.toDouble();
    return QString::number(v, 'f', std::abs(v - std::round(v)) < 0.001 ? 0 : 3);
}

const QSize kCustomSize(-1, -1);   // m_size item data for "Custom…"
}  // namespace

ExportSettingsPanel::ExportSettingsPanel(project::Document& doc, ExportController& controller, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_controller(controller) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    m_restoring = true;   // nothing is saved until the remembered choices are back (restoreState)
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    layout->addLayout(form);
    m_form = form;

    m_preset = new QComboBox(this);
    m_preset->setObjectName(QStringLiteral("exportPreset"));
    for (const auto& p : exportPresets()) {
        m_preset->addItem(tr(p.name));
        m_preset->setItemData(m_preset->count() - 1, tr(p.description), Qt::ToolTipRole);
    }
    m_preset->addItem(tr("Custom"));
    form->addRow(tr("Preset"), m_preset);

    auto* fileRow = new QWidget(this);
    auto* fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(4);
    m_path = new QLineEdit(fileRow);
    m_path->setObjectName(QStringLiteral("exportPath"));
    auto* browse = new QToolButton(fileRow);
    browse->setText(tr("Browse…"));
    fileLayout->addWidget(m_path, 1);
    fileLayout->addWidget(browse);
    form->addRow(tr("File"), fileRow);

    m_container = new QComboBox(this);
    for (auto c : {Container::Mp4, Container::Mov, Container::Mkv}) m_container->addItem(QString::fromLatin1(containerName(c)), static_cast<int>(c));
    form->addRow(tr("Format"), m_container);
    m_codec = new QComboBox(this);
    m_codec->setObjectName(QStringLiteral("exportCodec"));
    m_codec->addItem(tr("H.264 (plays everywhere)"), static_cast<int>(VideoCodec::H264));
    if (encoderAvailable("libx265")) m_codec->addItem(tr("HEVC / H.265 (smaller files)"), static_cast<int>(VideoCodec::Hevc));
    form->addRow(tr("Video codec"), m_codec);
    m_encoder = new QComboBox(this);
    m_encoder->setObjectName(QStringLiteral("exportEncoder"));
    form->addRow(tr("Encoder"), m_encoder);
    m_encoderStatus = new QLabel(this);
    m_encoderStatus->setObjectName(QStringLiteral("exportEncoderStatus"));
    m_encoderStatus->setProperty("muted", true);
    m_encoderStatus->setWordWrap(true);
    form->addRow(QString(), m_encoderStatus);
    m_size = new QComboBox(this);
    m_size->setObjectName(QStringLiteral("exportSize"));
    form->addRow(tr("Resolution"), m_size);
    m_customSize = new QWidget(this);
    auto* customLayout = new QHBoxLayout(m_customSize);
    customLayout->setContentsMargins(0, 0, 0, 0);
    m_customWidth = new QSpinBox(m_customSize);
    m_customHeight = new QSpinBox(m_customSize);
    for (QSpinBox* box : {m_customWidth, m_customHeight}) {
        box->setRange(16, 7680);
        box->setSingleStep(2);
        box->setSuffix(tr(" px"));
    }
    m_customWidth->setObjectName(QStringLiteral("exportCustomWidth"));
    m_customHeight->setObjectName(QStringLiteral("exportCustomHeight"));
    m_customWidth->setValue(1920);
    m_customHeight->setValue(1080);
    customLayout->addWidget(m_customWidth);
    customLayout->addWidget(new QLabel(QStringLiteral("×"), m_customSize));
    customLayout->addWidget(m_customHeight);
    customLayout->addStretch(1);
    m_customSize->setToolTip(tr("Odd sizes are rounded to even numbers. A different aspect ratio than the project adds black bars."));
    form->addRow(tr("Custom size"), m_customSize);
    m_fps = new QComboBox(this);
    m_fps->setObjectName(QStringLiteral("exportFps"));
    m_fps->addItem(tr("Project"), QSize(0, 1));
    for (const Rational r : standardFrameRates()) m_fps->addItem(tr("%1 fps").arg(fpsText(r)), QSize(static_cast<int>(r.num), static_cast<int>(r.den)));
    m_fps->setToolTip(tr("Frames are taken from the timeline at this rate (no frame blending)"));
    form->addRow(tr("Frame rate"), m_fps);
    m_rate = new QComboBox(this);
    m_rate->addItem(tr("Constant quality"), static_cast<int>(RateControl::Quality));
    m_rate->addItem(tr("Average bitrate"), static_cast<int>(RateControl::Bitrate));
    form->addRow(tr("Rate control"), m_rate);
    auto* qualityRow = new QWidget(this);
    auto* qualityLayout = new QHBoxLayout(qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    m_quality = new QSlider(Qt::Horizontal, qualityRow);
    m_quality->setRange(14, 32);
    m_quality->setInvertedAppearance(true);   // right = better
    m_quality->setFocusPolicy(Qt::NoFocus);
    m_qualityLabel = new QLabel(qualityRow);
    m_qualityLabel->setMinimumWidth(150);
    qualityLayout->addWidget(m_quality, 1);
    qualityLayout->addWidget(m_qualityLabel);
    form->addRow(tr("Quality"), qualityRow);
    m_bitrate = new QSpinBox(this);
    m_bitrate->setRange(300, 100000);
    m_bitrate->setSingleStep(500);
    m_bitrate->setSuffix(tr(" kbit/s"));
    m_bitrate->setValue(12000);
    form->addRow(tr("Bitrate"), m_bitrate);
    m_speed = new QComboBox(this);
    for (const char* sp : kSpeeds) m_speed->addItem(QString::fromLatin1(sp));
    m_speed->setToolTip(tr("Slower presets make smaller files at the same quality; faster ones finish sooner"));
    form->addRow(tr("Encoder speed"), m_speed);
    m_audio = new QCheckBox(tr("Include audio (AAC)"), this);
    form->addRow(tr("Audio"), m_audio);
    m_audioBitrate = new QComboBox(this);
    for (int kbps : {128, 160, 192, 256, 320}) m_audioBitrate->addItem(tr("%1 kbit/s").arg(kbps), kbps);
    form->addRow(tr("Audio bitrate"), m_audioBitrate);
    m_sampleRate = new QComboBox(this);
    m_sampleRate->setObjectName(QStringLiteral("exportSampleRate"));
    m_sampleRate->addItem(tr("48 kHz (video standard)"), 48000);
    m_sampleRate->addItem(tr("44.1 kHz (CD)"), 44100);
    form->addRow(tr("Sample rate"), m_sampleRate);
    m_channels = new QComboBox(this);
    m_channels->setObjectName(QStringLiteral("exportChannels"));
    m_channels->addItem(tr("Stereo"), 2);
    m_channels->addItem(tr("Mono"), 1);
    form->addRow(tr("Channels"), m_channels);
    m_range = new QComboBox(this);
    m_range->setObjectName(QStringLiteral("exportRange"));
    m_range->addItem(tr("Entire timeline"));
    m_range->addItem(tr("In to Out"));
    form->addRow(tr("Range"), m_range);
    m_burnSubtitles = new QCheckBox(tr("Burn subtitles into the picture"), this);
    m_burnSubtitles->setChecked(true);
    m_subtitleSidecar = new QCheckBox(tr("Also save subtitles as .srt next to the video"), this);
    form->addRow(tr("Subtitles"), m_burnSubtitles);
    form->addRow(QString(), m_subtitleSidecar);
    m_lossless = new QCheckBox(tr("Lossless fast cut (copy without re-encoding)"), this);
    m_lossless->setObjectName(QStringLiteral("exportLossless"));
    m_lossless->setToolTip(tr("Only possible when the range is one unedited piece of a single file: no effects, colour, text, transitions, speed or audio changes"));
    form->addRow(tr("Mode"), m_lossless);
    m_losslessInfo = new QLabel(this);
    m_losslessInfo->setObjectName(QStringLiteral("exportMode"));
    m_losslessInfo->setWordWrap(true);
    m_losslessInfo->setTextFormat(Qt::RichText);
    form->addRow(QString(), m_losslessInfo);

    // Advanced (collapsed): fast start, pixel format, metadata.
    m_advancedToggle = new QToolButton(this);
    m_advancedToggle->setObjectName(QStringLiteral("exportAdvancedToggle"));
    m_advancedToggle->setText(tr("Advanced"));
    m_advancedToggle->setCheckable(true);
    m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedToggle->setArrowType(Qt::RightArrow);
    m_advancedToggle->setAutoRaise(true);
    layout->addWidget(m_advancedToggle);
    m_advanced = new QWidget(this);
    m_advanced->setObjectName(QStringLiteral("exportAdvanced"));
    auto* advancedForm = new QFormLayout(m_advanced);
    advancedForm->setContentsMargins(0, 0, 0, 0);
    advancedForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    advancedForm->setHorizontalSpacing(10);
    advancedForm->setVerticalSpacing(6);
    m_fastStart = new QCheckBox(tr("Fast start (index first: plays while downloading)"), m_advanced);
    advancedForm->addRow(tr("MP4 / MOV"), m_fastStart);
    auto* pixelFormat = new QLabel(tr("YUV 4:2:0, 8-bit, BT.709 limited range (plays on every device)"), m_advanced);
    pixelFormat->setProperty("muted", true);
    pixelFormat->setWordWrap(true);
    advancedForm->addRow(tr("Pixel format"), pixelFormat);
    m_title = new QLineEdit(m_advanced);
    m_title->setObjectName(QStringLiteral("exportTitle"));
    m_comment = new QLineEdit(m_advanced);
    m_comment->setObjectName(QStringLiteral("exportComment"));
    advancedForm->addRow(tr("Title"), m_title);
    advancedForm->addRow(tr("Comment"), m_comment);
    m_advanced->setVisible(false);
    layout->addWidget(m_advanced);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("exportSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    layout->addWidget(m_summary);
    m_problems = new QLabel(this);
    m_problems->setObjectName(QStringLiteral("exportProblems"));
    m_problems->setWordWrap(true);
    m_problems->setTextFormat(Qt::RichText);
    layout->addWidget(m_problems);
    layout->addStretch(1);

    auto* buttons = new QHBoxLayout();
    auto* frame = new QPushButton(tr("Export Frame…"), this);
    frame->setToolTip(tr("Save the frame at the playhead as a PNG image"));
    m_export = new QPushButton(tr("Export"), this);
    m_export->setObjectName(QStringLiteral("exportStart"));
    m_export->setProperty("primary", true);
    m_export->setToolTip(tr("Add this render to the queue; it starts right away when nothing else is rendering"));
    buttons->addWidget(frame);
    buttons->addStretch(1);
    buttons->addWidget(m_export);
    auto* footer = new QWidget(this);
    footer->setLayout(buttons);
    buttons->setContentsMargins(12, 6, 12, 10);
    outer->addWidget(footer);

    // Wiring: every change refreshes the summary; manual changes switch the preset to Custom.
    connect(m_preset, &QComboBox::currentIndexChanged, this, &ExportSettingsPanel::applyPresetIndex);
    connect(browse, &QToolButton::clicked, this, &ExportSettingsPanel::browse);
    connect(m_path, &QLineEdit::textChanged, this, &ExportSettingsPanel::updateSummary);
    connect(m_container, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_updating) {
            // Keep the name, switch the extension.
            QFileInfo fi(m_path->text());
            if (!fi.fileName().isEmpty()) {
                const auto c = static_cast<Container>(m_container->currentData().toInt());
                m_path->setText(QDir::toNativeSeparators(fi.dir().filePath(fi.completeBaseName() + QString::fromLatin1(containerExtension(c)))));
            }
            markCustom();
        }
        updateSummary();
    });
    for (QComboBox* box : {m_codec, m_size, m_rate, m_speed, m_audioBitrate, m_range, m_fps, m_sampleRate, m_channels}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { markCustom(); updateSummary(); });
    }
    for (QSpinBox* box : {m_customWidth, m_customHeight}) connect(box, &QSpinBox::valueChanged, this, [this] { markCustom(); updateSummary(); });
    connect(m_lossless, &QCheckBox::toggled, this, &ExportSettingsPanel::updateSummary);
    connect(m_title, &QLineEdit::textChanged, this, &ExportSettingsPanel::updateSummary);
    connect(m_comment, &QLineEdit::textChanged, this, &ExportSettingsPanel::updateSummary);
    connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool open) {
        m_advanced->setVisible(open);
        m_advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(m_codec, &QComboBox::currentIndexChanged, this, &ExportSettingsPanel::updateEncoders);
    connect(m_encoder, &QComboBox::currentIndexChanged, this, [this] { m_encoder->setProperty("want", QVariant()); markCustom(); updateSummary(); });
    connect(m_quality, &QSlider::valueChanged, this, [this] { markCustom(); updateSummary(); });
    connect(m_bitrate, &QSpinBox::valueChanged, this, [this] { markCustom(); updateSummary(); });
    connect(m_audio, &QCheckBox::toggled, this, [this] { markCustom(); updateSummary(); });
    connect(m_fastStart, &QCheckBox::toggled, this, &ExportSettingsPanel::updateSummary);
    connect(m_burnSubtitles, &QCheckBox::toggled, this, &ExportSettingsPanel::updateSummary);
    connect(m_subtitleSidecar, &QCheckBox::toggled, this, &ExportSettingsPanel::updateSummary);
    connect(m_export, &QPushButton::clicked, this, [this] { startExport(); });
    connect(frame, &QPushButton::clicked, this, &ExportSettingsPanel::exportFrameRequested);

    m_listenerToken = m_doc.addListener([this](const project::Change& c) {
        if (c.kind == project::Change::Kind::Reset) resetOutputName();
        else updateSummary();
    });

    updateEncoders();
    m_audio->setChecked(true);
    m_fastStart->setChecked(true);
    m_preset->setCurrentIndex(1);   // YouTube 1080p
    applyPresetIndex(1);
    restoreState();
    resetOutputName();
}

ExportSettingsPanel::~ExportSettingsPanel() { m_doc.removeListener(m_listenerToken); }

void ExportSettingsPanel::setEncoderProbes(const std::vector<hw::EncoderProbe>& probes) {
    m_probes = probes;
    m_probed = true;
    updateEncoders();
}

void ExportSettingsPanel::updateEncoders() {
    // A remembered hardware encoder is selected once the probe has confirmed it.
    QString keep = m_encoder->currentData().toString();
    if (const QVariant want = m_encoder->property("want"); want.isValid()) keep = want.toString();
    const bool hevc = static_cast<VideoCodec>(m_codec->currentData().toInt()) == VideoCodec::Hevc;
    QSignalBlocker block(m_encoder);
    m_encoder->clear();
    m_encoder->addItem(hevc ? tr("x265 (CPU)") : tr("x264 (CPU, best quality)"), QString());
    // Hardware / OS encoders the probe confirmed work on this machine.
    for (const auto& p : m_probes) {
        if (!p.available || p.name == "libx264") continue;
        const bool isHevc = p.name.find("hevc") != std::string::npos;
        if (isHevc != hevc) continue;
        m_encoder->addItem(QString::fromStdString(p.displayName), QString::fromStdString(p.name));
    }
    if (const int i = m_encoder->findData(keep); i >= 0) {
        m_encoder->setCurrentIndex(i);
        m_encoder->setProperty("want", QVariant());
    }
    if (m_probed) m_encoder->setProperty("want", QVariant());
    m_encoder->setEnabled(m_encoder->count() > 1);
    // What this PC can use, measured by the hardware probe (never guessed from the GPU name).
    QStringList hardware;
    for (const auto& p : m_probes)
        if (p.available && p.kind == hw::EncoderKind::Hardware) hardware << QString::fromStdString(p.displayName);
    if (!m_probed) m_encoderStatus->setText(tr("Checking for hardware encoders…"));
    else if (!hardware.isEmpty()) m_encoderStatus->setText(tr("Hardware encoder available: %1").arg(hardware.join(QStringLiteral(", "))));
    else m_encoderStatus->setText(tr("CPU encoder (no working hardware encoder on this PC)"));
    updateSummary();
}

void ExportSettingsPanel::markCustom() {
    if (m_updating) return;
    QSignalBlocker block(m_preset);
    m_preset->setCurrentIndex(m_preset->count() - 1);
}

void ExportSettingsPanel::applyPresetIndex(int index) {
    const auto presets = exportPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) return;   // Custom: leave as is
    const auto& p = presets[static_cast<std::size_t>(index)];
    m_updating = true;
    m_container->setCurrentIndex(m_container->findData(static_cast<int>(p.container)));
    const int codec = m_codec->findData(static_cast<int>(p.codec));
    m_codec->setCurrentIndex(codec >= 0 ? codec : 0);
    updateEncoders();
    m_encoder->setCurrentIndex(0);
    // Size list is rebuilt in updateSummary; select by height.
    m_size->setProperty("wantHeight", p.height);
    m_rate->setCurrentIndex(m_rate->findData(static_cast<int>(p.rateControl)));
    if (p.crf > 0) m_quality->setValue(p.crf);
    if (p.videoKbps > 0) m_bitrate->setValue(p.videoKbps);
    m_speed->setCurrentText(QString::fromLatin1(p.speedPreset));
    m_audioBitrate->setCurrentIndex(std::max(0, m_audioBitrate->findData(p.audioKbps)));
    QFileInfo fi(m_path->text());
    if (!fi.fileName().isEmpty()) m_path->setText(QDir::toNativeSeparators(fi.dir().filePath(fi.completeBaseName() + QString::fromLatin1(containerExtension(p.container)))));
    m_updating = false;
    updateSummary();
}

QString ExportSettingsPanel::defaultOutputPath() const {
    const auto c = static_cast<Container>(m_container->currentData().toInt());
    QString base = QString::fromStdString(m_doc.project().settings.name).trimmed();
    if (!m_doc.filePath().empty()) base = QFileInfo(QString::fromStdString(m_doc.filePath())).completeBaseName();
    if (base.isEmpty()) base = tr("Untitled");
    for (QChar ch : QStringLiteral("\\/:*?\"<>|")) base.replace(ch, QLatin1Char('_'));
    const QDir dir(AppPaths::path(AppPaths::Kind::Exports));
    const QString ext = QString::fromLatin1(containerExtension(c));
    QString candidate = dir.filePath(base + ext);
    // Not an existing file, nor one a queued/rendering job is about to write.
    auto taken = [this](const QString& path) {
        if (QFileInfo::exists(path)) return true;
        for (const auto& j : m_controller.jobs()) {
            if ((j.state == ExportJob::State::Queued || j.state == ExportJob::State::Rendering) &&
                QDir::cleanPath(QString::fromStdString(j.settings.outputPath)).compare(QDir::cleanPath(QDir::fromNativeSeparators(path)), Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };
    for (int i = 2; taken(candidate); ++i) candidate = dir.filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ext));
    return QDir::toNativeSeparators(candidate);
}

void ExportSettingsPanel::resetOutputName() {
    QDir().mkpath(AppPaths::path(AppPaths::Kind::Exports));
    m_path->setText(defaultOutputPath());
    updateSummary();
}

void ExportSettingsPanel::browse() {
    const auto c = static_cast<Container>(m_container->currentData().toInt());
    const QString ext = QString::fromLatin1(containerExtension(c));
    const QString filter = tr("%1 video (*%2)").arg(QString::fromLatin1(containerName(c)), ext);
    QString file = QFileDialog::getSaveFileName(this, tr("Export Video"), m_path->text(), filter);
    if (file.isEmpty()) return;
    if (!file.endsWith(ext, Qt::CaseInsensitive)) file += ext;
    m_path->setText(QDir::toNativeSeparators(file));
}

ExportSettings ExportSettingsPanel::settings() const {
    ExportSettings s;
    s.outputPath = QDir::fromNativeSeparators(m_path->text().trimmed()).toStdString();
    s.container = static_cast<Container>(m_container->currentData().toInt());
    s.codec = static_cast<VideoCodec>(m_codec->currentData().toInt());
    s.encoder = m_encoder->currentData().toString().toStdString();
    QSize size = m_size->currentData().toSize();
    if (size == kCustomSize) size = QSize(m_customWidth->value() & ~1, m_customHeight->value() & ~1);
    s.width = size.width();
    s.height = size.height();
    if (const QSize fps = m_fps->currentData().toSize(); fps.width() > 0) s.frameRate = Rational{fps.width(), fps.height()};
    s.rateControl = static_cast<RateControl>(m_rate->currentData().toInt());
    s.crf = m_quality->value();
    s.videoKbps = m_bitrate->value();
    s.speedPreset = m_speed->currentText().toStdString();
    s.includeAudio = m_audio->isChecked();
    s.audioKbps = m_audioBitrate->currentData().toInt();
    s.audioSampleRate = m_sampleRate->currentData().toInt();
    s.audioChannels = m_channels->currentData().toInt();
    s.title = m_title->text().trimmed().toStdString();
    s.comment = m_comment->text().trimmed().toStdString();
    s.losslessFastCut = m_lossless->isChecked();
    s.fastStart = m_fastStart->isChecked();
    s.burnSubtitles = m_burnSubtitles->isChecked();
    s.subtitleSidecar = m_subtitleSidecar->isChecked() && m_subtitleSidecar->isEnabled();
    const auto& seq = m_doc.project().sequence;
    if (m_range->currentIndex() == 1 && seq.hasInOut()) {
        s.rangeStart = seq.markIn;
        s.rangeEnd = seq.markOut;
    }
    return s;
}

void ExportSettingsPanel::updateSummary() {
    if (m_updating) return;
    const auto& project = m_doc.project();
    // Resolution choices: the project size, the standard sizes with its aspect ratio (up to 4K), and Custom.
    {
        int wantHeight = m_size->property("wantHeight").isValid() ? m_size->property("wantHeight").toInt() : -1;
        if (wantHeight > project.settings.height) wantHeight = 0;   // presets never upscale (same as applyPreset)
        const QSize keep = m_size->currentData().toSize();
        QSignalBlocker block(m_size);
        m_size->clear();
        m_size->addItem(tr("Project (%1×%2)").arg(project.settings.width).arg(project.settings.height), QSize(0, 0));
        for (int h : {2160, 1440, 1080, 720, 480, 360}) {
            if (h == project.settings.height) continue;
            const int w = 2 * static_cast<int>(std::lround(static_cast<double>(project.settings.width) * h / project.settings.height / 2.0));
            m_size->addItem(h > project.settings.height ? tr("%1×%2 (upscaled)").arg(w).arg(h) : QStringLiteral("%1×%2").arg(w).arg(h), QSize(w, h));
        }
        m_size->addItem(tr("Custom…"), kCustomSize);
        int index = 0;
        for (int i = 0; i < m_size->count(); ++i) {
            const QSize sz = m_size->itemData(i).toSize();
            if (wantHeight >= 0 ? (sz.height() == wantHeight && sz != kCustomSize) || (wantHeight == 0 && i == 0) : sz == keep) index = i;
        }
        m_size->setCurrentIndex(index);
        m_size->setProperty("wantHeight", QVariant());
    }
    m_form->setRowVisible(m_customSize, m_size->currentData().toSize() == kCustomSize);
    m_fps->setItemText(0, tr("Project (%1 fps)").arg(fpsText(project.settings.frameRate)));

    // Lossless fast cut: copies the source, so every re-encoding choice is moot.
    const ExportSettings current = settings();
    const FastCutCheck fastCut = checkFastCut(project, current);
    const bool lossless = m_lossless->isChecked();
    if (lossless && fastCut.eligible)
        m_losslessInfo->setText(tr("<b style='color:#4FB477'>LOSSLESS</b> — the original video and audio are copied: no quality loss, finishes in seconds. "
                                   "The file starts at the keyframe at or before the range start, so it can begin slightly earlier."));
    else if (lossless)
        m_losslessInfo->setText(tr("<b style='color:#E5A33D'>RE-ENCODE needed</b> — %1.").arg(QString::fromStdString(fastCut.reason).toHtmlEscaped()));
    else
        m_losslessInfo->setText(fastCut.eligible ? tr("<b>RE-ENCODE</b> — this range is unedited and could also be exported losslessly.")
                                                 : tr("<b>RE-ENCODE</b> — the picture and sound are rendered and compressed again."));
    for (QWidget* w : std::initializer_list<QWidget*>{m_codec, m_size, m_customSize, m_fps, m_rate, m_audioBitrate, m_sampleRate, m_channels})
        w->setEnabled(!lossless);
    const bool quality = static_cast<RateControl>(m_rate->currentData().toInt()) == RateControl::Quality;
    const bool x26x = m_encoder->currentData().toString().isEmpty();
    m_encoder->setEnabled(!lossless && m_encoder->count() > 1);
    m_quality->setEnabled(!lossless && quality);
    m_qualityLabel->setText(qualityText(m_quality->value()));
    m_bitrate->setEnabled(!lossless && (!quality || !x26x));
    m_speed->setEnabled(!lossless && x26x);
    m_audioBitrate->setEnabled(!lossless && m_audio->isChecked());
    m_sampleRate->setEnabled(!lossless && m_audio->isChecked());
    m_channels->setEnabled(!lossless && m_audio->isChecked());
    const auto c = static_cast<Container>(m_container->currentData().toInt());
    m_fastStart->setEnabled(c != Container::Mkv);
    const bool hasSubtitles = !project::subtitleCues(project.sequence).empty();
    m_burnSubtitles->setEnabled(hasSubtitles && !lossless);
    m_subtitleSidecar->setEnabled(hasSubtitles && !lossless);
    m_range->setItemText(1, project.sequence.hasInOut() ? tr("In to Out (%1)").arg(formatClock(project.sequence.markOut - project.sequence.markIn)) : tr("In to Out (set I and O first)"));
    if (!project.sequence.hasInOut() && m_range->currentIndex() == 1) m_range->setCurrentIndex(0);

    const ExportSettings& s = current;
    int w = 0, h = 0;
    outputSize(s, project, w, h);
    const TimeRange range = exportRange(s, project);
    QString summary;
    if (lossless && fastCut.eligible) {
        summary = tr("<b>Stream copy</b> of %1 · %2 (source size, frame rate and codecs)")
                      .arg(QFileInfo(QString::fromStdString(fastCut.sourcePath)).fileName().toHtmlEscaped(), formatClock(fastCut.sourceOut - fastCut.sourceIn));
    } else {
        summary = tr("<b>%1×%2</b> · %3 fps · %4 · %5 frames").arg(w).arg(h).arg(fpsText(outputFrameRate(s, project)), formatClock(range.duration)).arg(frameCount(s, project));
        summary += tr("<br>About <b>%1</b> · %2 + %3").arg(QString::fromStdString(formatBytes(estimatedBytes(s, project))),
                                                            s.codec == VideoCodec::Hevc ? QStringLiteral("HEVC") : QStringLiteral("H.264"),
                                                            s.includeAudio ? (s.audioChannels == 1 ? tr("AAC mono") : QStringLiteral("AAC")) : tr("no audio"));
    }
    const QStorageInfo storage(QFileInfo(m_path->text()).absolutePath());
    if (storage.isValid()) summary += tr("<br>Free space on %1: %2").arg(storage.rootPath(), QString::fromStdString(formatBytes(static_cast<std::uint64_t>(storage.bytesAvailable()))));
    m_summary->setText(summary);

    QStringList problems;
    for (const auto& p : validate(s, project)) problems << QString::fromStdString(p).toHtmlEscaped();
    const auto offline = offlineMediaInRange(s, project);
    if (!offline.empty()) {
        QStringList names;
        for (const auto& n : offline) names << QString::fromStdString(n).toHtmlEscaped();
        problems << tr("Missing media in the range: %1. Relink it in the Media panel (right-click > Relink) first.").arg(names.join(QStringLiteral(", ")));
    }
    if (storage.isValid() && static_cast<std::uint64_t>(storage.bytesAvailable()) < estimatedBytes(s, project) + 50ull * 1024 * 1024)
        problems << tr("There may not be enough free disk space for this export.");
    if (lossless && !fastCut.eligible) problems << tr("Lossless fast cut is not possible here; untick it to re-encode.");
    if (QFileInfo::exists(m_path->text())) problems << tr("<span style='color:#E5A33D'>A file with this name exists and will be replaced.</span>");
    m_problems->setText(problems.join(QStringLiteral("<br>")));
    // Replacing a file is a warning, everything else blocks.
    const bool blocking = !validate(s, project).empty() || !offline.empty() || (lossless && !fastCut.eligible);
    m_export->setEnabled(!blocking);
    saveState();
}

// Remembered between sessions (not the file name or range: those belong to the project).
void ExportSettingsPanel::saveState() const {
    if (m_restoring) return;
    QSettings st;
    st.beginGroup(QStringLiteral("export"));
    st.setValue(QStringLiteral("preset"), m_preset->currentIndex());
    st.setValue(QStringLiteral("container"), m_container->currentData());
    st.setValue(QStringLiteral("codec"), m_codec->currentData());
    st.setValue(QStringLiteral("encoder"), m_encoder->property("want").isValid() ? m_encoder->property("want") : m_encoder->currentData());
    st.setValue(QStringLiteral("size"), m_size->currentData().toSize());
    st.setValue(QStringLiteral("customSize"), QSize(m_customWidth->value(), m_customHeight->value()));
    st.setValue(QStringLiteral("fps"), m_fps->currentData().toSize());
    st.setValue(QStringLiteral("rateControl"), m_rate->currentData());
    st.setValue(QStringLiteral("crf"), m_quality->value());
    st.setValue(QStringLiteral("kbps"), m_bitrate->value());
    st.setValue(QStringLiteral("speed"), m_speed->currentText());
    st.setValue(QStringLiteral("audio"), m_audio->isChecked());
    st.setValue(QStringLiteral("audioKbps"), m_audioBitrate->currentData());
    st.setValue(QStringLiteral("sampleRate"), m_sampleRate->currentData());
    st.setValue(QStringLiteral("channels"), m_channels->currentData());
    st.setValue(QStringLiteral("fastStart"), m_fastStart->isChecked());
    st.setValue(QStringLiteral("burnSubtitles"), m_burnSubtitles->isChecked());
    st.setValue(QStringLiteral("subtitleSidecar"), m_subtitleSidecar->isChecked());
    st.setValue(QStringLiteral("lossless"), m_lossless->isChecked());
    st.setValue(QStringLiteral("comment"), m_comment->text());
    st.setValue(QStringLiteral("advancedOpen"), m_advancedToggle->isChecked());
}

void ExportSettingsPanel::restoreState() {
    QSettings st;
    st.beginGroup(QStringLiteral("export"));
    if (!st.contains(QStringLiteral("preset"))) {   // first run: the default preset stays
        m_restoring = false;
        return;
    }
    m_restoring = true;
    m_updating = true;
    auto select = [](QComboBox* box, const QVariant& value) {
        if (const int i = box->findData(value); i >= 0) box->setCurrentIndex(i);
    };
    select(m_container, st.value(QStringLiteral("container")));
    select(m_codec, st.value(QStringLiteral("codec")));
    m_encoder->setProperty("want", st.value(QStringLiteral("encoder")).toString());
    const QSize custom = st.value(QStringLiteral("customSize"), QSize(1920, 1080)).toSize();
    m_customWidth->setValue(custom.width());
    m_customHeight->setValue(custom.height());
    select(m_fps, st.value(QStringLiteral("fps")));
    select(m_rate, st.value(QStringLiteral("rateControl")));
    m_quality->setValue(st.value(QStringLiteral("crf"), m_quality->value()).toInt());
    m_bitrate->setValue(st.value(QStringLiteral("kbps"), m_bitrate->value()).toInt());
    m_speed->setCurrentText(st.value(QStringLiteral("speed"), m_speed->currentText()).toString());
    m_audio->setChecked(st.value(QStringLiteral("audio"), true).toBool());
    select(m_audioBitrate, st.value(QStringLiteral("audioKbps")));
    select(m_sampleRate, st.value(QStringLiteral("sampleRate")));
    select(m_channels, st.value(QStringLiteral("channels")));
    m_fastStart->setChecked(st.value(QStringLiteral("fastStart"), true).toBool());
    m_burnSubtitles->setChecked(st.value(QStringLiteral("burnSubtitles"), true).toBool());
    m_subtitleSidecar->setChecked(st.value(QStringLiteral("subtitleSidecar"), false).toBool());
    m_lossless->setChecked(st.value(QStringLiteral("lossless"), false).toBool());
    m_comment->setText(st.value(QStringLiteral("comment")).toString());
    m_advancedToggle->setChecked(st.value(QStringLiteral("advancedOpen"), false).toBool());
    {
        // The size list is rebuilt for the project in updateSummary, which keeps the current entry when it exists.
        QSignalBlocker block(m_size);
        const QSize size = st.value(QStringLiteral("size"), QSize(0, 0)).toSize();
        if (m_size->findData(size) < 0) m_size->addItem(QString(), size);
        select(m_size, size);
    }
    {
        QSignalBlocker block(m_preset);
        const int preset = st.value(QStringLiteral("preset"), 1).toInt();
        m_preset->setCurrentIndex(preset >= 0 && preset < m_preset->count() ? preset : m_preset->count() - 1);
    }
    m_updating = false;
    updateEncoders();   // applies the remembered encoder once the probe has confirmed it; refreshes the summary
    m_restoring = false;
    saveState();
}

int ExportSettingsPanel::startExport() {
    const ExportSettings s = settings();
    const auto& project = m_doc.project();
    if (!validate(s, project).empty() || !offlineMediaInRange(s, project).empty()) return 0;
    if (s.losslessFastCut && !checkFastCut(project, s).eligible) return 0;
    const int id = m_controller.enqueue(project, s);
    emit exportQueued(id);
    // Next export gets a fresh name so it never overwrites this one by accident.
    resetOutputName();
    return id;
}

}  // namespace lf::ui
