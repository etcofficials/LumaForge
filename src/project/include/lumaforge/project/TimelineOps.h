#pragma once

#include "lumaforge/project/Document.h"
#include "lumaforge/project/TimelineCommands.h"

#include <optional>
#include <vector>

// Pure timeline editing logic. Every function computes a SequenceEdit from
// the current project state without mutating it; the caller wraps the edit
// in a SequenceEditCommand. Keeping this Qt-free and side-effect-free is what
// makes trimming, overwriting and rippling unit-testable with exact numbers.
namespace lf::project::ops {

inline constexpr Duration kDefaultImageDuration = Duration{kFlicksPerSecond * 5};
inline constexpr Duration kDefaultTextDuration = Duration{kFlicksPerSecond * 5};

// Where the media should land when added to the timeline.
struct Placement {
    TrackId videoTrack = TrackId::Invalid;   // Invalid = don't create a visual clip
    TrackId audioTrack = TrackId::Invalid;   // Invalid = don't create an audio clip
    TimePoint start;
};

// Clips (video/image + linked audio) for one media item. Ids are allocated
// from the document; nothing is inserted.
std::vector<Clip> clipsForMedia(Document& doc, const MediaItem& item, const Placement& placement, Duration imageDuration = kDefaultImageDuration);

// Default landing spot: V1/A1 at `at` (or the sequence end when at < 0).
Placement defaultPlacement(const Project& project, const MediaItem& item, TimePoint at);

// Makes room for `range` on `track`: clips overlapping it are trimmed, split
// or removed (overwrite edit). Clips in `ignore` are left alone (the clips
// being moved). New right-hand pieces get ids from `doc`.
void overwriteRange(Document& doc, TrackId track, TimeRange range, const std::vector<ClipId>& ignore, SequenceEdit& edit);

// Inserts clips with overwrite semantics at their own start/track.
SequenceEdit insertClips(Document& doc, std::vector<Clip> clips);

// Moves clips by a time delta and/or to other tracks (trackDelta in display
// rows; a clip only moves onto a track of its own kind). Overwrites at the
// destination. Returns nullopt when the move is impossible (negative start,
// no compatible track).
std::optional<SequenceEdit> moveClips(Document& doc, const std::vector<ClipId>& ids, Duration delta, int trackDelta);

enum class Edge { Start, End };
// Trims one edge of a clip to `time` (sequence time). Clamped so the clip
// keeps at least one frame and never runs past the media. Linked clips are
// trimmed together when `withLinked` is set. Overwrites clips it grows into.
SequenceEdit trimClip(Document& doc, ClipId id, Edge edge, TimePoint time, bool withLinked = true);

// Splits the clip (and its linked partner) at `time`. Empty when time is not strictly inside.
SequenceEdit splitClips(Document& doc, const std::vector<ClipId>& ids, TimePoint time);
// Splits every clip under the playhead on unlocked tracks.
SequenceEdit splitAt(Document& doc, TimePoint time);

// Removes clips; with ripple, later clips on every track shift left to close the gap.
SequenceEdit removeClips(Document& doc, const std::vector<ClipId>& ids, bool ripple);

enum class PasteMode { Overwrite, Insert };
// Copies of the clips (with links preserved between copies) placed so that the
// earliest copy starts at `at`, on the same tracks; new ids from `doc`.
// Overwrite replaces what lies underneath; Insert first shifts everything at
// or after `at` on the affected tracks right by the span of the group.
SequenceEdit pasteClips(Document& doc, const std::vector<Clip>& source, TimePoint at, PasteMode mode = PasteMode::Overwrite);
// Copies inserted (never overwriting) immediately after the group.
SequenceEdit duplicateClips(Document& doc, const std::vector<ClipId>& ids);
// Fresh copies (new ids, links remapped among the copies) shifted by `delta`.
std::vector<Clip> cloneClips(Document& doc, const std::vector<Clip>& source, Duration delta);

// Changes playback speed (num/den > 0) of the clips and their linked partners.
// The clip keeps its start and sourceIn; its timeline duration follows the
// same source range at the new speed, overwriting neighbours when it grows.
SequenceEdit setSpeed(Document& doc, const std::vector<ClipId>& ids, Rational speed);

// Ripple trim: trims one edge (like trimClip) and shifts every clip that
// starts at or after the clip's original end on unlocked tracks by the change
// in length, so nothing after the edit overlaps or leaves a gap. A shortening
// is limited so no shifted clip collides with an unshifted one.
SequenceEdit rippleTrim(Document& doc, ClipId id, Edge edge, TimePoint time);

// Roll edit: moves the cut between `left` (ending at the cut) and `right`
// (starting there) on one track to `time`; the total length is unchanged.
// Clamped to a frame per side and to the media each clip has available.
// Linked partners that share the same cut roll with them.
SequenceEdit rollEdit(Document& doc, ClipId left, ClipId right, TimePoint time);

// The clips directly left and right of the cut nearest to `t` on `track`
// within `tolerance` (both must exist and touch).
std::optional<std::pair<ClipId, ClipId>> cutAt(const Sequence& seq, TrackId track, TimePoint t, Duration tolerance);

// Lift: removes everything inside `range` on unlocked tracks (gap stays).
SequenceEdit liftRange(Document& doc, TimeRange range);
// Extract: lift + every clip after the range on unlocked tracks moves left by its length.
SequenceEdit extractRange(Document& doc, TimeRange range);

// Every clip start/end on the sequence plus zero, sorted, unique.
std::vector<TimePoint> editPoints(const Sequence& seq);
// Nearest edit point strictly before / after t (nullopt at the ends).
std::optional<TimePoint> previousEditPoint(const Sequence& seq, TimePoint t);
std::optional<TimePoint> nextEditPoint(const Sequence& seq, TimePoint t);

// ---- tracks. Video tracks are always displayed above audio tracks; default
// names ("V1", "A2") are renumbered by position after structural edits,
// custom names are kept.

// New track of `kind` inserted at display index `index` (clamped into the kind's
// region; -1 = above the top video track / below the last audio track).
SequenceEdit addTrack(Document& doc, TrackKind kind, int index = -1);
// Removes a track and its clips. Refused (empty) for the last track of a kind.
SequenceEdit removeTrack(Document& doc, TrackId id);
// A copy of the track (settings + clips with new ids) directly above it.
SequenceEdit duplicateTrack(Document& doc, TrackId id);
// Moves a track up (-1) or down (+1) within its kind's region.
SequenceEdit moveTrack(Document& doc, TrackId id, int delta);
SequenceEdit renameTrack(Document& doc, TrackId id, const std::string& name);
SequenceEdit setTrackHeight(Document& doc, TrackId id, int height);
// Display names after renumbering (exposed for tests).
std::vector<Track> renumbered(std::vector<Track> tracks);

// Clip ids plus their linked partners (deduplicated).
std::vector<ClipId> withLinked(const Sequence& seq, const std::vector<ClipId>& ids);

// Snapping: interesting times = clip edges (excluding `ignore`), the playhead and zero.
std::vector<TimePoint> snapTargets(const Sequence& seq, const std::vector<ClipId>& ignore, TimePoint playhead);
// Nearest target within tolerance, else nullopt.
std::optional<TimePoint> snap(TimePoint t, const std::vector<TimePoint>& targets, Duration tolerance);

// Media duration in sequence time for a clip's source (images/text: unbounded).
std::optional<Duration> mediaDuration(const Project& project, const Clip& clip);

// The sequence's clips as they will be once `edit` executes (for previews).
std::vector<Clip> applied(const Sequence& seq, const SequenceEdit& edit);

}  // namespace lf::project::ops
