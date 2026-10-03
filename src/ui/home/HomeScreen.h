#pragma once

#include <QDateTime>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QFrame;

namespace lf::ui {

// Start page shown when no project is open: New / Open / Import, the recent
// projects list, and (when a previous session ended unexpectedly) a banner
// offering the recovered autosave. Plain widgets, no web view, no network.
class HomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit HomeScreen(QWidget* parent = nullptr);

    void setRecentProjects(const QStringList& paths);
    // Shows/hides the recovery banner. `projectName` is what the autosave belongs to.
    void setRecovery(const QString& autosavePath, const QString& projectName, const QDateTime& when);
    void clearRecovery();

signals:
    void newProjectRequested();
    void openProjectRequested();
    void openRecentRequested(const QString& path);
    void importMediaRequested();
    void openRecoveryRequested(const QString& autosavePath);
    void discardRecoveryRequested(const QString& autosavePath);
    void clearRecentRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void activate(QListWidgetItem* item);

    QListWidget* m_recent;
    QLabel* m_recentEmpty;
    QFrame* m_recoveryBanner;
    QLabel* m_recoveryText;
    QPushButton* m_clearRecent;
    QString m_recoveryPath;
};

}  // namespace lf::ui
