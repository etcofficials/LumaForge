#include "lumaforge/media/Thumbnailer.h"

#include "FFmpegUtil.h"
#include "lumaforge/core/Log.h"

extern "C" {
#include <libavutil/imgutils.h>
}

#include <algorithm>

namespace lf::media {

namespace {
constexpr const char* kLog = "media";

// Decode packets until one frame comes out at or after `target` (or the first
// frame when target is unset). Returns nullptr at EOF.
ff::FramePtr decodeUntil(AVFormatContext* fmt, AVCodecContext* dec, int streamIndex, std::int64_t targetPts) {
    ff::PacketPtr pkt(av_packet_alloc());
    ff::FramePtr frame(av_frame_alloc());
    ff::FramePtr last;
    bool draining = false;
    for (int guard = 0; guard < 2000; ++guard) {
        if (!draining) {
            const int r = av_read_frame(fmt, pkt.get());
            if (r < 0) {
                draining = true;
                avcodec_send_packet(dec, nullptr);
            } else {
                if (pkt->stream_index == streamIndex) avcodec_send_packet(dec, pkt.get());
                av_packet_unref(pkt.get());
            }
        }
        while (true) {
            const int r = avcodec_receive_frame(dec, frame.get());
            if (r == AVERROR(EAGAIN)) break;
            if (r < 0) return last;   // EOF or error: best effort
            const std::int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
            if (targetPts == AV_NOPTS_VALUE || pts >= targetPts) {
                ff::FramePtr out(av_frame_alloc());
                av_frame_move_ref(out.get(), frame.get());
                return out;
            }
            // Keep the most recent frame in case we never reach the target.
            if (!last) last.reset(av_frame_alloc());
            av_frame_unref(last.get());
            av_frame_move_ref(last.get(), frame.get());
        }
        if (draining && guard > 1990) break;
    }
    return last;
}
}  // namespace

Result<Image> Thumbnailer::render(const std::string& utf8Path, const Options& options) {
    ff::installLogBridge();
    auto opened = ff::openInput(utf8Path);
    if (!opened) return opened.error();
    AVFormatContext* fmt = opened->get();

    const int vIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIndex < 0) return Error{"The file has no video stream to make a thumbnail from.", "no video stream"};
    AVStream* stream = fmt->streams[vIndex];

    auto decoder = ff::openDecoder(stream, 1);
    if (!decoder) return decoder.error();
    AVCodecContext* dec = decoder->get();
    // Thumbnails don't need every pixel: let the decoder skip non-reference frames.
    dec->skip_frame = AVDISCARD_NONREF;

    // Choose a position: explicit, or ~10% in (min 0.5 s) for videos.
    std::int64_t targetPts = AV_NOPTS_VALUE;
    const bool isImage = fmt->iformat && (std::string(fmt->iformat->name).find("image2") != std::string::npos || std::string(fmt->iformat->name).find("_pipe") != std::string::npos);
    if (!isImage && fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        double seconds = options.at.ticks >= 0 ? options.at.toSeconds() : std::max(0.5, fmt->duration / static_cast<double>(AV_TIME_BASE) * 0.1);
        seconds = std::min(seconds, fmt->duration / static_cast<double>(AV_TIME_BASE));
        const std::int64_t ts = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
        if (av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(dec);
            targetPts = av_rescale_q(ts, AVRational{1, AV_TIME_BASE}, stream->time_base);
        }
    }

    ff::FramePtr frame = decodeUntil(fmt, dec, vIndex, targetPts);
    if (!frame) {
        // Seek may have overshot on short files: retry from the start.
        av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
        frame = decodeUntil(fmt, dec, vIndex, AV_NOPTS_VALUE);
    }
    if (!frame || frame->width <= 0) return Error{"No frame could be decoded for the thumbnail.", "decode produced no frame"};

    // Scale preserving aspect (and respecting rotation metadata is left to the renderer).
    const int srcW = frame->width, srcH = frame->height;
    int dstW = options.maxWidth, dstH = options.maxHeight;
    if (srcW * dstH > dstW * srcH) dstH = std::max(1, static_cast<int>(static_cast<std::int64_t>(dstW) * srcH / srcW));
    else dstW = std::max(1, static_cast<int>(static_cast<std::int64_t>(dstH) * srcW / srcH));
    dstW = std::min(dstW, srcW);
    dstH = std::min(dstH, srcH);

    ff::SwsContextPtr sws(sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(frame->format), dstW, dstH, AV_PIX_FMT_BGRA,
                                         SWS_AREA, nullptr, nullptr, nullptr));
    if (!sws) return Error{"The frame format is not supported for thumbnails.", std::string("sws_getContext failed for pix_fmt ") + std::to_string(frame->format)};

    Image img;
    img.width = dstW;
    img.height = dstH;
    img.bgra.resize(static_cast<std::size_t>(dstW) * dstH * 4);
    std::uint8_t* dstData[4] = {img.bgra.data(), nullptr, nullptr, nullptr};
    int dstLines[4] = {dstW * 4, 0, 0, 0};
    sws_scale(sws.get(), frame->data, frame->linesize, 0, srcH, dstData, dstLines);
    LF_LOG_TRACE(kLog, "thumbnail {}x{} from {}", dstW, dstH, utf8Path);
    return img;
}

}  // namespace lf::media
