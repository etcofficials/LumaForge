#pragma once

#include "lumaforge/media/Waveform.h"
#include "lumaforge/project/Ids.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <memory>

namespace lf::ui {

// Produces audio peak envelopes off the UI thread and keeps them per media
// item. The timeline asks with peaksFor(); a miss schedules the analysis and
// `ready` fires when it can repaint. Bounded so many long files cannot pin
// unbounded memory (a 1 h file is ~360 KB; the cap keeps ~64 of those).
class WaveformService : public QObject {
    Q_OBJECT
public:
    explicit WaveformService(QObject* parent = nullptr);

    // Cached peaks, or null (and a background request) when not yet available.
    std::shared_ptr<const media::Waveform> peaksFor(project::MediaId id, const QString& path);
    void forget(project::MediaId id);   // media removed / relinked

signals:
    void ready(lf::project::MediaId id);

private:
    void store(project::MediaId id, std::shared_ptr<const media::Waveform> wave);

    QHash<project::MediaId, std::shared_ptr<const media::Waveform>> m_cache;
    QHash<project::MediaId, quint64> m_lastUse;
    QSet<project::MediaId> m_pending;
    QSet<project::MediaId> m_failed;
    quint64 m_useCounter = 0;
    static constexpr std::size_t kMaxBytes = 24u * 1024u * 1024u;
};

}  // namespace lf::ui
