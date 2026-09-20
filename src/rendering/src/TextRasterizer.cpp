#include "lumaforge/rendering/TextRasterizer.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

using Microsoft::WRL::ComPtr;

namespace lf::render {

namespace {

constexpr const char* kLog = "text";

Error hrError(const char* what, HRESULT hr) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return Error{"Text could not be rendered.", std::string(what) + " failed: " + buf, static_cast<int>(hr)};
}

D2D1_COLOR_F toColor(const ColorRgba& c, float alphaScale = 1.0f) { return D2D1::ColorF(c.r, c.g, c.b, c.a * alphaScale); }

// Collects glyph outlines from a DirectWrite layout as Direct2D geometry so
// the text can be filled, stroked (outline) and shadowed as vector shapes.
class GeometryCollector final : public IDWriteTextRenderer {
public:
    GeometryCollector(ID2D1Factory* factory) : m_factory(factory) {}
    std::vector<ComPtr<ID2D1Geometry>> geometries;

    // IUnknown (stack-allocated; refcounting is a formality)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IDWriteTextRenderer)) { *out = this; return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    // IDWritePixelSnapping
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override { *disabled = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override { *m = {1, 0, 0, 1, 0, 0}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* ppd) override { *ppd = 1.0f; return S_OK; }

    // IDWriteTextRenderer
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT baselineX, FLOAT baselineY, DWRITE_MEASURING_MODE, DWRITE_GLYPH_RUN const* run,
                                           DWRITE_GLYPH_RUN_DESCRIPTION const*, IUnknown*) override {
        ComPtr<ID2D1PathGeometry> path;
        HRESULT hr = m_factory->CreatePathGeometry(&path);
        if (FAILED(hr)) return hr;
        ComPtr<ID2D1GeometrySink> sink;
        hr = path->Open(&sink);
        if (FAILED(hr)) return hr;
        hr = run->fontFace->GetGlyphRunOutline(run->fontEmSize, run->glyphIndices, run->glyphAdvances, run->glyphOffsets, run->glyphCount,
                                               run->isSideways, (run->bidiLevel % 2) == 1, sink.Get());
        sink->Close();
        if (FAILED(hr)) return hr;
        ComPtr<ID2D1TransformedGeometry> moved;
        hr = m_factory->CreateTransformedGeometry(path.Get(), D2D1::Matrix3x2F::Translation(baselineX, baselineY), &moved);
        if (FAILED(hr)) return hr;
        geometries.push_back(moved);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }

private:
    ID2D1Factory* m_factory;
};

}  // namespace

struct TextRasterizer::Impl {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IWICImagingFactory> wic;
    bool comInitialised = false;
};

TextRasterizer::TextRasterizer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

TextRasterizer::~TextRasterizer() {
    if (m_impl) {
        m_impl->wic.Reset();
        m_impl->dwrite.Reset();
        m_impl->d2d.Reset();
        if (m_impl->comInitialised) CoUninitialize();
    }
}

Result<std::unique_ptr<TextRasterizer>> TextRasterizer::create() {
    auto impl = std::make_unique<Impl>();
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl->comInitialised = SUCCEEDED(co);   // S_FALSE (already initialised) still needs a matching uninit
    if (co == RPC_E_CHANGED_MODE) impl->comInitialised = false;

    D2D1_FACTORY_OPTIONS options{};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), &options, reinterpret_cast<void**>(impl->d2d.GetAddressOf()));
    if (FAILED(hr)) return hrError("D2D1CreateFactory", hr);
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(impl->dwrite.GetAddressOf()));
    if (FAILED(hr)) return hrError("DWriteCreateFactory", hr);
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl->wic));
    if (FAILED(hr)) return hrError("CoCreateInstance(WICImagingFactory)", hr);
    LF_LOG_INFO(kLog, "text rasteriser ready (DirectWrite + Direct2D)");
    return std::unique_ptr<TextRasterizer>(new TextRasterizer(std::move(impl)));
}

std::vector<std::string> TextRasterizer::fontFamilies() const {
    std::vector<std::string> out;
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(m_impl->dwrite->GetSystemFontCollection(&fonts))) return out;
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"en-us";
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    const UINT32 count = fonts->GetFontFamilyCount();
    std::set<std::string> names;
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(fonts->GetFontFamily(i, &family))) continue;
        ComPtr<IDWriteLocalizedStrings> strings;
        if (FAILED(family->GetFamilyNames(&strings))) continue;
        UINT32 index = 0;
        BOOL exists = FALSE;
        strings->FindLocaleName(locale, &index, &exists);
        if (!exists) strings->FindLocaleName(L"en-us", &index, &exists);
        if (!exists) index = 0;
        UINT32 len = 0;
        strings->GetStringLength(index, &len);
        std::wstring name(len + 1, L'\0');
        strings->GetString(index, name.data(), len + 1);
        name.resize(len);
        if (!name.empty() && name[0] != L'@') names.insert(wideToUtf8(name));
    }
    out.assign(names.begin(), names.end());
    return out;
}

std::uint64_t TextRasterizer::hash(const TextParams& p, int w, int h) noexcept {
    std::uint64_t x = 1469598103934665603ull;
    auto mix = [&](const void* data, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) { x ^= b[i]; x *= 1099511628211ull; }
    };
    mix(p.text.data(), p.text.size());
    mix("|", 1);
    mix(p.fontFamily.data(), p.fontFamily.size());
    const float floats[] = {p.fontSize, p.color.r, p.color.g, p.color.b, p.color.a, p.outlineWidth, p.outlineColor.r, p.outlineColor.g,
                            p.outlineColor.b, p.outlineColor.a, p.shadowOffsetX, p.shadowOffsetY, p.shadowBlur, p.shadowColor.r, p.shadowColor.g,
                            p.shadowColor.b, p.shadowColor.a, p.backgroundOpacity, p.backgroundColor.r, p.backgroundColor.g, p.backgroundColor.b,
                            p.backgroundColor.a, p.backgroundPadding, p.lineSpacing, p.letterSpacing};
    mix(floats, sizeof floats);
    const int ints[] = {p.bold, p.italic, static_cast<int>(p.align), p.shadow, w, h};
    mix(ints, sizeof ints);
    return x;
}

Result<TextBitmap> TextRasterizer::render(const TextParams& p, int frameW, int frameH) {
    auto& I = *m_impl;
    if (frameW <= 0 || frameH <= 0) return Error{"Text could not be rendered.", "empty frame"};

    // ---- layout
    const std::wstring family = utf8ToWide(p.fontFamily.empty() ? "Segoe UI" : p.fontFamily);
    const std::wstring text = utf8ToWide(p.text);
    const float fontSize = std::clamp(p.fontSize, 4.0f, 2000.0f);
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = I.dwrite->CreateTextFormat(family.c_str(), nullptr, p.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                            p.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &format);
    if (FAILED(hr)) return hrError("CreateTextFormat", hr);
    format->SetTextAlignment(p.align == TextAlign::Left ? DWRITE_TEXT_ALIGNMENT_LEADING : p.align == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (std::fabs(p.lineSpacing - 1.0f) > 1e-3f) {
        const float line = fontSize * 1.25f * std::clamp(p.lineSpacing, 0.5f, 3.0f);
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, line, line * 0.8f);
    }
    const float margin = std::round(frameW * 0.05f);
    const float maxWidth = std::max(1.0f, frameW - 2 * margin);
    ComPtr<IDWriteTextLayout> layout;
    hr = I.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), maxWidth, static_cast<float>(frameH), &layout);
    if (FAILED(hr)) return hrError("CreateTextLayout", hr);
    if (std::fabs(p.letterSpacing) > 1e-3f) {
        ComPtr<IDWriteTextLayout1> layout1;
        if (SUCCEEDED(layout.As(&layout1))) layout1->SetCharacterSpacing(0.0f, p.letterSpacing, 0.0f, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(text.size())});
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    // Centre the text block in the frame whatever the alignment (alignment
    // only arranges the lines within the block); the transform moves it.
    const float originX = std::round((frameW - metrics.width) * 0.5f - metrics.left);
    const float originY = std::round((frameH - metrics.height) * 0.5f - metrics.top);

    // ---- target
    ComPtr<IWICBitmap> bitmap;
    hr = I.wic->CreateBitmap(static_cast<UINT>(frameW), static_cast<UINT>(frameH), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap);
    if (FAILED(hr)) return hrError("IWICImagingFactory::CreateBitmap", hr);
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> rt;
    hr = I.d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt);
    if (FAILED(hr)) return hrError("CreateWicBitmapRenderTarget", hr);
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // ---- geometry
    GeometryCollector collector(I.d2d.Get());
    hr = layout->Draw(nullptr, &collector, originX, originY);
    if (FAILED(hr)) return hrError("IDWriteTextLayout::Draw", hr);
    ComPtr<ID2D1GeometryGroup> group;
    if (!collector.geometries.empty()) {
        std::vector<ID2D1Geometry*> raw;
        for (auto& g : collector.geometries) raw.push_back(g.Get());
        hr = I.d2d->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, raw.data(), static_cast<UINT32>(raw.size()), &group);
        if (FAILED(hr)) return hrError("CreateGeometryGroup", hr);
    }

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush);

    const D2D1_RECT_F block = D2D1::RectF(originX + metrics.left, originY + metrics.top, originX + metrics.left + metrics.width, originY + metrics.top + metrics.height);
    if (p.backgroundOpacity > 0.0f && !text.empty()) {
        const float pad = std::max(0.0f, p.backgroundPadding);
        brush->SetColor(toColor(p.backgroundColor, std::clamp(p.backgroundOpacity, 0.0f, 1.0f)));
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(block.left - pad, block.top - pad, block.right + pad, block.bottom + pad), pad * 0.3f, pad * 0.3f), brush.Get());
    }
    if (group) {
        if (p.shadow && p.shadowColor.a > 0.0f) {
            // Soft shadow: several offset fills with jitter proportional to the blur radius.
            const int taps = p.shadowBlur > 0.5f ? 9 : 1;
            const float r = std::max(0.0f, p.shadowBlur);
            brush->SetColor(toColor(p.shadowColor, 1.0f / taps));
            for (int i = 0; i < taps; ++i) {
                const float a = static_cast<float>(i) / taps * 6.2831853f;
                const float dx = i == 0 ? 0.0f : std::cos(a) * r * 0.6f;
                const float dy = i == 0 ? 0.0f : std::sin(a) * r * 0.6f;
                rt->SetTransform(D2D1::Matrix3x2F::Translation(p.shadowOffsetX + dx, p.shadowOffsetY + dy));
                rt->FillGeometry(group.Get(), brush.Get());
            }
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        if (p.outlineWidth > 0.0f) {
            // Stroke at twice the width, then fill on top: only the outer half remains.
            brush->SetColor(toColor(p.outlineColor));
            ComPtr<ID2D1StrokeStyle> stroke;
            I.d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &stroke);
            rt->DrawGeometry(group.Get(), brush.Get(), p.outlineWidth * 2.0f, stroke.Get());
        }
        brush->SetColor(toColor(p.color));
        rt->FillGeometry(group.Get(), brush.Get());
    }
    hr = rt->EndDraw();
    if (FAILED(hr)) return hrError("EndDraw", hr);

    // ---- copy out
    TextBitmap out;
    out.width = frameW;
    out.height = frameH;
    out.blockLeft = block.left; out.blockTop = block.top; out.blockWidth = metrics.width; out.blockHeight = metrics.height;
    out.bgra.resize(static_cast<std::size_t>(frameW) * frameH * 4);
    ComPtr<IWICBitmapLock> lock;
    WICRect rect{0, 0, frameW, frameH};
    hr = bitmap->Lock(&rect, WICBitmapLockRead, &lock);
    if (FAILED(hr)) return hrError("IWICBitmap::Lock", hr);
    UINT stride = 0, size = 0;
    BYTE* data = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&size, &data);
    for (int y = 0; y < frameH; ++y) std::memcpy(out.bgra.data() + static_cast<std::size_t>(y) * frameW * 4, data + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(frameW) * 4);
    return out;
}

}  // namespace lf::render
