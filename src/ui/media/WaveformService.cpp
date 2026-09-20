#include "ui/media/WaveformService.h"

#include "lumaforge/core/Log.h"

#include <QPointer>
#include <QThreadPool>

namespace lf::ui {

WaveformService::WaveformService(QObject* parent) : QObject(parent) {}

std::shared_ptr<const media::Waveform> WaveformService::peaksFor(project::MediaId id, const QString& path) {
    if (auto it = m_cache.find(id); it != m_cache.end()) {
        m_lastUse[id] = ++m_useCounter;
        return it.value();
    }
    if (m_pending.contains(id) || m_failed.contains(id) || path.isEmpty()) return nullptr;
    m_pending.insert(id);
    QPointer<WaveformService> self(this);
    QThreadPool::globalInstance()->start([self, id, path] {
        auto result = media::analyseWaveform(path.toStdString());
        if (!self) return;
        std::shared_ptr<const media::Waveform> wave;
        if (result) wave = std::make_shared<const media::Waveform>(std::move(result.value()));
        else LF_LOG_WARN("media", "waveform failed for {}: {}", path.toStdString(), result.error().fullText());
        QMetaObject::invokeMethod(self.data(), [self, id, wave] { if (self) self->store(id, wave); }, Qt::QueuedConnection);
    });
    return nullptr;
}

void WaveformService::store(project::MediaId id, std::shared_ptr<const media::Waveform> wave) {
    m_pending.remove(id);
    if (!wave) { m_failed.insert(id); return; }
    m_cache.insert(id, wave);
    m_lastUse[id] = ++m_useCounter;
    // Evict least recently used entries beyond the byte budget.
    auto bytes = [&] {
        std::size_t total = 0;
        for (const auto& w : m_cache) total += w->peaks.size();
        return total;
    };
    while (m_cache.size() > 1 && bytes() > kMaxBytes) {
        project::MediaId oldest = project::MediaId::Invalid;
        quint64 best = ~0ull;
        for (auto it = m_lastUse.begin(); it != m_lastUse.end(); ++it) {
            if (it.key() != id && m_cache.contains(it.key()) && it.value() < best) { best = it.value(); oldest = it.key(); }
        }
        if (!project::isValid(oldest)) break;
        m_cache.remove(oldest);
        m_lastUse.remove(oldest);
    }
    emit ready(id);
}

void WaveformService::forget(project::MediaId id) {
    m_cache.remove(id);
    m_lastUse.remove(id);
    m_failed.remove(id);
}

}  // namespace lf::ui
