#include "lumaforge/media/AudioDecoder.h"

#include "FFmpegUtil.h"
#include "lumaforge/core/Log.h"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <vector>

namespace lf::media {

namespace {
constexpr const char* kLog = "decode";
}

struct AudioDecoder::Impl {
    ff::FormatContextPtr format;
    ff::CodecContextPtr codec;
    ff::PacketPtr packet{av_packet_alloc()};
    ff::FramePtr frame{av_frame_alloc()};
    SwrContext* swr = nullptr;
    AVAudioFifo* fifo = nullptr;
    AVStream* stream = nullptr;
    int streamIndex = -1;
    Rational timeBase;
    std::int64_t startPts = 0;
    bool draining = false;
    std::vector<std::uint8_t> convBuffer;
    bool havePosition = false;   // first decoded frame after a seek sets the position

    ~Impl() {
        if (fifo) av_audio_fifo_free(fifo);
        if (swr) swr_free(&swr);
    }
};

AudioDecoder::~AudioDecoder() = default;

Result<std::unique_ptr<AudioDecoder>> AudioDecoder::open(const std::string& utf8Path, const Options& options) {
    ff::installLogBridge();
    std::unique_ptr<AudioDecoder> dec(new AudioDecoder());
    dec->m_rate = options.sampleRate;
    dec->m_channels = options.channels;
    auto impl = std::make_unique<Impl>();

    auto opened = ff::openInput(utf8Path);
    if (!opened) return opened.error();
    impl->format = std::move(opened.value());
    impl->streamIndex = av_find_best_stream(impl->format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (impl->streamIndex < 0) return Error{"The file has no audio stream.", "av_find_best_stream(AUDIO) failed"};
    impl->stream = impl->format->streams[impl->streamIndex];
    for (unsigned i = 0; i < impl->format->nb_streams; ++i) {
        if (static_cast<int>(i) != impl->streamIndex) impl->format->streams[i]->discard = AVDISCARD_ALL;
    }
    impl->timeBase = ff::toRational(impl->stream->time_base);
    impl->startPts = impl->stream->start_time != AV_NOPTS_VALUE ? impl->stream->start_time : 0;

    auto codec = ff::openDecoder(impl->stream, 1);
    if (!codec) return codec.error();
    impl->codec = std::move(codec.value());

    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, options.channels);
    // Files with an unspecified layout (common for WAV) get the default layout
    // for their channel count; swresample would otherwise guess a matrix.
    AVChannelLayout inLayout{};
    if (impl->codec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC && impl->codec->ch_layout.nb_channels > 0) av_channel_layout_copy(&inLayout, &impl->codec->ch_layout);
    else av_channel_layout_default(&inLayout, std::max(1, impl->codec->ch_layout.nb_channels));
    int err = swr_alloc_set_opts2(&impl->swr, &outLayout, AV_SAMPLE_FMT_FLT, options.sampleRate, &inLayout, impl->codec->sample_fmt,
                                  impl->codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (err < 0 || !impl->swr) return ff::makeError("The audio format cannot be converted.", "swr_alloc_set_opts2", err);
    // Mono sources play at full level in every output channel (editor
    // convention). swresample's built-in mono->stereo matrix is hard-wired to
    // -3 dB, so supply the matrix explicitly.
    if (impl->codec->ch_layout.nb_channels == 1 && options.channels > 1) {
        std::vector<double> matrix(static_cast<std::size_t>(options.channels), 1.0);   // out x in (in = 1)
        swr_set_matrix(impl->swr, matrix.data(), 1);
    }
    err = swr_init(impl->swr);
    if (err < 0) return ff::makeError("The audio resampler could not be initialised.", "swr_init", err);
    impl->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, options.channels, options.sampleRate);
    if (!impl->fifo) return Error{"Out of memory while opening the audio decoder."};

    dec->m_info.streamIndex = impl->streamIndex;
    dec->m_info.codecName = impl->codec->codec->name;
    dec->m_info.channels = impl->codec->ch_layout.nb_channels;
    dec->m_info.sampleRate = impl->codec->sample_rate;
    dec->m_info.timeBase = impl->timeBase;
    if (impl->format->duration != AV_NOPTS_VALUE && impl->format->duration > 0) dec->m_duration = Duration{ticksFromTimeBase(impl->format->duration, Rational{1, AV_TIME_BASE})};
    else if (impl->stream->duration != AV_NOPTS_VALUE) dec->m_duration = Duration{ticksFromTimeBase(impl->stream->duration, impl->timeBase)};

    dec->m_impl = std::move(impl);
    LF_LOG_INFO(kLog, "opened audio {} ({} ch {} Hz -> {} ch {} Hz)", dec->m_info.codecName, dec->m_info.channels, dec->m_info.sampleRate, options.channels, options.sampleRate);
    return dec;
}

TimePoint AudioDecoder::position() const noexcept {
    return TimePoint{ticksFromTimeBase(m_positionSamples, Rational{1, m_rate})};
}

Status AudioDecoder::seek(TimePoint t) {
    auto& i = *m_impl;
    const Ticks target = std::max<Ticks>(0, t.ticks);
    const std::int64_t ts = timeBaseFromTicks(target, i.timeBase) + i.startPts;
    const int err = av_seek_frame(i.format.get(), i.streamIndex, ts, AVSEEK_FLAG_BACKWARD);
    if (err < 0) return ff::makeError("Seeking in the audio failed.", "av_seek_frame", err);
    avcodec_flush_buffers(i.codec.get());
    swr_init(i.swr);   // drops resampler history
    av_audio_fifo_reset(i.fifo);
    i.draining = false;
    i.havePosition = false;
    m_eof = false;
    // Decoding resumes at a keyframe <= target; the first frame's pts tells us
    // how much to discard so read() starts exactly at `target`.
    m_positionSamples = timeBaseFromTicks(target, Rational{1, m_rate});
    m_skipSamples = -1;   // resolved on the first decoded frame
    return Status::success();
}

Status AudioDecoder::decodeMore() {
    auto& i = *m_impl;
    for (int guard = 0; guard < 512; ++guard) {
        const int r = avcodec_receive_frame(i.codec.get(), i.frame.get());
        if (r == 0) {
            if (!i.havePosition) {
                const std::int64_t pts = (i.frame->best_effort_timestamp != AV_NOPTS_VALUE ? i.frame->best_effort_timestamp : i.frame->pts) - i.startPts;
                const std::int64_t frameStartSamples = timeBaseFromTicks(ticksFromTimeBase(pts, i.timeBase), Rational{1, m_rate});
                if (m_skipSamples < 0) m_skipSamples = std::max<std::int64_t>(0, m_positionSamples - frameStartSamples);
                if (frameStartSamples > m_positionSamples) m_positionSamples = frameStartSamples;   // gap: start where data begins
                i.havePosition = true;
            }
            const int outCap = swr_get_out_samples(i.swr, i.frame->nb_samples);
            i.convBuffer.resize(static_cast<std::size_t>(outCap) * m_channels * sizeof(float));
            std::uint8_t* outPlanes[1] = {i.convBuffer.data()};
            const int converted = swr_convert(i.swr, outPlanes, outCap, const_cast<const std::uint8_t**>(i.frame->extended_data), i.frame->nb_samples);
            av_frame_unref(i.frame.get());
            if (converted < 0) return ff::makeError("Audio conversion failed.", "swr_convert", converted);
            if (converted > 0) {
                int skip = static_cast<int>(std::min<std::int64_t>(m_skipSamples, converted));
                m_skipSamples -= skip;
                void* data = i.convBuffer.data() + static_cast<std::size_t>(skip) * m_channels * sizeof(float);
                if (converted - skip > 0) av_audio_fifo_write(i.fifo, &data, converted - skip);
            }
            if (av_audio_fifo_size(i.fifo) > 0) return Status::success();
            continue;
        }
        if (r == AVERROR_EOF) {
            // Flush the resampler tail.
            const int tail = swr_get_out_samples(i.swr, 0);
            if (tail > 0) {
                i.convBuffer.resize(static_cast<std::size_t>(tail) * m_channels * sizeof(float));
                std::uint8_t* outPlanes[1] = {i.convBuffer.data()};
                const int converted = swr_convert(i.swr, outPlanes, tail, nullptr, 0);
                if (converted > 0) { void* data = i.convBuffer.data(); av_audio_fifo_write(i.fifo, &data, converted); }
            }
            m_eof = true;
            return Status::success();
        }
        if (r != AVERROR(EAGAIN)) {
            LF_LOG_WARN(kLog, "audio decode error: {}", ff::errorString(r));
            if (i.draining) { m_eof = true; return Status::success(); }
        }
        // Need more input
        while (!i.draining) {
            const int rr = av_read_frame(i.format.get(), i.packet.get());
            if (rr == AVERROR_EOF) {
                i.draining = true;
                avcodec_send_packet(i.codec.get(), nullptr);
                break;
            }
            if (rr < 0) return ff::makeError("Reading audio from the file failed.", "av_read_frame", rr);
            if (i.packet->stream_index != i.streamIndex) { av_packet_unref(i.packet.get()); continue; }
            const int s = avcodec_send_packet(i.codec.get(), i.packet.get());
            av_packet_unref(i.packet.get());
            if (s == 0 || s == AVERROR(EAGAIN)) break;
            if (s < 0 && s != AVERROR_EOF) LF_LOG_WARN(kLog, "audio packet rejected: {}", ff::errorString(s));
        }
    }
    return Status::success();
}

Result<int> AudioDecoder::read(float* out, int frames) {
    auto& i = *m_impl;
    int written = 0;
    while (written < frames) {
        if (av_audio_fifo_size(i.fifo) == 0) {
            if (m_eof) break;
            if (auto s = decodeMore(); !s) return s.error();
            if (av_audio_fifo_size(i.fifo) == 0 && m_eof) break;
            continue;
        }
        const int want = std::min(frames - written, av_audio_fifo_size(i.fifo));
        void* dst = out + static_cast<std::size_t>(written) * m_channels;
        const int got = av_audio_fifo_read(i.fifo, &dst, want);
        if (got <= 0) break;
        written += got;
    }
    m_positionSamples += written;
    return written;
}

}  // namespace lf::media
