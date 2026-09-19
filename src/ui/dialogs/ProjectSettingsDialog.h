#pragma once

#include "lumaforge/project/Project.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;

namespace lf::ui {

// Name, frame size and frame rate of the sequence. Presets fill the fields;
// the fields remain editable for custom sizes.
class ProjectSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectSettingsDialog(const project::ProjectSettings& current, QWidget* parent = nullptr);

    [[nodiscard]] project::ProjectSettings settings() const;

    // Common frame rates, in the order shown.
    static const std::vector<std::pair<QString, Rational>>& frameRates();

private:
    void applyPreset(int index);
    void updateSummary();

    QLineEdit* m_name;
    QComboBox* m_preset;
    QSpinBox* m_width;
    QSpinBox* m_height;
    QComboBox* m_frameRate;
    QComboBox* m_sampleRate;
    QLabel* m_summary;
    Rational m_originalRate;   // kept when the project uses a rate outside the list
};

}  // namespace lf::ui
