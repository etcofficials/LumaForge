#include "TestMain.h"

#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/media/VideoDecoder.h"
#include "lumaforge/rendering/Compositor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace lf;
using namespace lf::render;

namespace {
std::string mediaPath(const char* name) { return std::string(LF_TEST_MEDIA_DIR) + "/" + name; }

struct Px { int b, g, r, a; };
Px pixel(const std::vector<std::uint8_t>& img, int w, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
    return {img[i], img[i + 1], img[i + 2], img[i + 3]};
}

// Renders the first frame of a file through the compositor and returns BGRA.
std::vector<std::uint8_t> renderFirstFrame(Compositor& comp, const std::shared_ptr<hw::GpuDevice>& gpu, bool hardware, int outW, int outH, Layer base = {}) {
    media::VideoDecoder::Options o;
    o.gpu = gpu;
    o.allowHardware = hardware;
    auto dec = media::VideoDecoder::open(mediaPath("countdown_720p30.mp4"), o);
    if (!dec) return {};
    auto f = (*dec)->next();
    if (!f || !f->has_value()) return {};
    auto tex = comp.texturePool().acquireFor(**f);
    if (!tex) { std::printf("     acquireFor failed: %s\n", tex.error().fullText().c_str()); return {}; }
    auto target = comp.createTarget(outW, outH);
    if (!target) return {};
    Layer layer = base;
    layer.texture = *tex;
    std::vector<std::uint8_t> out;
    Layer layers[] = {layer};
    if (!comp.compose(**target, 1280, 720, layers, Rgba{0, 0, 0, 1})) return {};
    if (!comp.readback(**target, out)) return {};
    return out;
}
}  // namespace

TEST_CASE("compositor: renders a decoded frame into an offscreen target") {
    auto gpu = hw::GpuDevice::create({});
    CHECK(gpu.ok());
    if (!gpu) return;
    auto comp = Compositor::create(*gpu);
    CHECK(comp.ok());
    if (!comp) return;
    auto img = renderFirstFrame(**comp, *gpu, false, 320, 180);
    CHECK(img.size() == 320u * 180u * 4u);
    if (img.empty()) return;
    // testsrc2 has colour bars in the top area: not uniformly black, alpha opaque.
    std::uint64_t sum = 0;
    int opaque = 0;
    for (std::size_t i = 0; i < img.size(); i += 4) { sum += img[i] + img[i + 1] + img[i + 2]; if (img[i + 3] == 255) ++opaque; }
    CHECK(sum > 320u * 180u * 30u);
    CHECK(opaque == 320 * 180);
}

TEST_CASE("compositor: hardware and software decode paths render the same picture") {
    auto gpu = hw::GpuDevice::create({});
    if (!gpu) return;
    if (!(*gpu)->videoDevice() || (*gpu)->isWarp()) { std::printf("     (no hardware decode on this device; skipped)\n"); return; }
    auto comp = Compositor::create(*gpu);
    CHECK(comp.ok());
    if (!comp) return;
    if (!(*comp)->texturePool().supportsNv12Sampling()) { std::printf("     (NV12 sampling unsupported; skipped)\n"); return; }
    auto sw = renderFirstFrame(**comp, *gpu, false, 320, 180);
    auto hwImg = renderFirstFrame(**comp, *gpu, true, 320, 180);
    CHECK(sw.size() == hwImg.size() && !sw.empty());
    if (sw.size() != hwImg.size() || sw.empty()) return;
    double diffSum = 0;
    int maxDiff = 0;
    for (std::size_t i = 0; i < sw.size(); ++i) {
        if ((i & 3) == 3) continue;   // skip alpha
        const int d = std::abs(int(sw[i]) - int(hwImg[i]));
        diffSum += d;
        maxDiff = std::max(maxDiff, d);
    }
    const double mean = diffSum / (sw.size() * 0.75);
    std::printf("     (hw vs sw: mean abs diff %.2f, max %d)\n", mean, maxDiff);
    CHECK(mean < 2.0);      // identical up to chroma siting / rounding
}

TEST_CASE("compositor: BGRA layer transform places a solid quad where expected") {
    auto gpu = hw::GpuDevice::create({});
    if (!gpu) return;
    auto comp = Compositor::create(*gpu);
    CHECK(comp.ok());
    if (!comp) return;
    // 100x50 solid red (straight alpha), placed at +200 px right in a 1000x500 frame, scale 1, no fit.
    auto tex = (*comp)->texturePool().acquireBgra(100, 50, true);
    CHECK(tex.ok());
    if (!tex) return;
    std::vector<std::uint8_t> red(100 * 50 * 4);
    for (std::size_t i = 0; i < red.size(); i += 4) { red[i] = 0; red[i + 1] = 0; red[i + 2] = 255; red[i + 3] = 255; }
    CHECK((*tex)->fillBgra(red.data(), 100 * 4, false).ok());
    auto target = (*comp)->createTarget(1000, 500);
    CHECK(target.ok());
    if (!target) return;
    Layer layer;
    layer.texture = *tex;
    layer.fitToFrame = false;
    layer.transform.positionX = 200;
    Layer layers[] = {layer};
    CHECK((*comp)->compose(**target, 1000, 500, layers, Rgba{0, 0, 0, 1}).ok());
    std::vector<std::uint8_t> out;
    CHECK((*comp)->readback(**target, out).ok());
    if (out.empty()) return;
    // Quad centre at (700, 250); spans x 650..750, y 225..275
    Px inside = pixel(out, 1000, 700, 250);
    Px outsideLeft = pixel(out, 1000, 500, 250);   // frame centre: empty now
    Px outsideTop = pixel(out, 1000, 700, 200);
    CHECK(inside.r > 250 && inside.g < 5 && inside.b < 5);
    CHECK(outsideLeft.r < 5 && outsideTop.r < 5);
    Px edgeIn = pixel(out, 1000, 655, 250);
    Px edgeOut = pixel(out, 1000, 645, 250);
    CHECK(edgeIn.r > 200);
    CHECK(edgeOut.r < 50);
}

TEST_CASE("compositor: opacity and grayscale are applied") {
    auto gpu = hw::GpuDevice::create({});
    if (!gpu) return;
    auto comp = Compositor::create(*gpu);
    if (!comp) return;
    auto tex = (*comp)->texturePool().acquireBgra(8, 8, true);
    if (!tex) return;
    std::vector<std::uint8_t> px(8 * 8 * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) { px[i] = 40; px[i + 1] = 120; px[i + 2] = 200; px[i + 3] = 255; }
    (*tex)->fillBgra(px.data(), 32, false);
    auto target = (*comp)->createTarget(64, 64);
    if (!target) return;
    std::vector<std::uint8_t> out;

    Layer half;
    half.texture = *tex;
    half.transform.opacity = 0.5f;
    Layer l1[] = {half};
    (*comp)->compose(**target, 64, 64, l1, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    Px p = pixel(out, 64, 32, 32);
    CHECK(std::abs(p.r - 100) <= 3 && std::abs(p.g - 60) <= 3 && std::abs(p.b - 20) <= 3);

    Layer grey;
    grey.texture = *tex;
    grey.effects.grayscale = 1.0f;
    Layer l2[] = {grey};
    (*comp)->compose(**target, 64, 64, l2, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    p = pixel(out, 64, 32, 32);
    CHECK(std::abs(p.r - p.g) <= 2 && std::abs(p.g - p.b) <= 2);
    const int expected = static_cast<int>(std::lround(0.2126 * 200 + 0.7152 * 120 + 0.0722 * 40));
    CHECK(std::abs(p.g - expected) <= 3);
}

TEST_CASE("compositor: WARP device renders identically to hardware for a solid layer") {
    auto warp = hw::GpuDevice::create({.forceWarp = true});
    CHECK(warp.ok());
    if (!warp) return;
    CHECK((*warp)->isWarp());
    auto comp = Compositor::create(*warp);
    CHECK(comp.ok());
    if (!comp) return;
    auto tex = (*comp)->texturePool().acquireBgra(4, 4, true);
    if (!tex) return;
    std::vector<std::uint8_t> px(4 * 4 * 4, 255);
    (*tex)->fillBgra(px.data(), 16, false);
    auto target = (*comp)->createTarget(32, 32);
    if (!target) return;
    Layer l;
    l.texture = *tex;
    l.color.saturation = 0.0f;
    Layer ls[] = {l};
    CHECK((*comp)->compose(**target, 32, 32, ls, Rgba{0, 0, 0, 1}).ok());
    std::vector<std::uint8_t> out;
    CHECK((*comp)->readback(**target, out).ok());
    if (out.empty()) return;
    Px p = pixel(out, 32, 16, 16);
    CHECK(p.r == 255 && p.g == 255 && p.b == 255);
}

TEST_CASE("compositor: ellipse mask (with invert), pixelate/posterize/invert, adjustment layer") {
    auto gpu = hw::GpuDevice::create({});
    if (!gpu) return;
    auto comp = Compositor::create(*gpu);
    if (!comp) return;
    auto red = (*comp)->texturePool().acquireBgra(64, 64, true);
    if (!red) return;
    std::vector<std::uint8_t> px(64 * 64 * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) { px[i] = 0; px[i + 1] = 0; px[i + 2] = 200; px[i + 3] = 255; }
    (*red)->fillBgra(px.data(), 64 * 4, false);
    auto target = (*comp)->createTarget(64, 64);
    if (!target) return;
    std::vector<std::uint8_t> out;

    Layer masked;
    masked.texture = *red;
    masked.mask.shape = MaskShape::Ellipse;
    masked.mask.width = masked.mask.height = 0.5f;
    masked.mask.feather = 0.0f;
    Layer l1[] = {masked};
    (*comp)->compose(**target, 64, 64, l1, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    CHECK(pixel(out, 64, 32, 32).r > 190);   // centre: inside
    CHECK(pixel(out, 64, 2, 2).r < 5);       // corner: outside
    masked.mask.invert = true;
    Layer l2[] = {masked};
    (*comp)->compose(**target, 64, 64, l2, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    CHECK(pixel(out, 64, 32, 32).r < 5 && pixel(out, 64, 2, 2).r > 190);

    // Invert effect: 200 -> 55.
    Layer inv;
    inv.texture = *red;
    inv.effects.invert = 1.0f;
    Layer l3[] = {inv};
    (*comp)->compose(**target, 64, 64, l3, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    Px p = pixel(out, 64, 32, 32);
    CHECK(std::abs(p.r - 55) <= 3 && std::abs(p.g - 255) <= 3);

    // Adjustment layer: the red picture below goes grey; a masked adjustment only inside its ellipse.
    Layer base;
    base.texture = *red;
    Layer adj;
    adj.adjustment = true;
    adj.effects.grayscale = 1.0f;
    Layer l4[] = {base, adj};
    (*comp)->compose(**target, 64, 64, l4, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    p = pixel(out, 64, 32, 32);
    const int grey = static_cast<int>(std::lround(0.2126 * 200));
    CHECK(std::abs(p.r - grey) <= 3 && std::abs(p.g - grey) <= 3 && std::abs(p.b - grey) <= 3);
    adj.mask.shape = MaskShape::Ellipse;
    adj.mask.feather = 0.0f;
    Layer l5[] = {base, adj};
    (*comp)->compose(**target, 64, 64, l5, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    CHECK(std::abs(pixel(out, 64, 32, 32).r - grey) <= 3);   // inside: adjusted
    CHECK(pixel(out, 64, 2, 2).r > 190);                       // outside: untouched red
}

TEST_CASE("compositor: directional blur streaks along its angle only") {
    auto gpu = hw::GpuDevice::create({});
    if (!gpu) return;
    auto comp = Compositor::create(*gpu);
    if (!comp) return;
    // 64x64: left half black, right half white -> one vertical edge at x = 32.
    auto edge = (*comp)->texturePool().acquireBgra(64, 64, true);
    if (!edge) return;
    std::vector<std::uint8_t> px(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 64 + x) * 4;
            const std::uint8_t v = x < 32 ? 0 : 255;
            px[i] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    (*edge)->fillBgra(px.data(), 64 * 4, false);
    auto target = (*comp)->createTarget(64, 64);
    if (!target) return;
    std::vector<std::uint8_t> out;
    Layer layer;
    layer.texture = *edge;
    Layer plain[] = {layer};
    (*comp)->compose(**target, 64, 64, plain, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    CHECK(pixel(out, 64, 29, 32).r < 5 && pixel(out, 64, 35, 32).r > 250);   // sharp edge without the effect

    // Horizontal streak of 25% of the width (16 px): pixels near the edge mix both sides.
    EffectParams h;
    h.directionalBlur = 0.25f;
    h.directionalBlurAngle = 0.0f;
    layer.effects = h;
    Layer hl[] = {layer};
    (*comp)->compose(**target, 64, 64, hl, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    const int left = pixel(out, 64, 29, 32).r, right = pixel(out, 64, 35, 32).r;
    CHECK(left > 20 && left < 128);
    CHECK(right > 128 && right < 235);
    CHECK(pixel(out, 64, 2, 32).r < 5 && pixel(out, 64, 61, 32).r > 250);   // far from the edge: unchanged

    // Vertical streak: the edge is parallel to the streak, so it stays sharp.
    layer.effects.directionalBlurAngle = 90.0f;
    Layer vl[] = {layer};
    (*comp)->compose(**target, 64, 64, vl, Rgba{0, 0, 0, 1});
    (*comp)->readback(**target, out);
    CHECK(pixel(out, 64, 29, 32).r < 5 && pixel(out, 64, 35, 32).r > 250);
}

TEST_CASE("effects: directional blur effects flatten to one streak (length, angle)") {
    std::vector<Effect> fx{makeEffect(EffectType::DirectionalBlurH)};
    fx[0].amount = 10.0f;   // 10% of the width
    ColorAdjust color;
    EffectParams looks;
    applyEffects(fx, color, looks);
    CHECK(std::fabs(looks.directionalBlur - 0.10f) < 1e-5f && std::fabs(looks.directionalBlurAngle) < 1e-3f);
    Effect v = makeEffect(EffectType::DirectionalBlurV);
    v.amount = 10.0f;
    fx.push_back(v);
    looks = {};
    applyEffects(fx, color, looks);
    CHECK(std::fabs(looks.directionalBlur - 0.1414f) < 1e-3f && std::fabs(looks.directionalBlurAngle - 45.0f) < 1e-2f);
    CHECK(effectTypeFromId("directionalBlurH") == EffectType::DirectionalBlurH);
    CHECK(effectTypeFromId("directionalBlurV") == EffectType::DirectionalBlurV);
    fx[0].enabled = fx[1].enabled = false;
    looks = {};
    applyEffects(fx, color, looks);
    CHECK(looks.directionalBlur == 0.0f && looks.isIdentity());
}
