#include "lumaforge/rendering/PreviewRenderer.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

namespace lf::render {

namespace {
constexpr const char* kLog = "render";
constexpr UINT kBufferCount = 2;
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

Error dxError(const char* what, HRESULT hr) {
    const bool lost = hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
    return Error{lost ? "The graphics device was reset." : "A preview rendering error occurred.",
                 std::string(what) + ": " + hresultToString(hr), hr};
}
}  // namespace

Result<std::unique_ptr<PreviewRenderer>> PreviewRenderer::create(std::shared_ptr<hw::GpuDevice> device, void* hwnd, int width, int height) {
    if (!device || !hwnd) return Error{"Preview renderer needs a device and a window."};
    std::unique_ptr<PreviewRenderer> r(new PreviewRenderer());
    r->m_device = std::move(device);
    r->m_width = std::max(width, 1);
    r->m_height = std::max(height, 1);

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(r->m_width);
    desc.Height = static_cast<UINT>(r->m_height);
    desc.Format = kFormat;
    desc.SampleDesc = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;   // Windows 10 flip model: lowest latency, no extra copy
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    HRESULT hr = r->m_device->dxgiFactory()->CreateSwapChainForHwnd(r->m_device->device(), static_cast<HWND>(hwnd), &desc, nullptr, nullptr, &r->m_swapChain);
    if (FAILED(hr)) return dxError("CreateSwapChainForHwnd", hr);

    // The window belongs to Qt; DXGI must not react to Alt+Enter on it.
    r->m_device->dxgiFactory()->MakeWindowAssociation(static_cast<HWND>(hwnd), DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    if (auto s = r->createTargetView(); !s) return s.error();
    LF_LOG_DEBUG(kLog, "preview swap chain created {}x{}", r->m_width, r->m_height);
    return r;
}

PreviewRenderer::~PreviewRenderer() {
    if (m_device && m_device->immediateContext()) {
        auto lock = m_device->contextLock();
        ID3D11RenderTargetView* nullViews[] = {nullptr};
        m_device->immediateContext()->OMSetRenderTargets(1, nullViews, nullptr);
    }
}

Status PreviewRenderer::createTargetView() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return dxError("IDXGISwapChain::GetBuffer", hr);
    hr = m_device->device()->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
    if (FAILED(hr)) return dxError("CreateRenderTargetView", hr);
    return Status::success();
}

Status PreviewRenderer::resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == m_width && height == m_height && m_rtv) return Status::success();

    auto lock = m_device->contextLock();
    m_rtv.Reset();
    ID3D11RenderTargetView* nullViews[] = {nullptr};
    m_device->immediateContext()->OMSetRenderTargets(1, nullViews, nullptr);
    m_device->immediateContext()->Flush();

    HRESULT hr = m_swapChain->ResizeBuffers(kBufferCount, static_cast<UINT>(width), static_cast<UINT>(height), kFormat, 0);
    if (FAILED(hr)) return dxError("ResizeBuffers", hr);
    m_width = width;
    m_height = height;
    return createTargetView();
}

Status PreviewRenderer::render(const PreviewScene& scene) {
    if (!m_rtv) return Error{"Preview renderer has no render target."};
    auto lock = m_device->contextLock();
    auto* ctx = m_device->immediateContext();

    m_frameRect = fitFrame(m_width, m_height, scene.frameWidth, scene.frameHeight);

    // Surround, then the project frame area. Until the video pipeline lands
    // the frame is a plain clear inside a scissor rect: no shaders involved.
    const float surround[4] = {scene.surround.r, scene.surround.g, scene.surround.b, scene.surround.a};
    ctx->ClearRenderTargetView(m_rtv.Get(), surround);

    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    if (m_frameRect.width > 0 && m_frameRect.height > 0) {
        const D3D11_RECT rect{m_frameRect.x, m_frameRect.y, m_frameRect.x + m_frameRect.width, m_frameRect.y + m_frameRect.height};
        // ClearView honours the rect list; available on D3D11.1 contexts (Win8+).
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ctx1;
        const float fill[4] = {scene.frameBackground.r, scene.frameBackground.g, scene.frameBackground.b, scene.frameBackground.a};
        if (SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&ctx1)))) {
            ctx1->ClearView(m_rtv.Get(), fill, &rect, 1);
        }
    }

    return present(false);
}

Status PreviewRenderer::present(bool vsync) {
    auto lock = m_device->contextLock();
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, 0);
    if (FAILED(hr)) return dxError("Present", hr);
    ++m_presented;
    return Status::success();
}

}  // namespace lf::render
