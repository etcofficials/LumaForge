#pragma once

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/core/VisualParams.h"
#include "lumaforge/project/Ids.h"

#include <string>
#include <vector>

namespace lf::project {

enum class TrackKind { Video, Audio };

struct Track {
    TrackId id = TrackId::Invalid;
    TrackKind kind = TrackKind::Video;
    std::string name;          // "V1", "A2"
    bool muted = false;        // audio: silent; video: hidden
    bool locked = false;       // no edits
    bool solo = false;
    friend bool operator==(const Track&, const Track&) = default;
};

enum class ClipKind { Video, Audio, Image, Text };

[[nodiscard]] constexpr bool isVisualClip(ClipKind k) noexcept { return k != ClipKind::Audio; }

struct AudioParams {
    float volume = 1.0f;       // linear gain; the UI shows dB
    bool muted = false;
    Duration fadeIn;
    Duration fadeOut;
    friend constexpr bool operator==(const AudioParams&, const AudioParams&) = default;
};

// One item on a track. Timeline geometry is [start, start + duration) in
// sequence time; the media it shows is [sourceIn, sourceIn + duration * speed).
struct Clip {
    ClipId id = ClipId::Invalid;
    TrackId track = TrackId::Invalid;
    ClipKind kind = ClipKind::Video;
    MediaId media = MediaId::Invalid;    // Invalid for text clips
    std::string name;
    TimePoint start;
    Duration duration;
    TimePoint sourceIn;
    Rational speed{1, 1};                // >0; 2/1 = double speed
    ClipId link = ClipId::Invalid;       // A/V partner created from the same media

    Transform2D transform;
    Crop crop;
    ColorAdjust color;
    std::vector<Effect> effects;   // ordered; see applyEffects()
    AudioParams audio;
    TextParams text;

    [[nodiscard]] constexpr TimePoint end() const noexcept { return start + duration; }
    [[nodiscard]] constexpr TimeRange range() const noexcept { return {start, duration}; }
    [[nodiscard]] constexpr bool contains(TimePoint t) const noexcept { return range().contains(t); }

    // Sequence time -> media time (exact integer arithmetic).
    [[nodiscard]] constexpr TimePoint sourceTimeAt(TimePoint t) const noexcept {
        const Ticks offset = t.ticks - start.ticks;
        return TimePoint{sourceIn.ticks + offset * speed.num / speed.den};
    }
    [[nodiscard]] constexpr Duration sourceDuration() const noexcept { return Duration{duration.ticks * speed.num / speed.den}; }
    [[nodiscard]] constexpr TimePoint sourceOut() const noexcept { return sourceIn + sourceDuration(); }
    // Media duration -> sequence duration at this clip's speed.
    [[nodiscard]] constexpr Duration timelineDurationFor(Duration source) const noexcept { return Duration{source.ticks * speed.den / speed.num}; }

    friend bool operator==(const Clip&, const Clip&) = default;
};

// Tracks are stored in display order, top to bottom (V2, V1, A1, A2).
// Video compositing order is bottom-up in display terms: V1 under V2.
struct Sequence {
    std::vector<Track> tracks;
    std::vector<Clip> clips;

    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept { for (const auto& t : tracks) if (t.id == id) return &t; return nullptr; }
    [[nodiscard]] Track* findTrack(TrackId id) noexcept { for (auto& t : tracks) if (t.id == id) return &t; return nullptr; }
    [[nodiscard]] const Clip* findClip(ClipId id) const noexcept { for (const auto& c : clips) if (c.id == id) return &c; return nullptr; }
    [[nodiscard]] Clip* findClip(ClipId id) noexcept { for (auto& c : clips) if (c.id == id) return &c; return nullptr; }
    [[nodiscard]] int trackIndex(TrackId id) const noexcept { for (std::size_t i = 0; i < tracks.size(); ++i) if (tracks[i].id == id) return static_cast<int>(i); return -1; }

    // Clips on one track, sorted by start.
    [[nodiscard]] std::vector<const Clip*> clipsOnTrack(TrackId id) const;
    // The clip covering t on the track, if any.
    [[nodiscard]] const Clip* clipAt(TrackId id, TimePoint t) const noexcept;
    // End of the last clip (zero when empty).
    [[nodiscard]] Duration duration() const noexcept;
    // Video tracks bottom-to-top (compositing order); audio tracks top-to-bottom.
    [[nodiscard]] std::vector<const Track*> videoTracksBottomUp() const;
    [[nodiscard]] std::vector<const Track*> audioTracks() const;
    // First track of the kind counting from the timeline centre (V1 / A1).
    [[nodiscard]] const Track* primaryTrack(TrackKind kind) const noexcept;

    // Two video and two audio tracks with ids 1..4 (ids are bumped by Document).
    static Sequence withDefaultTracks();
};

}  // namespace lf::project
