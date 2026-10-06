#include "lumaforge/export/FastCut.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/project/Subtitles.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <filesystem>
#include <vector>

namespace lf::exporting {

using namespace lf::project;

namespace {
constexpr const char* kLog = "export";

std::string avError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

std::filesystem::path toPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

const char* muxerName(Container c) {
    switch (c) {
        case Container::Mov: return "mov";
        case Container::Mkv: return "matroska";
        default: return "mp4";
    }
}

// The first reason the clip would change its picture or sound, or empty.
std::string processingIn(const Clip& c) {
    if (c.speed != Rational{1, 1} || c.reverse || c.freeze) return "the clip's speed is changed";
    if (!c.animation.empty()) return "the clip has keyframes";
    if (c.transitionIn || c.transitionOut) return "the clip has a transition";
    if (isVisualClip(c.kind)) {
        if (!(c.transform == Transform2D{})) return "the clip is moved, scaled, rotated or faded";
        if (!c.crop.isIdentity() || c.mask.isActive()) return "the clip is cropped or masked";
        if (!c.color.isIdentity()) return "the clip has a colour correction";
        for (const auto& e : c.effects) if (e.enabled) return "the clip has effects";
    } else {
        if (!(c.audio == AudioParams{})) return "the clip's volume, pan, fades or audio effects are changed";
    }
    return {};
}
}  // namespace

FastCutCheck checkFastCut(const Project& project, const ExportSettings& settings) {
    FastCutCheck r;
    const auto& seq = project.sequence;
    const TimeRange range = exportRange(settings, project);
    if (range.duration.ticks <= 0) { r.reason = "nothing to export"; return r; }
    if (seq.masterVolume != 1.0f) { r.reason = "the master volume is changed"; return r; }
    if (settings.burnSubtitles && !subtitleCues(seq, range).empty()) { r.reason = "subtitles are burnt into the picture"; return r; }

    const Clip* video = nullptr;
    const Clip* audio = nullptr;
    for (const auto& c : seq.clips) {
        if (!c.enabled || !(c.start < range.end() && range.start < c.end())) continue;
        const Track* t = seq.findTrack(c.track);
        if (!t || t->muted) continue;
        if (c.kind == ClipKind::Video && !video) { video = &c; continue; }
        if (c.kind == ClipKind::Audio && !audio && video && c.link == video->id) { audio = &c; continue; }
        if (c.kind == ClipKind::Audio && !audio && !video) { audio = &c; continue; }
        r.reason = "more than one clip is in the range";
        return r;
    }
    if (!video) { r.reason = "the range has no single video clip"; return r; }
    if (audio && (audio->link != video->id || audio->start != video->start || audio->duration != video->duration)) { r.reason = "audio and video are not one linked clip"; return r; }
    if (range.start < video->start || range.end() > video->end()) { r.reason = "the range is not inside one clip (there are gaps)"; return r; }
    for (const Clip* c : {video, audio}) {
        if (!c) continue;
        if (auto why = processingIn(*c); !why.empty()) { r.reason = why; return r; }
        if (const Track* t = seq.findTrack(c->track); t && c->kind == ClipKind::Audio && (t->volume != 1.0f || t->pan != 0.0f)) { r.reason = "the audio track's volume or pan is changed"; return r; }
    }
    const MediaItem* m = project.findMedia(video->media);
    if (!m || m->offline) { r.reason = "the source file is missing"; return r; }
    r.eligible = true;
    r.sourcePath = m->path;
    r.sourceIn = video->sourceTimeAt(range.start);
    r.sourceOut = video->sourceTimeAt(range.end());
    r.withAudio = audio != nullptr && settings.includeAudio;
    return r;
}

Status fastCut(const FastCutCheck& check, const ExportSettings& settings, const std::function<void(double)>& progress, const std::atomic<bool>& cancel) {
    if (!check.eligible) return Error{"Lossless export is not possible: " + check.reason + "."};
    AVFormatContext* in = nullptr;
    int r = avformat_open_input(&in, check.sourcePath.c_str(), nullptr, nullptr);
    if (r < 0) return Error{"The source file could not be opened.", avError(r)};
    struct Guard { AVFormatContext*& in; AVFormatContext* out = nullptr; ~Guard() { if (out) { if (out->pb) avio_closep(&out->pb); avformat_free_context(out); } avformat_close_input(&in); } } g{in};
    if ((r = avformat_find_stream_info(in, nullptr)) < 0) return Error{"The source file could not be read.", avError(r)};
    const int vs = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int as = check.withAudio ? av_find_best_stream(in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) : -1;
    if (vs < 0) return Error{"The source has no video stream."};

    const auto finalPath = toPath(settings.outputPath);
    auto partial = finalPath;
    partial.replace_extension(std::u8string(u8".partial") + finalPath.extension().u8string());
    const auto pu8 = partial.u8string();
    const std::string partialPath(pu8.begin(), pu8.end());
    if ((r = avformat_alloc_output_context2(&g.out, nullptr, muxerName(settings.container), partialPath.c_str())) < 0) return Error{"The output file could not be created.", avError(r)};
    std::vector<int> map(in->nb_streams, -1);
    for (int s : {vs, as}) {
        if (s < 0) continue;
        AVStream* o = avformat_new_stream(g.out, nullptr);
        avcodec_parameters_copy(o->codecpar, in->streams[s]->codecpar);
        o->codecpar->codec_tag = 0;
        o->time_base = in->streams[s]->time_base;
        map[s] = o->index;
    }
    if ((r = avio_open(&g.out->pb, partialPath.c_str(), AVIO_FLAG_WRITE)) < 0) return Error{"The output file could not be written.", avError(r)};
    AVDictionary* opts = nullptr;
    if (settings.fastStart && settings.container != Container::Mkv) av_dict_set(&opts, "movflags", "+faststart", 0);
    r = avformat_write_header(g.out, &opts);
    av_dict_free(&opts);
    if (r < 0) return Error{"This source cannot be copied into " + std::string(containerName(settings.container)) + " without re-encoding.", avError(r)};

    // Seek to the keyframe at or before the In point (stream-relative to the file start).
    AVStream* vstream = in->streams[vs];
    const std::int64_t startOffset = vstream->start_time != AV_NOPTS_VALUE ? vstream->start_time : 0;
    const AVRational flicks{1, static_cast<int>(kFlicksPerSecond)};
    const std::int64_t seekTs = av_rescale_q(check.sourceIn.ticks, flicks, vstream->time_base) + startOffset;
    const std::int64_t endTs = av_rescale_q(check.sourceOut.ticks, flicks, vstream->time_base) + startOffset;
    av_seek_frame(in, vs, seekTs, AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    std::int64_t zeroUs = AV_NOPTS_VALUE;   // first copied video keyframe, in microseconds: becomes time 0
    bool videoDone = false;
    const std::int64_t endUs = av_rescale_q(endTs - startOffset, vstream->time_base, AVRational{1, 1000000}) + av_rescale_q(startOffset, vstream->time_base, AVRational{1, 1000000});
    bool sawKey = false;
    const double totalUs = (check.sourceOut - check.sourceIn).toSeconds() * 1e6;
    auto abort = [&](Error e) -> Status {
        av_packet_free(&pkt);
        if (g.out->pb) avio_closep(&g.out->pb);
        std::error_code ec;
        std::filesystem::remove(partial, ec);
        LF_LOG_WARN(kLog, "fast cut stopped: {}", e.fullText());
        return e;
    };
    while (av_read_frame(in, pkt) >= 0) {
        if (cancel.load()) return abort(Error{"Export cancelled."});
        const int si = pkt->stream_index;
        if (si >= static_cast<int>(map.size()) || map[si] < 0) { av_packet_unref(pkt); continue; }
        AVStream* is = in->streams[si];
        const std::int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        const std::int64_t tsUs = ts != AV_NOPTS_VALUE ? av_rescale_q(ts, is->time_base, AVRational{1, 1000000}) : 0;
        bool keep = true;
        if (si == vs) {
            if (!sawKey && (pkt->flags & AV_PKT_FLAG_KEY)) {
                sawKey = true;
                zeroUs = av_rescale_q(pkt->dts != AV_NOPTS_VALUE ? pkt->dts : ts, is->time_base, AVRational{1, 1000000});
            }
            if (!sawKey) keep = false;
            else if (ts != AV_NOPTS_VALUE && ts >= endTs) { videoDone = true; keep = false; }
        } else {
            // Audio: from the first keyframe's time to the Out point.
            if (!sawKey || tsUs >= endUs) keep = false;
            if (videoDone && tsUs >= endUs) { av_packet_unref(pkt); break; }
        }
        if (videoDone && as < 0) { av_packet_unref(pkt); break; }
        if (!keep) { av_packet_unref(pkt); continue; }
        // Shift every stream by the same amount so audio stays in sync; drop what precedes 0.
        const std::int64_t shift = av_rescale_q(zeroUs, AVRational{1, 1000000}, is->time_base);
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= shift;
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= shift;
        if (si != vs && pkt->pts != AV_NOPTS_VALUE && pkt->pts < 0) { av_packet_unref(pkt); continue; }
        AVStream* os = g.out->streams[map[si]];
        av_packet_rescale_ts(pkt, is->time_base, os->time_base);
        pkt->stream_index = map[si];
        pkt->pos = -1;
        if (progress && si == vs && pkt->pts != AV_NOPTS_VALUE && totalUs > 0)
            progress(std::clamp(av_rescale_q(pkt->pts, os->time_base, AVRational{1, 1000000}) / totalUs, 0.0, 1.0));
        if ((r = av_interleaved_write_frame(g.out, pkt)) < 0) return abort(Error{"Writing the file failed (is the disk full?).", avError(r)});
    }
    av_packet_free(&pkt);
    if ((r = av_write_trailer(g.out)) < 0) return abort(Error{"Finishing the file failed.", avError(r)});
    avio_closep(&g.out->pb);
    std::error_code ec;
    std::filesystem::remove(finalPath, ec);
    std::filesystem::rename(partial, finalPath, ec);
    if (ec) return Error{"The finished file could not be renamed into place.", ec.message()};
    if (progress) progress(1.0);
    LF_LOG_INFO(kLog, "fast cut (lossless) {} [{:.3f}..{:.3f} s] -> {}", check.sourcePath, check.sourceIn.toSeconds(), check.sourceOut.toSeconds(), settings.outputPath);
    return Status::success();
}

}  // namespace lf::exporting
