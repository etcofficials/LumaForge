#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/TimelineCommands.h"

#include <string>
#include <vector>

// Subtitles live on video tracks flagged `subtitles`: every cue is a text clip
// with the Subtitle look, so it renders (burns in) through the normal path and
// the track's visibility switch leaves it out of the picture. SRT and WebVTT
// convert to and from those clips.
namespace lf::project {

struct SubtitleCue {
    TimePoint start;
    TimePoint end;
    std::string text;   // UTF-8, lines separated by '\n'
    friend bool operator==(const SubtitleCue&, const SubtitleCue&) = default;
};

// Parses SubRip (.srt) or WebVTT (.vtt) text; the format is detected from
// the content. Cues come back sorted by start; malformed blocks are skipped.
Result<std::vector<SubtitleCue>> parseSubtitles(const std::string& utf8Text);
[[nodiscard]] std::string writeSrt(const std::vector<SubtitleCue>& cues);
[[nodiscard]] std::string writeVtt(const std::vector<SubtitleCue>& cues);

// The first subtitle track (top-most), if any.
[[nodiscard]] const Track* subtitleTrack(const Sequence& seq) noexcept;
// Cues from the text clips on every subtitle track, in time order; with a
// range, clipped to it and shifted so the range starts at 0.
[[nodiscard]] std::vector<SubtitleCue> subtitleCues(const Sequence& seq, std::optional<TimeRange> range = std::nullopt);

// One undoable edit: creates a subtitle track at the top when there is none,
// then places the cues (offset by `at`) on it, replacing what overlaps.
SequenceEdit importSubtitles(Document& doc, const std::vector<SubtitleCue>& cues, TimePoint at = TimePoint::zero());
// A 2 s cue at `at` on the subtitle track (created when missing); text "Subtitle".
SequenceEdit addSubtitle(Document& doc, TimePoint at);

}  // namespace lf::project
