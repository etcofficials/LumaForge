#pragma once

#include <QWidget>

class QToolButton;
class QLabel;
class QVBoxLayout;
class QFormLayout;

namespace lf::ui {

// Inspector building block: a clickable header that shows/hides its content.
// The open/closed state is remembered per section key across sessions.
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, const QString& settingsKey, QWidget* parent = nullptr);

    [[nodiscard]] QWidget* content() const noexcept { return m_content; }
    // Convenience: a two-column form inside the content area.
    [[nodiscard]] QFormLayout* form();
    // Optional small button at the right of the header (e.g. "Reset").
    QToolButton* addHeaderButton(const QString& text, const QString& toolTip);
    void setExpanded(bool expanded);
    [[nodiscard]] bool isExpanded() const noexcept { return m_expanded; }
    void setSubtitle(const QString& text);
    // The key passed at construction ("transform", "color"...).
    [[nodiscard]] QString sectionKey() const { return m_key.section(QLatin1Char('/'), -1); }

signals:
    void toggled(bool expanded);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateArrow();

    QString m_key;
    QToolButton* m_toggle;
    QLabel* m_subtitle;
    QWidget* m_headerRow;
    QWidget* m_content;
    QVBoxLayout* m_contentLayout;
    QFormLayout* m_form = nullptr;
    bool m_expanded = true;
};

}  // namespace lf::ui
