#pragma once

#include <QStringList>
#include <QWidget>

class QTabWidget;

namespace lf::ui {

// Left-hand browser of the editor: tabs for Media, Effects, Transitions,
// Titles, Markers and Project. Tabs are added by MainWindow as the panels
// exist; workspaces switch the visible tab by key.
class BrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit BrowserPanel(QWidget* parent = nullptr);

    void addPage(QWidget* page, const QString& title, const QString& key, const QString& toolTip = {});
    void showPage(const QString& key);
    [[nodiscard]] QString currentKey() const;

private:
    QTabWidget* m_tabs;
    QStringList m_keys;
};

}  // namespace lf::ui
