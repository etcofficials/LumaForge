#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <map>

class QDockWidget;
class QMainWindow;
class QTabBar;

namespace lf::ui {

enum class Workspace { Edit, Color, Audio, Text, Effects, Export };
[[nodiscard]] QString workspaceName(Workspace w);
[[nodiscard]] QString workspaceKey(Workspace w);

// Docks the workspaces arrange. Registered by MainWindow as panels exist.
enum class DockId { Browser, Inspector, Timeline, Mixer, Scopes, ExportSettings, ExportQueue, Performance };

// Owns the dock layout of the editor window. Each workspace is a named
// QMainWindow state: switching saves the layout the user left behind and
// restores (or builds, the first time / after Reset) the target layout.
// Workspaces whose panels do not exist yet are not offered.
class WorkspaceManager : public QObject {
    Q_OBJECT
public:
    WorkspaceManager(QMainWindow* editor, QTabBar* bar, QObject* parent = nullptr);

    void registerDock(DockId id, QDockWidget* dock);
    [[nodiscard]] QDockWidget* dock(DockId id) const;
    void setAvailable(Workspace w, bool available);
    [[nodiscard]] bool isAvailable(Workspace w) const;

    [[nodiscard]] Workspace current() const noexcept { return m_current; }
    void switchTo(Workspace w);
    void resetCurrent();
    // Restores the workspace that was active last session (call once docks exist).
    void restoreSession();
    void saveSession();

signals:
    void workspaceChanged(lf::ui::Workspace w);

private:
    void applyDefault(Workspace w);
    void saveState(Workspace w);
    bool restoreState(Workspace w);
    int tabIndex(Workspace w) const;

    QMainWindow* m_editor;
    QTabBar* m_bar;
    std::map<DockId, QDockWidget*> m_docks;
    QHash<int, bool> m_available;
    Workspace m_current = Workspace::Edit;
    bool m_switching = false;
};

}  // namespace lf::ui
