#include "lumaforge/hardware/GpuDevice.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <array>

namespace lf::hw {

namespace {
constexpr const char* kLog = "hw";

constexpr std::array kFeatureLevels = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
};
}  // namespace

std::string featureLevelToString(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_12_2: return "12_2";
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        case D3D_FEATURE_LEVEL_9_3: return "9_3";
        case D3D_FEATURE_LEVEL_9_2: return "9_2";
        case D3D_FEATURE_LEVEL_9_1: return "9_1";
        default: return std::format("0x{:X}", static_cast<unsigned>(level));
    }
}

Result<std::shared_ptr<GpuDevice>> GpuDevice::create(const CreateOptions& options) {
    std::shared_ptr<GpuDevice> gpu(new GpuDevice());

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&gpu->m_factory));
    if (FAILED(hr)) {
        return Error{"Direct3D is not available on this system.", "CreateDXGIFactory1: " + hresultToString(hr), hr};
    }

    // Pick the adapter: explicit index, else the first hardware adapter DXGI reports.
    ComPtr<IDXGIAdapter1> adapter;
    if (options.adapterIndex >= 0) {
        hr = gpu->m_factory->EnumAdapters1(static_cast<UINT>(options.adapterIndex), &adapter);
        if (FAILED(hr)) return Error{"The requested graphics adapter was not found.", "EnumAdapters1: " + hresultToString(hr), hr};
    } else {
        for (UINT i = 0; gpu->m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
            adapter.Reset();
        }
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifndef NDEBUG
    const bool tryDebug = options.allowDebugLayer;
#else
    const bool tryDebug = false;
#endif

    auto tryCreate = [&](D3D_DRIVER_TYPE type, UINT createFlags) {
        gpu->m_device.Reset();
        gpu->m_context.Reset();
        return D3D11CreateDevice(type == D3D_DRIVER_TYPE_UNKNOWN ? adapter.Get() : nullptr, type, nullptr,
                                 createFlags, kFeatureLevels.data(), static_cast<UINT>(kFeatureLevels.size()),
                                 D3D11_SDK_VERSION, &gpu->m_device, &gpu->m_featureLevel, &gpu->m_context);
    };

    hr = E_FAIL;
    if (!options.forceWarp) {
        const D3D_DRIVER_TYPE type = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
        if (tryDebug) {
            hr = tryCreate(type, flags | D3D11_CREATE_DEVICE_DEBUG);
            gpu->m_debugLayer = SUCCEEDED(hr);
        }
        if (FAILED(hr)) hr = tryCreate(type, flags);
        if (FAILED(hr)) {
            // VIDEO_SUPPORT can be refused by some drivers; a device without it is still useful for rendering.
            flags &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
            hr = tryCreate(type, flags);
        }
        if (FAILED(hr)) LF_LOG_WARN(kLog, "hardware D3D11 device unavailable ({}); trying WARP", hresultToString(hr));
    }
    if (FAILED(hr) && options.allowWarp) {
        // WARP: Microsoft's software rasteriser, part of Windows. Every shader
        // LumaForge uses runs on it unchanged, so the CPU fallback is the same
        // compositor, only slower. No video decode acceleration.
        adapter.Reset();
        flags &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        hr = tryCreate(D3D_DRIVER_TYPE_WARP, flags);
        gpu->m_isWarp = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        return Error{"Could not create a Direct3D 11 device. The preview will use software rendering.",
                     "D3D11CreateDevice: " + hresultToString(hr), hr};
    }

    gpu->m_adapter = adapter;
    if (!gpu->m_adapter) {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> baseAdapter;
        if (SUCCEEDED(gpu->m_device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&baseAdapter))) {
            baseAdapter.As(&gpu->m_adapter);
        }
    }

    // Multithread protection is required because the decoder (FFmpeg thread)
    // and the renderer (render thread) share this device.
    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(gpu->m_context.As(&multithread))) multithread->SetMultithreadProtected(TRUE);

    // Video device is optional: without it we simply have no D3D11VA decode.
    gpu->m_device.As(&gpu->m_videoDevice);

    LF_LOG_INFO(kLog, "D3D11 device created: {}feature level {}{}{}", gpu->m_isWarp ? "WARP (software), " : "", gpu->featureLevelString(),
                gpu->m_videoDevice ? ", video device available" : ", no video device",
                gpu->m_debugLayer ? ", debug layer" : "");
    return gpu;
}

GpuDevice::~GpuDevice() {
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }
}

std::string GpuDevice::featureLevelString() const { return featureLevelToString(m_featureLevel); }

bool GpuDevice::isLost() const { return m_device && m_device->GetDeviceRemovedReason() != S_OK; }

HRESULT GpuDevice::deviceRemovedReason() const { return m_device ? m_device->GetDeviceRemovedReason() : E_FAIL; }

}  // namespace lf::hw
