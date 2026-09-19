#pragma once

// Plain-data visual parameters shared by the timeline model (what the user
// edits), the project file (what is saved) and the compositor (what is
// rendered). Deliberately free of any rendering or Qt types.

namespace lf {

struct Transform2D {
    float positionX = 0.0f;   // frame pixels, offset from centre (+x right)
    float positionY = 0.0f;   // frame pixels, offset from centre (+y down)
    float scale = 1.0f;       // 1 = fit to frame
    float rotation = 0.0f;    // degrees, clockwise
    float opacity = 1.0f;     // 0..1
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

// Whole-frame looks and simple filters (Phase 10 fills these in).
struct EffectParams {
    bool grayscale = false;
    bool sepia = false;
    float vignette = 0.0f;    // 0..1
    float blur = 0.0f;        // radius in frame pixels, 0 = off
    float sharpen = 0.0f;     // 0..1
    friend constexpr bool operator==(const EffectParams&, const EffectParams&) = default;
};

}  // namespace lf
