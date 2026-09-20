#pragma once

// Plain-data visual parameters shared by the timeline model (what the user
// edits), the project file (what is saved) and the compositor (what is
// rendered). Deliberately free of any rendering or Qt types.

#include <string>
#include <vector>

namespace lf {

struct Transform2D {
    float positionX = 0.0f;   // frame pixels, offset from centre (+x right)
    float positionY = 0.0f;   // frame pixels, offset from centre (+y down)
    float scaleX = 1.0f;      // 1 = fit to frame
    float scaleY = 1.0f;
    float rotation = 0.0f;    // degrees, clockwise
    float opacity = 1.0f;     // 0..1
    bool flipH = false;
    bool flipV = false;
    friend constexpr bool operator==(const Transform2D&, const Transform2D&) = default;
};

// Fractions of the source removed from each edge, 0..1.
struct Crop {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    [[nodiscard]] constexpr bool isIdentity() const noexcept { return left == 0.0f && right == 0.0f && top == 0.0f && bottom == 0.0f; }
    friend constexpr bool operator==(const Crop&, const Crop&) = default;
};

// Basic colour correction. Neutral values are the defaults.
struct ColorAdjust {
    float exposure = 0.0f;     // stops, -3..3
    float brightness = 0.0f;   // -1..1
    float contrast = 1.0f;     // 0..2
    float saturation = 1.0f;   // 0..2
    float temperature = 0.0f;  // -1 (cool) .. 1 (warm)
    float tint = 0.0f;         // -1 (magenta) .. 1 (green)
    float highlights = 0.0f;   // -1..1
    float shadows = 0.0f;      // -1..1
    float gamma = 1.0f;        // 0.2..3
    float vibrance = 0.0f;     // -1..1
    float hue = 0.0f;          // degrees
    [[nodiscard]] constexpr bool isIdentity() const noexcept {
        return exposure == 0.0f && brightness == 0.0f && contrast == 1.0f && saturation == 1.0f && temperature == 0.0f && tint == 0.0f &&
               highlights == 0.0f && shadows == 0.0f && gamma == 1.0f && vibrance == 0.0f && hue == 0.0f;
    }
    friend constexpr bool operator==(const ColorAdjust&, const ColorAdjust&) = default;
};

// ---------------------------------------------------------------- effects
//
// A clip carries an ordered list of Effects; each has one type, an enabled
// flag and one amount whose meaning/range comes from effectInfo(). Before
// rendering the list is flattened (applyEffects) into the ColorAdjust the
// shader already understands plus EffectParams for the looks/filters, so
// adding an effect type never touches the compositor's interface.
enum class EffectType { Brightness, Contrast, Saturation, Grayscale, Sepia, Blur, Sharpen, Vignette, Count };

struct EffectInfo {
    const char* id;        // stable key in project files
    const char* name;      // UI label
    float min, max, def;   // amount range and default
    int decimals;
    const char* suffix;
};
[[nodiscard]] const EffectInfo& effectInfo(EffectType type) noexcept;
// Type for a project-file id; nullopt-like sentinel is EffectType::Count.
[[nodiscard]] EffectType effectTypeFromId(const std::string& id) noexcept;

struct Effect {
    EffectType type = EffectType::Brightness;
    bool enabled = true;
    float amount = 0.0f;
    friend constexpr bool operator==(const Effect&, const Effect&) = default;
};
[[nodiscard]] Effect makeEffect(EffectType type) noexcept;

// Flattened render-side parameters for the looks and filters.
struct EffectParams {
    float grayscale = 0.0f;   // 0..1 mix
    float sepia = 0.0f;       // 0..1 mix
    float vignette = 0.0f;    // 0..1
    float blur = 0.0f;        // radius in source pixels, 0 = off
    float sharpen = 0.0f;     // 0..1
    [[nodiscard]] constexpr bool isIdentity() const noexcept { return grayscale == 0.0f && sepia == 0.0f && vignette == 0.0f && blur == 0.0f && sharpen == 0.0f; }
    friend constexpr bool operator==(const EffectParams&, const EffectParams&) = default;
};

// Folds enabled effects into `color` (brightness/contrast/saturation stack
// multiplicatively / additively on top of the clip's own correction) and `looks`.
void applyEffects(const std::vector<Effect>& effects, ColorAdjust& color, EffectParams& looks) noexcept;

struct ColorRgba {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    friend constexpr bool operator==(const ColorRgba&, const ColorRgba&) = default;
};

enum class TextAlign { Left, Center, Right };

// A text clip's look. Rendered by the text rasteriser into a layer texture;
// the Transform2D of the clip then places it like any other layer.
struct TextParams {
    std::string text = "Title";
    std::string fontFamily = "Segoe UI";
    float fontSize = 72.0f;            // frame pixels (relative to the sequence height)
    bool bold = true;
    bool italic = false;
    ColorRgba color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align = TextAlign::Center;
    float outlineWidth = 0.0f;         // frame pixels, 0 = off
    ColorRgba outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    bool shadow = false;
    float shadowOffsetX = 4.0f, shadowOffsetY = 4.0f;
    float shadowBlur = 6.0f;
    ColorRgba shadowColor{0.0f, 0.0f, 0.0f, 0.75f};
    float backgroundOpacity = 0.0f;    // box behind the text, 0 = none
    ColorRgba backgroundColor{0.0f, 0.0f, 0.0f, 1.0f};
    float lineSpacing = 1.0f;
    friend bool operator==(const TextParams&, const TextParams&) = default;
};

}  // namespace lf
