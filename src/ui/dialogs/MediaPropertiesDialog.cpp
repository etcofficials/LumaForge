#include "ui/dialogs/MediaPropertiesDialog.h"

#include "lumaforge/core/StringUtil.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
QLabel* value(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
}
}  // namespace

MediaPropertiesDialog::MediaPropertiesDialog(const project::MediaItem& item, const QImage& thumbnail, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Media Properties — %1").arg(qs(item.displayName)));
    setMinimumWidth(460);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    if (!thumbnail.isNull()) {
        auto* pic = new QLabel(this);
        pic->setPixmap(QPixmap::fromImage(thumbnail.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        pic->setAlignment(Qt::AlignCenter);
        layout->addWidget(pic);
    }

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(5);
    const auto& info = item.info;

    form->addRow(tr("File"), value(qs(info.fileName), this));
    form->addRow(tr("Location"), value(qs(item.path), this));
    form->addRow(tr("Type"), value(QString::fromLatin1(media::mediaTypeName(info.type)), this));
    form->addRow(tr("Container"), value(info.containerLongName.empty() ? qs(info.containerName) : qs(info.containerLongName), this));
    form->addRow(tr("Size"), value(qs(formatBytes(info.fileSize)), this));
    if (info.type != media::MediaType::Image) {
        const Rational fps = info.video && info.video->frameRate.isValid() && !info.video->frameRate.isZero() ? info.video->frameRate : fps::f30;
        form->addRow(tr("Duration"), value(QStringLiteral("%1  (%2)").arg(formatTimecode(TimePoint{info.duration.ticks}, fps), formatClock(info.duration)), this));
    }
    if (info.bitRate > 0) form->addRow(tr("Bit rate"), value(QStringLiteral("%1 kb/s").arg(info.bitRate / 1000), this));

    if (info.video) {
        const auto& v = *info.video;
        auto* h = new QLabel(tr("Video"), this);
        h->setProperty("secondary", true);
        QFont f = h->font(); f.setWeight(QFont::DemiBold); h->setFont(f);
        form->addRow(h);
        form->addRow(tr("Codec"), value(v.codecLongName.empty() ? qs(v.codecName) : QStringLiteral("%1 (%2)").arg(qs(v.codecLongName), qs(v.codecName)), this));
        if (!v.profile.empty()) form->addRow(tr("Profile"), value(qs(v.profile), this));
        form->addRow(tr("Resolution"), value(QStringLiteral("%1 × %2").arg(v.width).arg(v.height), this));
        if (v.frameRate.isValid() && !v.frameRate.isZero()) form->addRow(tr("Frame rate"), value(QStringLiteral("%1 fps (%2/%3)").arg(v.frameRate.toDouble(), 0, 'f', 3).arg(v.frameRate.num).arg(v.frameRate.den), this));
        form->addRow(tr("Pixel format"), value(QStringLiteral("%1, %2-bit%3").arg(qs(v.pixelFormat)).arg(v.bitDepth).arg(v.isFullRange ? tr(", full range") : QString()), this));
        if (v.rotationDegrees) form->addRow(tr("Rotation"), value(QStringLiteral("%1°").arg(v.rotationDegrees), this));
        if (v.frameCount > 0 && info.type == media::MediaType::Video) form->addRow(tr("Frames"), value(QString::number(v.frameCount), this));
    }
    if (info.audio) {
        const auto& a = *info.audio;
        auto* h = new QLabel(tr("Audio"), this);
        h->setProperty("secondary", true);
        QFont f = h->font(); f.setWeight(QFont::DemiBold); h->setFont(f);
        form->addRow(h);
        form->addRow(tr("Codec"), value(a.codecLongName.empty() ? qs(a.codecName) : QStringLiteral("%1 (%2)").arg(qs(a.codecLongName), qs(a.codecName)), this));
        form->addRow(tr("Channels"), value(QStringLiteral("%1 (%2)").arg(a.channels).arg(qs(a.channelLayout)), this));
        form->addRow(tr("Sample rate"), value(QStringLiteral("%1 Hz, %2").arg(a.sampleRate).arg(qs(a.sampleFormat)), this));
    }
    if (item.addedUnixTime) form->addRow(tr("Imported"), value(QDateTime::fromSecsSinceEpoch(item.addedUnixTime).toString(QStringLiteral("yyyy-MM-dd HH:mm")), this));
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

}  // namespace lf::ui
