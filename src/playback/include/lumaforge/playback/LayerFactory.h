#pragma once

#include "lumaforge/playback/Program.h"
#include "lumaforge/rendering/Compositor.h"
#include "lumaforge/rendering/FrameTexture.h"
#include "lumaforge/rendering/TextRasterizer.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace lf::playback {

// Turns a program's layer requests into compositor layers. The preview
// engine and the exporter both go through here, so what plays is exactly
// what is rendered to file. Generated layers (text, solid colour,
// adjustment) are produced and cached here; media layers wrap a decoded
// texture supplied by the caller. Use from one thread (the render thread).
class LayerFactory {
public:
    explicit LayerFactory(std::shared_ptr<render::FrameTexturePool> pool);
    ~LayerFactory();

    // A layer for a request with no media path. False when nothing should be
    // drawn (e.g. text rendering unavailable).
    bool generated(const VideoLayerRequest& req, int frameW, int frameH, render::Layer& out);
    // A layer for a media request whose picture is `texture`.
    [[nodiscard]] static render::Layer media(const VideoLayerRequest& req, std::shared_ptr<render::FrameTexture> texture);

    void clear();   // drop cached textures (device teardown)

private:
    std::shared_ptr<render::FrameTexture> textTexture(const TextParams& text, int frameW, int frameH);
    std::shared_ptr<render::FrameTexture> solidTexture(const ColorRgba& color);

    struct TextEntry {
        std::shared_ptr<render::FrameTexture> texture;
        std::uint64_t lastUse = 0;
    };
    std::shared_ptr<render::FrameTexturePool> m_pool;
    std::unique_ptr<render::TextRasterizer> m_text;   // created lazily
    bool m_textUnavailable = false;
    std::unordered_map<std::uint64_t, TextEntry> m_textCache;
    std::unordered_map<std::uint32_t, std::shared_ptr<render::FrameTexture>> m_solidCache;   // BGRA colour -> 16x16 texture
    std::uint64_t m_useCounter = 0;
};

}  // namespace lf::playback
