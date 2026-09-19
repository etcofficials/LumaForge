#pragma once

#include "lumaforge/core/Time.h"

#include <cstdint>
#include <memory>

struct AVFrame;

namespace lf::media {

enum class PixelLayout {
    Unknown,
    Yuv420p,      // CPU: 3 planes, 8-bit
    Nv12,         // CPU: Y + interleaved UV, 8-bit
    D3D11Nv12,    // GPU: ID3D11Texture2D array slice (decoder output)
    Bgra,         // CPU: packed 8-bit BGRA (images, text)
};

enum class ColorMatrix { Bt601, Bt709, Bt2020 };

// A decoded picture. Ownership of the pixel storage is shared through the
// underlying AVFrame reference, so frames can sit in queues and caches
// without copies. For GPU frames the AVFrame keeps the decoder's texture
// array slice alive; the renderer copies it into its own pool once.
class VideoFrame {
public:
    VideoFrame() = default;
    explicit VideoFrame(AVFrame* takeOwnership, TimePoint pts, Duration duration, Rational timeBase);

    [[nodiscard]] bool isNull() const noexcept { return !m_frame; }
    [[nodiscard]] TimePoint pts() const noexcept { return m_pts; }
    [[nodiscard]] Duration duration() const noexcept { return m_duration; }
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] PixelLayout layout() const noexcept;
    [[nodiscard]] ColorMatrix colorMatrix() const noexcept;
    [[nodiscard]] bool isFullRange() const noexcept;
    [[nodiscard]] bool isGpu() const noexcept { return layout() == PixelLayout::D3D11Nv12; }

    // CPU planes (valid for CPU layouts).
    [[nodiscard]] const std::uint8_t* plane(int i) const noexcept;
    [[nodiscard]] int stride(int i) const noexcept;

    // GPU: the decoder texture and array index (valid for D3D11Nv12).
    [[nodiscard]] void* d3d11Texture() const noexcept;   // ID3D11Texture2D*
    [[nodiscard]] unsigned d3d11ArrayIndex() const noexcept;

    [[nodiscard]] const AVFrame* avFrame() const noexcept { return m_frame.get(); }

private:
    std::shared_ptr<AVFrame> m_frame;
    TimePoint m_pts;
    Duration m_duration;
};

}  // namespace lf::media
