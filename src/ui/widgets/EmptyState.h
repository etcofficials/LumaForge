#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace lf::ui {

// Centered icon + title + hint, shown in a panel that has no content yet.
// Tells a new user what the panel is for and how to fill it, without
// documentation.
class EmptyState : public QWidget {
    Q_OBJECT
public:
    EmptyState(const QString& iconName, const QString& title, const QString& hint, QWidget* parent = nullptr);

    // Optional call-to-action button (e.g. "Import Media").
    QPushButton* setAction(const QString& text);

private:
    QLabel* m_icon;
    QLabel* m_title;
    QLabel* m_hint;
    QPushButton* m_button = nullptr;
};

}  // namespace lf::ui
