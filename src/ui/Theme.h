#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

namespace lf::ui {

// LumaForge visual language: a calm, low-contrast dark workspace with one
// accent colour and one signal colour (the playhead). Every colour in the UI
// comes from here; the stylesheet is generated from the same tokens.
struct Palette {
    // Surfaces, darkest to lightest
    QColor bgWindow{0x1B, 0x1D, 0x21};
    QColor bgPanel{0x21, 0x24, 0x29};
    QColor bgHeader{0x26, 0x2A, 0x30};
    QColor bgElevated{0x2B, 0x2F, 0x36};
    QColor bgInput{0x17, 0x19, 0x1D};
    QColor bgHover{0x2E, 0x33, 0x3B};
    QColor bgPressed{0x35, 0x3B, 0x44};

    QColor border{0x2F, 0x34, 0x3C};
    QColor borderStrong{0x3A, 0x40, 0x4A};

    QColor textPrimary{0xE4, 0xE7, 0xEB};
    QColor textSecondary{0x9B, 0xA3, 0xAE};
    QColor textMuted{0x6C, 0x74, 0x81};
    QColor textDisabled{0x50, 0x56, 0x60};

    QColor accent{0x4C, 0x8D, 0xFF};
    QColor accentHover{0x6A, 0xA1, 0xFF};
    QColor accentText{0xFF, 0xFF, 0xFF};
    QColor selection{0x2F, 0x4E, 0x86};

    QColor playhead{0xE5, 0x53, 0x3D};
    QColor danger{0xE5, 0x53, 0x3D};
    QColor warning{0xE0, 0xA4, 0x3A};
    QColor success{0x4C, 0xAF, 0x7D};

    // Timeline
    QColor timelineBg{0x1A, 0x1C, 0x20};
    QColor rulerBg{0x20, 0x23, 0x29};
    QColor trackVideo{0x1E, 0x21, 0x26};
    QColor trackAudio{0x1C, 0x1F, 0x24};
    QColor trackHeader{0x24, 0x28, 0x2E};
    QColor gridMajor{0x33, 0x38, 0x41};
    QColor gridMinor{0x27, 0x2B, 0x32};
    QColor clipVideo{0x3C, 0x6C, 0xB0};
    QColor clipAudio{0x3E, 0x9A, 0x78};

    // Preview surround (matches PreviewScene::surround)
    QColor previewSurround{0x16, 0x18, 0x1B};
};

struct Metrics {
    int panelHeaderHeight = 30;
    int toolbarHeight = 38;
    int transportHeight = 40;
    int rulerHeight = 26;
    int videoTrackHeight = 60;
    int audioTrackHeight = 44;
    int trackHeaderWidth = 164;
    int splitterWidth = 4;
    int radius = 3;
    int spacing = 6;
};

class Theme {
public:
    static const Palette& palette();
    static const Metrics& metrics();
    static QFont uiFont();
    static QFont monoFont();

    // Installs style, palette, fonts and the stylesheet on the application.
    static void apply(QApplication& app);

private:
    static QString buildStyleSheet();
};

}  // namespace lf::ui
