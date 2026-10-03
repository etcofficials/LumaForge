#pragma once

#include "ui/inspector/InspectorSection.h"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPlainTextEdit;
class QToolButton;

namespace lf::ui {

class ColorButton;
class ParamSlider;

// Text content + presets.
class TextContentSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit TextContentSection(InspectorContext& ctx, QWidget* parent = nullptr);
    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Text; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Text; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    QPlainTextEdit* m_text;
    QComboBox* m_preset;
};

// Font family / size / weight / style / spacing / alignment.
class TypographySection final : public InspectorSection {
    Q_OBJECT
public:
    explicit TypographySection(InspectorContext& ctx, QWidget* parent = nullptr);
    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Text; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Text; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    QFontComboBox* m_font;
    ParamSlider* m_size;
    QToolButton* m_bold;
    QToolButton* m_italic;
    QToolButton* m_alignLeft;
    QToolButton* m_alignCenter;
    QToolButton* m_alignRight;
    ParamSlider* m_letterSpacing;
    ParamSlider* m_lineSpacing;
};

// Fill colour + opacity, outline, shadow and background box.
class TextStyleSection final : public InspectorSection {
    Q_OBJECT
public:
    enum class Part { Fill, Outline, Shadow, Background };
    TextStyleSection(Part part, InspectorContext& ctx, QWidget* parent = nullptr);
    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Text; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Text; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    Part m_part;
    QCheckBox* m_enabled = nullptr;
    ColorButton* m_color = nullptr;
    ParamSlider* m_opacity = nullptr;
    ParamSlider* m_width = nullptr;     // outline width / background padding
    ParamSlider* m_offsetX = nullptr;
    ParamSlider* m_offsetY = nullptr;
    ParamSlider* m_blur = nullptr;
};

// Applies a named preset to the clip's text and transform.
void applyTextPreset(project::Clip& clip, int preset, int frameWidth, int frameHeight);
const QStringList& textPresetNames();

}  // namespace lf::ui
