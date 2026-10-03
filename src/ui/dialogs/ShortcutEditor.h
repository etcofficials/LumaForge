#pragma once

#include <QDialog>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace lf::ui {

class ActionRegistry;
enum class ActionId;

// Searchable table of every command: Command | Shortcut | Context. Change
// captures a new key combination (conflicts are shown and must be resolved
// explicitly), Clear removes it, Reset / Reset All restore the defaults.
// Used by Help > Keyboard Shortcuts and Settings > Shortcuts.
class ShortcutEditor : public QWidget {
    Q_OBJECT
public:
    explicit ShortcutEditor(ActionRegistry& actions, QWidget* parent = nullptr);

private:
    void refill();
    void applyFilter();
    void changeCurrent();
    void clearCurrent();
    void resetCurrent();
    void resetAll();
    [[nodiscard]] ActionId currentId(bool* ok) const;
    void updateButtons();

    ActionRegistry& m_actions;
    QLineEdit* m_search;
    QTableWidget* m_table;
    QPushButton* m_change;
    QPushButton* m_clear;
    QPushButton* m_reset;
};

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(ActionRegistry& actions, QWidget* parent = nullptr);
};

}  // namespace lf::ui
