#pragma once

#include "ui/inspector/InspectorSection.h"

class QCheckBox;
class QToolButton;

namespace lf::ui {

class ParamSlider;

// Volume (dB), mute and fades of the audio clip (or the linked audio of a
// selected video clip).
class AudioSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit AudioSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Audio; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Audio; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    ParamSlider* m_volume;
    QCheckBox* m_mute;
    ParamSlider* m_fadeIn;
    ParamSlider* m_fadeOut;
};

// Playback speed: presets + custom percentage. Changes the clip's duration.
class SpeedSection final : public InspectorSection {
    Q_OBJECT
public:
    explicit SpeedSection(InspectorContext& ctx, QWidget* parent = nullptr);

    [[nodiscard]] bool appliesTo(project::ClipKind kind) const override { return kind == project::ClipKind::Video || kind == project::ClipKind::Audio; }
    [[nodiscard]] InspectorTab tab() const override { return InspectorTab::Speed; }
    void load(const project::Clip& primary, int selectedCount) override;

private:
    void apply(Rational speed);
    std::vector<QToolButton*> m_presets;
    ParamSlider* m_custom;
};

}  // namespace lf::ui
