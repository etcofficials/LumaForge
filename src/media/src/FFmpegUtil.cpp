#include "FFmpegUtil.h"

#include "lumaforge/core/Log.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/log.h>
}

#include <cstdio>
#include <mutex>

namespace lf::media::ff {

std::string errorString(int averror) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(averror, buf, sizeof buf);
    return buf;
}

Error makeError(const std::string& userMessage, const char* where, int averror) {
    return Error{userMessage, std::string(where) + ": " + errorString(averror) + " (" + std::to_string(averror) + ")", averror};
}

Result<FormatContextPtr> openInput(const std::string& utf8Path) {
    AVFormatContext* raw = nullptr;
    int err = avformat_open_input(&raw, utf8Path.c_str(), nullptr, nullptr);
    if (err < 0) {
        if (err == AVERROR(ENOENT)) return Error{"The file could not be found.", "avformat_open_input: " + errorString(err), err};
        if (err == AVERROR_INVALIDDATA) return Error{"This file is not a media file LumaForge can read, or it is damaged.", "avformat_open_input: " + errorString(err), err};
        return makeError("The file could not be opened.", "avformat_open_input", err);
    }
    FormatContextPtr ctx(raw);
    err = avformat_find_stream_info(ctx.get(), nullptr);
    if (err < 0) return makeError("The file's streams could not be read; it may be damaged or use an unsupported format.", "avformat_find_stream_info", err);
    if (ctx->nb_streams == 0) return Error{"The file contains no audio or video streams.", "nb_streams == 0"};
    return ctx;
}

Result<CodecContextPtr> openDecoder(const AVStream* stream, int threads) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        const char* name = avcodec_get_name(stream->codecpar->codec_id);
        return Error{std::string("No decoder is available for the \"") + name + "\" codec.", std::string("avcodec_find_decoder: ") + name};
    }
    CodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx) return Error{"Out of memory while opening the decoder.", "avcodec_alloc_context3"};
    int err = avcodec_parameters_to_context(ctx.get(), stream->codecpar);
    if (err < 0) return makeError("The stream parameters could not be applied to the decoder.", "avcodec_parameters_to_context", err);
    ctx->pkt_timebase = stream->time_base;
    ctx->thread_count = threads;   // 0 = FFmpeg picks
    err = avcodec_open2(ctx.get(), codec, nullptr);
    if (err < 0) return makeError(std::string("The \"") + codec->name + "\" decoder could not be opened.", "avcodec_open2", err);
    return ctx;
}

namespace {
std::once_flag g_logOnce;

void avLogBridge(void* avcl, int level, const char* fmt, va_list args) {
    // FFmpeg is chatty; keep only warnings and errors, and never let it print to stderr.
    if (level > AV_LOG_WARNING) return;
    char line[1024];
    int printPrefix = 1;
    av_log_format_line(avcl, level, fmt, args, line, sizeof line, &printPrefix);
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) return;
    if (level <= AV_LOG_ERROR) LF_LOG_ERROR("ffmpeg", "{}", s);
    else LF_LOG_WARN("ffmpeg", "{}", s);
}
}  // namespace

void installLogBridge() {
    std::call_once(g_logOnce, [] {
        av_log_set_level(AV_LOG_WARNING);
        av_log_set_callback(&avLogBridge);
    });
}

}  // namespace lf::media::ff
