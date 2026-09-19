#include "lumaforge/media/MediaProbe.h"

#include "FFmpegUtil.h"
#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

extern "C" {
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace lf::media {

namespace {
constexpr const char* kLog = "media";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int rotationFromStream(const AVStream* stream) {
    const AVPacketSideData* sd = av_packet_side_data_get(stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * sizeof(int32_t)) return 0;
    const double theta = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (std::isnan(theta)) return 0;
    int deg = static_cast<int>(std::lround(-theta));
    deg = ((deg % 360) + 360) % 360;
    return deg;
}

bool isImageContainer(const AVFormatContext* ctx) {
    const std::string name = ctx->iformat ? ctx->iformat->name : "";
    return name.find("image2") != std::string::npos || name.find("_pipe") != std::string::npos;
}
}  // namespace

const std::vector<std::string>& MediaProbe::videoExtensions() {
    static const std::vector<std::string> v = {"mp4", "mov", "m4v", "mkv", "webm", "avi", "wmv", "mpg", "mpeg", "ts", "m2ts", "mts", "flv", "3gp", "mxf"};
    return v;
}
const std::vector<std::string>& MediaProbe::audioExtensions() {
    static const std::vector<std::string> v = {"wav", "mp3", "aac", "m4a", "flac", "ogg", "opus", "wma", "aiff", "aif"};
    return v;
}
const std::vector<std::string>& MediaProbe::imageExtensions() {
    static const std::vector<std::string> v = {"png", "jpg", "jpeg", "bmp", "tif", "tiff", "webp", "gif"};
    return v;
}

bool MediaProbe::looksLikeMedia(std::string_view utf8Path) {
    const auto dot = utf8Path.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    const std::string ext = lower(std::string(utf8Path.substr(dot + 1)));
    auto has = [&](const std::vector<std::string>& list) { return std::find(list.begin(), list.end(), ext) != list.end(); };
    return has(videoExtensions()) || has(audioExtensions()) || has(imageExtensions());
}

Result<MediaInfo> MediaProbe::probe(const std::string& utf8Path) {
    ff::installLogBridge();

    MediaInfo info;
    info.path = utf8Path;
    {
        std::error_code ec;
        const std::filesystem::path p = std::filesystem::path(utf8ToWide(utf8Path));
        info.fileName = wideToUtf8(p.filename().wstring());
        const auto size = std::filesystem::file_size(p, ec);
        if (!ec) info.fileSize = size;
        const auto mtime = std::filesystem::last_write_time(p, ec);
        if (!ec) {
            const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
            info.modifiedTimeUnix = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
        }
    }

    auto opened = ff::openInput(utf8Path);
    if (!opened) {
        LF_LOG_WARN(kLog, "probe failed for {}: {}", utf8Path, opened.error().fullText());
        return opened.error();
    }
    AVFormatContext* ctx = opened->get();

    info.containerName = ctx->iformat ? ctx->iformat->name : "";
    info.containerLongName = ctx->iformat && ctx->iformat->long_name ? ctx->iformat->long_name : "";
    info.bitRate = ctx->bit_rate;
    if (ctx->duration != AV_NOPTS_VALUE && ctx->duration > 0) {
        info.duration = Duration{ticksFromTimeBase(ctx->duration, Rational{1, AV_TIME_BASE})};
    }

    const int vIndex = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int aIndex = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (vIndex >= 0) {
        const AVStream* s = ctx->streams[vIndex];
        const AVCodecParameters* p = s->codecpar;
        // Attached pictures (album art) are not video.
        if (!(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            VideoStreamInfo v;
            v.streamIndex = vIndex;
            v.codecName = avcodec_get_name(p->codec_id);
            if (const AVCodecDescriptor* d = avcodec_descriptor_get(p->codec_id)) v.codecLongName = d->long_name ? d->long_name : "";
            if (const char* prof = avcodec_profile_name(p->codec_id, p->profile)) v.profile = prof;
            v.width = p->width;
            v.height = p->height;
            v.timeBase = ff::toRational(s->time_base);
            AVRational fr = s->avg_frame_rate;
            if (fr.num <= 0 || fr.den <= 0) fr = s->r_frame_rate;
            if (fr.num > 0 && fr.den > 0) v.frameRate = ff::toRational(fr);
            if (p->sample_aspect_ratio.num > 0 && p->sample_aspect_ratio.den > 0) v.sampleAspectRatio = ff::toRational(p->sample_aspect_ratio);
            const auto fmt = static_cast<AVPixelFormat>(p->format);
            if (const char* name = av_get_pix_fmt_name(fmt)) v.pixelFormat = name;
            if (const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt)) v.bitDepth = desc->comp[0].depth;
            v.bitRate = p->bit_rate;
            v.rotationDegrees = rotationFromStream(s);
            v.isFullRange = p->color_range == AVCOL_RANGE_JPEG;
            v.frameCount = s->nb_frames;
            if (v.frameCount == 0 && info.duration.ticks > 0 && v.frameRate.isValid() && !v.frameRate.isZero()) {
                v.frameCount = static_cast<std::int64_t>(std::llround(info.duration.toSeconds() * v.frameRate.toDouble()));
            }
            info.video = std::move(v);
        }
    }

    if (aIndex >= 0) {
        const AVStream* s = ctx->streams[aIndex];
        const AVCodecParameters* p = s->codecpar;
        AudioStreamInfo a;
        a.streamIndex = aIndex;
        a.codecName = avcodec_get_name(p->codec_id);
        if (const AVCodecDescriptor* d = avcodec_descriptor_get(p->codec_id)) a.codecLongName = d->long_name ? d->long_name : "";
        a.channels = p->ch_layout.nb_channels;
        char layout[64]{};
        if (av_channel_layout_describe(&p->ch_layout, layout, sizeof layout) > 0) a.channelLayout = layout;
        a.sampleRate = p->sample_rate;
        if (const char* name = av_get_sample_fmt_name(static_cast<AVSampleFormat>(p->format))) a.sampleFormat = name;
        a.bitRate = p->bit_rate;
        a.timeBase = ff::toRational(s->time_base);
        info.audio = std::move(a);
    }

    if (!info.video && !info.audio) {
        return Error{"The file contains no usable audio or video stream.", "no decodable streams (subtitles/data only)"};
    }

    if (info.video && isImageContainer(ctx)) {
        info.type = MediaType::Image;
        info.duration = Duration::zero();
        info.video->frameRate = Rational{};
        info.video->frameCount = 1;
    } else if (info.video) {
        info.type = MediaType::Video;
    } else {
        info.type = MediaType::Audio;
    }

    if (info.type != MediaType::Image && info.duration.ticks <= 0) {
        // Fall back to the longest stream duration.
        for (unsigned i = 0; i < ctx->nb_streams; ++i) {
            const AVStream* s = ctx->streams[i];
            if (s->duration != AV_NOPTS_VALUE && s->duration > 0) {
                const Duration d{ticksFromTimeBase(s->duration, ff::toRational(s->time_base))};
                info.duration = std::max(info.duration, d);
            }
        }
        if (info.duration.ticks <= 0) return Error{"The file's duration could not be determined.", "no duration in container or streams"};
    }

    LF_LOG_DEBUG(kLog, "probed {} [{}] {} {}x{} @ {:.3f} fps, {} {} ch, {:.2f} s, {}", info.fileName, mediaTypeName(info.type),
                 info.video ? info.video->codecName : "-", info.video ? info.video->width : 0, info.video ? info.video->height : 0,
                 info.video ? info.video->frameRate.toDouble() : 0.0, info.audio ? info.audio->codecName : "-",
                 info.audio ? info.audio->channels : 0, info.duration.toSeconds(), formatBytes(info.fileSize));
    return info;
}

}  // namespace lf::media
