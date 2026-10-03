#include "lumaforge/project/TitlePresets.h"

#include "lumaforge/project/TimelineOps.h"

namespace lf::project {

namespace {
constexpr TitlePreset kPresets[] = {
    {"title",      "Title",       "Large centred title with a soft shadow",            "Title"},
    {"subtitle",   "Subtitle",    "Smaller outlined line near the bottom",             "Subtitle"},
    {"lowerThird", "Lower Third", "Name and role on a dark box, bottom left",           "Name\nRole"},
    {"caption",    "Caption",     "Boxed caption centred at the bottom",               "Caption"},
    {"chapter",    "Chapter",     "Spaced-out chapter heading on the left",            "CHAPTER 1\nThe Beginning"},
    {"callout",    "Callout",     "Bright note box in the top-right corner",           "Note"},
    {"quote",      "Quote",       "Italic centred quotation",                          "“A quote worth sharing.”"},
};
}  // namespace

std::span<const TitlePreset> titlePresets() noexcept { return kPresets; }

void applyTitlePreset(Clip& c, int index, int frameWidth, int frameHeight, bool keepText) {
    if (index < 0 || index >= static_cast<int>(std::size(kPresets))) return;
    const float h = static_cast<float>(frameHeight);
    const float w = static_cast<float>(frameWidth);
    const TitlePreset& preset = kPresets[index];
    TextParams t;
    t.text = (keepText && !c.text.text.empty() && c.text.text != "Title") ? c.text.text : std::string(preset.sampleText);
    t.fontFamily = c.text.fontFamily.empty() ? t.fontFamily : c.text.fontFamily;
    Transform2D tf;
    switch (index) {
        case 0:   // Title: big, bold, centred, soft shadow
            t.fontSize = h * 0.09f; t.bold = true; t.align = TextAlign::Center;
            t.shadow = true; t.shadowBlur = 8; t.shadowOffsetX = t.shadowOffsetY = 4;
            break;
        case 1:   // Subtitle: smaller, regular, near the bottom, outline for legibility
            t.fontSize = h * 0.05f; t.bold = false; t.align = TextAlign::Center; t.outlineWidth = 2.0f;
            tf.positionY = h * 0.36f;
            break;
        case 2:   // Lower third: left aligned, boxed, bottom-left
            t.fontSize = h * 0.045f; t.bold = true; t.align = TextAlign::Left; t.backgroundOpacity = 0.75f;
            t.backgroundColor = {0.09f, 0.10f, 0.12f, 1.0f}; t.backgroundPadding = h * 0.015f;
            tf.positionX = -w * 0.25f; tf.positionY = h * 0.33f;
            break;
        case 3:   // Caption: boxed, centred, bottom
            t.fontSize = h * 0.04f; t.bold = false; t.align = TextAlign::Center; t.backgroundOpacity = 0.6f;
            t.backgroundColor = {0.0f, 0.0f, 0.0f, 1.0f}; t.backgroundPadding = h * 0.012f;
            tf.positionY = h * 0.4f;
            break;
        case 4:   // Chapter: left, wide letter spacing, shadow
            t.fontSize = h * 0.065f; t.bold = true; t.align = TextAlign::Left; t.letterSpacing = h * 0.006f; t.lineSpacing = 1.1f;
            t.shadow = true; t.shadowBlur = 10; t.shadowOffsetX = t.shadowOffsetY = 3;
            tf.positionX = -w * 0.22f;
            break;
        case 5:   // Callout: dark text on an amber box, top right
            t.fontSize = h * 0.04f; t.bold = true; t.align = TextAlign::Center;
            t.color = {0.08f, 0.08f, 0.09f, 1.0f};
            t.backgroundOpacity = 1.0f; t.backgroundColor = {0.98f, 0.72f, 0.20f, 1.0f}; t.backgroundPadding = h * 0.014f;
            tf.positionX = w * 0.34f; tf.positionY = -h * 0.36f;
            break;
        default:  // Quote: italic, centred, gentle shadow
            t.fontSize = h * 0.055f; t.bold = false; t.italic = true; t.align = TextAlign::Center; t.lineSpacing = 1.15f;
            t.shadow = true; t.shadowBlur = 6; t.shadowOffsetX = t.shadowOffsetY = 2;
            break;
    }
    c.text = t;
    c.transform = tf;
}

Clip makeTitleClip(int index, int frameWidth, int frameHeight) {
    Clip c;
    c.kind = ClipKind::Text;
    c.duration = ops::kDefaultTextDuration;
    applyTitlePreset(c, index, frameWidth, frameHeight, false);
    const auto presets = titlePresets();
    c.name = (index >= 0 && index < static_cast<int>(presets.size())) ? presets[static_cast<std::size_t>(index)].name : "Title";
    return c;
}

}  // namespace lf::project
