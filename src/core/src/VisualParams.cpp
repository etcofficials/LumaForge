#include "lumaforge/core/VisualParams.h"

#include <algorithm>
#include <cstring>

namespace lf {

namespace {
// Order must match EffectType.
using C = EffectCategory;
constexpr EffectInfo kInfo[] = {
    {"brightness",  "Brightness",  -1.0f,   1.0f,  0.0f, 2, "",        C::Color,       "Lightens or darkens the whole picture"},
    {"contrast",    "Contrast",     0.0f,   2.0f,  1.0f, 2, "",        C::Color,       "Spreads or compresses tones around mid grey"},
    {"saturation",  "Saturation",   0.0f,   2.0f,  1.0f, 2, "",        C::Color,       "Scales colour intensity"},
    {"grayscale",   "Grayscale",    0.0f,   1.0f,  1.0f, 2, "",        C::Stylize,     "Removes colour (amount mixes with the original)"},
    {"sepia",       "Sepia",        0.0f,   1.0f,  1.0f, 2, "",        C::Stylize,     "Warm brown old-photo tone"},
    {"blur",        "Blur",         0.0f,  40.0f,  6.0f, 1, " px",     C::BlurSharpen, "Softens the picture (radius in source pixels)"},
    {"sharpen",     "Sharpen",      0.0f,   1.0f,  0.5f, 2, "",        C::BlurSharpen, "Crisps edges with an unsharp mask"},
    {"vignette",    "Vignette",     0.0f,   1.0f,  0.5f, 2, "",        C::Stylize,     "Darkens the corners"},
    {"exposure",    "Exposure",    -3.0f,   3.0f,  0.5f, 2, " EV",     C::Color,       "Brightens or darkens in photographic stops"},
    {"temperature", "Temperature", -1.0f,   1.0f,  0.3f, 2, "",        C::Color,       "Warms (positive) or cools (negative) the picture"},
    {"tint",        "Tint",        -1.0f,   1.0f,  0.2f, 2, "",        C::Color,       "Shifts towards green (positive) or magenta (negative)"},
    {"hueShift",    "Hue Shift", -180.0f, 180.0f, 30.0f, 0, "\u00b0",  C::Color,       "Rotates every hue around the colour wheel"},
    {"vibrance",    "Vibrance",    -1.0f,   1.0f,  0.4f, 2, "",        C::Color,       "Boosts muted colours more than saturated ones"},
    {"invert",      "Invert",       0.0f,   1.0f,  1.0f, 2, "",        C::Stylize,     "Photographic negative (amount mixes with the original)"},
    {"posterize",   "Posterize",    2.0f,  32.0f,  6.0f, 0, " levels", C::Stylize,     "Reduces each channel to a few flat levels"},
    {"pixelate",    "Pixelate",     2.0f,  96.0f, 16.0f, 0, " px",     C::Stylize,     "Large square blocks (block size in source pixels)"},
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<std::size_t>(EffectType::Count));
}  // namespace

const char* effectCategoryName(EffectCategory category) noexcept {
    switch (category) {
        case EffectCategory::Color: return "Color";
        case EffectCategory::BlurSharpen: return "Blur & Sharpen";
        case EffectCategory::Stylize: return "Stylize";
    }
    return "";
}

const EffectInfo& effectInfo(EffectType type) noexcept {
    const auto i = static_cast<std::size_t>(type);
    return kInfo[i < static_cast<std::size_t>(EffectType::Count) ? i : 0];
}

EffectType effectTypeFromId(const std::string& id) noexcept {
    for (std::size_t i = 0; i < static_cast<std::size_t>(EffectType::Count); ++i) if (id == kInfo[i].id) return static_cast<EffectType>(i);
    return EffectType::Count;
}

Effect makeEffect(EffectType type) noexcept {
    Effect e;
    e.type = type;
    e.amount = effectInfo(type).def;
    return e;
}

void applyEffects(const std::vector<Effect>& effects, ColorAdjust& color, EffectParams& looks) noexcept {
    for (const Effect& e : effects) {
        if (!e.enabled) continue;
        const EffectInfo& info = effectInfo(e.type);
        const float a = std::clamp(e.amount, info.min, info.max);
        switch (e.type) {
            case EffectType::Brightness: color.brightness += a; break;
            case EffectType::Contrast:   color.contrast *= a; break;
            case EffectType::Saturation: color.saturation *= a; break;
            case EffectType::Grayscale:  looks.grayscale = std::max(looks.grayscale, a); break;
            case EffectType::Sepia:      looks.sepia = std::max(looks.sepia, a); break;
            case EffectType::Blur:       looks.blur += a; break;
            case EffectType::Sharpen:    looks.sharpen = std::min(1.0f, looks.sharpen + a); break;
            case EffectType::Vignette:   looks.vignette = std::min(1.0f, looks.vignette + a); break;
            case EffectType::Exposure:   color.exposure += a; break;
            case EffectType::Temperature: color.temperature += a; break;
            case EffectType::Tint:       color.tint += a; break;
            case EffectType::HueShift:   color.hue += a; break;
            case EffectType::Vibrance:   color.vibrance += a; break;
            case EffectType::Invert:     looks.invert = std::max(looks.invert, a); break;
            // Stacked posterize keeps the coarsest; stacked pixelate the largest block.
            case EffectType::Posterize:  looks.posterize = looks.posterize > 0.0f ? std::min(looks.posterize, a) : a; break;
            case EffectType::Pixelate:   looks.pixelate = std::max(looks.pixelate, a); break;
            case EffectType::Count:      break;
        }
    }
}

}  // namespace lf
