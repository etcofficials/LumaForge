#pragma once

#include "lumaforge/export/Exporter.h"
#include "lumaforge/project/Project.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace lf::hw { class GpuDevice; }

namespace lf::ui {

// One render in the queue. The project is a snapshot taken when the job was
// queued, so editing on while it renders never changes what is written.
struct ExportJob {
    enum class State { Queued, Rendering, Done, Failed, Cancelled };
    int id = 0;
    QString name;                       // file name shown in the queue
    exporting::ExportSettings settings;
    project::Project snapshot;
    State state = State::Queued;
    exporting::ExportProgress progress;
    QString message;                    // error text, or a summary when done
    exporting::ExportResult result;
};

// Runs export jobs one after another on a worker thread and reports progress
// on the UI thread. Destroying the controller cancels and joins the worker.
class ExportController : public QObject {
    Q_OBJECT
public:
    ExportController(std::shared_ptr<hw::GpuDevice> gpu, QObject* parent = nullptr);
    ~ExportController() override;

    int enqueue(const project::Project& snapshot, const exporting::ExportSettings& settings);
    void cancel(int id);                 // queued: dropped; rendering: stopped
    void remove(int id);                 // finished jobs only
    void clearFinished();
    [[nodiscard]] const std::vector<ExportJob>& jobs() const noexcept { return m_jobs; }
    [[nodiscard]] const ExportJob* job(int id) const;
    [[nodiscard]] bool isBusy() const noexcept { return m_running >= 0; }

    // Renders one frame to a PNG at project size (on a worker thread); emits frameSaved.
    void saveFrame(const project::Project& snapshot, TimePoint t, const QString& path);

signals:
    void jobsChanged();                  // added / removed / state changed
    void jobProgress(int id);
    void jobFinished(int id);
    void frameSaved(const QString& path, bool ok, const QString& error);

private:
    void startNext();
    ExportJob* find(int id);

    std::shared_ptr<hw::GpuDevice> m_gpu;
    std::vector<ExportJob> m_jobs;
    int m_nextId = 1;
    int m_running = -1;                  // id of the job on the worker, -1 = idle
    std::atomic<bool> m_cancel{false};
    std::thread m_worker;
    std::thread m_frameWorker;
};

}  // namespace lf::ui
