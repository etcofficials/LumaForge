#include "ui/panels/TransitionsBrowserPanel.h"

#include "ui/DragMime.h"
#include "ui/Icons.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lf::ui {

using project::TransitionType;

namespace {
constexpr int kTypeRole = Qt::UserRole + 1;

class TransitionTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    QDoubleSpinBox* duration = nullptr;

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(mime::kTransition)}; }
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.isEmpty() || !items.front()->data(0, kTypeRole).isValid()) return nullptr;
        const auto type = static_cast<TransitionType>(items.front()->data(0, kTypeRole).toInt());
        // Payload: "<id>;<duration ticks>"
        const Ticks d = Duration::fromSeconds(duration ? duration->value() : 1.0).ticks;
        auto* data = new QMimeData();
        data->setData(QString::fromLatin1(mime::kTransition), QByteArray(project::transitionInfo(type).id) + ';' + QByteArray::number(static_cast<qlonglong>(d)));
        return data;
    }
};
}  // namespace

TransitionsBrowserPanel::TransitionsBrowserPanel(QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search transitions"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(24);
    layout->addWidget(m_search);

    auto* tree = new TransitionTree(this);
    m_tree = tree;
    m_tree->setObjectName(QStringLiteral("transitionsTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setFocusPolicy(Qt::ClickFocus);
    layout->addWidget(m_tree, 1);

    QStringList categories;
    for (int i = 0; i < static_cast<int>(TransitionType::Count); ++i) {
        const QString cat = QString::fromLatin1(project::transitionInfo(static_cast<TransitionType>(i)).category);
        if (!categories.contains(cat)) categories << cat;
    }
    for (const QString& cat : categories) {
        auto* group = new QTreeWidgetItem(m_tree, {tr(qPrintable(cat))});
        group->setFlags(Qt::ItemIsEnabled);
        group->setIcon(0, Icons::icon(QStringLiteral("folder")));
        for (int i = 0; i < static_cast<int>(TransitionType::Count); ++i) {
            const auto& info = project::transitionInfo(static_cast<TransitionType>(i));
            if (cat != QLatin1String(info.category)) continue;
            auto* item = new QTreeWidgetItem(group, {tr(info.name)});
            item->setData(0, kTypeRole, i);
            item->setIcon(0, Icons::icon(QStringLiteral("transition")));
            item->setToolTip(0, tr("<b>%1</b><br>%2<br><i>Drag onto a clip edge, or select clips and press Add.</i>").arg(tr(info.name), tr(info.description)));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        group->setExpanded(true);
    }

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    m_duration = new QDoubleSpinBox(this);
    m_duration->setObjectName(QStringLiteral("transitionDuration"));
    m_duration->setRange(0.1, 10.0);
    m_duration->setSingleStep(0.25);
    m_duration->setDecimals(2);
    m_duration->setValue(1.0);
    m_duration->setSuffix(tr(" s"));
    form->addRow(tr("Duration"), m_duration);
    layout->addLayout(form);
    tree->duration = m_duration;

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setProperty("muted", true);
    layout->addWidget(m_hint);
    m_apply = new QPushButton(tr("Add to Selected Clips"), this);
    m_apply->setObjectName(QStringLiteral("transitionsApply"));
    m_apply->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_apply);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
            auto* group = m_tree->topLevelItem(g);
            int visible = 0;
            for (int i = 0; i < group->childCount(); ++i) {
                const bool match = text.isEmpty() || group->child(i)->text(0).contains(text, Qt::CaseInsensitive);
                group->child(i)->setHidden(!match);
                visible += match;
            }
            group->setHidden(visible == 0);
        }
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &TransitionsBrowserPanel::updateApply);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item && item->data(0, kTypeRole).isValid() && m_canApply) emit applyRequested(static_cast<TransitionType>(item->data(0, kTypeRole).toInt()), duration());
    });
    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (currentType() != TransitionType::Count) emit applyRequested(currentType(), duration());
    });
    updateApply();
}

TransitionType TransitionsBrowserPanel::currentType() const {
    const auto* item = m_tree->currentItem();
    if (!item || !item->isSelected() || !item->data(0, kTypeRole).isValid()) return TransitionType::Count;
    return static_cast<TransitionType>(item->data(0, kTypeRole).toInt());
}

Duration TransitionsBrowserPanel::duration() const { return Duration::fromSeconds(m_duration->value()); }

void TransitionsBrowserPanel::setCanApply(bool can) {
    m_canApply = can;
    updateApply();
}

void TransitionsBrowserPanel::updateApply() {
    const bool chosen = currentType() != TransitionType::Count;
    m_apply->setEnabled(m_canApply && chosen);
    if (!m_canApply) m_hint->setText(tr("Select clips on the timeline (the edge nearest the playhead gets the transition), or drag one onto a clip edge."));
    else if (!chosen) m_hint->setText(tr("Choose a transition."));
    else m_hint->setText(tr(project::transitionInfo(currentType()).description));
}

}  // namespace lf::ui
