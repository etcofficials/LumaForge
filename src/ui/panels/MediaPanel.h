#pragma once

#include "lumaforge/project/Document.h"

#include <QWidget>

class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStackedWidget;
class QToolButton;
class QTreeView;

namespace lf::ui {

class ActionRegistry;
class EmptyState;
class ImportController;
class MediaModel;
class PanelHeader;
class ThumbnailService;

// The media bin: grid or list view over the document's media, with search,
// sorting, drag-out to the timeline, drop-in from Explorer and a context menu.
class MediaPanel : public QWidget {
    Q_OBJECT
public:
    MediaPanel(project::Document& doc, ActionRegistry& actions, ImportController& importer, QWidget* parent = nullptr);
    ~MediaPanel() override;

    [[nodiscard]] QList<project::MediaId> selectedMedia() const;
    // Cached thumbnail (null until generated); requests generation on a miss.
    [[nodiscard]] QImage thumbnail(project::MediaId id) const;

signals:
    void thumbnailsChanged();
    void openRequested(lf::project::MediaId id);          // double-click: source preview
    void addToTimelineRequested(const QList<lf::project::MediaId>& ids);
    void selectionChanged(const QList<lf::project::MediaId>& ids);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setViewMode(bool grid);
    void updateEmptyState();
    void showContextMenu(const QPoint& globalPos);
    void removeSelected();
    void relinkMedia(project::MediaId id);
    void revealInExplorer(project::MediaId id);
    void showProperties(project::MediaId id);
    void selectMedia(const QList<project::MediaId>& ids);

    project::Document& m_doc;
    ActionRegistry& m_actions;
    ImportController& m_importer;
    MediaModel* m_model;
    QSortFilterProxyModel* m_proxy;
    ThumbnailService* m_thumbnails;

    PanelHeader* m_header;
    QWidget* m_toolRow;
    QLineEdit* m_search;
    QToolButton* m_gridButton;
    QToolButton* m_listButton;
    QToolButton* m_sortButton;
    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QListView* m_grid;
    QTreeView* m_list;
    bool m_dropHover = false;
    int m_listenerToken = 0;
};

}  // namespace lf::ui
