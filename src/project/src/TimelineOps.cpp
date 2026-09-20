#include "lumaforge/project/TimelineOps.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>

namespace lf::project::ops {

namespace {

// The sequence's clips with a pending edit applied: what the timeline will
// look like once the edit executes. Used so successive overwrites inside one
// edit see each other's results.
std::vector<Clip> effectiveClips(const Sequence& seq, const SequenceEdit& edit) {
    std::vector<Clip> out;
    out.reserve(seq.clips.size() + edit.added.size());
    for (const auto& c : seq.clips) {
        if (std::find(edit.removed.begin(), edit.removed.end(), c.id) != edit.removed.end()) continue;
        const Clip* m = nullptr;
        for (const auto& mod : edit.modified) if (mod.id == c.id) { m = &mod; break; }
        out.push_back(m ? *m : c);
    }
    for (const auto& a : edit.added) out.push_back(a);
    return out;
}

bool contains(const std::vector<ClipId>& ids, ClipId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// Right-hand piece of a clip cut at `time` with a fresh id.
Clip rightPiece(const Clip& c, TimePoint time, ClipId newId) {
    Clip right = c;
    right.id = newId;
    right.link = ClipId::Invalid;
    right.start = time;
    right.sourceIn = c.sourceTimeAt(time);
    right.duration = c.end() - time;
    right.audio.fadeIn = Duration::zero();
    return right;
}

Clip leftPiece(const Clip& c, TimePoint time) {
    Clip left = c;
    left.duration = time - c.start;
    left.audio.fadeOut = Duration::zero();
    return left;
}

using Pieces = std::vector<std::pair<ClipId, ClipId>>;   // (origin, new right piece)

void overwriteRangeImpl(Document& doc, TrackId track, TimeRange range, const std::vector<ClipId>& ignore, SequenceEdit& edit, Pieces& pieces) {
    if (range.isEmpty()) return;
    const auto& seq = doc.project().sequence;
    const auto* t = seq.findTrack(track);
    if (!t || t->locked) return;
    const TimePoint rs = range.start, re = range.end();
    for (const Clip& c : effectiveClips(seq, edit)) {
        if (c.track != track || contains(ignore, c.id)) continue;
        if (!c.range().overlaps(range)) continue;
        if (c.start >= rs && c.end() <= re) {
            edit.remove(c.id);
        } else if (c.start < rs && c.end() > re) {
            Clip right = rightPiece(c, re, doc.allocateClipId());
            edit.modify(leftPiece(c, rs));
            pieces.emplace_back(c.id, right.id);
            edit.add(std::move(right));
        } else if (c.start < rs) {
            Clip left = c;
            left.duration = rs - c.start;
            edit.modify(std::move(left));
        } else {
            Clip right = c;
            right.sourceIn = c.sourceTimeAt(re);
            right.duration = c.end() - re;
            right.start = re;
            edit.modify(std::move(right));
        }
    }
}

// Right-hand pieces cut from linked partners become linked to each other.
void relinkPieces(const Sequence& seq, const Pieces& pieces, SequenceEdit& edit) {
    auto findAdded = [&](ClipId id) -> Clip* { for (auto& a : edit.added) if (a.id == id) return &a; return nullptr; };
    for (const auto& [originA, pieceA] : pieces) {
        const Clip* a = seq.findClip(originA);
        if (!a || !isValid(a->link)) continue;
        for (const auto& [originB, pieceB] : pieces) {
            if (originB != a->link) continue;
            Clip* pa = findAdded(pieceA);
            Clip* pb = findAdded(pieceB);
            if (pa && pb && pa->start == pb->start) { pa->link = pb->id; pb->link = pa->id; }
        }
    }
}

const Track* trackAtOffset(const Sequence& seq, TrackId from, int delta, TrackKind kind) {
    const int idx = seq.trackIndex(from);
    if (idx < 0) return nullptr;
    const int target = idx + delta;
    if (target < 0 || target >= static_cast<int>(seq.tracks.size())) return nullptr;
    const Track& t = seq.tracks[static_cast<std::size_t>(target)];
    return t.kind == kind ? &t : nullptr;
}

}  // namespace

// ---------------------------------------------------------------- creation

Placement defaultPlacement(const Project& project, const MediaItem& item, TimePoint at) {
    Placement p;
    const auto& seq = project.sequence;
    const bool visual = item.info.hasVideo() || item.info.type == media::MediaType::Image;
    if (visual) if (const auto* v = seq.primaryTrack(TrackKind::Video)) p.videoTrack = v->id;
    if (item.info.hasAudio()) if (const auto* a = seq.primaryTrack(TrackKind::Audio)) p.audioTrack = a->id;
    p.start = at.ticks >= 0 ? at : TimePoint{seq.duration().ticks};
    return p;
}

std::vector<Clip> clipsForMedia(Document& doc, const MediaItem& item, const Placement& placement, Duration imageDuration) {
    std::vector<Clip> out;
    const auto& info = item.info;
    const bool isImage = info.type == media::MediaType::Image;
    const Duration length = isImage ? imageDuration : info.duration;
    if (length.ticks <= 0) return out;

    Clip* visual = nullptr;
    if (isValid(placement.videoTrack) && (info.hasVideo() || isImage)) {
        Clip c;
        c.id = doc.allocateClipId();
        c.track = placement.videoTrack;
        c.kind = isImage ? ClipKind::Image : ClipKind::Video;
        c.media = item.id;
        c.name = item.displayName;
        c.start = placement.start;
        c.duration = length;
        out.push_back(std::move(c));
        visual = &out.back();
    }
    if (isValid(placement.audioTrack) && info.hasAudio() && !isImage) {
        Clip a;
        a.id = doc.allocateClipId();
        a.track = placement.audioTrack;
        a.kind = ClipKind::Audio;
        a.media = item.id;
        a.name = item.displayName;
        a.start = placement.start;
        a.duration = length;
        if (visual) { visual->link = a.id; a.link = visual->id; }
        out.push_back(std::move(a));
    }
    return out;
}

// ---------------------------------------------------------------- overwrite / insert

void overwriteRange(Document& doc, TrackId track, TimeRange range, const std::vector<ClipId>& ignore, SequenceEdit& edit) {
    Pieces pieces;
    overwriteRangeImpl(doc, track, range, ignore, edit, pieces);
    relinkPieces(doc.project().sequence, pieces, edit);
}

SequenceEdit insertClips(Document& doc, std::vector<Clip> clips) {
    SequenceEdit edit;
    std::vector<ClipId> ids;
    for (const auto& c : clips) ids.push_back(c.id);
    Pieces pieces;
    for (const auto& c : clips) overwriteRangeImpl(doc, c.track, c.range(), ids, edit, pieces);
    relinkPieces(doc.project().sequence, pieces, edit);
    for (auto& c : clips) edit.add(std::move(c));
    return edit;
}

// ---------------------------------------------------------------- move

std::optional<SequenceEdit> moveClips(Document& doc, const std::vector<ClipId>& ids, Duration delta, int trackDelta) {
    const auto& seq = doc.project().sequence;
    std::vector<const Clip*> clips;
    for (auto id : ids) {
        const Clip* c = seq.findClip(id);
        if (!c) continue;
        const Track* t = seq.findTrack(c->track);
        if (!t || t->locked) continue;
        clips.push_back(c);
    }
    if (clips.empty()) return std::nullopt;

    // Never move before zero: clamp the delta for the whole group.
    Ticks minStart = clips.front()->start.ticks;
    for (const auto* c : clips) minStart = std::min(minStart, c->start.ticks);
    if (minStart + delta.ticks < 0) delta = Duration{-minStart};

    // Track change is all-or-nothing: every clip needs a compatible, unlocked target.
    std::map<ClipId, TrackId> targetTrack;
    if (trackDelta != 0) {
        for (const auto* c : clips) {
            const Track* kind = seq.findTrack(c->track);
            const Track* target = trackAtOffset(seq, c->track, trackDelta, kind->kind);
            if (!target || target->locked) { trackDelta = 0; break; }
            targetTrack[c->id] = target->id;
        }
    }
    if (delta.isZero() && trackDelta == 0) return SequenceEdit{};

    SequenceEdit edit;
    std::vector<ClipId> moving;
    for (const auto* c : clips) moving.push_back(c->id);
    std::vector<Clip> moved;
    for (const auto* c : clips) {
        Clip m = *c;
        m.start = c->start + delta;
        if (trackDelta != 0) m.track = targetTrack[c->id];
        moved.push_back(m);
        edit.modify(m);
    }
    Pieces pieces;
    for (const auto& m : moved) overwriteRangeImpl(doc, m.track, m.range(), moving, edit, pieces);
    relinkPieces(seq, pieces, edit);
    return edit;
}

// ---------------------------------------------------------------- trim

std::optional<Duration> mediaDuration(const Project& project, const Clip& clip) {
    if (clip.kind == ClipKind::Text || clip.kind == ClipKind::Image) return std::nullopt;
    const MediaItem* item = project.findMedia(clip.media);
    if (!item || item->info.duration.ticks <= 0) return std::nullopt;
    return item->info.duration;
}

namespace {

// Trims one clip; returns the new clip (or nullopt if nothing changed).
std::optional<Clip> trimmed(const Project& project, const Clip& c, Edge edge, TimePoint time) {
    const Duration frame = frameDuration(project.settings.frameRate);
    Clip n = c;
    if (edge == Edge::Start) {
        Ticks lo = 0;
        // Cannot reveal media before its first sample.
        lo = std::max<Ticks>(lo, c.start.ticks - c.timelineDurationFor(Duration{c.sourceIn.ticks}).ticks);
        const Ticks hi = c.end().ticks - frame.ticks;
        const Ticks ns = std::clamp(time.ticks, std::min(lo, hi), hi);
        n.start = TimePoint{ns};
        n.sourceIn = c.sourceTimeAt(n.start);
        n.duration = c.end() - n.start;
    } else {
        Ticks hi = std::numeric_limits<Ticks>::max() / 4;
        if (auto md = mediaDuration(project, c)) {
            const Duration remaining = *md - Duration{c.sourceIn.ticks};
            hi = c.start.ticks + c.timelineDurationFor(remaining).ticks;
        }
        const Ticks lo = c.start.ticks + frame.ticks;
        const Ticks ne = std::clamp(time.ticks, lo, std::max(lo, hi));
        n.duration = TimePoint{ne} - c.start;
    }
    if (n.start == c.start && n.duration == c.duration) return std::nullopt;
    return n;
}

}  // namespace

SequenceEdit trimClip(Document& doc, ClipId id, Edge edge, TimePoint time, bool withLinkedClips) {
    SequenceEdit edit;
    const auto& project = doc.project();
    const auto& seq = project.sequence;
    const Clip* c = seq.findClip(id);
    if (!c) return edit;
    if (const Track* t = seq.findTrack(c->track); !t || t->locked) return edit;

    std::vector<std::pair<const Clip*, TimePoint>> jobs{{c, time}};
    if (withLinkedClips && isValid(c->link)) {
        if (const Clip* p = seq.findClip(c->link)) {
            const Track* pt = seq.findTrack(p->track);
            const bool aligned = edge == Edge::Start ? p->start == c->start : p->end() == c->end();
            if (pt && !pt->locked && aligned) jobs.emplace_back(p, time);
        }
    }
    std::vector<ClipId> ignore;
    for (const auto& j : jobs) ignore.push_back(j.first->id);
    Pieces pieces;
    for (const auto& [clip, t] : jobs) {
        auto n = trimmed(project, *clip, edge, t);
        if (!n) continue;
        // Growing into neighbours overwrites them.
        if (n->start < clip->start) overwriteRangeImpl(doc, clip->track, {n->start, clip->start - n->start}, ignore, edit, pieces);
        if (n->end() > clip->end()) overwriteRangeImpl(doc, clip->track, {clip->end(), n->end() - clip->end()}, ignore, edit, pieces);
        edit.modify(*n);
    }
    relinkPieces(seq, pieces, edit);
    return edit;
}

// ---------------------------------------------------------------- split

SequenceEdit splitClips(Document& doc, const std::vector<ClipId>& ids, TimePoint time) {
    SequenceEdit edit;
    const auto& seq = doc.project().sequence;
    Pieces pieces;
    for (auto id : withLinked(seq, ids)) {
        const Clip* c = seq.findClip(id);
        if (!c) continue;
        if (const Track* t = seq.findTrack(c->track); !t || t->locked) continue;
        if (!(time > c->start && time < c->end())) continue;
        Clip right = rightPiece(*c, time, doc.allocateClipId());
        edit.modify(leftPiece(*c, time));
        pieces.emplace_back(c->id, right.id);
        edit.add(std::move(right));
    }
    relinkPieces(seq, pieces, edit);
    return edit;
}

SequenceEdit splitAt(Document& doc, TimePoint time) {
    std::vector<ClipId> ids;
    for (const auto& c : doc.project().sequence.clips) if (time > c.start && time < c.end()) ids.push_back(c.id);
    return splitClips(doc, ids, time);
}

// ---------------------------------------------------------------- remove

SequenceEdit removeClips(Document& doc, const std::vector<ClipId>& ids, bool ripple) {
    SequenceEdit edit;
    const auto& seq = doc.project().sequence;
    std::vector<TimeRange> gaps;
    for (auto id : ids) {
        const Clip* c = seq.findClip(id);
        if (!c) continue;
        if (const Track* t = seq.findTrack(c->track); !t || t->locked) continue;
        edit.remove(c->id);
        gaps.push_back(c->range());
    }
    if (!ripple || gaps.empty()) return edit;

    // Merge the removed ranges into disjoint intervals.
    std::sort(gaps.begin(), gaps.end(), [](const TimeRange& a, const TimeRange& b) { return a.start < b.start; });
    std::vector<TimeRange> merged;
    for (const auto& g : gaps) {
        if (!merged.empty() && g.start <= merged.back().end()) {
            const TimePoint e = std::max(merged.back().end(), g.end());
            merged.back().duration = e - merged.back().start;
        } else {
            merged.push_back(g);
        }
    }
    // Only intervals no surviving clip overlaps can be closed safely.
    const auto survivors = effectiveClips(seq, edit);
    std::vector<TimeRange> closable;
    for (const auto& m : merged) {
        const bool blocked = std::any_of(survivors.begin(), survivors.end(), [&](const Clip& c) { return c.range().overlaps(m); });
        if (!blocked) closable.push_back(m);
    }
    for (const Clip& c : survivors) {
        Ticks shift = 0;
        for (const auto& g : closable) if (g.end() <= c.start) shift += g.duration.ticks;
        if (shift == 0) continue;
        Clip m = c;
        m.start = TimePoint{c.start.ticks - shift};
        edit.modify(std::move(m));
    }
    return edit;
}

// ---------------------------------------------------------------- copy / paste / duplicate

std::vector<Clip> cloneClips(Document& doc, const std::vector<Clip>& source, Duration delta) {
    std::vector<Clip> out;
    std::map<ClipId, ClipId> remap;
    for (const Clip& c : source) {
        Clip n = c;
        n.id = doc.allocateClipId();
        n.start = c.start + delta;
        remap[c.id] = n.id;
        out.push_back(std::move(n));
    }
    for (Clip& n : out) {
        auto it = remap.find(n.link);
        n.link = it == remap.end() ? ClipId::Invalid : it->second;
    }
    return out;
}

SequenceEdit pasteClips(Document& doc, const std::vector<Clip>& source, TimePoint at, PasteMode mode) {
    std::vector<Clip> valid;
    const auto& seq = doc.project().sequence;
    for (const Clip& c : source) {
        const Track* t = seq.findTrack(c.track);
        if (!t || t->locked) continue;
        if (c.kind != ClipKind::Text && !doc.project().findMedia(c.media)) continue;
        valid.push_back(c);
    }
    if (valid.empty()) return {};
    Ticks earliest = valid.front().start.ticks, latest = valid.front().end().ticks;
    for (const Clip& c : valid) { earliest = std::min(earliest, c.start.ticks); latest = std::max(latest, c.end().ticks); }
    const TimePoint target{std::max<Ticks>(0, at.ticks)};
    const Duration delta = target - TimePoint{earliest};
    std::vector<Clip> copies = cloneClips(doc, valid, delta);
    if (mode == PasteMode::Overwrite) return insertClips(doc, std::move(copies));

    // Insert: open a gap of the group's span on every destination track. A
    // clip straddling the insertion point is split so its tail moves too.
    const Duration span{latest - earliest};
    SequenceEdit edit;
    std::vector<TrackId> tracks;
    for (const Clip& c : copies) if (std::find(tracks.begin(), tracks.end(), c.track) == tracks.end()) tracks.push_back(c.track);
    Pieces pieces;
    for (TrackId track : tracks) {
        for (const Clip& c : effectiveClips(seq, edit)) {
            if (c.track != track) continue;
            if (c.start >= target) {
                Clip m = c;
                m.start = c.start + span;
                edit.modify(std::move(m));
            } else if (c.end() > target) {
                Clip right = rightPiece(c, target, doc.allocateClipId());
                right.start = target + span;
                edit.modify(leftPiece(c, target));
                pieces.emplace_back(c.id, right.id);
                edit.add(std::move(right));
            }
        }
    }
    relinkPieces(seq, pieces, edit);
    for (Clip& c : copies) edit.add(std::move(c));
    return edit;
}

SequenceEdit duplicateClips(Document& doc, const std::vector<ClipId>& ids) {
    const auto& seq = doc.project().sequence;
    std::vector<Clip> source;
    for (auto id : ids) if (const Clip* c = seq.findClip(id)) source.push_back(*c);
    if (source.empty()) return {};
    Ticks latest = source.front().end().ticks;
    for (const Clip& c : source) latest = std::max(latest, c.end().ticks);
    return pasteClips(doc, source, TimePoint{latest}, PasteMode::Insert);
}

// ---------------------------------------------------------------- helpers

std::vector<Clip> applied(const Sequence& seq, const SequenceEdit& edit) { return effectiveClips(seq, edit); }

std::vector<ClipId> withLinked(const Sequence& seq, const std::vector<ClipId>& ids) {
    std::vector<ClipId> out;
    auto push = [&](ClipId id) { if (isValid(id) && !contains(out, id)) out.push_back(id); };
    for (auto id : ids) {
        push(id);
        if (const Clip* c = seq.findClip(id)) push(c->link);
    }
    return out;
}

std::vector<TimePoint> snapTargets(const Sequence& seq, const std::vector<ClipId>& ignore, TimePoint playhead) {
    std::vector<TimePoint> t{TimePoint::zero(), playhead};
    for (const auto& c : seq.clips) {
        if (contains(ignore, c.id)) continue;
        t.push_back(c.start);
        t.push_back(c.end());
    }
    std::sort(t.begin(), t.end());
    t.erase(std::unique(t.begin(), t.end()), t.end());
    return t;
}

std::optional<TimePoint> snap(TimePoint t, const std::vector<TimePoint>& targets, Duration tolerance) {
    std::optional<TimePoint> best;
    Ticks bestDist = tolerance.ticks + 1;
    for (const auto& c : targets) {
        const Ticks d = std::llabs(c.ticks - t.ticks);
        if (d < bestDist) { bestDist = d; best = c; }
    }
    return best;
}

}  // namespace lf::project::ops
