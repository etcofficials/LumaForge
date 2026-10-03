#include "ui/panels/EffectsBrowserPanel.h"

#include "ui/DragMime.h"
#include "ui/Icons.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
constexpr int kTypeRole = Qt::UserRole + 1;

// Tree whose effect rows drag out as the effect id.
class EffectTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(mime::kEffect)}; }
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.isEmpty() || !items.front()->data(0, kTypeRole).isValid()) return nullptr;
        const auto type = static_cast<EffectType>(items.front()->data(0, kTypeRole).toInt());
        auto* data = new QMimeData();
        data->setData(QString::fromLatin1(mime::kEffect), QByteArray(effectInfo(type).id));
        return data;
    }
};
}  // namespace

EffectsBrowserPanel::EffectsBrowserPanel(QWidget* parent) : QWidget(parent) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search effects"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(24);
    layout->addWidget(m_search);

    m_tree = new EffectTree(this);
    m_tree->setObjectName(QStringLiteral("effectsTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setFocusPolicy(Qt::ClickFocus);
    layout->addWidget(m_tree, 1);

    for (auto category : {EffectCategory::Color, EffectCategory::BlurSharpen, EffectCategory::Stylize}) {
        auto* group = new QTreeWidgetItem(m_tree, {tr(effectCategoryName(category))});
        group->setFlags(Qt::ItemIsEnabled);
        group->setIcon(0, Icons::icon(QStringLiteral("folder")));
        for (int i = 0; i < static_cast<int>(EffectType::Count); ++i) {
            const auto type = static_cast<EffectType>(i);
            const EffectInfo& info = effectInfo(type);
            if (info.category != category) continue;
            auto* item = new QTreeWidgetItem(group, {tr(info.name)});
            item->setData(0, kTypeRole, i);
            item->setIcon(0, Icons::icon(QStringLiteral("effects")));
            item->setToolTip(0, tr("<b>%1</b><br>%2<br><i>Drag onto a clip, or double-click to apply to the selected clips.</i>")
                                    .arg(tr(info.name), tr(info.description)));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        group->setExpanded(true);
    }

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setProperty("muted", true);
    layout->addWidget(m_hint);
    m_apply = new QPushButton(tr("Apply to Selected Clips"), this);
    m_apply->setObjectName(QStringLiteral("effectsApply"));
    m_apply->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_apply);

    connect(m_search, &QLineEdit::textChanged, this, &EffectsBrowserPanel::filter);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &EffectsBrowserPanel::updateApply);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item && item->data(0, kTypeRole).isValid() && m_canApply) emit applyRequested(static_cast<EffectType>(item->data(0, kTypeRole).toInt()));
    });
    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (currentEffect() != EffectType::Count) emit applyRequested(currentEffect());
    });
    updateApply();
}

EffectType EffectsBrowserPanel::currentEffect() const {
    const auto* item = m_tree->currentItem();
    if (!item || !item->isSelected() || !item->data(0, kTypeRole).isValid()) return EffectType::Count;
    return static_cast<EffectType>(item->data(0, kTypeRole).toInt());
}

void EffectsBrowserPanel::setCanApply(bool can) {
    m_canApply = can;
    updateApply();
}

void EffectsBrowserPanel::updateApply() {
    const bool chosen = currentEffect() != EffectType::Count;
    m_apply->setEnabled(m_canApply && chosen);
    if (!m_canApply) m_hint->setText(tr("Select clips on the timeline, or drag an effect onto a clip."));
    else if (!chosen) m_hint->setText(tr("Choose an effect to apply to the selected clips."));
    else m_hint->setText(tr(effectInfo(currentEffect()).description));
}

void EffectsBrowserPanel::filter(const QString& text) {
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        auto* group = m_tree->topLevelItem(g);
        int visible = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            auto* item = group->child(i);
            const auto type = static_cast<EffectType>(item->data(0, kTypeRole).toInt());
            const bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive) ||
                               tr(effectInfo(type).description).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
            visible += match ? 1 : 0;
        }
        group->setHidden(visible == 0);
        if (!text.isEmpty()) group->setExpanded(true);
    }
}

}  // namespace lf::ui
