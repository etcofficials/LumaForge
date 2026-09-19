#include "ui/panels/MediaPanel.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/project/MediaCommands.h"
#include "ui/ActionRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/dialogs/MediaPropertiesDialog.h"
#include "ui/media/ImportController.h"
#include "ui/media/MediaModel.h"
#include "ui/media/MediaTileDelegate.h"
#include "ui/media/ThumbnailService.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/PanelHeader.h"

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
constexpr const char* kViewModeKey = "media/gridView";
constexpr const char* kSortKey = "media/sortColumn";
constexpr const char* kSortOrderKey = "media/sortOrder";

QToolButton* smallButton(QWidget* parent, const QString& icon, const QString& tip, bool checkable) {
    auto* b = new QToolButton(parent);
    b->setIcon(Icons::icon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCheckable(checkable);
    b->setFixedSize(24, 24);
    b->setIconSize(QSize(16, 16));
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
}  // namespace

MediaPanel::MediaPanel(project::Document& doc, ActionRegistry& actions, ImportController& importer, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_actions(actions), m_importer(importer) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(240);
    setAcceptDrops(true);

    m_model = new MediaModel(m_doc, this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(MediaModel::SortKeyRole);
    m_proxy->setFilterRole(Qt::DisplayRole);
    m_proxy->setFilterKeyColumn(MediaModel::Name);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setDynamicSortFilter(true);

    m_thumbnails = new ThumbnailService(this);
    connect(m_model, &MediaModel::thumbnailNeeded, m_thumbnails, &ThumbnailService::request);
    connect(m_thumbnails, &ThumbnailService::ready, m_model, &MediaModel::setThumbnail);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new PanelHeader(tr("Media"), this);
    m_header->addAction(actions.action(ActionId::MediaImport));
    layout->addWidget(m_header);

    // Tool row: search + view toggles + sort
    m_toolRow = new QWidget(this);
    auto* row = new QHBoxLayout(m_toolRow);
    row->setContentsMargins(8, 6, 8, 6);
    row->setSpacing(4);
    m_search = new QLineEdit(m_toolRow);
    m_search->setPlaceholderText(tr("Search media"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(24);
    row->addWidget(m_search, 1);
    m_gridButton = smallButton(m_toolRow, QStringLiteral("view-grid"), tr("Thumbnail view"), true);
    m_listButton = smallButton(m_toolRow, QStringLiteral("view-list"), tr("List view"), true);
    m_sortButton = smallButton(m_toolRow, QStringLiteral("sort"), tr("Sort"), false);
    m_sortButton->setPopupMode(QToolButton::InstantPopup);
    row->addWidget(m_gridButton);
    row->addWidget(m_listButton);
    row->addWidget(m_sortButton);
    layout->addWidget(m_toolRow);

    auto* sortMenu = new QMenu(m_sortButton);
    auto* sortGroup = new QActionGroup(sortMenu);
    const struct { MediaModel::Column col; const char* text; } sorts[] = {
        {MediaModel::Name, "Name"}, {MediaModel::Type, "Type"}, {MediaModel::Duration, "Duration"}, {MediaModel::Size, "File size"}};
    QSettings settings;
    const int savedCol = settings.value(QString::fromLatin1(kSortKey), int(MediaModel::Name)).toInt();
    const auto savedOrder = static_cast<Qt::SortOrder>(settings.value(QString::fromLatin1(kSortOrderKey), int(Qt::AscendingOrder)).toInt());
    for (const auto& s : sorts) {
        auto* a = sortMenu->addAction(tr(s.text));
        a->setCheckable(true);
        a->setChecked(s.col == savedCol);
        sortGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, col = s.col] {
            m_proxy->sort(col, m_proxy->sortOrder());
            QSettings().setValue(QString::fromLatin1(kSortKey), int(col));
        });
    }
    sortMenu->addSeparator();
    auto* desc = sortMenu->addAction(tr("Descending"));
    desc->setCheckable(true);
    desc->setChecked(savedOrder == Qt::DescendingOrder);
    connect(desc, &QAction::toggled, this, [this](bool on) {
        m_proxy->sort(m_proxy->sortColumn() < 0 ? 0 : m_proxy->sortColumn(), on ? Qt::DescendingOrder : Qt::AscendingOrder);
        QSettings().setValue(QString::fromLatin1(kSortOrderKey), int(on ? Qt::DescendingOrder : Qt::AscendingOrder));
    });
    m_sortButton->setMenu(sortMenu);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack, 1);

    m_empty = new EmptyState(QStringLiteral("media-empty"), tr("No media yet"),
                             tr("Drop video, audio or image files here,\nor click Import to browse."), this);
    auto* importButton = m_empty->setAction(tr("Import Media…"));
    connect(importButton, &QPushButton::clicked, actions.action(ActionId::MediaImport), &QAction::trigger);
    m_stack->addWidget(m_empty);

    m_grid = new QListView(this);
    m_grid->setModel(m_proxy);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);
    m_grid->setSpacing(4);
    m_grid->setWordWrap(false);
    m_grid->setItemDelegate(new MediaTileDelegate(m_grid));
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setDragEnabled(true);
    m_grid->setDragDropMode(QAbstractItemView::DragOnly);
    m_grid->setMouseTracking(true);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->setFrameShape(QFrame::NoFrame);
    m_grid->viewport()->setAcceptDrops(false);
    m_stack->addWidget(m_grid);

    m_list = new QTreeView(this);
    m_list->setModel(m_proxy);
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(true);
    m_list->setUniformRowHeights(true);
    m_list->setSortingEnabled(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setMouseTracking(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegateForColumn(MediaModel::Name, new MediaNameDelegate(m_list));
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(MediaModel::Name, QHeaderView::Stretch);
    for (int c = MediaModel::Type; c < MediaModel::ColumnCount; ++c) m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_list->header()->setSectionsMovable(false);
    m_list->viewport()->setAcceptDrops(false);
    m_stack->addWidget(m_list);

    // Both views share one selection model so switching views keeps the selection.
    m_list->setSelectionModel(m_grid->selectionModel());

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) { m_proxy->setFilterFixedString(t); updateEmptyState(); });
    connect(m_gridButton, &QToolButton::clicked, this, [this] { setViewMode(true); });
    connect(m_listButton, &QToolButton::clicked, this, [this] { setViewMode(false); });
    for (auto* view : {static_cast<QAbstractItemView*>(m_grid), static_cast<QAbstractItemView*>(m_list)}) {
        connect(view, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& idx) {
            const auto id = m_model->idAt(m_proxy->mapToSource(idx));
            if (project::isValid(id)) emit openRequested(id);
        });
        connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint& pos) { showContextMenu(view->viewport()->mapToGlobal(pos)); });
    }
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { emit selectionChanged(selectedMedia()); });

    m_proxy->sort(savedCol, savedOrder);
    setViewMode(settings.value(QString::fromLatin1(kViewModeKey), true).toBool());
    m_listenerToken = m_doc.addListener([this](const project::Change&) { updateEmptyState(); });
    updateEmptyState();
}

MediaPanel::~MediaPanel() { m_doc.removeListener(m_listenerToken); }

QList<project::MediaId> MediaPanel::selectedMedia() const {
    QList<project::MediaId> ids;
    for (const QModelIndex& idx : m_grid->selectionModel()->selectedRows(0)) {
        const auto id = m_model->idAt(m_proxy->mapToSource(idx));
        if (project::isValid(id)) ids << id;
    }
    return ids;
}

void MediaPanel::setViewMode(bool grid) {
    m_gridButton->setChecked(grid);
    m_listButton->setChecked(!grid);
    QSettings().setValue(QString::fromLatin1(kViewModeKey), grid);
    updateEmptyState();
}

void MediaPanel::updateEmptyState() {
    const bool hasMedia = m_model->rowCount() > 0;
    m_toolRow->setVisible(hasMedia);
    if (!hasMedia) {
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    m_stack->setCurrentWidget(m_gridButton->isChecked() ? static_cast<QWidget*>(m_grid) : static_cast<QWidget*>(m_list));
}

void MediaPanel::selectMedia(const QList<project::MediaId>& ids) {
    auto* sel = m_grid->selectionModel();
    sel->clearSelection();
    for (auto id : ids) {
        const QModelIndex src = m_model->indexOf(id);
        if (src.isValid()) sel->select(m_proxy->mapFromSource(src), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

// ---------------------------------------------------------------- context menu / actions

void MediaPanel::showContextMenu(const QPoint& globalPos) {
    const auto ids = selectedMedia();
    QMenu menu(this);
    if (!ids.isEmpty()) {
        auto* add = menu.addAction(Icons::icon(QStringLiteral("add-to-timeline")), ids.size() == 1 ? tr("Add to Timeline") : tr("Add %1 Items to Timeline").arg(ids.size()));
        connect(add, &QAction::triggered, this, [this, ids] { emit addToTimelineRequested(ids); });
        if (ids.size() == 1) {
            auto* open = menu.addAction(tr("Open in Preview"));
            connect(open, &QAction::triggered, this, [this, id = ids.first()] { emit openRequested(id); });
        }
        menu.addSeparator();
        if (ids.size() == 1) {
            auto* reveal = menu.addAction(tr("Reveal in Explorer"));
            connect(reveal, &QAction::triggered, this, [this, id = ids.first()] { revealInExplorer(id); });
            auto* props = menu.addAction(tr("Properties…"));
            connect(props, &QAction::triggered, this, [this, id = ids.first()] { showProperties(id); });
            menu.addSeparator();
        }
        auto* remove = menu.addAction(Icons::icon(QStringLiteral("delete")), ids.size() == 1 ? tr("Remove from Project") : tr("Remove %1 Items from Project").arg(ids.size()));
        connect(remove, &QAction::triggered, this, [this] { removeSelected(); });
        menu.addSeparator();
    }
    menu.addAction(m_actions.action(ActionId::MediaImport));
    menu.exec(globalPos);
}

void MediaPanel::removeSelected() {
    const auto ids = selectedMedia();
    if (ids.isEmpty()) return;
    std::vector<project::MediaId> v(ids.begin(), ids.end());
    m_doc.push(std::make_unique<project::RemoveMediaCommand>(m_doc, std::move(v)));
}

void MediaPanel::revealInExplorer(project::MediaId id) {
    const auto* item = m_model->item(id);
    if (!item) return;
    const QString native = QDir::toNativeSeparators(QString::fromStdString(item->path));
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
}

void MediaPanel::showProperties(project::MediaId id) {
    const auto* item = m_model->item(id);
    if (!item) return;
    MediaPropertiesDialog dlg(*item, m_model->indexOf(id).data(MediaModel::ThumbnailRole).value<QImage>(), this);
    dlg.exec();
}

// ---------------------------------------------------------------- drag & drop (from Explorer)

void MediaPanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() && !e->mimeData()->hasFormat(QString::fromLatin1(MediaModel::kMimeType))) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        m_dropHover = true;
        update();
    } else {
        e->ignore();
    }
}

void MediaPanel::dragMoveEvent(QDragMoveEvent* e) { e->acceptProposedAction(); }

void MediaPanel::dragLeaveEvent(QDragLeaveEvent*) {
    m_dropHover = false;
    update();
}

void MediaPanel::dropEvent(QDropEvent* e) {
    m_dropHover = false;
    update();
    const QStringList paths = ImportController::collectMediaPaths(e->mimeData()->urls());
    if (paths.isEmpty()) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    m_importer.importFiles(paths, window());
}

void MediaPanel::paintEvent(QPaintEvent* e) {
    QWidget::paintEvent(e);
    if (m_dropHover) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Theme::palette().accent, 2, Qt::DashLine));
        p.setBrush(QColor(Theme::palette().accent.red(), Theme::palette().accent.green(), Theme::palette().accent.blue(), 24));
        p.drawRoundedRect(rect().adjusted(4, 4, -4, -4), 6, 6);
    }
}

}  // namespace lf::ui
