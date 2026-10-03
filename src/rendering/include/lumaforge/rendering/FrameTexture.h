#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/VideoFrame.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace lf::render {

// A decoded picture living in textures the compositor can sample. Created
// through FrameTexturePool so buffers are recycled instead of reallocated
// every frame.
class FrameTexture {
public:
    enum class Kind { Nv12, I420, Bgra };

    [[nodiscard]] Kind kind() const noexcept { return m_kind; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] media::ColorMatrix colorMatrix() const noexcept { return m_matrix; }
    [[nodiscard]] bool isFullRange() const noexcept { return m_fullRange; }
    [[nodiscard]] bool isPremultiplied() const noexcept { return m_premultiplied; }
    [[nodiscard]] TimePoint pts() const noexcept { return m_pts; }
    void setPts(TimePoint t) noexcept { m_pts = t; }

    // Shader resource views in plane order (Y,UV | Y,U,V | RGBA).
    [[nodiscard]] ID3D11ShaderResourceView* const* views() const noexcept { return m_viewPtrs; }
    [[nodiscard]] int viewCount() const noexcept { return m_viewCount; }
    // The texture of one plane (copy targets for adjustment layers).
    [[nodiscard]] ID3D11Texture2D* plane(int i) const noexcept { return (i >= 0 && i < 3) ? m_tex[i].Get() : nullptr; }

    // Fill from a decoded frame: GPU copy for D3D11 frames, upload for CPU
    // frames. Takes the device context lock internally.
    Status fill(const media::VideoFrame& frame);
    // Fill from packed BGRA memory (images, text). rows are `stride` bytes apart.
    Status fillBgra(const std::uint8_t* bgra, int stride, bool premultiplied);

private:
    friend class FrameTexturePool;
    FrameTexture() = default;

    std::shared_ptr<hw::GpuDevice> m_device;
    Kind m_kind = Kind::Bgra;
    int m_width = 0;
    int m_height = 0;
    bool m_dynamic = false;   // CPU-writable (dynamic usage) vs GPU copy target (default usage)
    media::ColorMatrix m_matrix = media::ColorMatrix::Bt709;
    bool m_fullRange = false;
    bool m_premultiplied = false;
    TimePoint m_pts;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_tex[3];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv[3];
    ID3D11ShaderResourceView* m_viewPtrs[3] = {nullptr, nullptr, nullptr};
    int m_viewCount = 0;
};

// Recycles FrameTextures by (kind, size, dynamic). Thread-safe. Create via
// std::make_shared: textures hold a weak reference back to the pool.
class FrameTexturePool : public std::enable_shared_from_this<FrameTexturePool> {
public:
    explicit FrameTexturePool(std::shared_ptr<hw::GpuDevice> device);

    // Acquires a texture set matching the frame's layout and size, filled from it.
    Result<std::shared_ptr<FrameTexture>> acquireFor(const media::VideoFrame& frame);
    // Acquires a BGRA texture of the given size (caller fills it).
    Result<std::shared_ptr<FrameTexture>> acquireBgra(int width, int height, bool cpuWritable);

    // NV12 textures need shader sampling support; when the driver lacks it,
    // hardware frames are refused and the caller should decode in software.
    [[nodiscard]] bool supportsNv12Sampling() const noexcept { return m_nv12Sampling; }
    [[nodiscard]] std::size_t liveCount() const noexcept { return m_live.load(); }

private:
    Result<std::shared_ptr<FrameTexture>> acquire(FrameTexture::Kind kind, int w, int h, bool dynamic);
    Result<std::unique_ptr<FrameTexture>> createTexture(FrameTexture::Kind kind, int w, int h, bool dynamic);
    void recycle(FrameTexture* raw);

    std::shared_ptr<hw::GpuDevice> m_device;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<FrameTexture>> m_free;
    std::atomic<std::size_t> m_live{0};
    bool m_nv12Sampling = false;
    static constexpr std::size_t kMaxFree = 12;
};

}  // namespace lf::render
