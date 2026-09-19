#include "lumaforge/media/VideoFrame.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace lf::media {

VideoFrame::VideoFrame(AVFrame* takeOwnership, TimePoint pts, Duration duration, Rational) : m_pts(pts), m_duration(duration) {
    m_frame = std::shared_ptr<AVFrame>(takeOwnership, [](AVFrame* f) { av_frame_free(&f); });
}

int VideoFrame::width() const noexcept { return m_frame ? m_frame->width : 0; }
int VideoFrame::height() const noexcept { return m_frame ? m_frame->height : 0; }

PixelLayout VideoFrame::layout() const noexcept {
    if (!m_frame) return PixelLayout::Unknown;
    switch (m_frame->format) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P: return PixelLayout::Yuv420p;
        case AV_PIX_FMT_NV12: return PixelLayout::Nv12;
        case AV_PIX_FMT_D3D11: return PixelLayout::D3D11Nv12;
        case AV_PIX_FMT_BGRA: return PixelLayout::Bgra;
        default: return PixelLayout::Unknown;
    }
}

ColorMatrix VideoFrame::colorMatrix() const noexcept {
    if (!m_frame) return ColorMatrix::Bt709;
    switch (m_frame->colorspace) {
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_SMPTE240M: return ColorMatrix::Bt601;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: return ColorMatrix::Bt2020;
        case AVCOL_SPC_UNSPECIFIED:
            // Heuristic used by every player: SD is 601, HD is 709.
            return (m_frame->height <= 576) ? ColorMatrix::Bt601 : ColorMatrix::Bt709;
        default: return ColorMatrix::Bt709;
    }
}

bool VideoFrame::isFullRange() const noexcept {
    return m_frame && (m_frame->color_range == AVCOL_RANGE_JPEG || m_frame->format == AV_PIX_FMT_YUVJ420P);
}

const std::uint8_t* VideoFrame::plane(int i) const noexcept { return m_frame && i >= 0 && i < AV_NUM_DATA_POINTERS ? m_frame->data[i] : nullptr; }
int VideoFrame::stride(int i) const noexcept { return m_frame && i >= 0 && i < AV_NUM_DATA_POINTERS ? m_frame->linesize[i] : 0; }

void* VideoFrame::d3d11Texture() const noexcept { return m_frame && m_frame->format == AV_PIX_FMT_D3D11 ? m_frame->data[0] : nullptr; }
unsigned VideoFrame::d3d11ArrayIndex() const noexcept { return m_frame && m_frame->format == AV_PIX_FMT_D3D11 ? static_cast<unsigned>(reinterpret_cast<std::intptr_t>(m_frame->data[1])) : 0; }

}  // namespace lf::media
