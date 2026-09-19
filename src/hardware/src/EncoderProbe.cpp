// Encoder availability probing: actually open each candidate H.264 encoder
// with a small representative configuration. This is the only reliable way to
// know whether a hardware encoder works on *this* GPU + driver. It runs off
// the UI thread (see HardwareDetector::detect).
#include "lumaforge/hardware/HardwareDetector.h"

#include "lumaforge/core/Log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

namespace lf::hw {

namespace {
constexpr const char* kLog = "hw";

struct Candidate {
    const char* name;
    const char* displayName;
    EncoderKind kind;
    AVPixelFormat pixelFormat;
};

// Ordered by preference within each kind. The probe reports every one; the
// export module picks the first available of the requested kind.
const Candidate kCandidates[] = {
    {"h264_nvenc", "NVIDIA NVENC (H.264)", EncoderKind::Hardware, AV_PIX_FMT_NV12},
    {"h264_qsv", "Intel Quick Sync (H.264)", EncoderKind::Hardware, AV_PIX_FMT_NV12},
    {"h264_amf", "AMD AMF (H.264)", EncoderKind::Hardware, AV_PIX_FMT_NV12},
    {"libx264", "Software x264 (H.264)", EncoderKind::Software, AV_PIX_FMT_YUV420P},
    {"h264_mf", "Media Foundation (H.264)", EncoderKind::Software, AV_PIX_FMT_NV12},
};

std::string avErrorText(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(err, buf, sizeof buf);
    return std::format("{} ({})", buf, err);
}

// FFmpeg logs vendor failures (e.g. "Cannot load nvEncodeAPI64.dll") at
// error level to stderr. Capture the last line so the report can show *why*.
thread_local std::string g_lastAvLog;
void captureAvLog(void*, int level, const char* fmt, va_list args) {
    if (level > AV_LOG_WARNING) return;
    char line[1024];
    vsnprintf(line, sizeof line, fmt, args);
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (!s.empty()) g_lastAvLog = std::move(s);
}
}  // namespace

std::vector<EncoderProbe> HardwareDetector::probeH264Encoders() {
    std::vector<EncoderProbe> out;
    const int previousLevel = av_log_get_level();
    av_log_set_callback(&captureAvLog);
    av_log_set_level(AV_LOG_WARNING);

    for (const auto& c : kCandidates) {
        EncoderProbe probe{c.name, c.displayName, c.kind, false, {}};
        const AVCodec* codec = avcodec_find_encoder_by_name(c.name);
        if (!codec) {
            probe.detail = "not compiled into this FFmpeg build";
            out.push_back(std::move(probe));
            continue;
        }

        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) {
            probe.detail = "out of memory";
            out.push_back(std::move(probe));
            continue;
        }
        ctx->width = 1280;
        ctx->height = 720;
        ctx->time_base = {1, 30};
        ctx->framerate = {30, 1};
        ctx->pix_fmt = c.pixelFormat;
        ctx->bit_rate = 4'000'000;
        ctx->gop_size = 60;
        ctx->max_b_frames = 0;
        if (std::string_view(c.name) == "libx264") av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        if (std::string_view(c.name) == "h264_mf") av_opt_set(ctx->priv_data, "hw_encoding", "0", 0);

        g_lastAvLog.clear();
        const int err = avcodec_open2(ctx, codec, nullptr);
        if (err == 0) {
            probe.available = true;
        } else {
            probe.detail = g_lastAvLog.empty() ? avErrorText(err) : g_lastAvLog;
        }
        avcodec_free_context(&ctx);
        out.push_back(std::move(probe));
    }

    av_log_set_callback(av_log_default_callback);
    av_log_set_level(previousLevel);
    return out;
}

}  // namespace lf::hw
