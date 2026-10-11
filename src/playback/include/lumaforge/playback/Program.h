#pragma once

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/core/VisualParams.h"
#include "lumaforge/project/Sequence.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lf::playback {

// What to draw for one layer at a given program time.
struct VideoLayerRequest {
    std::string path;          // media file; empty for generated layers (text)
    TimePoint sourceTime;      // position inside the media
    bool isImage = false;      // still image: decode once, no seeking
    std::uint64_t layerKey = 0;   // stable identity (clip id) for caches
    Transform2D transform;
    Crop crop;
    Mask mask;
    ColorAdjust color;
    EffectParams effects;
    bool fitToFrame = true;
    std::optional<TextParams> text;   // generated text layer (path is empty)
    std::optional<ColorRgba> solid;   // generated full-frame colour layer (path is empty)
    bool adjustment = false;          // applies colour/effects/mask to everything below (path is empty)
};

// What to mix for one audio-bearing clip at a given program time.
struct AudioClipRequest {
    std::string path;
    std::uint64_t clipKey = 0;   // identity of the clip instance (readers are per clip)
    TimePoint sourceTime;        // media time corresponding to the program time asked for
    double speed = 1.0;
    bool reverse = false;        // media plays backwards: sourceTime is the latest sample of the block
    float gain = 1.0f;           // linear, after fades/mute
    float pan = 0.0f;            // clip pan, -1..1
    project::AudioFx fx;         // clip processing (EQ, compressor, gate)
    std::uint64_t trackKey = 0;  // the audio track (bus) this clip feeds
    float trackGain = 1.0f;      // that track's volume
    float trackPan = 0.0f;       // that track's pan
};

// An immutable description of what plays: the playback engine and the
// exporter query it by time from their own threads. Implementations are
// snapshots; editing produces a new Program rather than mutating one.
class Program {
public:
    virtual ~Program() = default;

    [[nodiscard]] virtual Duration duration() const = 0;
    [[nodiscard]] virtual Rational frameRate() const = 0;
    [[nodiscard]] virtual int frameWidth() const = 0;
    [[nodiscard]] virtual int frameHeight() const = 0;
    [[nodiscard]] virtual bool hasAudio() const = 0;

    // Layers bottom-to-top at time t. Must be cheap: called per rendered frame.
    virtual void videoLayersAt(TimePoint t, std::vector<VideoLayerRequest>& out) const = 0;
    // Audio clips audible at time t.
    virtual void audioClipsAt(TimePoint t, std::vector<AudioClipRequest>& out) const = 0;

    // Changes rarely; lets the engine drop readers for media no longer used.
    [[nodiscard]] virtual std::vector<std::string> mediaPaths() const = 0;

    // Final mix stage.
    [[nodiscard]] virtual float masterGain() const { return 1.0f; }
    [[nodiscard]] virtual bool masterLimiter() const { return false; }
    [[nodiscard]] virtual float masterCeilingDb() const { return -0.3f; }   // limiter ceiling, dBFS
};

}  // namespace lf::playback
