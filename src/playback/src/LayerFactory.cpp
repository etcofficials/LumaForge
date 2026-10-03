#include "lumaforge/playback/LayerFactory.h"

#include "lumaforge/core/Log.h"

#include <algorithm>
#include <vector>

namespace lf::playback {

namespace {
constexpr const char* kLog = "render";

void copyLook(const VideoLayerRequest& req, render::Layer& layer) {
    layer.transform = req.transform;
    layer.crop = req.crop;
    layer.mask = req.mask;
    layer.color = req.color;
    layer.effects = req.effects;
}
}  // namespace

LayerFactory::LayerFactory(std::shared_ptr<render::FrameTexturePool> pool) : m_pool(std::move(pool)) {}
LayerFactory::~LayerFactory() = default;

render::Layer LayerFactory::media(const VideoLayerRequest& req, std::shared_ptr<render::FrameTexture> texture) {
    render::Layer layer;
    layer.texture = std::move(texture);
    copyLook(req, layer);
    layer.fitToFrame = req.fitToFrame;
    return layer;
}

bool LayerFactory::generated(const VideoLayerRequest& req, int fw, int fh, render::Layer& layer) {
    layer = render::Layer{};
    if (req.adjustment) {
        // No texture: the compositor redraws what is below through this layer.
        layer.adjustment = true;
        copyLook(req, layer);
        return true;
    }
    if (req.solid) {
        // A tiny texture stretched over the frame (fit makes it square; widen the long side).
        layer.texture = solidTexture(*req.solid);
        if (!layer.texture) return false;
        copyLook(req, layer);
        layer.transform.scaleX *= std::max(1.0f, static_cast<float>(fw) / static_cast<float>(fh));
        layer.transform.scaleY *= std::max(1.0f, static_cast<float>(fh) / static_cast<float>(fw));
        layer.fitToFrame = true;
        return true;
    }
    if (req.text) {
        // Text rendered at frame size, cached by content.
        layer.texture = textTexture(*req.text, fw, fh);
        if (!layer.texture) return false;
        copyLook(req, layer);
        layer.fitToFrame = true;
        return true;
    }
    return false;
}

std::shared_ptr<render::FrameTexture> LayerFactory::textTexture(const TextParams& text, int frameW, int frameH) {
    const std::uint64_t key = render::TextRasterizer::hash(text, frameW, frameH);
    if (auto it = m_textCache.find(key); it != m_textCache.end()) {
        it->second.lastUse = ++m_useCounter;
        return it->second.texture;
    }
    if (m_textUnavailable) return nullptr;
    if (!m_text) {
        auto created = render::TextRasterizer::create();
        if (!created) {
            LF_LOG_ERROR(kLog, "text rendering unavailable: {}", created.error().fullText());
            m_textUnavailable = true;
            return nullptr;
        }
        m_text = std::move(created.value());
    }
    auto bitmap = m_text->render(text, frameW, frameH);
    if (!bitmap) {
        LF_LOG_WARN(kLog, "text render failed: {}", bitmap.error().fullText());
        return nullptr;
    }
    auto tex = m_pool->acquireBgra(bitmap->width, bitmap->height, true);
    if (!tex) {
        LF_LOG_WARN(kLog, "text texture failed: {}", tex.error().fullText());
        return nullptr;
    }
    if (auto s = (*tex)->fillBgra(bitmap->bgra.data(), bitmap->width * 4, true); !s) {
        LF_LOG_WARN(kLog, "text upload failed: {}", s.error().fullText());
        return nullptr;
    }
    // Bounded: a title change per slider tick must not accumulate 8 MB textures.
    constexpr std::size_t kMaxTextTextures = 6;
    while (m_textCache.size() >= kMaxTextTextures) {
        auto oldest = std::min_element(m_textCache.begin(), m_textCache.end(), [](const auto& a, const auto& b) { return a.second.lastUse < b.second.lastUse; });
        m_textCache.erase(oldest);
    }
    m_textCache[key] = TextEntry{*tex, ++m_useCounter};
    return *tex;
}

std::shared_ptr<render::FrameTexture> LayerFactory::solidTexture(const ColorRgba& color) {
    auto channel = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    const std::uint32_t bgra = channel(color.b) | (channel(color.g) << 8) | (channel(color.r) << 16) | (channel(color.a) << 24);
    if (auto it = m_solidCache.find(bgra); it != m_solidCache.end()) return it->second;
    constexpr int kSize = 16;
    auto tex = m_pool->acquireBgra(kSize, kSize, true);
    if (!tex) return nullptr;
    std::vector<std::uint32_t> pixels(kSize * kSize, bgra);
    if (!(*tex)->fillBgra(reinterpret_cast<const std::uint8_t*>(pixels.data()), kSize * 4, false)) return nullptr;
    if (m_solidCache.size() > 8) m_solidCache.clear();
    m_solidCache[bgra] = *tex;
    return *tex;
}

void LayerFactory::clear() {
    m_textCache.clear();
    m_solidCache.clear();
    m_text.reset();
    m_textUnavailable = false;
}

}  // namespace lf::playback
