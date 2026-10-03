#include "ui/panels/MarkersPanel.h"

#include "lumaforge/project/TimelineCommands.h"
#include "lumaforge/project/TimelineOps.h"
#include "ui/ActionRegistry.h"
#include "ui/Timecode.h"
#include "ui/timeline/TimelineView.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lf::ui {

using namespace lf::project;

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
enum Column { ColTime, ColName, ColCount };

QIcon swatch(int color) {
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(TimelineView::markerColor(color));
    p.drawRoundedRect(QRectF(1, 1, 10, 10), 2, 2);
    return QIcon(pm);
}
}  // namespace

MarkersPanel::MarkersPanel(Document& doc, ActionRegistry& actions, QWidget* parent) : QWidget(parent), m_doc(doc) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    auto* row = new QHBoxLayout();
    row->setSpacing(4);
    auto* add = new QPushButton(tr("Add at Playhead"), this);
    add->setFocusPolicy(Qt::NoFocus);
    add->setToolTip(actions.action(ActionId::MarkerAdd)->toolTip());
    m_delete = new QPushButton(tr("Delete"), this);
    m_delete->setObjectName(QStringLiteral("markersDelete"));
    m_delete->setFocusPolicy(Qt::NoFocus);
    row->addWidget(add, 1);
    row->addWidget(m_delete);
    layout->addLayout(row);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("markersList"));
    m_list->setColumnCount(ColCount);
    m_list->setHeaderLabels({tr("Time"), tr("Name")});
    m_list->setRootIsDecorated(false);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->header()->setSectionResizeMode(ColTime, QHeaderView::ResizeToContents);
    m_list->header()->setStretchLastSection(true);
    layout->addWidget(m_list, 1);

    m_empty = new QLabel(tr("No markers yet. Press M to add one at the playhead."), this);
    m_empty->setWordWrap(true);
    m_empty->setProperty("muted", true);
    layout->addWidget(m_empty);

    m_copy = new QPushButton(tr("Copy as YouTube Chapters"), this);
    m_copy->setObjectName(QStringLiteral("markersCopyChapters"));
    m_copy->setFocusPolicy(Qt::NoFocus);
    m_copy->setToolTip(tr("Copies \"0:00 Name\" lines for a video description. YouTube needs the first chapter at 0:00."));
    layout->addWidget(m_copy);

    connect(add, &QPushButton::clicked, actions.action(ActionId::MarkerAdd), &QAction::trigger);
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const auto ids = selectedIds();
        if (!ids.empty()) commit(ids.size() == 1 ? tr("Delete Marker") : tr("Delete Markers"), ops::removeMarkers(m_doc.project().sequence, ids));
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(QString::fromStdString(ops::chapterList(m_doc.project().sequence)));
    });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] { m_delete->setEnabled(!selectedIds().empty()); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (!item) return;
        if (column == ColName) return;   // name: edit in place
        const auto id = item->data(0, kIdRole).toULongLong();
        for (const auto& m : m_doc.project().sequence.markers) if (m.id == id) emit seekRequested(m.time);
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding || column != ColName) return;
        const auto id = item->data(0, kIdRole).toULongLong();
        for (const auto& m : m_doc.project().sequence.markers) {
            if (m.id != id) continue;
            const QString name = item->text(ColName).trimmed();
            if (name.isEmpty() || name.toStdString() == m.name) { rebuild(); return; }
            Marker n = m;
            n.name = name.toStdString();
            commit(tr("Rename Marker"), ops::updateMarker(m_doc.project().sequence, n));
            return;
        }
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const auto ids = selectedIds();
        if (ids.empty()) return;
        QMenu menu(this);
        auto* jump = menu.addAction(tr("Go to Marker"));
        jump->setEnabled(ids.size() == 1);
        connect(jump, &QAction::triggered, this, [this, id = ids.front()] {
            for (const auto& m : m_doc.project().sequence.markers) if (m.id == id) emit seekRequested(m.time);
        });
        QMenu* colors = menu.addMenu(tr("Colour"));
        const QStringList names{tr("Blue"), tr("Green"), tr("Yellow"), tr("Orange"), tr("Red"), tr("Purple")};
        for (int i = 0; i < TimelineView::markerColorCount(); ++i) {
            auto* a = colors->addAction(swatch(i), names.value(i));
            connect(a, &QAction::triggered, this, [this, ids, i] {
                SequenceEdit edit;
                auto list = m_doc.project().sequence.markers;
                for (auto& m : list) if (std::find(ids.begin(), ids.end(), m.id) != ids.end()) m.color = i;
                edit.markers = std::move(list);
                commit(tr("Marker Colour"), std::move(edit));
            });
        }
        menu.addSeparator();
        connect(menu.addAction(tr("Delete")), &QAction::triggered, m_delete, &QPushButton::click);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });

    m_listenerToken = m_doc.addListener([this](const Change& c) {
        if (c.kind == Change::Kind::MarksChanged || c.kind == Change::Kind::Reset || c.kind == Change::Kind::Settings) rebuild();
    });
    rebuild();
}

MarkersPanel::~MarkersPanel() { m_doc.removeListener(m_listenerToken); }

std::vector<std::uint64_t> MarkersPanel::selectedIds() const {
    std::vector<std::uint64_t> ids;
    for (auto* item : m_list->selectedItems()) ids.push_back(item->data(0, kIdRole).toULongLong());
    return ids;
}

void MarkersPanel::commit(const QString& text, SequenceEdit edit) {
    if (edit.empty()) return;
    m_doc.push(std::make_unique<SequenceEditCommand>(m_doc, text.toStdString(), std::move(edit)));
}

void MarkersPanel::rebuild() {
    m_rebuilding = true;
    const auto keep = selectedIds();
    m_list->clear();
    const Rational fps = m_doc.project().settings.frameRate;
    for (const auto& m : m_doc.project().sequence.markers) {
        auto* item = new QTreeWidgetItem(m_list, {formatTimecode(m.time, fps), QString::fromStdString(m.name)});
        item->setData(0, kIdRole, QVariant::fromValue<qulonglong>(m.id));
        item->setIcon(ColTime, swatch(m.color));
        item->setToolTip(ColName, m.note.empty() ? tr("Double-click the time to jump there; click a selected name to rename") : QString::fromStdString(m.note));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (std::find(keep.begin(), keep.end(), m.id) != keep.end()) item->setSelected(true);
    }
    const bool any = !m_doc.project().sequence.markers.empty();
    m_list->setVisible(any);
    m_empty->setVisible(!any);
    m_copy->setEnabled(any);
    m_delete->setEnabled(!selectedIds().empty());
    m_rebuilding = false;
}

}  // namespace lf::ui
