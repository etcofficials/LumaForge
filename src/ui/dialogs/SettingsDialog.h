#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QDoubleSpinBox;

namespace lf::ui {

class ActionRegistry;
class AutosaveManager;
class ProxyController;

// Application settings. Every control changes real behaviour; settings that
// need a restart say so. Applied with OK / Apply; Cancel leaves them alone.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(ActionRegistry& actions, AutosaveManager& autosave, ProxyController& proxies, QWidget* parent = nullptr);

    // Default length for new transitions (seconds), read by the transitions browser.
    static double defaultTransitionSeconds();
    // Whether the preview may use the GPU decoder (read at startup).
    static bool hardwareDecodeAllowed();

signals:
    void restartNeeded();

private:
    QWidget* generalPage();
    QWidget* previewPage();
    QWidget* editingPage();
    QWidget* exportPage();
    QWidget* keyboardPage(ActionRegistry& actions);
    void load();
    void apply();

    AutosaveManager& m_autosave;
    ProxyController& m_proxies;
    QListWidget* m_nav;
    QStackedWidget* m_pages;
    // General
    QLineEdit* m_dataRoot;
    QLabel* m_dataWarning;
    QLineEdit* m_projectsDir;
    QLineEdit* m_proxiesDir;
    QSpinBox* m_autosaveMinutes;
    QCheckBox* m_backups;
    QSpinBox* m_backupKeep;
    // Preview
    QCheckBox* m_hwDecode;
    QCheckBox* m_useProxies;
    // Editing
    QDoubleSpinBox* m_transitionSeconds;
    QCheckBox* m_snapDefault;
    // Export
    QLineEdit* m_exportsDir;
};

}  // namespace lf::ui
