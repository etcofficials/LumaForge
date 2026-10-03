#include "ui/panels/ProjectInfoPanel.h"

#include "ui/ActionRegistry.h"
#include "ui/AppPaths.h"
#include "ui/Theme.h"
#include "ui/Timecode.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
QLabel* value(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
}
QLabel* heading(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("font-weight: 600; color: %1; padding-top: 6px;").arg(Theme::palette().textPrimary.name()));
    return l;
}
}  // namespace

ProjectInfoPanel::ProjectInfoPanel(project::Document& doc, ActionRegistry& actions, QWidget* parent) : QWidget(parent), m_doc(doc) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(6);

    layout->addWidget(heading(tr("Project"), host));
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    m_name = value(host);
    m_file = value(host);
    m_format = value(host);
    m_duration = value(host);
    m_counts = value(host);
    m_offline = value(host);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("File"), m_file);
    form->addRow(tr("Format"), m_format);
    form->addRow(tr("Duration"), m_duration);
    form->addRow(tr("Contents"), m_counts);
    form->addRow(tr("Offline"), m_offline);
    layout->addLayout(form);
    auto* settingsButton = new QPushButton(tr("Project Settings…"), host);
    settingsButton->setToolTip(tr("Change name, frame size and frame rate"));
    connect(settingsButton, &QPushButton::clicked, actions.action(ActionId::ProjectSettings), &QAction::trigger);
    layout->addWidget(settingsButton, 0, Qt::AlignLeft);

    layout->addWidget(heading(tr("Data locations"), host));
    auto* note = new QLabel(tr("Generated files (caches, proxies, exports, autosaves) are kept here, not in your user profile. "
                               "Change them in Settings."), host);
    note->setWordWrap(true);
    note->setProperty("muted", true);
    layout->addWidget(note);
    m_locations = new QFormLayout();
    layout->addLayout(m_locations);
    layout->addStretch(1);
    scroll->setWidget(host);
    outer->addWidget(scroll);

    m_listener = m_doc.addListener([this](const project::Change&) { refresh(); });
    refresh();
    refreshLocations();
}

ProjectInfoPanel::~ProjectInfoPanel() { m_doc.removeListener(m_listener); }

void ProjectInfoPanel::refresh() {
    const auto& p = m_doc.project();
    m_name->setText(QString::fromStdString(p.settings.name));
    m_file->setText(m_doc.filePath().empty() ? tr("Not saved yet") : QString::fromStdString(m_doc.filePath()));
    m_format->setText(tr("%1 × %2  ·  %3 fps  ·  %4 Hz")
                          .arg(p.settings.width)
                          .arg(p.settings.height)
                          .arg(p.settings.frameRate.toDouble(), 0, 'f', p.settings.frameRate.den == 1 ? 0 : 3)
                          .arg(p.settings.audioSampleRate));
    m_duration->setText(formatTimecode(TimePoint{p.sequence.duration().ticks}, p.settings.frameRate));
    int offline = 0;
    for (const auto& m : p.media) if (m.offline) ++offline;
    int videoTracks = 0, audioTracks = 0;
    for (const auto& t : p.sequence.tracks) (t.kind == project::TrackKind::Audio ? audioTracks : videoTracks)++;
    m_counts->setText(tr("%1 media  ·  %2 clips  ·  %3 video / %4 audio tracks").arg(p.media.size()).arg(p.sequence.clips.size()).arg(videoTracks).arg(audioTracks));
    m_offline->setText(offline ? tr("%n file(s) missing — right-click them in Media and choose Relink", nullptr, offline) : tr("None"));
    m_offline->setStyleSheet(offline ? QStringLiteral("color: %1;").arg(Theme::palette().warning.name()) : QString());
}

void ProjectInfoPanel::refreshLocations() {
    while (m_locations->rowCount() > 0) m_locations->removeRow(0);
    for (auto kind : {AppPaths::Kind::Projects, AppPaths::Kind::Exports, AppPaths::Kind::Proxies, AppPaths::Kind::Cache, AppPaths::Kind::Autosave, AppPaths::Kind::Logs}) {
        const QString path = AppPaths::path(kind);
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(path, row);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(label, 1);
        auto* open = new QToolButton(row);
        open->setText(tr("Open"));
        open->setToolTip(tr("Open this folder in Explorer"));
        connect(open, &QToolButton::clicked, row, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        h->addWidget(open);
        QString name = AppPaths::displayName(kind);
        m_locations->addRow(name.section(QLatin1Char(' '), 0, 0), row);
    }
}

}  // namespace lf::ui
