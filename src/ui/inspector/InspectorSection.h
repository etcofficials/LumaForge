#pragma once

#include "lumaforge/project/Project.h"
#include "ui/widgets/CollapsibleSection.h"

#include <functional>
#include <vector>

namespace lf::project { class Document; struct SequenceEdit; }

namespace lf::ui {

// How sections write back to the document. Every edit becomes one
// SequenceEditCommand; edits with the same propertyId that arrive before
// endGesture() merge into a single undo step (slider drags).
class InspectorContext {
public:
    virtual ~InspectorContext() = default;
    using ClipEdit = std::function<void(project::Clip&)>;
    // Applies `fn` to every selected clip the section applies to.
    virtual void editSelection(const QString& undoText, int propertyId, const ClipEdit& fn) = 0;
    virtual void endGesture() = 0;
    // Structural edits (duration changes etc.): the builder receives the
    // document and the selected clip ids and returns a SequenceEdit.
    using EditBuilder = std::function<project::SequenceEdit(project::Document&, const std::vector<project::ClipId>&)>;
    virtual void pushEdit(const QString& undoText, const EditBuilder& build) = 0;
    [[nodiscard]] virtual const project::Project& project() const = 0;
};

// One collapsible group of controls. `load` refreshes the widgets from the
// primary clip (first selected) without emitting edits; `appliesTo` decides
// whether the section is shown for the current selection.
class InspectorSection : public CollapsibleSection {
    Q_OBJECT
public:
    InspectorSection(const QString& title, const QString& key, InspectorContext& ctx, QWidget* parent = nullptr)
        : CollapsibleSection(title, key, parent), m_ctx(ctx) {}

    [[nodiscard]] virtual bool appliesTo(project::ClipKind kind) const = 0;
    virtual void load(const project::Clip& primary, int selectedCount) = 0;

protected:
    // Guards against feeding programmatic widget updates back as edits.
    struct LoadGuard {
        explicit LoadGuard(bool& flag) : f(flag) { f = true; }
        ~LoadGuard() { f = false; }
        bool& f;
    };
    InspectorContext& m_ctx;
    bool m_loading = false;
};

// Property ids for undo merging. Each control gets its own id.
namespace prop {
enum : int {
    Name = 1,
    PositionX, PositionY, ScaleX, ScaleY, ScaleUniform, Rotation, Opacity, FlipH, FlipV,
    CropLeft, CropRight, CropTop, CropBottom,
    Exposure, Brightness, Contrast, Saturation, Temperature, Tint, Highlights, Shadows, Vibrance, Gamma, Hue,
    EffectGrayscale, EffectSepia, EffectVignette, EffectBlur, EffectSharpen,
    TextContent, TextFont, TextSize, TextBold, TextItalic, TextColor, TextAlign, TextLetterSpacing, TextLineSpacing,
    TextOutline, TextOutlineColor, TextOutlineWidth, TextShadow, TextShadowColor, TextShadowOpacity, TextShadowOffsetX,
    TextShadowOffsetY, TextShadowBlur, TextBackground, TextBackgroundColor, TextBackgroundOpacity, TextPadding, TextPreset,
    Volume, Mute, FadeIn, FadeOut,
    Speed,
    ResetGroup = 900,
};
}

}  // namespace lf::ui
