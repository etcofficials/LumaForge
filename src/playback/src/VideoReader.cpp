#include "lumaforge/playback/VideoReader.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/Thumbnailer.h"

#include <windows.h>

#include <algorithm>

namespace lf::playback {

namespace {
constexpr const char* kLog = "playback";
// A seek is cheaper than decoding forward beyond this distance.
constexpr Ticks kForwardDecodeLimit = kFlicksPerSecond * 2;
}  // namespace

VideoReader::VideoReader(std::string path, Options options) : m_path(std::move(path)), m_options(std::move(options)) {
    m_thread = std::thread([this] { threadMain(); });
}

VideoReader::~VideoReader() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

std::string VideoReader::errorText() const { return m_error; }

void VideoReader::hint(TimePoint t) {
    std::lock_guard lock(m_mutex);
    if (t != m_target) {
        m_target = t;
        m_targetChanged = true;
        m_cv.notify_all();
    }
}

std::shared_ptr<render::FrameTexture> VideoReader::frameAt(TimePoint t, bool& exact) {
    std::unique_lock lock(m_mutex);
    exact = false;
    if (t != m_target) {
        m_target = t;
        m_targetChanged = true;
        m_cv.notify_all();
    }
    if (m_options.isImage) {
        exact = m_lastReturned != nullptr;
        return m_lastReturned;
    }
    // Drop frames that ended before t, but keep the newest such frame so a
    // consumer that is slightly ahead still has something to show.
    bool consumed = false;
    while (m_queue.size() > 1 && m_queue[1].pts <= t) { m_queue.pop_front(); consumed = true; }
    if (consumed) m_cv.notify_all();   // room in the prefetch window: wake the decoder
    if (!m_queue.empty()) {
        const Entry& e = m_queue.front();
        if (e.pts <= t && t < e.pts + e.duration) {
            exact = true;
        } else if (e.pts <= t && m_eof) {
            exact = true;   // holding the last frame past the end of stream
        }
        // Frames from before a seek landed (pts > t by a lot) are still better than nothing.
        m_lastReturned = e.texture;
        m_lastReturnedPts = e.pts;
        return e.texture;
    }
    return m_lastReturned;
}

bool VideoReader::openDecoder() {
    media::VideoDecoder::Options o;
    o.gpu = m_options.gpu;
    o.allowHardware = m_options.allowHardware && m_options.pool && m_options.pool->supportsNv12Sampling();
    auto opened = media::VideoDecoder::open(m_path, o);
    if (!opened) {
        m_error = opened.error().fullText();
        m_failed = true;
        LF_LOG_ERROR(kLog, "reader failed to open {}: {}", m_path, m_error);
        return false;
    }
    m_decoder = std::move(opened.value());
    m_hardware = m_decoder->isHardware();
    m_frameDuration = m_decoder->frameDuration();
    return true;
}

void VideoReader::decodeImage() {
    media::Thumbnailer::Options o;
    o.maxWidth = 4096;
    o.maxHeight = 4096;
    auto img = media::Thumbnailer::render(m_path, o);
    if (!img) {
        m_error = img.error().fullText();
        m_failed = true;
        return;
    }
    auto tex = m_options.pool->acquireBgra(img->width, img->height, true);
    if (!tex) {
        m_error = tex.error().fullText();
        m_failed = true;
        return;
    }
    if (auto s = (*tex)->fillBgra(img->bgra.data(), img->stride(), false); !s) {
        m_error = s.error().fullText();
        m_failed = true;
        return;
    }
    std::lock_guard lock(m_mutex);
    m_lastReturned = *tex;
    m_decoded = 1;
}

void VideoReader::threadMain() {
    SetThreadDescription(GetCurrentThread(), L"lf-video-reader");
    if (m_options.isImage) {
        decodeImage();
        return;
    }
    if (!openDecoder()) return;

    while (true) {
        TimePoint target;
        bool retarget = false;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stop || m_targetChanged || (static_cast<int>(m_queue.size()) < m_options.prefetchFrames && !m_eof); });
            if (m_stop) return;
            target = m_target;
            retarget = m_targetChanged;
            m_targetChanged = false;
        }

        if (retarget) {
            // Decide whether the queue already covers the target, whether we can
            // decode forward to it, or whether a seek is needed.
            bool needSeek = false;
            {
                std::lock_guard lock(m_mutex);
                // The queue covers [front.pts, back.pts + duration). Inside or just
                // ahead of that range we simply keep decoding forward; a seek is
                // only needed when the target lies before what we have, or far ahead.
                const bool haveFrames = !m_queue.empty();
                const Ticks coveredStart = haveFrames ? m_queue.front().pts.ticks : m_decodePos.ticks;
                const Ticks coveredEnd = haveFrames ? (m_queue.back().pts + m_queue.back().duration).ticks : m_decodePos.ticks + m_frameDuration.ticks;
                const bool nothingYet = m_decodePos.ticks < 0;
                const bool behind = !nothingYet && target.ticks < coveredStart;
                const bool farAhead = !nothingYet && target.ticks > coveredEnd + kForwardDecodeLimit;
                needSeek = nothingYet || behind || farAhead;
                if (needSeek) m_queue.clear();
            }
            if (needSeek) {
                if (auto s = m_decoder->seek(target); !s) {
                    LF_LOG_WARN(kLog, "seek failed in {}: {}", m_path, s.error().fullText());
                }
                m_eof = false;
                m_decodePos = TimePoint{-1};
                // Decode forward until we reach the target frame; stop early if a newer target arrives.
                while (true) {
                    auto r = m_decoder->next();
                    if (!r) { m_error = r.error().fullText(); LF_LOG_WARN(kLog, "decode error in {}: {}", m_path, m_error); break; }
                    if (!r->has_value()) { m_eof = true; break; }
                    const media::VideoFrame& f = **r;
                    m_decodePos = f.pts();
                    // Frames that end before the target are decode-only (needed as
                    // references, never displayed). The first frame ending after the
                    // target is the one to show.
                    if (f.pts() + f.duration() <= target) {
                        std::lock_guard lock(m_mutex);
                        if (m_targetChanged || m_stop) break;   // newer request or shutdown: abandon this seek
                        continue;
                    }
                    auto tex = m_options.pool->acquireFor(f);
                    if (!tex) { LF_LOG_WARN(kLog, "texture upload failed: {}", tex.error().fullText()); continue; }
                    ++m_decoded;
                    std::lock_guard lock(m_mutex);
                    m_queue.push_back({*tex, f.pts(), f.duration()});
                    break;
                }
                continue;
            }
        }

        // Steady state: keep the prefetch window full.
        bool needMore = false;
        {
            std::lock_guard lock(m_mutex);
            needMore = static_cast<int>(m_queue.size()) < m_options.prefetchFrames && !m_eof;
        }
        if (!needMore) continue;
        auto r = m_decoder->next();
        if (!r) { m_error = r.error().fullText(); LF_LOG_WARN(kLog, "decode error in {}: {}", m_path, m_error); m_eof = true; continue; }
        if (!r->has_value()) { m_eof = true; continue; }
        const media::VideoFrame& f = **r;
        m_decodePos = f.pts();
        auto tex = m_options.pool->acquireFor(f);
        if (!tex) { LF_LOG_WARN(kLog, "texture upload failed: {}", tex.error().fullText()); continue; }
        ++m_decoded;
        std::lock_guard lock(m_mutex);
        m_queue.push_back({*tex, f.pts(), f.duration()});
    }
}

}  // namespace lf::playback
