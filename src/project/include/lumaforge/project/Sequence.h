#pragma once

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/core/VisualParams.h"
#include "lumaforge/project/Animation.h"
#include "lumaforge/project/Ids.h"
#include "lumaforge/project/Transitions.h"

#include <algorithm>
#include <cstdint>
#include <optional>
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
    int height = 0;            // lane height in the timeline, pixels; 0 = default for the kind
    float volume = 1.0f;       // audio tracks: linear gain applied to the track's mix
    float pan = 0.0f;          // audio tracks: -1 left .. 1 right
    bool subtitles = false;    // video track holding subtitle cues (text clips); see Subtitles.h
    friend bool operator==(const Track&, const Track&) = default;
};

// Solid: a full-frame colour. Adjustment: applies its colour correction,
// effects and mask to everything composited below it on lower tracks.
enum class ClipKind { Video, Audio, Image, Text, Solid, Adjustment };

[[nodiscard]] constexpr bool isVisualClip(ClipKind k) noexcept { return k != ClipKind::Audio; }
// Made by LumaForge itself rather than read from a media file.
[[nodiscard]] constexpr bool isGeneratedClip(ClipKind k) noexcept { return k == ClipKind::Text || k == ClipKind::Solid || k == ClipKind::Adjustment; }

// Per-clip audio processing, applied before the clip's gain and pan.
struct AudioFx {
    float eqLow = 0.0f;            // dB, low shelf at 120 Hz
    float eqMid = 0.0f;            // dB, peak at 1 kHz
    float eqHigh = 0.0f;           // dB, high shelf at 8 kHz
    bool compressor = false;
    float compThreshold = -18.0f;  // dBFS
    float compRatio = 3.0f;        // n:1
    bool gate = false;
    float gateThreshold = -50.0f;  // dBFS: quieter than this is muted
    [[nodiscard]] constexpr bool isIdentity() const noexcept { return eqLow == 0.0f && eqMid == 0.0f && eqHigh == 0.0f && !compressor && !gate; }
    friend constexpr bool operator==(const AudioFx&, const AudioFx&) = default;
};

struct AudioParams {
    float volume = 1.0f;       // linear gain; the UI shows dB
    bool muted = false;
    Duration fadeIn;
    Duration fadeOut;
    float pan = 0.0f;          // -1 left .. 1 right (equal power)
    AudioFx fx;
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
    bool enabled = true;                 // disabled clips stay on the timeline but are not rendered or heard
    bool reverse = false;                // plays its media range backwards
    bool freeze = false;                 // holds the frame at sourceIn (frame hold); silent

    Transform2D transform;
    Crop crop;
    Mask mask;
    ColorAdjust color;
    std::vector<Effect> effects;   // ordered; see applyEffects()
    AudioParams audio;
    TextParams text;
    ColorRgba solidColor{0.0f, 0.0f, 0.0f, 1.0f};   // Solid clips
    std::vector<KeyTrack> animation;   // keyframed properties (see Animation.h); empty = static
    std::optional<Transition> transitionIn;    // visual clips only; see Transitions.h
    std::optional<Transition> transitionOut;

    [[nodiscard]] constexpr TimePoint end() const noexcept { return start + duration; }
    [[nodiscard]] constexpr TimeRange range() const noexcept { return {start, duration}; }
    [[nodiscard]] constexpr bool contains(TimePoint t) const noexcept { return range().contains(t); }

    // Sequence time -> media time (exact integer arithmetic). Reversed clips run
    // from the end of their media range back to sourceIn; frozen ones hold sourceIn.
    [[nodiscard]] constexpr TimePoint sourceTimeAt(TimePoint t) const noexcept {
        if (freeze) return sourceIn;
        const Ticks offset = t.ticks - start.ticks;
        const Ticks forward = offset * speed.num / speed.den;
        if (reverse) return TimePoint{std::max(sourceIn.ticks, sourceIn.ticks + duration.ticks * speed.num / speed.den - 1 - forward)};
        return TimePoint{sourceIn.ticks + forward};
    }
    // Media time -> sequence time (inverse of sourceTimeAt; frozen clips map to their start).
    [[nodiscard]] constexpr TimePoint sequenceTimeAt(TimePoint source) const noexcept {
        if (freeze) return start;
        if (reverse) return TimePoint{start.ticks + (sourceIn.ticks + duration.ticks * speed.num / speed.den - 1 - source.ticks) * speed.den / speed.num};
        return TimePoint{start.ticks + (source.ticks - sourceIn.ticks) * speed.den / speed.num};
    }
    [[nodiscard]] constexpr Duration sourceDuration() const noexcept { return Duration{duration.ticks * speed.num / speed.den}; }
    [[nodiscard]] constexpr TimePoint sourceOut() const noexcept { return sourceIn + sourceDuration(); }
    // Media duration -> sequence duration at this clip's speed.
    [[nodiscard]] constexpr Duration timelineDurationFor(Duration source) const noexcept { return Duration{source.ticks * speed.den / speed.num}; }

    friend bool operator==(const Clip&, const Clip&) = default;
};

// The same clip covering [newStart, newEnd) on the timeline and showing the
// same media at every sequence time it still covers - forward, reversed or
// frozen. Trims, splits and rolls all go through this.
[[nodiscard]] inline Clip retimed(const Clip& c, TimePoint newStart, TimePoint newEnd) noexcept {
    Clip n = c;
    n.start = newStart;
    n.duration = newEnd - newStart;
    if (c.freeze) return n;
    if (!c.reverse) {
        n.sourceIn = TimePoint{c.sourceIn.ticks + (newStart.ticks - c.start.ticks) * c.speed.num / c.speed.den};
    } else {
        // Reversed: the end of the media range plays first. Keep the top sample
        // aligned with the same sequence time, then derive sourceIn.
        const Ticks top = c.sourceIn.ticks + c.duration.ticks * c.speed.num / c.speed.den;
        const Ticks newTop = top + (c.start.ticks - newStart.ticks) * c.speed.num / c.speed.den;
        n.sourceIn = TimePoint{newTop - n.duration.ticks * c.speed.num / c.speed.den};
    }
    return n;
}

// ---- keyframe helpers (Animation.cpp). `source` is media time; values are in
// model units (fractions for crop/opacity, 1 = 100% for scale, linear volume).
[[nodiscard]] const KeyTrack* findKeyTrack(const Clip& c, AnimProp prop) noexcept;
[[nodiscard]] inline bool isAnimated(const Clip& c, AnimProp prop) noexcept { return findKeyTrack(c, prop) != nullptr; }
[[nodiscard]] float baseValue(const Clip& c, AnimProp prop) noexcept;
void setBaseValue(Clip& c, AnimProp prop, float value) noexcept;
// The value shown at `source`: the animation when keyed, else the static value.
[[nodiscard]] float valueAt(const Clip& c, AnimProp prop, TimePoint source) noexcept;
[[nodiscard]] const Keyframe* keyAt(const Clip& c, AnimProp prop, TimePoint source) noexcept;
// Adds or replaces the key at `source` (interp defaults to that of the key before it).
void setKey(Clip& c, AnimProp prop, TimePoint source, float value, std::optional<Interp> interp = std::nullopt);
// Removes the key at `source`; removing the last key leaves the property static at its value.
bool removeKey(Clip& c, AnimProp prop, TimePoint source);
// Drops every key of the property, keeping the value it has at `source`.
void clearAnimation(Clip& c, AnimProp prop, TimePoint source);
// An edit from the UI: sets a key at `source` when the property is animated, else the static value.
void setValueAt(Clip& c, AnimProp prop, TimePoint source, float value);
// Applies animated transform / crop properties at `source` on top of the static values.
void animatedTransform(const Clip& c, TimePoint source, Transform2D& transform, Crop& crop) noexcept;
// Media time for keyframing at a sequence time, clamped into the clip.
[[nodiscard]] TimePoint animationTime(const Clip& c, TimePoint sequenceTime) noexcept;
// Every key time of every property, sorted, unique (media time).
[[nodiscard]] std::vector<TimePoint> keyTimes(const Clip& c);

// ---- transitions (Transitions.cpp)
// One effective transition on a track: [start, end) in sequence time. `from`
// is the outgoing clip and `to` the incoming one; either may be null (a
// transition from / to whatever lies underneath). `owner` carries the
// Transition (transitionIn when atStart, else transitionOut).
struct TransitionWindow {
    TimePoint start;
    TimePoint end;
    const Clip* from = nullptr;
    const Clip* to = nullptr;
    const Clip* owner = nullptr;
    bool atStart = true;
    Transition transition;
    [[nodiscard]] double progressAt(TimePoint t) const noexcept {
        const Ticks len = end.ticks - start.ticks;
        return len > 0 ? static_cast<double>(t.ticks - start.ticks) / static_cast<double>(len) : 1.0;
    }
};
// Windows for the clips of one track, given sorted by start. Durations are
// clamped so a window never extends beyond either clip it involves.
[[nodiscard]] std::vector<TransitionWindow> transitionWindows(const std::vector<const Clip*>& trackClipsSorted);
// Longest duration a transition at that edge of the clip can have.
[[nodiscard]] Duration maxTransitionDuration(const std::vector<const Clip*>& trackClipsSorted, const Clip& clip, bool atStart);
// The clip touching `clip`'s start (atStart) or end on its track, if any.
[[nodiscard]] const Clip* touchingNeighbour(const std::vector<const Clip*>& trackClipsSorted, const Clip& clip, bool atStart);

// A named point on the sequence (chapters, notes, sync points).
struct Marker {
    std::uint64_t id = 0;      // unique within the sequence
    TimePoint time;
    std::string name;
    int color = 0;             // index into the UI's marker palette
    std::string note;
    friend bool operator==(const Marker&, const Marker&) = default;
};

// Tracks are stored in display order, top to bottom (V2, V1, A1, A2).
// Video compositing order is bottom-up in display terms: V1 under V2.
struct Sequence {
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    TimePoint markIn{-1};    // In/Out range (loop, lift/extract, export range); -1 = unset
    TimePoint markOut{-1};
    std::vector<Marker> markers;   // sorted by time
    float masterVolume = 1.0f;     // linear gain of the final mix
    bool masterLimiter = true;     // keep the final mix below 0 dBFS without hard clipping
    float masterCeilingDb = -0.3f; // limiter ceiling (dBFS); e.g. -1 for streaming/social delivery headroom

    [[nodiscard]] bool hasInOut() const noexcept { return markIn.ticks >= 0 && markOut.ticks > markIn.ticks; }

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
