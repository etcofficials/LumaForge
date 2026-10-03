#include "lumaforge/export/Proxy.h"

#include "lumaforge/core/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace lf::exporting {

namespace {
constexpr const char* kLog = "proxy";

std::string avError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}
Error fail(const std::string& message, const char* where, int err) { return Error{message, std::string(where) + ": " + avError(err), err}; }

std::uint64_t fnv1a(const std::string& s, std::uint64_t h = 1469598103934665603ull) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

std::filesystem::path toPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// RAII bundle for the transcoder's FFmpeg objects.
struct Contexts {
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    AVCodecContext* dec = nullptr;
    AVCodecContext* enc = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* scaled = nullptr;
    AVPacket* packet = nullptr;
    ~Contexts() {
        if (out && !(out->oformat->flags & AVFMT_NOFILE) && out->pb) avio_closep(&out->pb);
        if (out) avformat_free_context(out);
        if (in) avformat_close_input(&in);
        avcodec_free_context(&dec);
        avcodec_free_context(&enc);
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_frame_free(&scaled);
        av_packet_free(&packet);
    }
};
}  // namespace

std::string proxyFileName(const project::MediaItem& item, int height) {
    std::uint64_t h = fnv1a(item.path);
    h = fnv1a(std::to_string(item.fingerprintSize) + ":" + std::to_string(item.fingerprintMtime), h);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%016llx_%dp.mp4", static_cast<unsigned long long>(h), height);
    return buf;
}

bool proxyApplies(const project::MediaItem& item) noexcept { return item.type() == media::MediaType::Video && item.info.hasVideo(); }

Status makeProxy(const std::string& source, const std::string& destination, int height, const std::function<void(double)>& progress,
                 const std::atomic<bool>& cancel) {
    Contexts c;
    int r = avformat_open_input(&c.in, source.c_str(), nullptr, nullptr);
    if (r < 0) return fail("The source file could not be opened.", "avformat_open_input", r);
    if ((r = avformat_find_stream_info(c.in, nullptr)) < 0) return fail("The source file could not be read.", "avformat_find_stream_info", r);
    const int vs = av_find_best_stream(c.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) return Error{"The file has no video to make a proxy from."};
    AVStream* inStream = c.in->streams[vs];
    const AVCodec* decoder = avcodec_find_decoder(inStream->codecpar->codec_id);
    if (!decoder) return Error{"This video format cannot be decoded."};
    c.dec = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(c.dec, inStream->codecpar);
    c.dec->thread_count = 0;
    if ((r = avcodec_open2(c.dec, decoder, nullptr)) < 0) return fail("The video decoder could not start.", "avcodec_open2(decoder)", r);

    const int srcW = c.dec->width, srcH = c.dec->height;
    const int outH = std::min(srcH, height) & ~1;
    const int outW = std::max(2, static_cast<int>(static_cast<std::int64_t>(srcW) * outH / std::max(1, srcH)) & ~1);

    const auto finalPath = toPath(destination);
    auto partial = finalPath;
    partial.replace_extension(".partial.mp4");
    const auto partialU8 = partial.u8string();
    const std::string partialPath(partialU8.begin(), partialU8.end());
    std::error_code ec;
    std::filesystem::create_directories(finalPath.parent_path(), ec);

    if ((r = avformat_alloc_output_context2(&c.out, nullptr, "mp4", partialPath.c_str())) < 0) return fail("The proxy file could not be created.", "avformat_alloc_output_context2", r);
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) return Error{"The H.264 encoder is not available in this build."};
    c.enc = avcodec_alloc_context3(encoder);
    AVRational fps = av_guess_frame_rate(c.in, inStream, nullptr);
    if (fps.num <= 0 || fps.den <= 0) fps = AVRational{30, 1};
    c.enc->width = outW;
    c.enc->height = outH;
    c.enc->time_base = inStream->time_base;          // keep the source timestamps as they are
    c.enc->framerate = fps;
    c.enc->pix_fmt = AV_PIX_FMT_YUV420P;
    c.enc->gop_size = std::max(1, static_cast<int>(av_q2d(fps) / 2.0 + 0.5));   // keyframe every 0.5 s: fast seeking
    c.enc->max_b_frames = 0;                                                     // simple decode order: cheap scrubbing
    c.enc->colorspace = AVCOL_SPC_BT709;
    c.enc->color_primaries = AVCOL_PRI_BT709;
    c.enc->color_trc = AVCOL_TRC_BT709;
    c.enc->color_range = AVCOL_RANGE_MPEG;
    c.enc->thread_count = 0;
    av_opt_set(c.enc->priv_data, "preset", "veryfast", 0);
    av_opt_set(c.enc->priv_data, "crf", "23", 0);
    av_opt_set(c.enc->priv_data, "tune", "fastdecode", 0);
    if (c.out->oformat->flags & AVFMT_GLOBALHEADER) c.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((r = avcodec_open2(c.enc, encoder, nullptr)) < 0) return fail("The proxy encoder could not start.", "avcodec_open2(encoder)", r);
    AVStream* outStream = avformat_new_stream(c.out, nullptr);
    avcodec_parameters_from_context(outStream->codecpar, c.enc);
    outStream->time_base = c.enc->time_base;
    if ((r = avio_open(&c.out->pb, partialPath.c_str(), AVIO_FLAG_WRITE)) < 0) return fail("The proxy file could not be written.", "avio_open", r);
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    r = avformat_write_header(c.out, &opts);
    av_dict_free(&opts);
    if (r < 0) return fail("The proxy file could not be written.", "avformat_write_header", r);

    c.frame = av_frame_alloc();
    c.scaled = av_frame_alloc();
    c.scaled->format = AV_PIX_FMT_YUV420P;
    c.scaled->width = outW;
    c.scaled->height = outH;
    av_frame_get_buffer(c.scaled, 32);
    c.packet = av_packet_alloc();
    const double totalSeconds = c.in->duration > 0 ? c.in->duration / static_cast<double>(AV_TIME_BASE) : 0.0;
    const std::int64_t startPts = inStream->start_time != AV_NOPTS_VALUE ? inStream->start_time : 0;

    auto drainEncoder = [&]() -> Status {
        while (true) {
            AVPacket* out = av_packet_alloc();
            const int e = avcodec_receive_packet(c.enc, out);
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) { av_packet_free(&out); return Status::success(); }
            if (e < 0) { av_packet_free(&out); return fail("Encoding the proxy failed.", "avcodec_receive_packet", e); }
            av_packet_rescale_ts(out, c.enc->time_base, outStream->time_base);
            out->stream_index = outStream->index;
            const int w = av_interleaved_write_frame(c.out, out);
            av_packet_free(&out);
            if (w < 0) return fail("Writing the proxy failed (is the disk full?).", "av_interleaved_write_frame", w);
        }
    };
    auto encodeFrame = [&](AVFrame* f) -> Status {
        if (f) {
            if (!c.sws) {
                c.sws = sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), outW, outH, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!c.sws) return Error{"This video's pixel format cannot be converted."};
            }
            av_frame_make_writable(c.scaled);
            sws_scale(c.sws, f->data, f->linesize, 0, f->height, c.scaled->data, c.scaled->linesize);
            // Media time 0 = first sample of the file, like the decoder the preview uses.
            const std::int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
            c.scaled->pts = pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : pts - startPts;
            if (progress && totalSeconds > 0 && pts != AV_NOPTS_VALUE) progress(std::clamp((pts - startPts) * av_q2d(inStream->time_base) / totalSeconds, 0.0, 1.0));
        }
        const int e = avcodec_send_frame(c.enc, f ? c.scaled : nullptr);
        if (e < 0 && e != AVERROR_EOF) return fail("Encoding the proxy failed.", "avcodec_send_frame", e);
        return drainEncoder();
    };
    auto drainDecoder = [&]() -> Status {
        while (true) {
            const int e = avcodec_receive_frame(c.dec, c.frame);
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) return Status::success();
            if (e < 0) return fail("Decoding the source failed.", "avcodec_receive_frame", e);
            auto s = encodeFrame(c.frame);
            av_frame_unref(c.frame);
            if (!s) return s;
        }
    };

    auto abort = [&](Error e) -> Status {
        if (c.out && c.out->pb) avio_closep(&c.out->pb);
        std::filesystem::remove(partial, ec);
        LF_LOG_WARN(kLog, "proxy for {} stopped: {}", source, e.fullText());
        return e;
    };
    while (av_read_frame(c.in, c.packet) >= 0) {
        if (cancel.load()) { av_packet_unref(c.packet); return abort(Error{"Proxy creation cancelled."}); }
        if (c.packet->stream_index == vs) {
            const int e = avcodec_send_packet(c.dec, c.packet);
            if (e < 0 && e != AVERROR(EAGAIN)) { av_packet_unref(c.packet); return abort(fail("Decoding the source failed.", "avcodec_send_packet", e)); }
            if (auto s = drainDecoder(); !s) { av_packet_unref(c.packet); return abort(s.error()); }
        }
        av_packet_unref(c.packet);
    }
    avcodec_send_packet(c.dec, nullptr);
    if (auto s = drainDecoder(); !s) return abort(s.error());
    if (auto s = encodeFrame(nullptr); !s) return abort(s.error());
    if ((r = av_write_trailer(c.out)) < 0) return abort(fail("Finishing the proxy failed.", "av_write_trailer", r));
    avio_closep(&c.out->pb);
    std::filesystem::remove(finalPath, ec);
    std::filesystem::rename(partial, finalPath, ec);
    if (ec) return abort(Error{"The proxy could not be moved into place.", ec.message()});
    if (progress) progress(1.0);
    LF_LOG_INFO(kLog, "proxy ready: {} -> {} ({}x{})", source, destination, outW, outH);
    return Status::success();
}

}  // namespace lf::exporting
