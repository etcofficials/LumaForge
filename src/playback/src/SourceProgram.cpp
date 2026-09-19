#include "lumaforge/playback/SourceProgram.h"

namespace lf::playback {

SourceProgram::SourceProgram(media::MediaInfo info, int frameWidth, int frameHeight, Rational frameRate)
    : m_info(std::move(info)), m_frameRate(frameRate), m_width(frameWidth), m_height(frameHeight) {
    // Images preview for 5 s; the source's own rate drives frame stepping when known.
    m_duration = m_info.type == media::MediaType::Image ? Duration::fromSeconds(5.0) : m_info.duration;
    if (m_info.video && m_info.video->frameRate.isValid() && !m_info.video->frameRate.isZero()) m_frameRate = m_info.video->frameRate;
    if (m_info.video) {
        // Show the source at its native aspect: frame = source size.
        m_width = m_info.video->width;
        m_height = m_info.video->height;
    }
}

void SourceProgram::videoLayersAt(TimePoint t, std::vector<VideoLayerRequest>& out) const {
    if (!m_info.video) return;
    if (t < TimePoint::zero() || t >= TimePoint{m_duration.ticks}) return;
    VideoLayerRequest r;
    r.path = m_info.path;
    r.sourceTime = m_info.type == media::MediaType::Image ? TimePoint::zero() : t;
    r.isImage = m_info.type == media::MediaType::Image;
    r.layerKey = 1;
    out.push_back(std::move(r));
}

void SourceProgram::audioClipsAt(TimePoint t, std::vector<AudioClipRequest>& out) const {
    if (!m_info.audio) return;
    if (t < TimePoint::zero() || t >= TimePoint{m_duration.ticks}) return;
    AudioClipRequest r;
    r.path = m_info.path;
    r.clipKey = 1;
    r.sourceTime = t;
    out.push_back(std::move(r));
}

}  // namespace lf::playback
