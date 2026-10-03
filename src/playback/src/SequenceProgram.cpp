#include "lumaforge/playback/SequenceProgram.h"

#include <algorithm>

namespace lf::playback {

using namespace project;

SequenceProgram::SequenceProgram(const Project& project)
    : m_frameRate(project.settings.frameRate), m_width(project.settings.width), m_height(project.settings.height) {
    const auto& seq = project.sequence;
    m_duration = seq.duration();

    for (const Track* track : seq.videoTracksBottomUp()) {
        std::vector<VisualEntry> entries;
        if (!track->muted) {
            for (const Clip& c : seq.clips) {
                if (c.track != track->id || !isVisualClip(c.kind) || !c.enabled) continue;
                VisualEntry e;
                e.clip = c;
                if (c.kind != ClipKind::Text) {
                    const MediaItem* item = project.findMedia(c.media);
                    if (!item || item->offline) continue;
                    e.path = item->path;
                    e.isImage = c.kind == ClipKind::Image;
                }
                entries.push_back(std::move(e));
            }
            std::sort(entries.begin(), entries.end(), [](const VisualEntry& a, const VisualEntry& b) { return a.clip.start < b.clip.start; });
        }
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

void SequenceProgram::videoLayersAt(TimePoint t, std::vector<VideoLayerRequest>& out) const {
    for (const auto& track : m_videoTracks) {
        // Clips on a track never overlap, so the first hit is the only hit.
        for (const auto& e : track) {
            if (e.clip.start > t) break;
            if (!e.clip.contains(t)) continue;
            VideoLayerRequest r;
            r.path = e.path;
            r.isImage = e.isImage;
            r.sourceTime = e.isImage ? TimePoint::zero() : e.clip.sourceTimeAt(t);
            r.layerKey = raw(e.clip.id);
            r.transform = e.clip.transform;
            r.crop = e.clip.crop;
            r.color = e.clip.color;
            applyEffects(e.clip.effects, r.color, r.effects);
            r.fitToFrame = true;
            if (e.clip.kind == ClipKind::Text) r.text = e.clip.text;
            out.push_back(std::move(r));
            break;
        }
    }
}

float SequenceProgram::gainAt(const Clip& clip, TimePoint t) noexcept {
    if (clip.audio.muted) return 0.0f;
    float g = clip.audio.volume;
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
