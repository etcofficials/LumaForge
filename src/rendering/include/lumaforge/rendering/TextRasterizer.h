#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/VisualParams.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lf::render {

// A rendered text block: premultiplied BGRA, sized to the sequence frame so
// the compositor places it like any full-frame layer (fit scale = 1). The
// text sits centred; Transform2D moves it.
struct TextBitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;   // width*4 bytes per row, premultiplied
    // Text block bounds inside the bitmap (for the UI / debugging).
    float blockLeft = 0, blockTop = 0, blockWidth = 0, blockHeight = 0;
};

// DirectWrite layout + Direct2D rendering into a WIC bitmap. Device
// independent (no GPU state), so it can run on any thread; the playback
// engine calls it from the render thread and caches the result as a texture.
class TextRasterizer {
public:
    static Result<std::unique_ptr<TextRasterizer>> create();
    ~TextRasterizer();

    Result<TextBitmap> render(const TextParams& params, int frameWidth, int frameHeight);

    // Installed font family names, sorted (for the font chooser).
    [[nodiscard]] std::vector<std::string> fontFamilies() const;

    // Stable hash of everything that affects the bitmap.
    static std::uint64_t hash(const TextParams& params, int frameWidth, int frameHeight) noexcept;

private:
    struct Impl;
    explicit TextRasterizer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

}  // namespace lf::render
