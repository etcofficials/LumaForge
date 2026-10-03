#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/VisualParams.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/rendering/FrameLayout.h"
#include "lumaforge/rendering/FrameTexture.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace lf::render {

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
};

// One picture to draw into the frame, bottom-to-top order in a Layer list.
struct Layer {
    std::shared_ptr<FrameTexture> texture;
    Transform2D transform;
    Crop crop;
    Mask mask;
    ColorAdjust color;
    EffectParams effects;
    // Scale-to-fit is the default for media; text/images already in frame
    // pixels set this false so 1 texel = 1 frame pixel at scale 1.
    bool fitToFrame = true;
    // Adjustment layer (no texture): what has been composited so far is
    // redrawn through this layer's colour, effects, mask, crop and opacity.
    bool adjustment = false;
};

// Offscreen frame buffer (BGRA8) the composition renders into. Sized to the
// preview quality or to the export resolution; the frame's logical size (the
// project resolution) is passed separately so layouts stay identical.
class RenderTarget {
public:
    ~RenderTarget();
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] ID3D11ShaderResourceView* srv() const noexcept { return m_srv.Get(); }
    [[nodiscard]] ID3D11RenderTargetView* rtv() const noexcept { return m_rtv.Get(); }
    [[nodiscard]] ID3D11Texture2D* texture() const noexcept { return m_texture.Get(); }

private:
    friend class Compositor;
    RenderTarget() = default;
    int m_width = 0, m_height = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;   // lazily created for readback
};

// Draws layers into a RenderTarget and blits targets to the screen. All
// immediate-context work happens under GpuDevice::contextLock(); callers on
// any thread may use it, one call at a time.
class Compositor {
public:
    static Result<std::unique_ptr<Compositor>> create(std::shared_ptr<hw::GpuDevice> device);
    ~Compositor();

    Result<std::unique_ptr<RenderTarget>> createTarget(int width, int height);

    // Renders `layers` (bottom to top) over `background` into `target`.
    // frameWidth/Height is the project resolution the layer transforms refer to.
    Status compose(RenderTarget& target, int frameWidth, int frameHeight, std::span<const Layer> layers, Rgba background);

    // Draws `source` letterboxed into `dst` on `backBuffer` (size bbW x bbH), surround-filled.
    Status blit(RenderTarget& source, ID3D11RenderTargetView* backBuffer, int bbW, int bbH, IntRect dst, Rgba surround);

    // Copies the target to CPU memory as packed BGRA (stride = width*4). Blocking.
    Status readback(RenderTarget& target, std::vector<std::uint8_t>& outBgra);

    [[nodiscard]] hw::GpuDevice& device() const noexcept { return *m_device; }
    [[nodiscard]] FrameTexturePool& texturePool() noexcept { return *m_pool; }
    [[nodiscard]] std::shared_ptr<FrameTexturePool> texturePoolPtr() const noexcept { return m_pool; }

private:
    Compositor() = default;
    Status init();
    void drawLayer(const Layer& layer, int frameW, int frameH, int targetW, int targetH);
    void drawTexture(ID3D11ShaderResourceView* srv, const float transform[16], bool premultiplied);

    std::shared_ptr<hw::GpuDevice> m_device;
    std::shared_ptr<FrameTexturePool> m_pool;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendPremultiplied;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendOpaque;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_noDepth;
};

}  // namespace lf::render
