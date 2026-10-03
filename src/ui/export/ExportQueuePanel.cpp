#include "ui/export/ExportQueuePanel.h"

#include "lumaforge/core/StringUtil.h"
#include "ui/export/ExportController.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace lf::ui {

namespace {
QString clockText(double seconds) {
    if (seconds < 0) return QStringLiteral("—");
    const int s = static_cast<int>(std::lround(seconds));
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QPushButton* rowButton(QWidget* parent, const QString& text) {
    auto* b = new QPushButton(text, parent);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
}  // namespace

ExportQueuePanel::ExportQueuePanel(ExportController& controller, QWidget* parent) : QWidget(parent), m_controller(controller) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);
    auto* header = new QHBoxLayout();
    auto* title = new QLabel(tr("Render queue"), this);
    title->setProperty("sectionHeading", true);
    m_clear = rowButton(this, tr("Clear Finished"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_clear);
    layout->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    m_rows = new QVBoxLayout(host);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(6);
    m_rows->addStretch(1);
    scroll->setWidget(host);
    layout->addWidget(scroll, 1);
    m_empty = new QLabel(tr("Nothing queued. Choose settings on the right and press Export."), this);
    m_empty->setProperty("muted", true);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty);

    connect(m_clear, &QPushButton::clicked, &m_controller, &ExportController::clearFinished);
    connect(&m_controller, &ExportController::jobsChanged, this, &ExportQueuePanel::rebuild);
    connect(&m_controller, &ExportController::jobProgress, this, &ExportQueuePanel::refresh);
    rebuild();
}

void ExportQueuePanel::rebuild() {
    for (auto& row : m_rowById) row.widget->deleteLater();
    m_rowById.clear();
    for (const auto& job : m_controller.jobs()) {
        Row row;
        row.widget = new QWidget(this);
        row.widget->setObjectName(QStringLiteral("exportJobRow"));
        row.widget->setAttribute(Qt::WA_StyledBackground, true);
        auto* v = new QVBoxLayout(row.widget);
        v->setContentsMargins(8, 6, 8, 6);
        v->setSpacing(4);
        auto* top = new QHBoxLayout();
        row.name = new QLabel(row.widget);
        row.name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row.status = new QLabel(row.widget);
        row.status->setProperty("muted", true);
        top->addWidget(row.name, 1);
        top->addWidget(row.status);
        v->addLayout(top);
        row.bar = new QProgressBar(row.widget);
        row.bar->setRange(0, 1000);
        row.bar->setTextVisible(false);
        row.bar->setFixedHeight(6);
        v->addWidget(row.bar);
        auto* buttons = new QHBoxLayout();
        row.cancel = rowButton(row.widget, tr("Cancel"));
        row.folder = rowButton(row.widget, tr("Show in Folder"));
        row.play = rowButton(row.widget, tr("Play"));
        row.remove = rowButton(row.widget, tr("Remove"));
        buttons->addStretch(1);
        for (auto* b : {row.cancel, row.folder, row.play, row.remove}) buttons->addWidget(b);
        v->addLayout(buttons);
        const int id = job.id;
        connect(row.cancel, &QPushButton::clicked, this, [this, id] { m_controller.cancel(id); });
        connect(row.remove, &QPushButton::clicked, this, [this, id] { m_controller.remove(id); });
        connect(row.folder, &QPushButton::clicked, this, [this, id] {
            if (const ExportJob* j = m_controller.job(id))
                QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(QString::fromStdString(j->result.path))});
        });
        connect(row.play, &QPushButton::clicked, this, [this, id] {
            if (const ExportJob* j = m_controller.job(id)) QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(j->result.path)));
        });
        m_rows->insertWidget(m_rows->count() - 1, row.widget);
        m_rowById.insert(id, row);
        fill(m_rowById[id], job);
    }
    m_empty->setVisible(m_controller.jobs().empty());
    bool anyFinished = false;
    for (const auto& j : m_controller.jobs()) anyFinished |= j.state != ExportJob::State::Queued && j.state != ExportJob::State::Rendering;
    m_clear->setEnabled(anyFinished);
}

void ExportQueuePanel::refresh(int id) {
    auto it = m_rowById.find(id);
    const ExportJob* job = m_controller.job(id);
    if (it == m_rowById.end() || !job) return;
    fill(*it, *job);
}

void ExportQueuePanel::fill(Row& row, const ExportJob& job) {
    using S = ExportJob::State;
    row.name->setText(QStringLiteral("<b>%1</b>").arg(job.name.toHtmlEscaped()));
    row.name->setToolTip(QString::fromStdString(job.settings.outputPath));
    const auto& p = job.progress;
    switch (job.state) {
        case S::Queued: row.status->setText(tr("Queued")); break;
        case S::Rendering:
            row.status->setText(tr("%1% · %2 fps · %3 left").arg(std::lround(p.fraction() * 100)).arg(p.framesPerSecond, 0, 'f', 1).arg(clockText(p.etaSeconds())));
            break;
        case S::Done:
            row.status->setText(tr("Done in %1 · %2").arg(clockText(job.result.seconds), QString::fromStdString(formatBytes(job.result.bytes))));
            break;
        case S::Failed: row.status->setText(tr("Failed: %1").arg(job.message)); break;
        case S::Cancelled: row.status->setText(job.message.isEmpty() ? tr("Cancelled") : job.message); break;
    }
    row.status->setToolTip(row.status->text());
    row.bar->setValue(job.state == S::Done ? 1000 : static_cast<int>(p.fraction() * 1000));
    row.bar->setVisible(job.state == S::Rendering || job.state == S::Done || job.state == S::Queued);
    row.cancel->setVisible(job.state == S::Queued || job.state == S::Rendering);
    row.folder->setVisible(job.state == S::Done);
    row.play->setVisible(job.state == S::Done);
    row.remove->setVisible(job.state == S::Done || job.state == S::Failed || job.state == S::Cancelled);
}

}  // namespace lf::ui
