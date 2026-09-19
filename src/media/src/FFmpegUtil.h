#pragma once

// Internal helpers shared by the media module: RAII wrappers over FFmpeg
// objects and error translation. Not part of the public API.

#include "lumaforge/core/Rational.h"
#include "lumaforge/core/Result.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>

namespace lf::media::ff {

struct FormatContextDeleter { void operator()(AVFormatContext* c) const noexcept { if (c) avformat_close_input(&c); } };
struct CodecContextDeleter  { void operator()(AVCodecContext* c) const noexcept { if (c) avcodec_free_context(&c); } };
struct FrameDeleter         { void operator()(AVFrame* f) const noexcept { if (f) av_frame_free(&f); } };
struct PacketDeleter        { void operator()(AVPacket* p) const noexcept { if (p) av_packet_free(&p); } };
struct SwsContextDeleter    { void operator()(SwsContext* s) const noexcept { if (s) sws_freeContext(s); } };

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr  = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr         = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr        = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsContextPtr    = std::unique_ptr<SwsContext, SwsContextDeleter>;

[[nodiscard]] std::string errorString(int averror);

// Builds an lf::Error with a user-facing message and the FFmpeg detail.
[[nodiscard]] Error makeError(const std::string& userMessage, const char* where, int averror);

[[nodiscard]] constexpr Rational toRational(AVRational r) noexcept { return Rational{r.num, r.den}; }
[[nodiscard]] constexpr AVRational toAVRational(Rational r) noexcept { return AVRational{static_cast<int>(r.num), static_cast<int>(r.den)}; }

// Opens a file and reads stream info. Path is UTF-8 (FFmpeg on Windows expects UTF-8).
[[nodiscard]] Result<FormatContextPtr> openInput(const std::string& utf8Path);

// Creates and opens a decoder for the given stream (software only).
[[nodiscard]] Result<CodecContextPtr> openDecoder(const AVStream* stream, int threads = 0);

// Routes FFmpeg's own log lines into lf::log at a sensible level. Idempotent.
void installLogBridge();

}  // namespace lf::media::ff
