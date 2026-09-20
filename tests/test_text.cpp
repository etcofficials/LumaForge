#include "TestMain.h"

#include "lumaforge/rendering/TextRasterizer.h"

using namespace lf;

namespace {
// Number of pixels with any coverage inside a rectangle.
int coverage(const render::TextBitmap& b, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (b.bgra[(static_cast<std::size_t>(y) * b.width + x) * 4 + 3] > 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("text rasteriser: centred title, outline and shadow add coverage, hash tracks content") {
    auto r = render::TextRasterizer::create();
    CHECK(r.ok());
    if (!r) { std::printf("     %s\n", r.error().fullText().c_str()); return; }
    TextParams p;
    p.text = "Hello";
    p.fontSize = 120;
    p.outlineWidth = 0;
    p.shadow = false;
    auto plain = (*r)->render(p, 1280, 720);
    CHECK(plain.ok());
    if (!plain) return;
    CHECK(plain->width == 1280 && plain->height == 720);
    CHECK(plain->bgra.size() == 1280u * 720u * 4u);
    // Text sits in the middle: coverage there, none in the corners.
    CHECK(coverage(*plain, 440, 300, 840, 420) > 2000);
    CHECK(coverage(*plain, 0, 0, 200, 100) == 0);
    CHECK(coverage(*plain, 1080, 620, 1280, 720) == 0);
    // Premultiplied white: colour never exceeds alpha.
    bool premultiplied = true;
    for (std::size_t i = 0; i < plain->bgra.size(); i += 4) if (plain->bgra[i] > plain->bgra[i + 3]) premultiplied = false;
    CHECK(premultiplied);

    p.outlineWidth = 6;
    auto outlined = (*r)->render(p, 1280, 720);
    CHECK(outlined.ok());
    if (outlined) CHECK(coverage(*outlined, 400, 280, 880, 440) > coverage(*plain, 400, 280, 880, 440));
    p.shadow = true;
    p.shadowOffsetX = 30;
    p.shadowOffsetY = 30;
    auto shadowed = (*r)->render(p, 1280, 720);
    CHECK(shadowed.ok());
    if (shadowed) CHECK(coverage(*shadowed, 400, 280, 920, 480) > coverage(*outlined, 400, 280, 920, 480));

    TextParams q = p;
    q.text = "Hello!";
    CHECK(render::TextRasterizer::hash(p, 1280, 720) != render::TextRasterizer::hash(q, 1280, 720));
    CHECK(render::TextRasterizer::hash(p, 1280, 720) != render::TextRasterizer::hash(p, 1920, 1080));
    CHECK(render::TextRasterizer::hash(p, 1280, 720) == render::TextRasterizer::hash(p, 1280, 720));

    // Alignment arranges lines inside the block; the block itself stays centred.
    q = p;
    q.align = TextAlign::Left;
    q.text = "Hello\nWorld wide";
    auto left = (*r)->render(q, 1280, 720);
    CHECK(left.ok());
    if (left) CHECK(std::abs((left->blockLeft + left->blockWidth / 2) - 640.0f) < 4.0f);

    CHECK(!(*r)->fontFamilies().empty());
}
