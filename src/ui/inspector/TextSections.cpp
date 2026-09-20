#include "ui/inspector/TextSections.h"

#include "ui/widgets/ColorButton.h"
#include "ui/widgets/ParamSlider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QToolButton>

#include <cmath>

namespace lf::ui {

using namespace lf::project;

// ---------------------------------------------------------------- presets

const QStringList& textPresetNames() {
    static const QStringList names{QObject::tr("Title"), QObject::tr("Subtitle"), QObject::tr("Lower Third"), QObject::tr("Caption")};
    return names;
}

void applyTextPreset(Clip& c, int preset, int frameW, int frameH) {
    // Presets only initialise properties; the user can change anything after.
    const float h = static_cast<float>(frameH);
    TextParams t;
    t.text = c.text.text;   // keep what the user typed
    Transform2D tf;
    switch (preset) {
        case 0:   // Title: big, bold, centred, soft shadow
            t.fontSize = h * 0.09f; t.bold = true; t.align = TextAlign::Center; t.shadow = true; t.shadowBlur = 8; t.shadowOffsetX = t.shadowOffsetY = 4;
            if (t.text.empty() || t.text == "Title") t.text = "Title";
            break;
        case 1:   // Subtitle: smaller, regular, near the bottom, outline for legibility
            t.fontSize = h * 0.05f; t.bold = false; t.align = TextAlign::Center; t.outlineWidth = 2.0f; t.shadow = false;
            tf.positionY = h * 0.36f;
            if (t.text.empty() || t.text == "Title") t.text = "Subtitle";
            break;
        case 2:   // Lower third: left aligned, boxed, bottom-left
            t.fontSize = h * 0.045f; t.bold = true; t.align = TextAlign::Left; t.backgroundOpacity = 0.75f;
            t.backgroundColor = {0.09f, 0.10f, 0.12f, 1.0f}; t.backgroundPadding = h * 0.015f; t.shadow = false;
            tf.positionX = -frameW * 0.25f; tf.positionY = h * 0.33f;
            if (t.text.empty() || t.text == "Title") t.text = "Name\nRole";
            break;
        default:  // Caption: boxed, centred, bottom
            t.fontSize = h * 0.04f; t.bold = false; t.align = TextAlign::Center; t.backgroundOpacity = 0.6f;
            t.backgroundColor = {0.0f, 0.0f, 0.0f, 1.0f}; t.backgroundPadding = h * 0.012f; t.shadow = false;
            tf.positionY = h * 0.4f;
            if (t.text.empty() || t.text == "Title") t.text = "Caption";
            break;
    }
    c.text = t;
    c.transform = tf;
    (void)frameW;
}

// ---------------------------------------------------------------- content

TextContentSection::TextContentSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Text"), QStringLiteral("text"), ctx, parent) {
    auto* layout = static_cast<QVBoxLayout*>(content()->layout());
    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(tr("Type your text"));
    m_text->setTabChangesFocus(true);
    m_text->setFixedHeight(72);
    layout->addWidget(m_text);
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    m_preset = new QComboBox(this);
    m_preset->setFocusPolicy(Qt::NoFocus);
    m_preset->addItem(tr("Apply preset…"));
    for (const QString& n : textPresetNames()) m_preset->addItem(n);
    row->addWidget(m_preset, 1);
    layout->addLayout(row);

    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading) return;
        const std::string s = m_text->toPlainText().toStdString();
        m_ctx.editSelection(tr("Edit Text"), prop::TextContent, [s](Clip& c) { c.text.text = s; });
    });
    connect(m_preset, &QComboBox::activated, this, [this](int index) {
        if (index <= 0) return;
        m_preset->setCurrentIndex(0);
        const int w = m_ctx.project().settings.width, h = m_ctx.project().settings.height;
        m_ctx.editSelection(tr("Text Preset"), prop::TextPreset, [index, w, h](Clip& c) { applyTextPreset(c, index - 1, w, h); });
        m_ctx.endGesture();
    });
}

void TextContentSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    const QString t = QString::fromStdString(c.text.text);
    if (m_text->toPlainText() != t) {
        // Keep the caret where it was when the model echoes our own edit back.
        const int pos = m_text->textCursor().position();
        m_text->setPlainText(t);
        QTextCursor cur = m_text->textCursor();
        cur.setPosition(std::min(pos, static_cast<int>(t.size())));
        m_text->setTextCursor(cur);
    }
}

// ---------------------------------------------------------------- typography

namespace {
QToolButton* toggle(QWidget* parent, const QString& text, const QString& tip, bool exclusive = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setAutoExclusive(exclusive);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(28, 22);
    return b;
}
}  // namespace

TypographySection::TypographySection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Typography"), QStringLiteral("typography"), ctx, parent) {
    auto* f = form();
    m_font = new QFontComboBox(this);
    m_font->setFontFilters(QFontComboBox::ScalableFonts);
    m_font->setFocusPolicy(Qt::ClickFocus);
    f->addRow(tr("Font"), m_font);
    m_size = new ParamSlider(4, 1000, 72, 0, this);
    m_size->setSliderRange(8, 300);
    m_size->setSuffix(QStringLiteral(" px"));
    f->addRow(tr("Size"), m_size);

    auto* styleRow = new QWidget(this);
    auto* sl = new QHBoxLayout(styleRow);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(4);
    m_bold = toggle(styleRow, QStringLiteral("B"), tr("Bold"));
    QFont bf = m_bold->font(); bf.setBold(true); m_bold->setFont(bf);
    m_italic = toggle(styleRow, QStringLiteral("I"), tr("Italic"));
    QFont itf = m_italic->font(); itf.setItalic(true); m_italic->setFont(itf);
    sl->addWidget(m_bold);
    sl->addWidget(m_italic);
    sl->addSpacing(10);
    m_alignLeft = toggle(styleRow, QStringLiteral("⯇"), tr("Align left"), true);
    m_alignCenter = toggle(styleRow, QStringLiteral("☰"), tr("Align centre"), true);
    m_alignRight = toggle(styleRow, QStringLiteral("⯈"), tr("Align right"), true);
    sl->addWidget(m_alignLeft);
    sl->addWidget(m_alignCenter);
    sl->addWidget(m_alignRight);
    sl->addStretch(1);
    f->addRow(tr("Style"), styleRow);

    m_letterSpacing = new ParamSlider(-20, 100, 0, 1, this);
    m_letterSpacing->setSliderRange(-10, 40);
    m_letterSpacing->setSuffix(QStringLiteral(" px"));
    f->addRow(tr("Letter spacing"), m_letterSpacing);
    m_lineSpacing = new ParamSlider(0.5, 3, 1, 2, this);
    f->addRow(tr("Line spacing"), m_lineSpacing);

    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        if (m_loading) return;
        const std::string family = font.family().toStdString();
        m_ctx.editSelection(tr("Font"), prop::TextFont, [family](Clip& c) { c.text.fontFamily = family; });
        m_ctx.endGesture();
    });
    connect(m_size, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Font Size"), prop::TextSize, [v](Clip& c) { c.text.fontSize = static_cast<float>(v); });
    });
    connect(m_size, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_bold, &QToolButton::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Bold"), prop::TextBold, [on](Clip& c) { c.text.bold = on; });
        m_ctx.endGesture();
    });
    connect(m_italic, &QToolButton::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Italic"), prop::TextItalic, [on](Clip& c) { c.text.italic = on; });
        m_ctx.endGesture();
    });
    auto align = [this](QToolButton* b, TextAlign a) {
        connect(b, &QToolButton::toggled, this, [this, a](bool on) {
            if (m_loading || !on) return;
            m_ctx.editSelection(tr("Alignment"), prop::TextAlign, [a](Clip& c) { c.text.align = a; });
            m_ctx.endGesture();
        });
    };
    align(m_alignLeft, TextAlign::Left);
    align(m_alignCenter, TextAlign::Center);
    align(m_alignRight, TextAlign::Right);
    connect(m_letterSpacing, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Letter Spacing"), prop::TextLetterSpacing, [v](Clip& c) { c.text.letterSpacing = static_cast<float>(v); });
    });
    connect(m_letterSpacing, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_lineSpacing, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Line Spacing"), prop::TextLineSpacing, [v](Clip& c) { c.text.lineSpacing = static_cast<float>(v); });
    });
    connect(m_lineSpacing, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
}

void TypographySection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    const QString family = QString::fromStdString(c.text.fontFamily);
    if (m_font->currentFont().family() != family) m_font->setCurrentFont(QFont(family));
    m_size->setValue(c.text.fontSize);
    m_bold->setChecked(c.text.bold);
    m_italic->setChecked(c.text.italic);
    m_alignLeft->setChecked(c.text.align == TextAlign::Left);
    m_alignCenter->setChecked(c.text.align == TextAlign::Center);
    m_alignRight->setChecked(c.text.align == TextAlign::Right);
    m_letterSpacing->setValue(c.text.letterSpacing);
    m_lineSpacing->setValue(c.text.lineSpacing);
}

// ---------------------------------------------------------------- fill / outline / shadow / background

TextStyleSection::TextStyleSection(Part part, InspectorContext& ctx, QWidget* parent)
    : InspectorSection(part == Part::Fill ? tr("Text Color") : part == Part::Outline ? tr("Outline") : part == Part::Shadow ? tr("Shadow") : tr("Background"),
                       part == Part::Fill ? QStringLiteral("textColor") : part == Part::Outline ? QStringLiteral("textOutline") : part == Part::Shadow ? QStringLiteral("textShadow") : QStringLiteral("textBackground"),
                       ctx, parent),
      m_part(part) {
    auto* f = form();
    if (part != Part::Fill) {
        m_enabled = new QCheckBox(tr("Enabled"), this);
        m_enabled->setFocusPolicy(Qt::NoFocus);
        f->addRow(QString(), m_enabled);
    }
    m_color = new ColorButton(this);
    f->addRow(tr("Color"), m_color);
    m_opacity = new ParamSlider(0, 100, 100, 0, this);
    m_opacity->setSuffix(QStringLiteral(" %"));
    f->addRow(tr("Opacity"), m_opacity);
    if (part == Part::Outline) {
        m_width = new ParamSlider(0, 60, 3, 1, this);
        m_width->setSuffix(QStringLiteral(" px"));
        f->addRow(tr("Width"), m_width);
    } else if (part == Part::Shadow) {
        m_offsetX = new ParamSlider(-100, 100, 4, 1, this);
        m_offsetX->setSuffix(QStringLiteral(" px"));
        m_offsetY = new ParamSlider(-100, 100, 4, 1, this);
        m_offsetY->setSuffix(QStringLiteral(" px"));
        m_blur = new ParamSlider(0, 60, 6, 1, this);
        m_blur->setSuffix(QStringLiteral(" px"));
        f->addRow(tr("Offset X"), m_offsetX);
        f->addRow(tr("Offset Y"), m_offsetY);
        f->addRow(tr("Blur"), m_blur);
    } else if (part == Part::Background) {
        m_width = new ParamSlider(0, 200, 24, 0, this);
        m_width->setSuffix(QStringLiteral(" px"));
        f->addRow(tr("Padding"), m_width);
    }

    const Part p = part;
    if (m_enabled) {
        connect(m_enabled, &QCheckBox::toggled, this, [this, p](bool on) {
            if (m_loading) return;
            m_ctx.editSelection(on ? tr("Enable") : tr("Disable"), prop::TextOutline + static_cast<int>(p), [p, on](Clip& c) {
                if (p == Part::Outline) c.text.outlineWidth = on ? (c.text.outlineWidth > 0 ? c.text.outlineWidth : 3.0f) : 0.0f;
                else if (p == Part::Shadow) c.text.shadow = on;
                else c.text.backgroundOpacity = on ? (c.text.backgroundOpacity > 0 ? c.text.backgroundOpacity : 0.6f) : 0.0f;
            });
            m_ctx.endGesture();
        });
    }
    connect(m_color, &ColorButton::colorEdited, this, [this, p](const ColorRgba& col) {
        if (m_loading) return;
        m_ctx.editSelection(tr("Color"), prop::TextColor + static_cast<int>(p), [p, col](Clip& c) {
            ColorRgba& target = p == Part::Fill ? c.text.color : p == Part::Outline ? c.text.outlineColor : p == Part::Shadow ? c.text.shadowColor : c.text.backgroundColor;
            target.r = col.r; target.g = col.g; target.b = col.b;
        });
        m_ctx.endGesture();
    });
    connect(m_opacity, &ParamSlider::valueEdited, this, [this, p](double v) {
        if (m_loading) return;
        const float a = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Opacity"), prop::TextShadowOpacity + static_cast<int>(p), [p, a](Clip& c) {
            if (p == Part::Fill) c.text.color.a = a;
            else if (p == Part::Outline) c.text.outlineColor.a = a;
            else if (p == Part::Shadow) c.text.shadowColor.a = a;
            else c.text.backgroundOpacity = a;
        });
    });
    connect(m_opacity, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    if (m_width) {
        connect(m_width, &ParamSlider::valueEdited, this, [this, p](double v) {
            if (m_loading) return;
            const float w = static_cast<float>(v);
            m_ctx.editSelection(p == Part::Outline ? tr("Outline Width") : tr("Padding"), p == Part::Outline ? prop::TextOutlineWidth : prop::TextPadding, [p, w](Clip& c) {
                if (p == Part::Outline) c.text.outlineWidth = w;
                else c.text.backgroundPadding = w;
            });
        });
        connect(m_width, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    }
    if (m_offsetX) {
        auto bind = [this](ParamSlider* s, int propertyId, float TextParams::*member) {
            connect(s, &ParamSlider::valueEdited, this, [this, propertyId, member](double v) {
                if (m_loading) return;
                m_ctx.editSelection(tr("Shadow"), propertyId, [v, member](Clip& c) { c.text.*member = static_cast<float>(v); });
            });
            connect(s, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
        };
        bind(m_offsetX, prop::TextShadowOffsetX, &TextParams::shadowOffsetX);
        bind(m_offsetY, prop::TextShadowOffsetY, &TextParams::shadowOffsetY);
        bind(m_blur, prop::TextShadowBlur, &TextParams::shadowBlur);
    }
}

void TextStyleSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    const TextParams& t = c.text;
    switch (m_part) {
        case Part::Fill:
            m_color->setColor(t.color);
            m_opacity->setValue(std::round(t.color.a * 100));
            break;
        case Part::Outline:
            m_enabled->setChecked(t.outlineWidth > 0);
            m_color->setColor(t.outlineColor);
            m_opacity->setValue(std::round(t.outlineColor.a * 100));
            m_width->setValue(t.outlineWidth);
            break;
        case Part::Shadow:
            m_enabled->setChecked(t.shadow);
            m_color->setColor(t.shadowColor);
            m_opacity->setValue(std::round(t.shadowColor.a * 100));
            m_offsetX->setValue(t.shadowOffsetX);
            m_offsetY->setValue(t.shadowOffsetY);
            m_blur->setValue(t.shadowBlur);
            break;
        case Part::Background:
            m_enabled->setChecked(t.backgroundOpacity > 0);
            m_color->setColor(t.backgroundColor);
            m_opacity->setValue(std::round(t.backgroundOpacity * 100));
            m_width->setValue(t.backgroundPadding);
            break;
    }
    const bool on = m_part == Part::Fill || m_enabled->isChecked();
    setSubtitle(m_part != Part::Fill && !on ? tr("off") : QString());
}

}  // namespace lf::ui
