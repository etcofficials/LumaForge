#include "ui/export/ExportController.h"

#include "lumaforge/core/Log.h"

#include <QFileInfo>
#include <QImage>
#include <QPointer>

namespace lf::ui {

ExportController::ExportController(std::shared_ptr<hw::GpuDevice> gpu, QObject* parent) : QObject(parent), m_gpu(std::move(gpu)) {}

ExportController::~ExportController() {
    m_cancel = true;
    if (m_worker.joinable()) m_worker.join();
    if (m_frameWorker.joinable()) m_frameWorker.join();
}

ExportJob* ExportController::find(int id) {
    for (auto& j : m_jobs) if (j.id == id) return &j;
    return nullptr;
}

const ExportJob* ExportController::job(int id) const {
    for (const auto& j : m_jobs) if (j.id == id) return &j;
    return nullptr;
}

int ExportController::enqueue(const project::Project& snapshot, const exporting::ExportSettings& settings) {
    ExportJob j;
    j.id = m_nextId++;
    j.name = QFileInfo(QString::fromStdString(settings.outputPath)).fileName();
    j.settings = settings;
    j.snapshot = snapshot;
    j.progress.totalFrames = exporting::frameCount(settings, snapshot);
    m_jobs.push_back(std::move(j));
    emit jobsChanged();
    startNext();
    return m_jobs.back().id;
}

void ExportController::cancel(int id) {
    ExportJob* j = find(id);
    if (!j) return;
    if (j->state == ExportJob::State::Queued) {
        j->state = ExportJob::State::Cancelled;
        j->message = tr("Cancelled before it started");
        emit jobsChanged();
    } else if (j->state == ExportJob::State::Rendering) {
        m_cancel = true;   // the worker reports back through its completion
    }
}

void ExportController::remove(int id) {
    std::erase_if(m_jobs, [id](const ExportJob& j) { return j.id == id && j.state != ExportJob::State::Rendering && j.state != ExportJob::State::Queued; });
    emit jobsChanged();
}

void ExportController::clearFinished() {
    std::erase_if(m_jobs, [](const ExportJob& j) { return j.state != ExportJob::State::Rendering && j.state != ExportJob::State::Queued; });
    emit jobsChanged();
}

void ExportController::startNext() {
    if (m_running >= 0) return;
    ExportJob* next = nullptr;
    for (auto& j : m_jobs) if (j.state == ExportJob::State::Queued) { next = &j; break; }
    if (!next) return;
    if (m_worker.joinable()) m_worker.join();   // the previous worker has already finished
    next->state = ExportJob::State::Rendering;
    m_running = next->id;
    m_cancel = false;
    emit jobsChanged();

    const int id = next->id;
    const project::Project snapshot = next->snapshot;
    const exporting::ExportSettings settings = next->settings;
    QPointer<ExportController> self(this);
    m_worker = std::thread([this, self, id, snapshot, settings] {
        exporting::Exporter exporter(m_gpu);
        auto result = exporter.run(snapshot, settings, [self, id](const exporting::ExportProgress& p) {
            QMetaObject::invokeMethod(self.data(), [self, id, p] {
                if (!self) return;
                if (ExportJob* j = self->find(id)) {
                    j->progress = p;
                    emit self->jobProgress(id);
                }
            }, Qt::QueuedConnection);
        }, m_cancel);
        const bool cancelled = m_cancel.load();
        QMetaObject::invokeMethod(self.data(), [self, id, result = std::move(result), cancelled]() mutable {
            if (!self) return;
            self->m_running = -1;
            if (ExportJob* j = self->find(id)) {
                if (result) {
                    j->state = ExportJob::State::Done;
                    j->result = *result;
                    j->progress.frame = j->progress.totalFrames;
                } else if (cancelled) {
                    j->state = ExportJob::State::Cancelled;
                    j->message = tr("Cancelled");
                } else {
                    j->state = ExportJob::State::Failed;
                    j->message = QString::fromStdString(result.error().message);
                    LF_LOG_ERROR("export", "job {} failed: {}", id, result.error().fullText());
                }
                emit self->jobFinished(id);
            }
            emit self->jobsChanged();
            self->startNext();
        }, Qt::QueuedConnection);
    });
}

void ExportController::saveFrame(const project::Project& snapshot, TimePoint t, const QString& path) {
    if (m_frameWorker.joinable()) m_frameWorker.join();
    QPointer<ExportController> self(this);
    m_frameWorker = std::thread([this, self, snapshot, t, path] {
        exporting::Exporter exporter(m_gpu);
        auto img = exporter.renderFrame(snapshot, t);
        bool ok = false;
        QString error;
        if (img) {
            QImage q(img->bgra.data(), img->width, img->height, img->width * 4, QImage::Format_ARGB32);
            ok = q.copy().save(path, "PNG");
            if (!ok) error = tr("The image could not be written.");
        } else {
            error = QString::fromStdString(img.error().message);
        }
        QMetaObject::invokeMethod(self.data(), [self, path, ok, error] {
            if (self) emit self->frameSaved(path, ok, error);
        }, Qt::QueuedConnection);
    });
}

}  // namespace lf::ui
