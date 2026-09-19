#include "lumaforge/rendering/FrameTexture.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <algorithm>
#include <cstring>

namespace lf::render {

namespace {
constexpr const char* kLog = "render";

Error dxError(const char* what, HRESULT hr) { return Error{"A GPU texture operation failed.", std::string(what) + ": " + hresultToString(hr), hr}; }

// Copies a plane row by row into a mapped dynamic texture.
void copyPlane(const D3D11_MAPPED_SUBRESOURCE& mapped, const std::uint8_t* src, int srcStride, int rowBytes, int rows) {
    auto* dst = static_cast<std::uint8_t*>(mapped.pData);
    if (static_cast<int>(mapped.RowPitch) == srcStride && srcStride == rowBytes) {
        std::memcpy(dst, src, static_cast<std::size_t>(rowBytes) * rows);
        return;
    }
    for (int y = 0; y < rows; ++y) std::memcpy(dst + static_cast<std::size_t>(y) * mapped.RowPitch, src + static_cast<std::size_t>(y) * srcStride, static_cast<std::size_t>(rowBytes));
}
}  // namespace

// ---------------------------------------------------------------- FrameTexture

Status FrameTexture::fill(const media::VideoFrame& frame) {
    m_matrix = frame.colorMatrix();
    m_fullRange = frame.isFullRange();
    m_pts = frame.pts();
    m_premultiplied = false;
    auto* ctx = m_device->immediateContext();
    auto lock = m_device->contextLock();

    if (frame.isGpu()) {
        if (m_kind != Kind::Nv12 || m_dynamic) return Error{"Texture kind mismatch for GPU frame."};
        auto* src = static_cast<ID3D11Texture2D*>(frame.d3d11Texture());
        // Decoder surfaces may be padded (e.g. 1088 rows for 1080p): copy only the visible box.
        D3D11_BOX box{0, 0, 0, static_cast<UINT>(m_width), static_cast<UINT>(m_height), 1};
        ctx->CopySubresourceRegion(m_tex[0].Get(), 0, 0, 0, 0, src, frame.d3d11ArrayIndex(), &box);
        return Status::success();
    }

    if (!m_dynamic) return Error{"Texture is not CPU-writable."};
    const int planes = m_kind == Kind::I420 ? 3 : (m_kind == Kind::Nv12 ? 2 : 1);
    for (int p = 0; p < planes; ++p) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = ctx->Map(m_tex[p].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return dxError("Map", hr);
        const bool chroma = p > 0;
        const int rows = chroma ? (m_height + 1) / 2 : m_height;
        int rowBytes = chroma ? (m_width + 1) / 2 : m_width;
        if (m_kind == Kind::Nv12 && p == 1) rowBytes = ((m_width + 1) / 2) * 2;
        if (m_kind == Kind::Bgra) rowBytes = m_width * 4;
        copyPlane(mapped, frame.plane(p), frame.stride(p), rowBytes, rows);
        ctx->Unmap(m_tex[p].Get(), 0);
    }
    return Status::success();
}

Status FrameTexture::fillBgra(const std::uint8_t* bgra, int stride, bool premultiplied) {
    if (m_kind != Kind::Bgra || !m_dynamic) return Error{"Texture is not a CPU-writable BGRA texture."};
    m_premultiplied = premultiplied;
    auto* ctx = m_device->immediateContext();
    auto lock = m_device->contextLock();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx->Map(m_tex[0].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return dxError("Map", hr);
    copyPlane(mapped, bgra, stride, m_width * 4, m_height);
    ctx->Unmap(m_tex[0].Get(), 0);
    return Status::success();
}

// ---------------------------------------------------------------- FrameTexturePool

FrameTexturePool::FrameTexturePool(std::shared_ptr<hw::GpuDevice> device) : m_device(std::move(device)) {
    UINT support = 0;
    if (SUCCEEDED(m_device->device()->CheckFormatSupport(DXGI_FORMAT_NV12, &support))) {
        m_nv12Sampling = (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) && (support & D3D11_FORMAT_SUPPORT_TEXTURE2D);
    }
    LF_LOG_INFO(kLog, "frame texture pool: NV12 shader sampling {}", m_nv12Sampling ? "supported" : "NOT supported (hardware frames will be refused)");
}

Result<std::unique_ptr<FrameTexture>> FrameTexturePool::createTexture(FrameTexture::Kind kind, int w, int h, bool dynamic) {
    std::unique_ptr<FrameTexture> t(new FrameTexture());
    t->m_device = m_device;
    t->m_kind = kind;
    t->m_width = w;
    t->m_height = h;
    t->m_dynamic = dynamic;
    auto* dev = m_device->device();

    auto makeTex = [&](int slot, DXGI_FORMAT fmt, int tw, int th) -> Status {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(tw);
        d.Height = static_cast<UINT>(th);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = fmt;
        d.SampleDesc = {1, 0};
        d.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
        HRESULT hr = dev->CreateTexture2D(&d, nullptr, &t->m_tex[slot]);
        if (FAILED(hr)) return dxError("CreateTexture2D", hr);
        return Status::success();
    };
    auto makeView = [&](int slot, int texSlot, DXGI_FORMAT fmt) -> Status {
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = fmt;
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipLevels = 1;
        HRESULT hr = dev->CreateShaderResourceView(t->m_tex[texSlot].Get(), &v, &t->m_srv[slot]);
        if (FAILED(hr)) return dxError("CreateShaderResourceView", hr);
        t->m_viewPtrs[slot] = t->m_srv[slot].Get();
        return Status::success();
    };

    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    switch (kind) {
        case FrameTexture::Kind::Nv12:
            if (dynamic) {
                // CPU NV12: two mappable textures (Y as R8, UV as R8G8).
                if (auto s = makeTex(0, DXGI_FORMAT_R8_UNORM, w, h); !s) return s.error();
                if (auto s = makeTex(1, DXGI_FORMAT_R8G8_UNORM, cw, ch); !s) return s.error();
                if (auto s = makeView(0, 0, DXGI_FORMAT_R8_UNORM); !s) return s.error();
                if (auto s = makeView(1, 1, DXGI_FORMAT_R8G8_UNORM); !s) return s.error();
            } else {
                // GPU NV12: one real NV12 texture (copy target) with two plane views.
                if (!m_nv12Sampling) return Error{"NV12 shader sampling is not supported on this device."};
                if (auto s = makeTex(0, DXGI_FORMAT_NV12, w, h); !s) return s.error();
                if (auto s = makeView(0, 0, DXGI_FORMAT_R8_UNORM); !s) return s.error();
                if (auto s = makeView(1, 0, DXGI_FORMAT_R8G8_UNORM); !s) return s.error();
            }
            t->m_viewCount = 2;
            break;
        case FrameTexture::Kind::I420:
            if (auto s = makeTex(0, DXGI_FORMAT_R8_UNORM, w, h); !s) return s.error();
            if (auto s = makeTex(1, DXGI_FORMAT_R8_UNORM, cw, ch); !s) return s.error();
            if (auto s = makeTex(2, DXGI_FORMAT_R8_UNORM, cw, ch); !s) return s.error();
            for (int i = 0; i < 3; ++i) if (auto s = makeView(i, i, DXGI_FORMAT_R8_UNORM); !s) return s.error();
            t->m_viewCount = 3;
            break;
        case FrameTexture::Kind::Bgra:
            if (auto s = makeTex(0, DXGI_FORMAT_B8G8R8A8_UNORM, w, h); !s) return s.error();
            if (auto s = makeView(0, 0, DXGI_FORMAT_B8G8R8A8_UNORM); !s) return s.error();
            t->m_viewCount = 1;
            break;
    }
    return t;
}

Result<std::shared_ptr<FrameTexture>> FrameTexturePool::acquire(FrameTexture::Kind kind, int w, int h, bool dynamic) {
    std::unique_ptr<FrameTexture> tex;
    {
        std::lock_guard lock(m_mutex);
        auto it = std::find_if(m_free.begin(), m_free.end(), [&](const auto& f) { return f->m_kind == kind && f->m_width == w && f->m_height == h && f->m_dynamic == dynamic; });
        if (it != m_free.end()) {
            tex = std::move(*it);
            m_free.erase(it);
        }
    }
    if (!tex) {
        auto created = createTexture(kind, w, h, dynamic);
        if (!created) return created.error();
        tex = std::move(created.value());
    }
    ++m_live;
    FrameTexture* raw = tex.release();
    // The shared_ptr deleter returns the texture to the pool instead of freeing it.
    std::weak_ptr<FrameTexturePool> weak = weak_from_this();
    return std::shared_ptr<FrameTexture>(raw, [weak](FrameTexture* p) {
        if (auto pool = weak.lock()) pool->recycle(p);
        else delete p;
    });
}

void FrameTexturePool::recycle(FrameTexture* raw) {
    std::unique_ptr<FrameTexture> tex(raw);
    std::lock_guard lock(m_mutex);
    --m_live;
    if (m_free.size() < kMaxFree) m_free.push_back(std::move(tex));
}

Result<std::shared_ptr<FrameTexture>> FrameTexturePool::acquireFor(const media::VideoFrame& frame) {
    FrameTexture::Kind kind;
    bool dynamic = true;
    switch (frame.layout()) {
        case media::PixelLayout::D3D11Nv12: kind = FrameTexture::Kind::Nv12; dynamic = false; break;
        case media::PixelLayout::Nv12: kind = FrameTexture::Kind::Nv12; break;
        case media::PixelLayout::Yuv420p: kind = FrameTexture::Kind::I420; break;
        case media::PixelLayout::Bgra: kind = FrameTexture::Kind::Bgra; break;
        default: return Error{"Unsupported pixel format for GPU upload.", "layout " + std::to_string(static_cast<int>(frame.layout()))};
    }
    auto tex = acquire(kind, frame.width(), frame.height(), dynamic);
    if (!tex) return tex.error();
    if (auto s = (*tex)->fill(frame); !s) return s.error();
    return tex;
}

Result<std::shared_ptr<FrameTexture>> FrameTexturePool::acquireBgra(int width, int height, bool cpuWritable) {
    return acquire(FrameTexture::Kind::Bgra, width, height, cpuWritable);
}

}  // namespace lf::render
