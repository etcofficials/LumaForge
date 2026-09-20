#include "lumaforge/core/VisualParams.h"

#include <algorithm>
#include <cstring>

namespace lf {

namespace {
// Order must match EffectType.
constexpr EffectInfo kInfo[] = {
    {"brightness", "Brightness", -1.0f, 1.0f, 0.0f, 2, ""},
    {"contrast",   "Contrast",    0.0f, 2.0f, 1.0f, 2, ""},
    {"saturation", "Saturation",  0.0f, 2.0f, 1.0f, 2, ""},
    {"grayscale",  "Grayscale",   0.0f, 1.0f, 1.0f, 2, ""},
    {"sepia",      "Sepia",       0.0f, 1.0f, 1.0f, 2, ""},
    {"blur",       "Blur",        0.0f, 40.0f, 6.0f, 1, " px"},
    {"sharpen",    "Sharpen",     0.0f, 1.0f, 0.5f, 2, ""},
    {"vignette",   "Vignette",    0.0f, 1.0f, 0.5f, 2, ""},
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<std::size_t>(EffectType::Count));
}  // namespace

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
            case EffectType::Count:      break;
        }
    }
}

}  // namespace lf
