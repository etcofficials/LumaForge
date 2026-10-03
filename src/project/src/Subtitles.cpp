#include "lumaforge/project/Subtitles.h"

#include "lumaforge/project/TimelineOps.h"
#include "lumaforge/project/TitlePresets.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace lf::project {

namespace {

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "hh:mm:ss,mmm" / "hh:mm:ss.mmm" / "mm:ss.mmm" -> time; false when malformed.
bool parseTimestamp(const std::string& raw, TimePoint& out) {
    std::string s = trim(raw);
    std::replace(s.begin(), s.end(), ',', '.');
    int h = 0, m = 0, sec = 0, ms = 0;
    const int colons = static_cast<int>(std::count(s.begin(), s.end(), ':'));
    int n = 0;
    if (colons == 2) n = std::sscanf(s.c_str(), "%d:%d:%d.%d", &h, &m, &sec, &ms);
    else if (colons == 1) { n = std::sscanf(s.c_str(), "%d:%d.%d", &m, &sec, &ms); if (n >= 2) ++n; }
    if (n < 3 || m < 0 || m > 59 || sec < 0 || sec > 59 || ms < 0) return false;
    // Milliseconds field may be written with fewer digits (".5" = 500 ms).
    const auto dot = s.find('.');
    if (dot != std::string::npos) {
        const std::size_t digits = s.size() - dot - 1;
        if (digits == 1) ms *= 100;
        else if (digits == 2) ms *= 10;
    }
    out = TimePoint{(static_cast<Ticks>(h) * 3600 + m * 60 + sec) * kFlicksPerSecond + static_cast<Ticks>(ms) * (kFlicksPerSecond / 1000)};
    return true;
}

std::string formatTimestamp(TimePoint t, char msSeparator) {
    const Ticks totalMs = std::max<Ticks>(0, t.ticks) / (kFlicksPerSecond / 1000);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld%c%03lld", static_cast<long long>(totalMs / 3600000), static_cast<long long>((totalMs / 60000) % 60),
                  static_cast<long long>((totalMs / 1000) % 60), msSeparator, static_cast<long long>(totalMs % 1000));
    return buf;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// Strips simple markup (<i>, </b>, <font ...>, {\an8}) that our text clips do not render.
std::string stripTags(const std::string& s) {
    std::string out;
    bool inAngle = false, inBrace = false;
    for (char c : s) {
        if (c == '<') { inAngle = true; continue; }
        if (c == '>' && inAngle) { inAngle = false; continue; }
        if (c == '{' && !inAngle) { inBrace = true; continue; }
        if (c == '}' && inBrace) { inBrace = false; continue; }
        if (!inAngle && !inBrace) out += c;
    }
    return out;
}

}  // namespace

Result<std::vector<SubtitleCue>> parseSubtitles(const std::string& text) {
    const auto all = lines(text);
    std::vector<SubtitleCue> cues;
    std::size_t i = 0;
    const bool vtt = !all.empty() && trim(all[0]).rfind("WEBVTT", 0) == 0;
    if (vtt) {
        // Skip the header block.
        while (i < all.size() && !trim(all[i]).empty()) ++i;
    }
    while (i < all.size()) {
        // Find the next timing line ("start --> end ...").
        while (i < all.size() && all[i].find("-->") == std::string::npos) {
            if (vtt && (trim(all[i]).rfind("NOTE", 0) == 0 || trim(all[i]).rfind("STYLE", 0) == 0)) {
                while (i < all.size() && !trim(all[i]).empty()) ++i;   // comment / style block
            }
            ++i;
        }
        if (i >= all.size()) break;
        const std::string& timing = all[i];
        const auto arrow = timing.find("-->");
        std::string endPart = timing.substr(arrow + 3);
        endPart = trim(endPart);
        if (const auto space = endPart.find_first_of(" \t"); space != std::string::npos) endPart = endPart.substr(0, space);   // VTT cue settings
        SubtitleCue cue;
        const bool ok = parseTimestamp(timing.substr(0, arrow), cue.start) && parseTimestamp(endPart, cue.end) && cue.end > cue.start;
        ++i;
        std::string body;
        while (i < all.size() && !trim(all[i]).empty()) {
            if (!body.empty()) body += '\n';
            body += stripTags(trim(all[i]));
            ++i;
        }
        if (ok && !trim(body).empty()) {
            cue.text = body;
            cues.push_back(std::move(cue));
        }
    }
    if (cues.empty()) return Error{"No subtitles were found in the file (expected SubRip .srt or WebVTT .vtt)."};
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
    return cues;
}

std::string writeSrt(const std::vector<SubtitleCue>& cues) {
    std::string out;
    int n = 1;
    for (const auto& c : cues) {
        out += std::to_string(n++) + "\n" + formatTimestamp(c.start, ',') + " --> " + formatTimestamp(c.end, ',') + "\n" + c.text + "\n\n";
    }
    return out;
}

std::string writeVtt(const std::vector<SubtitleCue>& cues) {
    std::string out = "WEBVTT\n\n";
    for (const auto& c : cues) out += formatTimestamp(c.start, '.') + " --> " + formatTimestamp(c.end, '.') + "\n" + c.text + "\n\n";
    return out;
}

const Track* subtitleTrack(const Sequence& seq) noexcept {
    for (const auto& t : seq.tracks) if (t.kind == TrackKind::Video && t.subtitles) return &t;
    return nullptr;
}

std::vector<SubtitleCue> subtitleCues(const Sequence& seq, std::optional<TimeRange> range) {
    std::vector<SubtitleCue> cues;
    for (const auto& t : seq.tracks) {
        if (t.kind != TrackKind::Video || !t.subtitles) continue;
        for (const Clip* c : seq.clipsOnTrack(t.id)) {
            if (c->kind != ClipKind::Text || !c->enabled) continue;
            SubtitleCue cue{c->start, c->end(), c->text.text};
            if (range) {
                if (!(cue.start < range->end() && range->start < cue.end)) continue;
                cue.start = TimePoint{std::max(cue.start.ticks, range->start.ticks) - range->start.ticks};
                cue.end = TimePoint{std::min(cue.end.ticks, range->end().ticks) - range->start.ticks};
            }
            cues.push_back(std::move(cue));
        }
    }
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
    return cues;
}

namespace {
// Track to put cues on: the existing subtitle track, or a new one at the top (added to `edit`).
TrackId subtitleTrackFor(Document& doc, SequenceEdit& edit) {
    if (const Track* t = subtitleTrack(doc.project().sequence)) return t->id;
    edit = ops::addTrack(doc, TrackKind::Video, 0);
    if (edit.tracksAdded.empty()) return TrackId::Invalid;
    Track& added = edit.tracksAdded.front().second;
    added.subtitles = true;
    added.name = "Subtitles";
    return added.id;
}

Clip cueClip(Document& doc, TrackId track, const SubtitleCue& cue, TimePoint at) {
    const auto& settings = doc.project().settings;
    Clip c = makeTitleClip(1, settings.width, settings.height);   // the Subtitle look
    c.id = doc.allocateClipId();
    c.track = track;
    c.name = "Subtitle";
    c.text.text = cue.text;
    c.start = TimePoint{cue.start.ticks + at.ticks};
    c.duration = cue.end - cue.start;
    return c;
}
}  // namespace

SequenceEdit importSubtitles(Document& doc, const std::vector<SubtitleCue>& cues, TimePoint at) {
    SequenceEdit edit;
    const TrackId track = subtitleTrackFor(doc, edit);
    if (!isValid(track)) return {};
    const bool newTrack = !edit.tracksAdded.empty();
    for (const auto& cue : cues) {
        Clip c = cueClip(doc, track, cue, at);
        // Replace what the cue overlaps on an existing track; cues themselves never overlap
        // after the previous one is trimmed (SRT files sometimes overlap by a frame).
        if (!newTrack) ops::overwriteRange(doc, track, c.range(), {}, edit);
        for (auto& prev : edit.added) {
            if (prev.track == track && prev.start < c.start && prev.end() > c.start) prev.duration = c.start - prev.start;
        }
        edit.add(std::move(c));
    }
    return edit;
}

SequenceEdit addSubtitle(Document& doc, TimePoint at) {
    return importSubtitles(doc, {SubtitleCue{TimePoint::zero(), TimePoint{2 * kFlicksPerSecond}, "Subtitle"}}, at);
}

}  // namespace lf::project
