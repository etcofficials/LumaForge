#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/rendering/FrameLayout.h"

#include <cstdint>
#include <memory>

namespace lf::render {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

struct PreviewScene {
    int frameWidth = 1920;      // project frame size
    int frameHeight = 1080;
    Color surround{0.086f, 0.094f, 0.106f, 1.0f};   // letterbox/pillarbox area
    Color frameBackground{0, 0, 0, 1};             // frame area when no video
};

// Direct3D 11 presentation surface for the preview. Owns a flip-model swap
// chain on a native window; shares the application's GpuDevice so decoded
// frames can later be drawn straight from their D3D11 textures.
//
// Thread model: any thread may call render()/resize() as long as calls are
// serialised by the owner (the widget for now; the render thread once
// playback exists). Immediate-context use is guarded by GpuDevice::contextLock().
class PreviewRenderer {
public:
    static Result<std::unique_ptr<PreviewRenderer>> create(std::shared_ptr<hw::GpuDevice> device, void* hwnd, int width, int height);
    ~PreviewRenderer();

    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;

    // Resizes the swap chain buffers; cheap when the size is unchanged.
    Status resize(int width, int height);

    // Draws the scene (surround + empty frame) and presents. Returns an error
    // carrying DXGI_ERROR_DEVICE_REMOVED/RESET when the device is lost; the
    // owner then recreates the device and this renderer.
    Status render(const PreviewScene& scene);

    [[nodiscard]] IntRect lastFrameRect() const noexcept { return m_frameRect; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] std::uint64_t presentedFrames() const noexcept { return m_presented; }

private:
    PreviewRenderer() = default;
    Status createTargetView();

    std::shared_ptr<hw::GpuDevice> m_device;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    int m_width = 0;
    int m_height = 0;
    IntRect m_frameRect;
    std::uint64_t m_presented = 0;
};

}  // namespace lf::render
