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
    float feather = 0.0f;     // soft edge width in source pixels, 0 = hard edge
    [[nodiscard]] constexpr bool isIdentity() const noexcept { return left == 0.0f && right == 0.0f && top == 0.0f && bottom == 0.0f && feather == 0.0f; }
    friend constexpr bool operator==(const Crop&, const Crop&) = default;
};

// A shape that limits where a layer is visible, in the layer's own picture
// space (0..1 across the source), so it follows position, scale and rotation.
enum class MaskShape { None, Rectangle, Ellipse };

struct Mask {
    MaskShape shape = MaskShape::None;
    float centerX = 0.5f;     // 0..1 of the picture width
    float centerY = 0.5f;     // 0..1 of the picture height
    float width = 0.5f;       // 0..1 of the picture width
    float height = 0.5f;      // 0..1 of the picture height
    float feather = 0.1f;     // soft edge as a fraction of the shape size, 0 = hard
    bool invert = false;      // show outside the shape instead
    [[nodiscard]] constexpr bool isActive() const noexcept { return shape != MaskShape::None; }
    friend constexpr bool operator==(const Mask&, const Mask&) = default;
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
// New types are appended so the enum stays in step with older builds; project
// files store the string id, never the number.
enum class EffectType {
    Brightness, Contrast, Saturation, Grayscale, Sepia, Blur, Sharpen, Vignette,
    Exposure, Temperature, Tint, HueShift, Vibrance, Invert, Posterize, Pixelate,
    DirectionalBlurH, DirectionalBlurV,
    Count
};

enum class EffectCategory { Color, BlurSharpen, Stylize };

struct EffectInfo {
    const char* id;        // stable key in project files
    const char* name;      // UI label
    float min, max, def;   // amount range and default
    int decimals;
    const char* suffix;
    EffectCategory category;
    const char* description;   // one line for tooltips and the effects browser
};
[[nodiscard]] const char* effectCategoryName(EffectCategory category) noexcept;
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
    float invert = 0.0f;      // 0..1 mix
    float posterize = 0.0f;   // levels per channel, 0 = off
    float pixelate = 0.0f;    // block size in source pixels, 0 = off
    // Streak along one direction in the picture (motion / whip-pan blur).
    // Length is a fraction of the picture width so it reads the same at any
    // source resolution; the angle is in degrees, 0 = horizontal, 90 = vertical.
    float directionalBlur = 0.0f;
    float directionalBlurAngle = 0.0f;
    [[nodiscard]] constexpr bool isIdentity() const noexcept {
        return grayscale == 0.0f && sepia == 0.0f && vignette == 0.0f && blur == 0.0f && sharpen == 0.0f && invert == 0.0f && posterize == 0.0f &&
               pixelate == 0.0f && directionalBlur == 0.0f;
    }
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
    float backgroundPadding = 24.0f;   // frame pixels around the text block
    float lineSpacing = 1.0f;          // multiple of the font's natural line height
    float letterSpacing = 0.0f;        // extra advance per character, frame pixels
    friend bool operator==(const TextParams&, const TextParams&) = default;
};

}  // namespace lf
