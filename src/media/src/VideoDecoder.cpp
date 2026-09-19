#include "lumaforge/media/VideoDecoder.h"

#include "FFmpegUtil.h"
#include "lumaforge/core/Log.h"
#include "lumaforge/hardware/GpuDevice.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <thread>

namespace lf::media {

namespace {
constexpr const char* kLog = "decode";


// FFmpeg asks which output format to use; prefer D3D11 when it is on the list.
AVPixelFormat chooseHwFormat(AVCodecContext*, const AVPixelFormat* formats) {
    for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D11) return *p;
    }
    return formats[0];
}
}  // namespace

struct VideoDecoder::Impl {
    ff::FormatContextPtr format;
    ff::CodecContextPtr codec;
    ff::PacketPtr packet{av_packet_alloc()};
    ff::FramePtr frame{av_frame_alloc()};
    AVBufferRef* hwDevice = nullptr;
    AVStream* stream = nullptr;
    int streamIndex = -1;
    Rational timeBase;
    std::int64_t startPts = 0;         // stream start offset so media time starts at 0
    bool draining = false;
    std::shared_ptr<hw::GpuDevice> gpu;

    ~Impl() {
        codec.reset();
        if (hwDevice) av_buffer_unref(&hwDevice);
    }
};

VideoDecoder::~VideoDecoder() = default;

Result<std::unique_ptr<VideoDecoder>> VideoDecoder::open(const std::string& utf8Path, const Options& options) {
    ff::installLogBridge();
    std::unique_ptr<VideoDecoder> dec(new VideoDecoder());
    dec->m_path = utf8Path;

    const bool wantHw = options.allowHardware && options.gpu && options.gpu->videoDevice() && !options.gpu->isWarp();
    Status s = dec->openStream(options, wantHw);
    if (!s && wantHw) {
        LF_LOG_WARN(kLog, "hardware decode unavailable for {} ({}); using software", utf8Path, s.error().fullText());
        s = dec->openStream(options, false);
    }
    if (!s) return s.error();
    LF_LOG_INFO(kLog, "opened {} ({}x{} {} {:.3f} fps) with {} decoder", dec->m_info.codecName, dec->m_info.width, dec->m_info.height,
                dec->m_info.pixelFormat, dec->m_info.frameRate.toDouble(), dec->m_hardware ? "D3D11VA" : "software");
    return dec;
}

Status VideoDecoder::openStream(const Options& options, bool hardware) {
    auto impl = std::make_unique<Impl>();
    auto opened = ff::openInput(m_path);
    if (!opened) return opened.error();
    impl->format = std::move(opened.value());

    impl->streamIndex = av_find_best_stream(impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (impl->streamIndex < 0) return Error{"The file has no video stream.", "av_find_best_stream(VIDEO) failed"};
    impl->stream = impl->format->streams[impl->streamIndex];
    // Skip demuxing streams we don't need (audio has its own decoder instance).
    for (unsigned i = 0; i < impl->format->nb_streams; ++i) {
        if (static_cast<int>(i) != impl->streamIndex) impl->format->streams[i]->discard = AVDISCARD_ALL;
    }
    impl->timeBase = ff::toRational(impl->stream->time_base);
    impl->startPts = impl->stream->start_time != AV_NOPTS_VALUE ? impl->stream->start_time : 0;

    const AVCodec* codec = avcodec_find_decoder(impl->stream->codecpar->codec_id);
    if (!codec) return Error{std::string("No decoder is available for the \"") + avcodec_get_name(impl->stream->codecpar->codec_id) + "\" codec."};
    impl->codec.reset(avcodec_alloc_context3(codec));
    if (!impl->codec) return Error{"Out of memory while opening the decoder."};
    int err = avcodec_parameters_to_context(impl->codec.get(), impl->stream->codecpar);
    if (err < 0) return ff::makeError("The stream parameters could not be applied to the decoder.", "avcodec_parameters_to_context", err);
    impl->codec->pkt_timebase = impl->stream->time_base;

    if (hardware) {
        impl->gpu = options.gpu;
        impl->hwDevice = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!impl->hwDevice) return Error{"D3D11VA is not available in this FFmpeg build."};
        auto* devCtx = reinterpret_cast<AVHWDeviceContext*>(impl->hwDevice->data);
        auto* d3d = static_cast<AVD3D11VADeviceContext*>(devCtx->hwctx);
        d3d->device = impl->gpu->device();
        d3d->device->AddRef();
        d3d->device_context = impl->gpu->immediateContext();
        d3d->device_context->AddRef();
        // FFmpeg's decoder and our renderer share one immediate context; the
        // hwcontext calls these around its context use.
        d3d->lock = [](void* ctx) { static_cast<hw::GpuDevice*>(ctx)->lockContext(); };
        d3d->unlock = [](void* ctx) { static_cast<hw::GpuDevice*>(ctx)->unlockContext(); };
        d3d->lock_ctx = impl->gpu.get();
        err = av_hwdevice_ctx_init(impl->hwDevice);
        if (err < 0) return ff::makeError("The D3D11VA device could not be initialised.", "av_hwdevice_ctx_init", err);
        impl->codec->hw_device_ctx = av_buffer_ref(impl->hwDevice);
        impl->codec->get_format = &chooseHwFormat;
        impl->codec->thread_count = 1;   // hardware decoders don't benefit from frame threads
        // The surface pool is fixed at init: leave room for the frames a
        // reader holds while the renderer copies them out (see ClipVideoReader).
        impl->codec->extra_hw_frames = 6;
    } else {
        // Leave headroom for the UI and the audio thread on a 4-core machine.
        const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
        impl->codec->thread_count = options.softwareThreads > 0 ? options.softwareThreads : static_cast<int>(std::clamp(hwThreads - 1, 1u, 8u));
        impl->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    err = avcodec_open2(impl->codec.get(), codec, nullptr);
    if (err < 0) return ff::makeError(std::string("The \"") + codec->name + "\" decoder could not be opened.", "avcodec_open2", err);

    // Stream description
    m_info.streamIndex = impl->streamIndex;
    m_info.codecName = codec->name;
    m_info.width = impl->codec->width;
    m_info.height = impl->codec->height;
    m_info.timeBase = impl->timeBase;
    AVRational fr = impl->stream->avg_frame_rate;
    if (fr.num <= 0 || fr.den <= 0) fr = impl->stream->r_frame_rate;
    m_info.frameRate = (fr.num > 0 && fr.den > 0) ? ff::toRational(fr) : fps::f30;
    if (const char* name = av_get_pix_fmt_name(impl->codec->pix_fmt)) m_info.pixelFormat = name;
    m_info.isFullRange = impl->codec->color_range == AVCOL_RANGE_JPEG;
    if (impl->format->duration != AV_NOPTS_VALUE && impl->format->duration > 0) m_duration = Duration{ticksFromTimeBase(impl->format->duration, Rational{1, AV_TIME_BASE})};
    else if (impl->stream->duration != AV_NOPTS_VALUE) m_duration = Duration{ticksFromTimeBase(impl->stream->duration, impl->timeBase)};

    // Hardware decoders can still fail on the first real frame (unsupported
    // profile/level). Decode one frame now so the fallback happens at open.
    m_impl = std::move(impl);
    m_hardware = hardware;
    m_eof = false;
    auto first = next();
    if (!first) {
        m_impl.reset();
        return first.error();
    }
    if (first->has_value() && hardware && !(*first)->isGpu()) {
        m_impl.reset();
        return Error{"Hardware decoder produced a software frame.", "get_format fell back"};
    }
    // Rewind so the caller starts from the beginning.
    return seek(TimePoint::zero());
}

Duration VideoDecoder::frameDuration() const noexcept { return lf::frameDuration(m_info.frameRate); }

Status VideoDecoder::seek(TimePoint t) {
    if (!m_impl) return Error{"Decoder is not open."};
    const std::int64_t ts = timeBaseFromTicks(std::max<Ticks>(0, t.ticks), m_impl->timeBase) + m_impl->startPts;
    const int err = av_seek_frame(m_impl->format.get(), m_impl->streamIndex, ts, AVSEEK_FLAG_BACKWARD);
    if (err < 0) return ff::makeError("Seeking in the file failed.", "av_seek_frame", err);
    avcodec_flush_buffers(m_impl->codec.get());
    m_impl->draining = false;
    m_eof = false;
    return Status::success();
}

Status VideoDecoder::pump() {
    auto& i = *m_impl;
    while (!i.draining) {
        const int r = av_read_frame(i.format.get(), i.packet.get());
        if (r == AVERROR_EOF) {
            i.draining = true;
            avcodec_send_packet(i.codec.get(), nullptr);
            return Status::success();
        }
        if (r < 0) return ff::makeError("Reading from the file failed.", "av_read_frame", r);
        if (i.packet->stream_index != i.streamIndex) {
            av_packet_unref(i.packet.get());
            continue;
        }
        const int s = avcodec_send_packet(i.codec.get(), i.packet.get());
        av_packet_unref(i.packet.get());
        if (s == AVERROR(EAGAIN)) return Status::success();   // decoder full: caller must receive first
        if (s < 0 && s != AVERROR_EOF) return ff::makeError("The decoder rejected a packet.", "avcodec_send_packet", s);
        return Status::success();
    }
    return Status::success();
}

Result<std::optional<VideoFrame>> VideoDecoder::next() {
    if (!m_impl) return Error{"Decoder is not open."};
    if (m_eof) return std::optional<VideoFrame>{};
    auto& i = *m_impl;
    for (int guard = 0; guard < 4096; ++guard) {
        const int r = avcodec_receive_frame(i.codec.get(), i.frame.get());
        if (r == 0) {
            const std::int64_t pts = (i.frame->best_effort_timestamp != AV_NOPTS_VALUE ? i.frame->best_effort_timestamp : i.frame->pts) - i.startPts;
            const TimePoint t{ticksFromTimeBase(pts, i.timeBase)};
            Duration d = i.frame->duration > 0 ? Duration{ticksFromTimeBase(i.frame->duration, i.timeBase)} : frameDuration();
            AVFrame* out = av_frame_alloc();
            av_frame_move_ref(out, i.frame.get());
            return std::optional<VideoFrame>{VideoFrame(out, t, d, i.timeBase)};
        }
        if (r == AVERROR_EOF) {
            m_eof = true;
            return std::optional<VideoFrame>{};
        }
        if (r != AVERROR(EAGAIN)) {
            // Corrupt packet: log and keep going rather than killing playback.
            LF_LOG_WARN(kLog, "decode error in {}: {}", m_path, ff::errorString(r));
            if (i.draining) { m_eof = true; return std::optional<VideoFrame>{}; }
        }
        if (auto s = pump(); !s) return s.error();
        if (i.draining && r == AVERROR(EAGAIN)) {
            // Drain loop: receive again until EOF.
            continue;
        }
    }
    return Error{"The decoder made no progress.", "next(): guard exhausted"};
}

}  // namespace lf::media
