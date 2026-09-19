#pragma once

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/core/VisualParams.h"

#include <cstdint>
#include <memory>
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
    ColorAdjust color;
    EffectParams effects;
    bool fitToFrame = true;
};

// What to mix for one audio-bearing clip at a given program time.
struct AudioClipRequest {
    std::string path;
    std::uint64_t clipKey = 0;   // identity of the clip instance (readers are per clip)
    TimePoint sourceTime;        // media time corresponding to the program time asked for
    double speed = 1.0;
    float gain = 1.0f;           // linear, after fades/mute
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
};

}  // namespace lf::playback
