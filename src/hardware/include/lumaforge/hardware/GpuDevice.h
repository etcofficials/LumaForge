#pragma once

#include "lumaforge/core/Result.h"

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>
#include <string>

namespace lf::hw {

// The one Direct3D 11 device shared by the D3D11VA decoder, the preview
// renderer and (later) the GPU compositor. Sharing a single device is what
// lets decoded NV12 textures be rendered without leaving VRAM.
//
// Thread model: ID3D11Device is free-threaded. The immediate context is not;
// every user of the immediate context must hold contextLock(). FFmpeg's
// d3d11va hwcontext is given the same lock through its lock/unlock callbacks.
class GpuDevice {
public:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct CreateOptions {
        int adapterIndex = -1;         // -1 = default adapter
        bool allowDebugLayer = false;  // tries D3D11_CREATE_DEVICE_DEBUG, falls back silently
        bool allowWarp = true;         // fall back to the WARP software rasteriser
        bool forceWarp = false;        // skip hardware entirely (testing / --software-preview)
    };

    static Result<std::shared_ptr<GpuDevice>> create(const CreateOptions& options = {});
    ~GpuDevice();

    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;

    [[nodiscard]] ID3D11Device* device() const noexcept { return m_device.Get(); }
    [[nodiscard]] ID3D11DeviceContext* immediateContext() const noexcept { return m_context.Get(); }
    [[nodiscard]] IDXGIFactory2* dxgiFactory() const noexcept { return m_factory.Get(); }
    [[nodiscard]] IDXGIAdapter1* adapter() const noexcept { return m_adapter.Get(); }
    [[nodiscard]] ID3D11VideoDevice* videoDevice() const noexcept { return m_videoDevice.Get(); }  // may be null

    [[nodiscard]] D3D_FEATURE_LEVEL featureLevel() const noexcept { return m_featureLevel; }
    [[nodiscard]] std::string featureLevelString() const;
    [[nodiscard]] bool debugLayerActive() const noexcept { return m_debugLayer; }
    [[nodiscard]] bool isWarp() const noexcept { return m_isWarp; }
    [[nodiscard]] std::string description() const { return (m_isWarp ? std::string("WARP software, FL ") : std::string("Direct3D 11, FL ")) + featureLevelString(); }

    // Serialises immediate-context use between threads.
    [[nodiscard]] std::unique_lock<std::recursive_mutex> contextLock() { return std::unique_lock(m_contextMutex); }
    // Plain lock/unlock for C callbacks (FFmpeg's d3d11va hwcontext).
    void lockContext() { m_contextMutex.lock(); }
    void unlockContext() { m_contextMutex.unlock(); }

    // True when the device has been removed/reset; owner should recreate.
    [[nodiscard]] bool isLost() const;
    [[nodiscard]] HRESULT deviceRemovedReason() const;

private:
    GpuDevice() = default;

    ComPtr<IDXGIFactory2> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11VideoDevice> m_videoDevice;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_9_1;
    bool m_debugLayer = false;
    bool m_isWarp = false;
    std::recursive_mutex m_contextMutex;
};

[[nodiscard]] std::string featureLevelToString(D3D_FEATURE_LEVEL level);

}  // namespace lf::hw
