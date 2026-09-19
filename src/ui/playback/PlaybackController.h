#pragma once

#include "lumaforge/core/Time.h"
#include "lumaforge/playback/PlaybackEngine.h"
#include "lumaforge/project/MediaItem.h"

#include <QObject>
#include <QString>

#include <memory>

namespace lf::hw { class GpuDevice; }

namespace lf::ui {

// Qt-side owner of the PlaybackEngine. Engine callbacks arrive on engine or
// audio threads; this class re-emits them as signals on the UI thread.
class PlaybackController : public QObject {
    Q_OBJECT
public:
    enum class Mode { None, Source, Sequence };

    PlaybackController(std::shared_ptr<hw::GpuDevice> gpu, bool allowHardwareDecode, QObject* parent = nullptr);
    ~PlaybackController() override;

    [[nodiscard]] bool isAvailable() const noexcept { return m_engine != nullptr; }
    [[nodiscard]] playback::PlaybackEngine* engine() const noexcept { return m_engine.get(); }
    [[nodiscard]] QString unavailableReason() const { return m_unavailableReason; }

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    [[nodiscard]] TimePoint position() const;
    [[nodiscard]] Duration duration() const;
    [[nodiscard]] Rational frameRate() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] QString sourceName() const { return m_sourceName; }
    [[nodiscard]] playback::PlaybackStats stats() const;

public slots:
    // Source monitor: play one media item.
    void openSource(const lf::project::MediaItem& item);
    void setProgram(std::shared_ptr<const lf::playback::Program> program, Mode mode, const QString& label, lf::TimePoint keepPosition = lf::TimePoint{-1});
    void clearProgram();

    void play();
    void pause();
    void togglePlay();
    void stop();
    void seek(lf::TimePoint t, bool scrubbing = false);
    void stepFrames(int frames);
    void goToStart();
    void goToEnd();
    void setQuality(lf::playback::PreviewQuality q);
    void setMuted(bool muted);

signals:
    void positionChanged(lf::TimePoint t);
    void playingChanged(bool playing);
    void statsChanged(const lf::playback::PlaybackStats& stats);
    void programChanged();      // mode/duration/frame rate may have changed
    void errorOccurred(const QString& message);

private:
    std::unique_ptr<playback::PlaybackEngine> m_engine;
    QString m_unavailableReason;
    Mode m_mode = Mode::None;
    QString m_sourceName;
    std::shared_ptr<const playback::Program> m_program;
};

}  // namespace lf::ui
