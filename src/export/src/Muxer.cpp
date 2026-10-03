#include "Muxer.h"

#include "lumaforge/core/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <vector>

namespace lf::exporting {

namespace {
constexpr const char* kLog = "export";

std::string avError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

Error fail(const std::string& message, const char* where, int err) {
    return Error{message, std::string(where) + ": " + avError(err), err};
}

const char* muxerName(Container c) {
    switch (c) {
        case Container::Mov: return "mov";
        case Container::Mkv: return "matroska";
        default: return "mp4";
    }
}

template <typename T>
std::vector<T> supported(const AVCodecContext* ctx, const AVCodec* codec, AVCodecConfig config, T terminator) {
    const void* list = nullptr;
    int count = 0;
    std::vector<T> out;
    if (avcodec_get_supported_config(ctx, codec, config, 0, &list, &count) >= 0 && list) {
        const T* items = static_cast<const T*>(list);
        for (int i = 0; i < count && items[i] != terminator; ++i) out.push_back(items[i]);
    }
    return out;
}
}  // namespace

struct Muxer::Impl {
    AVFormatContext* oc = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;
    AVPacket* packet = nullptr;
    std::vector<float> pendingAudio;   // interleaved, not yet a full encoder frame
    std::int64_t audioSamples = 0;     // samples sent so far (audio pts)
    int channels = 2;
    bool headerWritten = false;
    bool finished = false;

    ~Impl() {
        if (oc && headerWritten && !finished) av_write_trailer(oc);   // best effort on failure paths
        if (oc && !(oc->oformat->flags & AVFMT_NOFILE) && oc->pb) avio_closep(&oc->pb);
        if (oc) avformat_free_context(oc);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        sws_freeContext(sws);
        av_frame_free(&videoFrame);
        av_frame_free(&audioFrame);
        av_packet_free(&packet);
    }

    Status drain(AVCodecContext* ctx, AVStream* st) {
        while (true) {
            const int r = avcodec_receive_packet(ctx, packet);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return Status::success();
            if (r < 0) return fail("Encoding failed.", "avcodec_receive_packet", r);
            av_packet_rescale_ts(packet, ctx->time_base, st->time_base);
            packet->stream_index = st->index;
            const int w = av_interleaved_write_frame(oc, packet);
            av_packet_unref(packet);
            if (w < 0) return fail("Writing the video file failed (is the disk full?).", "av_interleaved_write_frame", w);
        }
    }

    Status send(AVCodecContext* ctx, AVStream* st, AVFrame* frame) {
        const int r = avcodec_send_frame(ctx, frame);
        if (r < 0 && r != AVERROR_EOF) return fail("Encoding failed.", "avcodec_send_frame", r);
        return drain(ctx, st);
    }

    Status sendAudioFrame(int frames) {
        if (av_frame_make_writable(audioFrame) < 0) return Error{"Audio encoding failed (no buffer)."};
        audioFrame->nb_samples = frames;
        for (int c = 0; c < channels; ++c) {
            auto* dst = reinterpret_cast<float*>(audioFrame->data[c]);
            for (int i = 0; i < frames; ++i) dst[i] = pendingAudio[static_cast<std::size_t>(i * channels + c)];
        }
        audioFrame->pts = audioSamples;
        audioSamples += frames;
        pendingAudio.erase(pendingAudio.begin(), pendingAudio.begin() + static_cast<std::ptrdiff_t>(frames * channels));
        return send(audio, audioStream, audioFrame);
    }
};

bool encoderAvailable(const std::string& name) noexcept { return avcodec_find_encoder_by_name(name.c_str()) != nullptr; }

Muxer::Muxer() : m_impl(std::make_unique<Impl>()) {}
Muxer::~Muxer() = default;

Result<std::unique_ptr<Muxer>> Muxer::open(const Spec& spec) {
    std::unique_ptr<Muxer> m(new Muxer());
    Impl& d = *m->m_impl;
    const ExportSettings& s = spec.settings;
    d.channels = spec.audioChannels;

    int r = avformat_alloc_output_context2(&d.oc, nullptr, muxerName(s.container), spec.path.c_str());
    if (r < 0 || !d.oc) return fail("Could not create the output file.", "avformat_alloc_output_context2", r);

    // ---- video
    const std::string encoderName = s.encoder.empty() ? defaultEncoder(s.codec) : s.encoder;
    const AVCodec* vcodec = avcodec_find_encoder_by_name(encoderName.c_str());
    if (!vcodec) return Error{"The video encoder \"" + encoderName + "\" is not available in this build."};
    d.video = avcodec_alloc_context3(vcodec);
    d.video->width = spec.width;
    d.video->height = spec.height;
    d.video->time_base = AVRational{static_cast<int>(spec.frameRate.den), static_cast<int>(spec.frameRate.num)};
    d.video->framerate = AVRational{static_cast<int>(spec.frameRate.num), static_cast<int>(spec.frameRate.den)};
    d.video->sample_aspect_ratio = AVRational{1, 1};
    d.video->gop_size = std::max(1, static_cast<int>(spec.frameRate.toDouble() * 2.0 + 0.5));   // a keyframe every 2 s
    d.video->color_primaries = AVCOL_PRI_BT709;
    d.video->color_trc = AVCOL_TRC_BT709;
    d.video->colorspace = AVCOL_SPC_BT709;
    d.video->color_range = AVCOL_RANGE_MPEG;
    d.video->thread_count = 0;
    const auto fmts = supported<AVPixelFormat>(d.video, vcodec, AV_CODEC_CONFIG_PIX_FORMAT, AV_PIX_FMT_NONE);
    AVPixelFormat pix = AV_PIX_FMT_YUV420P;
    if (!fmts.empty() && std::find(fmts.begin(), fmts.end(), AV_PIX_FMT_YUV420P) == fmts.end())
        pix = std::find(fmts.begin(), fmts.end(), AV_PIX_FMT_NV12) != fmts.end() ? AV_PIX_FMT_NV12 : fmts.front();
    d.video->pix_fmt = pix;
    const bool x26x = encoderName == "libx264" || encoderName == "libx265";
    if (x26x) {
        av_opt_set(d.video->priv_data, "preset", s.speedPreset.c_str(), 0);
        if (encoderName == "libx265") av_opt_set(d.video->priv_data, "x265-params", "log-level=error", 0);
    }
    if (s.rateControl == RateControl::Quality && x26x) {
        av_opt_set(d.video->priv_data, "crf", std::to_string(s.crf).c_str(), 0);
    } else {
        // Bitrate mode, or an encoder without CRF (Media Foundation): average bitrate.
        int kbps = s.videoKbps;
        if (s.rateControl == RateControl::Quality) {
            const double fps = spec.frameRate.toDouble();
            kbps = static_cast<int>(spec.width * spec.height * fps * 0.1 * std::pow(2.0, (20.0 - s.crf) / 6.0) / 1000.0);
        }
        d.video->bit_rate = static_cast<std::int64_t>(std::max(100, kbps)) * 1000;
        d.video->rc_max_rate = d.video->bit_rate * 3 / 2;
        d.video->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(d.video->bit_rate * 2, INT32_MAX));
    }
    if (d.oc->oformat->flags & AVFMT_GLOBALHEADER) d.video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    r = avcodec_open2(d.video, vcodec, nullptr);
    if (r < 0) return fail("The video encoder could not start with these settings.", "avcodec_open2(video)", r);
    d.videoStream = avformat_new_stream(d.oc, nullptr);
    avcodec_parameters_from_context(d.videoStream->codecpar, d.video);
    d.videoStream->time_base = d.video->time_base;
    d.videoStream->avg_frame_rate = d.video->framerate;
    m->m_videoEncoder = encoderName;

    d.videoFrame = av_frame_alloc();
    d.videoFrame->format = pix;
    d.videoFrame->width = spec.width;
    d.videoFrame->height = spec.height;
    d.videoFrame->color_primaries = AVCOL_PRI_BT709;
    d.videoFrame->color_trc = AVCOL_TRC_BT709;
    d.videoFrame->colorspace = AVCOL_SPC_BT709;
    d.videoFrame->color_range = AVCOL_RANGE_MPEG;
    if ((r = av_frame_get_buffer(d.videoFrame, 32)) < 0) return fail("Out of memory preparing video frames.", "av_frame_get_buffer", r);
    d.sws = sws_getContext(spec.width, spec.height, AV_PIX_FMT_BGRA, spec.width, spec.height, pix, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!d.sws) return Error{"Could not set up colour conversion for export."};
    // Full-range RGB in, BT.709 limited-range YCbCr out (what players expect).
    sws_setColorspaceDetails(d.sws, sws_getCoefficients(SWS_CS_ITU709), 1, sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);

    // ---- audio
    if (spec.audio) {
        const AVCodec* acodec = avcodec_find_encoder_by_name("aac");
        if (!acodec) return Error{"The AAC audio encoder is not available in this build."};
        d.audio = avcodec_alloc_context3(acodec);
        d.audio->sample_rate = s.audioSampleRate;
        av_channel_layout_default(&d.audio->ch_layout, spec.audioChannels);
        d.audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
        d.audio->bit_rate = static_cast<std::int64_t>(s.audioKbps) * 1000;
        d.audio->time_base = AVRational{1, s.audioSampleRate};
        if (d.oc->oformat->flags & AVFMT_GLOBALHEADER) d.audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        r = avcodec_open2(d.audio, acodec, nullptr);
        if (r < 0) return fail("The audio encoder could not start.", "avcodec_open2(audio)", r);
        d.audioStream = avformat_new_stream(d.oc, nullptr);
        avcodec_parameters_from_context(d.audioStream->codecpar, d.audio);
        d.audioStream->time_base = d.audio->time_base;
        d.audioFrame = av_frame_alloc();
        d.audioFrame->format = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_copy(&d.audioFrame->ch_layout, &d.audio->ch_layout);
        d.audioFrame->sample_rate = s.audioSampleRate;
        d.audioFrame->nb_samples = d.audio->frame_size > 0 ? d.audio->frame_size : 1024;
        if ((r = av_frame_get_buffer(d.audioFrame, 0)) < 0) return fail("Out of memory preparing audio frames.", "av_frame_get_buffer(audio)", r);
        m->m_audioEncoder = "aac";
    }

    // ---- file + header
    if (!(d.oc->oformat->flags & AVFMT_NOFILE)) {
        r = avio_open(&d.oc->pb, spec.path.c_str(), AVIO_FLAG_WRITE);
        if (r < 0) return fail("Could not open the output file for writing.", "avio_open", r);
    }
    AVDictionary* opts = nullptr;
    if (s.fastStart && (s.container == Container::Mp4 || s.container == Container::Mov)) av_dict_set(&opts, "movflags", "+faststart", 0);
    av_dict_set(&d.oc->metadata, "encoder", "LumaForge", 0);
    r = avformat_write_header(d.oc, &opts);
    av_dict_free(&opts);
    if (r < 0) return fail("Could not write the file header.", "avformat_write_header", r);
    d.headerWritten = true;
    d.packet = av_packet_alloc();
    LF_LOG_INFO(kLog, "encoder {} {}x{} {} {} | audio {}", encoderName, spec.width, spec.height, av_get_pix_fmt_name(pix),
                s.rateControl == RateControl::Quality && x26x ? "crf " + std::to_string(s.crf) : std::to_string(d.video->bit_rate / 1000) + " kbps",
                spec.audio ? "aac " + std::to_string(s.audioKbps) + " kbps" : std::string("none"));
    return m;
}

Status Muxer::writeVideo(const std::uint8_t* bgra, int stride, std::int64_t frameIndex) {
    Impl& d = *m_impl;
    if (av_frame_make_writable(d.videoFrame) < 0) return Error{"Video encoding failed (no buffer)."};
    const std::uint8_t* src[1] = {bgra};
    const int srcStride[1] = {stride};
    sws_scale(d.sws, src, srcStride, 0, d.video->height, d.videoFrame->data, d.videoFrame->linesize);
    d.videoFrame->pts = frameIndex;
    return d.send(d.video, d.videoStream, d.videoFrame);
}

Status Muxer::writeAudio(const float* interleaved, int frames) {
    Impl& d = *m_impl;
    if (!d.audio) return Status::success();
    d.pendingAudio.insert(d.pendingAudio.end(), interleaved, interleaved + static_cast<std::ptrdiff_t>(frames) * d.channels);
    const int frameSize = d.audio->frame_size > 0 ? d.audio->frame_size : 1024;
    while (static_cast<int>(d.pendingAudio.size()) >= frameSize * d.channels) {
        if (auto st = d.sendAudioFrame(frameSize); !st) return st;
    }
    return Status::success();
}

Status Muxer::finish() {
    Impl& d = *m_impl;
    if (d.audio) {
        if (!d.pendingAudio.empty()) {
            if (auto st = d.sendAudioFrame(static_cast<int>(d.pendingAudio.size()) / d.channels); !st) return st;
        }
        if (auto st = d.send(d.audio, d.audioStream, nullptr); !st) return st;
    }
    if (auto st = d.send(d.video, d.videoStream, nullptr); !st) return st;
    const int r = av_write_trailer(d.oc);
    d.finished = true;
    if (r < 0) return fail("Could not finish the video file.", "av_write_trailer", r);
    if (!(d.oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&d.oc->pb);
    return Status::success();
}

}  // namespace lf::exporting
