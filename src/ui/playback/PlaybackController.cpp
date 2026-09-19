#include "ui/playback/PlaybackController.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/playback/SourceProgram.h"

#include <QPointer>

namespace lf::ui {

PlaybackController::PlaybackController(std::shared_ptr<hw::GpuDevice> gpu, bool allowHardwareDecode, QObject* parent) : QObject(parent) {
    if (!gpu) {
        m_unavailableReason = tr("No Direct3D 11 device is available.");
        return;
    }
    auto engine = playback::PlaybackEngine::create(std::move(gpu), allowHardwareDecode);
    if (!engine) {
        m_unavailableReason = QString::fromStdString(engine.error().message);
        LF_LOG_ERROR("playback", "engine unavailable: {}", engine.error().fullText());
        return;
    }
    m_engine = std::move(engine.value());

    // Engine callbacks run on engine/audio threads: hop to the UI thread.
    QPointer<PlaybackController> self(this);
    playback::PlaybackEngine::Callbacks cb;
    cb.positionChanged = [self](TimePoint t) { QMetaObject::invokeMethod(self, [self, t] { if (self) emit self->positionChanged(t); }, Qt::QueuedConnection); };
    cb.playingChanged = [self](bool p) { QMetaObject::invokeMethod(self, [self, p] { if (self) emit self->playingChanged(p); }, Qt::QueuedConnection); };
    cb.statsChanged = [self](const playback::PlaybackStats& s) { QMetaObject::invokeMethod(self, [self, s] { if (self) emit self->statsChanged(s); }, Qt::QueuedConnection); };
    cb.error = [self](const std::string& m) { QMetaObject::invokeMethod(self, [self, m] { if (self) emit self->errorOccurred(QString::fromStdString(m)); }, Qt::QueuedConnection); };
    m_engine->setCallbacks(std::move(cb));
}

PlaybackController::~PlaybackController() = default;

TimePoint PlaybackController::position() const { return m_engine ? m_engine->position() : TimePoint::zero(); }
Duration PlaybackController::duration() const { return m_program ? m_program->duration() : Duration::zero(); }
Rational PlaybackController::frameRate() const { return m_program ? m_program->frameRate() : fps::f30; }
bool PlaybackController::isPlaying() const { return m_engine && m_engine->isPlaying(); }
playback::PlaybackStats PlaybackController::stats() const { return m_engine ? m_engine->stats() : playback::PlaybackStats{}; }

void PlaybackController::openSource(const project::MediaItem& item) {
    auto program = std::make_shared<playback::SourceProgram>(item.info, 1920, 1080, fps::f30);
    setProgram(program, Mode::Source, QString::fromStdString(item.displayName), TimePoint::zero());
}

void PlaybackController::setProgram(std::shared_ptr<const playback::Program> program, Mode mode, const QString& label, TimePoint keepPosition) {
    m_program = std::move(program);
    m_mode = m_program ? mode : Mode::None;
    m_sourceName = label;
    if (m_engine) m_engine->setProgram(m_program, keepPosition);
    emit programChanged();
    emit positionChanged(position());
}

void PlaybackController::clearProgram() { setProgram(nullptr, Mode::None, QString()); }

void PlaybackController::play() { if (m_engine) m_engine->play(); }
void PlaybackController::pause() { if (m_engine) m_engine->pause(); }
void PlaybackController::togglePlay() { if (m_engine) m_engine->togglePlay(); }
void PlaybackController::stop() { if (m_engine) m_engine->stop(); }
void PlaybackController::seek(TimePoint t, bool scrubbing) { if (m_engine) m_engine->seek(t, scrubbing); }
void PlaybackController::stepFrames(int frames) { if (m_engine) m_engine->stepFrames(frames); }
void PlaybackController::goToStart() { seek(TimePoint::zero()); }
void PlaybackController::goToEnd() { seek(TimePoint{duration().ticks}); }
void PlaybackController::setQuality(playback::PreviewQuality q) { if (m_engine) m_engine->setQuality(q); }
void PlaybackController::setMuted(bool muted) { if (m_engine) m_engine->setMuted(muted); }

}  // namespace lf::ui
