#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;

namespace lf::ui {

class ActionRegistry;

// Ctrl+K: type to find any command, Enter runs it. The list is the
// ActionRegistry itself (same QActions as menus and shortcuts), so a command
// shown here is exactly what the menu item does; unavailable commands are
// listed greyed with the reason they cannot run.
class CommandPalette : public QDialog {
    Q_OBJECT
public:
    CommandPalette(ActionRegistry& actions, QWidget* parent);

    // Fuzzy score of `query` against `text` (higher = better, < 0 = no match). Public for tests.
    static int score(const QString& query, const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void refill();
    void runCurrent();

    ActionRegistry& m_actions;
    QLineEdit* m_search;
    QListWidget* m_list;
};

}  // namespace lf::ui
