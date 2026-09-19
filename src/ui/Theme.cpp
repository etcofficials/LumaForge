#include "ui/Theme.h"

#include "lumaforge/core/Log.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace lf::ui {

namespace {
const Palette kPalette{};
const Metrics kMetrics{};

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }
}  // namespace

const Palette& Theme::palette() { return kPalette; }
const Metrics& Theme::metrics() { return kMetrics; }

QFont Theme::uiFont() {
    QFont f(QStringLiteral("Segoe UI"), 9);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont Theme::monoFont() {
    for (const auto& family : {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")}) {
        if (QFontDatabase::hasFamily(family)) {
            QFont f(family, 9);
            return f;
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

void Theme::apply(QApplication& app) {
    // Fusion is the only Qt style that fully respects QPalette + stylesheets.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setFont(uiFont());

    const Palette& p = kPalette;
    QPalette pal;
    pal.setColor(QPalette::Window, p.bgWindow);
    pal.setColor(QPalette::WindowText, p.textPrimary);
    pal.setColor(QPalette::Base, p.bgInput);
    pal.setColor(QPalette::AlternateBase, p.bgPanel);
    pal.setColor(QPalette::ToolTipBase, p.bgElevated);
    pal.setColor(QPalette::ToolTipText, p.textPrimary);
    pal.setColor(QPalette::Text, p.textPrimary);
    pal.setColor(QPalette::PlaceholderText, p.textMuted);
    pal.setColor(QPalette::Button, p.bgElevated);
    pal.setColor(QPalette::ButtonText, p.textPrimary);
    pal.setColor(QPalette::BrightText, p.accentText);
    pal.setColor(QPalette::Highlight, p.selection);
    pal.setColor(QPalette::HighlightedText, p.textPrimary);
    pal.setColor(QPalette::Link, p.accent);
    pal.setColor(QPalette::Light, p.borderStrong);
    pal.setColor(QPalette::Midlight, p.border);
    pal.setColor(QPalette::Mid, p.bgHeader);
    pal.setColor(QPalette::Dark, p.bgWindow);
    pal.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) pal.setColor(QPalette::Disabled, role, p.textDisabled);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, p.bgHeader);
    app.setPalette(pal);

    app.setStyleSheet(buildStyleSheet());
}

QString Theme::buildStyleSheet() {
    QFile file(QStringLiteral(":/theme/lumaforge.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LF_LOG_ERROR("ui", "theme stylesheet resource missing");
        return {};
    }
    QString qss = QString::fromUtf8(file.readAll());

    const Palette& p = kPalette;
    const Metrics& m = kMetrics;
    const QList<QPair<QString, QString>> tokens = {
        {"bg.window", hex(p.bgWindow)},       {"bg.panel", hex(p.bgPanel)},         {"bg.header", hex(p.bgHeader)},
        {"bg.elevated", hex(p.bgElevated)},   {"bg.input", hex(p.bgInput)},         {"bg.hover", hex(p.bgHover)},
        {"bg.pressed", hex(p.bgPressed)},     {"border", hex(p.border)},            {"border.strong", hex(p.borderStrong)},
        {"text.primary", hex(p.textPrimary)}, {"text.secondary", hex(p.textSecondary)}, {"text.muted", hex(p.textMuted)},
        {"text.disabled", hex(p.textDisabled)}, {"accent", hex(p.accent)},          {"accent.hover", hex(p.accentHover)},
        {"accent.text", hex(p.accentText)},   {"selection", hex(p.selection)},      {"danger", hex(p.danger)},
        {"warning", hex(p.warning)},          {"success", hex(p.success)},          {"playhead", hex(p.playhead)},
        {"radius", QString::number(m.radius)}, {"panelHeaderHeight", QString::number(m.panelHeaderHeight)},
        {"toolbarHeight", QString::number(m.toolbarHeight)}, {"splitterWidth", QString::number(m.splitterWidth)},
        {"font.mono", monoFont().family()},
    };
    for (const auto& [key, value] : tokens) qss.replace(QStringLiteral("{{%1}}").arg(key), value);
    return qss;
}

}  // namespace lf::ui
