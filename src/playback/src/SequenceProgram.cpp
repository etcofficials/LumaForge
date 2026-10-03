#include "lumaforge/playback/SequenceProgram.h"

#include <algorithm>

namespace lf::playback {

using namespace project;

SequenceProgram::SequenceProgram(const Project& project)
    : m_frameRate(project.settings.frameRate), m_width(project.settings.width), m_height(project.settings.height) {
    const auto& seq = project.sequence;
    m_duration = seq.duration();

    bool anyVideoSolo = false;
    for (const Track* t : seq.videoTracksBottomUp()) anyVideoSolo = anyVideoSolo || t->solo;
    for (const Track* track : seq.videoTracksBottomUp()) {
        std::vector<VisualEntry> entries;
        if (!track->muted && (!anyVideoSolo || track->solo)) {
            for (const Clip& c : seq.clips) {
                if (c.track != track->id || !isVisualClip(c.kind) || !c.enabled) continue;
                VisualEntry e;
                e.clip = c;
                if (c.kind != ClipKind::Text) {
                    const MediaItem* item = project.findMedia(c.media);
                    if (!item || item->offline) continue;
                    e.path = item->path;
                    e.isImage = c.kind == ClipKind::Image;
                    e.mediaDuration = item->info.duration;
                }
                entries.push_back(std::move(e));
            }
            std::sort(entries.begin(), entries.end(), [](const VisualEntry& a, const VisualEntry& b) { return a.clip.start < b.clip.start; });
        }
        // Transitions between the clips that actually render (disabled/offline clips are gaps).
        std::vector<const Clip*> ptrs;
        for (const auto& e : entries) ptrs.push_back(&e.clip);
        std::vector<WindowEntry> windows;
        for (const auto& w : transitionWindows(ptrs)) {
            WindowEntry we;
            we.start = w.start;
            we.end = w.end;
            we.transition = w.transition;
            for (std::size_t i = 0; i < ptrs.size(); ++i) {
                if (ptrs[i] == w.from) we.from = static_cast<int>(i);
                if (ptrs[i] == w.to) we.to = static_cast<int>(i);
            }
            windows.push_back(we);
        }
        m_windows.push_back(std::move(windows));
        m_videoTracks.push_back(std::move(entries));
    }

    bool anySolo = false;
    for (const Track* t : seq.audioTracks()) anySolo = anySolo || t->solo;
    for (const Track* track : seq.audioTracks()) {
        if (track->muted || (anySolo && !track->solo)) continue;
        for (const Clip& c : seq.clips) {
            if (c.track != track->id || c.kind != ClipKind::Audio || !c.enabled) continue;
            const MediaItem* item = project.findMedia(c.media);
            if (!item || item->offline || !item->info.hasAudio()) continue;
            m_audioClips.push_back({c, item->path});
        }
    }
    m_hasAudio = !m_audioClips.empty();

    for (const auto& track : m_videoTracks) for (const auto& e : track) if (!e.path.empty()) m_paths.push_back(e.path);
    for (const auto& a : m_audioClips) m_paths.push_back(a.path);
    std::sort(m_paths.begin(), m_paths.end());
    m_paths.erase(std::unique(m_paths.begin(), m_paths.end()), m_paths.end());
}

VideoLayerRequest SequenceProgram::layerFor(const VisualEntry& e, TimePoint t) const {
    VideoLayerRequest r;
    r.path = e.path;
    r.isImage = e.isImage;
    // Inside a transition a clip may be asked for times outside itself: it plays
    // on into its media handle, and holds its first / last frame beyond the media.
    TimePoint source = e.clip.sourceTimeAt(t);
    if (e.mediaDuration.ticks > 0) source = TimePoint{std::clamp<Ticks>(source.ticks, 0, e.mediaDuration.ticks - 1)};
    else source = TimePoint{std::max<Ticks>(0, source.ticks)};
    r.sourceTime = e.isImage ? TimePoint::zero() : source;
    r.layerKey = raw(e.clip.id);
    r.transform = e.clip.transform;
    r.crop = e.clip.crop;
    animatedTransform(e.clip, animationTime(e.clip, t), r.transform, r.crop);
    r.color = e.clip.color;
    applyEffects(e.clip.effects, r.color, r.effects);
    r.fitToFrame = true;
    if (e.clip.kind == ClipKind::Text) r.text = e.clip.text;
    return r;
}

void SequenceProgram::transitionLayers(const std::vector<VisualEntry>& entries, const WindowEntry& w, TimePoint t, std::vector<VideoLayerRequest>& out) const {
    std::optional<VideoLayerRequest> a, b;
    if (w.from >= 0) a = layerFor(entries[static_cast<std::size_t>(w.from)], t);
    if (w.to >= 0) b = layerFor(entries[static_cast<std::size_t>(w.to)], t);
    const Ticks len = w.end.ticks - w.start.ticks;
    const float p = len > 0 ? std::clamp(static_cast<float>(static_cast<double>(t.ticks - w.start.ticks) / static_cast<double>(len)), 0.0f, 1.0f) : 1.0f;
    const float fw = static_cast<float>(m_width), fh = static_cast<float>(m_height);
    auto push = [&out](std::optional<VideoLayerRequest>& r) { if (r) out.push_back(std::move(*r)); };
    using T = TransitionType;
    switch (w.transition.type) {
        case T::CrossDissolve:
            if (a && b) { b->transform.opacity *= p; push(a); push(b); }
            else if (b) { b->transform.opacity *= p; push(b); }
            else { a->transform.opacity *= 1.0f - p; push(a); }
            break;
        case T::DipToBlack:
        case T::DipToWhite: {
            VideoLayerRequest solid;
            solid.layerKey = 0;
            solid.solid = w.transition.type == T::DipToBlack ? ColorRgba{0, 0, 0, 1} : ColorRgba{1, 1, 1, 1};
            if (a && b) {
                if (p < 0.5f) { solid.transform.opacity = p * 2.0f; push(a); }
                else { solid.transform.opacity = (1.0f - p) * 2.0f; push(b); }
            } else if (b) { solid.transform.opacity = 1.0f - p; push(b); }
            else { solid.transform.opacity = p; push(a); }
            out.push_back(std::move(solid));
            break;
        }
        case T::WipeLeft:
        case T::WipeRight:
        case T::WipeUp:
        case T::WipeDown: {
            const float hide = 1.0f - p;   // share of the incoming picture still hidden
            if (b) {
                Crop& c = b->crop;
                if (w.transition.type == T::WipeRight) c.right = std::max(c.right, hide);
                else if (w.transition.type == T::WipeLeft) c.left = std::max(c.left, hide);
                else if (w.transition.type == T::WipeDown) c.bottom = std::max(c.bottom, hide);
                else c.top = std::max(c.top, hide);
                push(a);
                push(b);
            } else {
                // Wiping out to what lies underneath: the outgoing picture is eaten away.
                Crop& c = a->crop;
                if (w.transition.type == T::WipeRight) c.left = std::max(c.left, p);
                else if (w.transition.type == T::WipeLeft) c.right = std::max(c.right, p);
                else if (w.transition.type == T::WipeDown) c.top = std::max(c.top, p);
                else c.bottom = std::max(c.bottom, p);
                push(a);
            }
            break;
        }
        case T::SlideLeft:
        case T::SlideRight:
        case T::PushLeft:
        case T::PushRight: {
            const float dir = (w.transition.type == T::SlideLeft || w.transition.type == T::PushLeft) ? 1.0f : -1.0f;
            const bool push2 = w.transition.type == T::PushLeft || w.transition.type == T::PushRight;
            if (b) b->transform.positionX += dir * (1.0f - p) * fw;
            // A push (or a slide out with nothing incoming) moves the outgoing picture away.
            if (a && (push2 || !b)) a->transform.positionX -= dir * p * fw;
            push(a);
            push(b);
            break;
        }
        case T::Zoom:
            if (b && !a) {
                b->transform.scaleX *= 0.6f + 0.4f * p;
                b->transform.scaleY *= 0.6f + 0.4f * p;
                b->transform.opacity *= p;
                push(b);
            } else {
                a->transform.scaleX *= 1.0f + 0.5f * p;
                a->transform.scaleY *= 1.0f + 0.5f * p;
                a->transform.opacity *= 1.0f - p;
                push(b);   // incoming underneath, outgoing zooms away on top
                push(a);
            }
            break;
        case T::Count: push(a); push(b); break;
    }
    (void)fh;
}

void SequenceProgram::videoLayersAt(TimePoint t, std::vector<VideoLayerRequest>& out) const {
    for (std::size_t ti = 0; ti < m_videoTracks.size(); ++ti) {
        const auto& track = m_videoTracks[ti];
        const WindowEntry* window = nullptr;
        for (const auto& w : m_windows[ti]) {
            if (w.start > t) break;
            if (t < w.end) { window = &w; break; }
        }
        if (window) {
            transitionLayers(track, *window, t, out);
            continue;
        }
        // Clips on a track never overlap, so the first hit is the only hit.
        for (const auto& e : track) {
            if (e.clip.start > t) break;
            if (!e.clip.contains(t)) continue;
            out.push_back(layerFor(e, t));
            break;
        }
    }
}

float SequenceProgram::gainAt(const Clip& clip, TimePoint t) noexcept {
    if (clip.audio.muted) return 0.0f;
    float g = std::max(0.0f, valueAt(clip, AnimProp::Volume, clip.sourceTimeAt(t)));
    const Ticks in = t.ticks - clip.start.ticks;
    const Ticks outT = clip.end().ticks - t.ticks;
    if (clip.audio.fadeIn.ticks > 0 && in < clip.audio.fadeIn.ticks) g *= static_cast<float>(in) / static_cast<float>(clip.audio.fadeIn.ticks);
    if (clip.audio.fadeOut.ticks > 0 && outT < clip.audio.fadeOut.ticks) g *= static_cast<float>(outT) / static_cast<float>(clip.audio.fadeOut.ticks);
    return std::max(0.0f, g);
}

void SequenceProgram::audioClipsAt(TimePoint t, std::vector<AudioClipRequest>& out) const {
    for (const auto& a : m_audioClips) {
        if (!a.clip.contains(t)) continue;
        AudioClipRequest r;
        r.path = a.path;
        r.clipKey = raw(a.clip.id);
        r.sourceTime = a.clip.sourceTimeAt(t);
        r.speed = a.clip.speed.toDouble();
        r.gain = gainAt(a.clip, t);
        out.push_back(std::move(r));
    }
}

std::vector<std::string> SequenceProgram::mediaPaths() const { return m_paths; }

}  // namespace lf::playback
