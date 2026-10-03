#include "ui/inspector/ClipInfoSection.h"

#include "ui/Timecode.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace lf::ui {

using namespace lf::project;

namespace {
QString kindName(ClipKind k) {
    switch (k) {
        case ClipKind::Video: return ClipInfoSection::tr("Video");
        case ClipKind::Audio: return ClipInfoSection::tr("Audio");
        case ClipKind::Image: return ClipInfoSection::tr("Image");
        case ClipKind::Text: return ClipInfoSection::tr("Text");
        case ClipKind::Solid: return ClipInfoSection::tr("Solid colour");
        case ClipKind::Adjustment: return ClipInfoSection::tr("Adjustment");
    }
    return {};
}
QLabel* valueLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
}
}  // namespace

ClipInfoSection::ClipInfoSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Clip"), QStringLiteral("clip"), ctx, parent) {
    auto* f = form();
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("Clip name"));
    f->addRow(tr("Name"), m_name);
    m_kind = valueLabel(this);
    f->addRow(tr("Type"), m_kind);
    m_range = valueLabel(this);
    f->addRow(tr("Timeline"), m_range);
    m_source = valueLabel(this);
    f->addRow(tr("Source"), m_source);
    m_media = valueLabel(this);
    m_media->setProperty("muted", true);
    f->addRow(tr("Media"), m_media);

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (m_loading) return;
        const std::string name = m_name->text().trimmed().toStdString();
        if (name.empty()) return;
        m_ctx.editSelection(tr("Rename Clip"), prop::Name, [name](Clip& c) { c.name = name; });
        m_ctx.endGesture();
    });
}

void ClipInfoSection::load(const Clip& c, int count) {
    LoadGuard guard(m_loading);
    const Project& project = m_ctx.project();
    const Rational fps = project.settings.frameRate;
    if (!m_name->hasFocus()) m_name->setText(QString::fromStdString(c.name));
    m_name->setEnabled(count == 1);
    setSubtitle(count > 1 ? tr("%n clips", nullptr, count) : QString());

    const Track* track = project.sequence.findTrack(c.track);
    m_kind->setText(track ? QStringLiteral("%1  ·  %2").arg(kindName(c.kind), QString::fromStdString(track->name)) : kindName(c.kind));
    m_range->setText(QStringLiteral("%1 → %2\n%3").arg(formatTimecode(c.start, fps), formatTimecode(c.end(), fps), formatClock(c.duration)));
    if (isGeneratedClip(c.kind)) {
        m_source->setText(tr("Generated"));
        m_media->setText(QString());
    } else {
        QString src = QStringLiteral("%1 → %2").arg(formatTimecode(c.sourceIn, fps), formatTimecode(c.sourceOut(), fps));
        if (c.speed != Rational{1, 1}) src += tr("  at %1%").arg(std::llround(c.speed.toDouble() * 100));
        m_source->setText(src);
        const MediaItem* item = project.findMedia(c.media);
        m_media->setText(item ? QFileInfo(QString::fromStdString(item->path)).fileName() + (item->offline ? tr("  (offline)") : QString()) : tr("Missing"));
    }
}

}  // namespace lf::ui
