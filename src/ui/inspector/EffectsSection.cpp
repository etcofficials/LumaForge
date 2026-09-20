#include "ui/inspector/EffectsSection.h"

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/ParamSlider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

using namespace lf::project;

namespace {
QToolButton* smallButton(QWidget* parent, const QString& text, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setText(text);
    b->setToolTip(tip);
    b->setFixedSize(20, 20);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
}  // namespace

EffectsSection::EffectsSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Effects"), QStringLiteral("effects"), ctx, parent) {
    auto* layout = static_cast<QVBoxLayout*>(content()->layout());
    m_rows = new QVBoxLayout();
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(4);
    layout->addLayout(m_rows);

    auto* addRow = new QWidget(this);
    auto* addLayout = new QHBoxLayout(addRow);
    addLayout->setContentsMargins(0, 4, 0, 0);
    addLayout->setSpacing(6);
    m_add = new QComboBox(addRow);
    m_add->setFocusPolicy(Qt::NoFocus);
    m_add->addItem(tr("Add effect…"));
    for (int i = 0; i < static_cast<int>(EffectType::Count); ++i) m_add->addItem(QString::fromUtf8(effectInfo(static_cast<EffectType>(i)).name), i);
    addLayout->addWidget(m_add, 1);
    layout->addWidget(addRow);

    connect(m_add, &QComboBox::activated, this, [this](int index) {
        if (index <= 0) return;
        const auto type = static_cast<EffectType>(m_add->itemData(index).toInt());
        m_add->setCurrentIndex(0);
        editEffects(tr("Add %1").arg(QString::fromUtf8(effectInfo(type).name)), prop::ResetGroup, [type](std::vector<Effect>& fx) { fx.push_back(makeEffect(type)); });
    });
    auto* clear = addHeaderButton(tr("Clear"), tr("Remove all effects"));
    connect(clear, &QToolButton::clicked, this, [this] { editEffects(tr("Clear Effects"), prop::ResetGroup, [](std::vector<Effect>& fx) { fx.clear(); }); });
}

void EffectsSection::editEffects(const QString& text, int propertyId, const std::function<void(std::vector<Effect>&)>& fn) {
    m_ctx.editSelection(text, propertyId, [&fn](Clip& c) { fn(c.effects); });
    m_ctx.endGesture();
}

void EffectsSection::rebuildRows(const std::vector<Effect>& effects) {
    for (auto& r : m_rowWidgets) { m_rows->removeWidget(r.widget); r.widget->deleteLater(); }
    m_rowWidgets.clear();
    m_shape.clear();
    const auto& pal = Theme::palette();
    for (std::size_t i = 0; i < effects.size(); ++i) {
        const Effect& e = effects[i];
        const EffectInfo& info = effectInfo(e.type);
        auto* w = new QWidget(this);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(4);
        auto* enabled = new QCheckBox(QString::fromUtf8(info.name), w);
        enabled->setChecked(e.enabled);
        enabled->setFocusPolicy(Qt::NoFocus);
        head->addWidget(enabled, 1);
        auto* up = smallButton(w, QStringLiteral("▲"), tr("Move up"));
        auto* down = smallButton(w, QStringLiteral("▼"), tr("Move down"));
        auto* remove = smallButton(w, QStringLiteral("✕"), tr("Remove effect"));
        up->setEnabled(i > 0);
        down->setEnabled(i + 1 < effects.size());
        head->addWidget(up);
        head->addWidget(down);
        head->addWidget(remove);
        v->addLayout(head);
        auto* amount = new ParamSlider(info.min, info.max, info.def, info.decimals, w);
        if (*info.suffix) amount->setSuffix(QString::fromUtf8(info.suffix));
        amount->setValue(e.amount);
        amount->setEnabled(e.enabled);
        v->addWidget(amount);
        auto* line = new QFrame(w);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color: %1;").arg(pal.border.name()));
        v->addWidget(line);
        m_rows->addWidget(w);
        m_rowWidgets.push_back({w, enabled, amount, e.type});
        m_shape.push_back(e.type);

        const int index = static_cast<int>(i);
        const int propBase = prop::EffectGrayscale + index * 10;   // distinct merge id per row
        connect(enabled, &QCheckBox::toggled, this, [this, index, amount](bool on) {
            if (m_loading) return;
            amount->setEnabled(on);
            editEffects(on ? tr("Enable Effect") : tr("Disable Effect"), prop::ResetGroup, [index, on](std::vector<Effect>& fx) { if (index < static_cast<int>(fx.size())) fx[static_cast<std::size_t>(index)].enabled = on; });
        });
        connect(amount, &ParamSlider::valueEdited, this, [this, index, propBase](double vv) {
            if (m_loading) return;
            const float a = static_cast<float>(vv);
            m_ctx.editSelection(tr("Effect Amount"), propBase, [index, a](Clip& c) { if (index < static_cast<int>(c.effects.size())) c.effects[static_cast<std::size_t>(index)].amount = a; });
        });
        connect(amount, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
        connect(up, &QToolButton::clicked, this, [this, index] {
            editEffects(tr("Reorder Effects"), prop::ResetGroup, [index](std::vector<Effect>& fx) { if (index > 0 && index < static_cast<int>(fx.size())) std::swap(fx[static_cast<std::size_t>(index)], fx[static_cast<std::size_t>(index) - 1]); });
        });
        connect(down, &QToolButton::clicked, this, [this, index] {
            editEffects(tr("Reorder Effects"), prop::ResetGroup, [index](std::vector<Effect>& fx) { if (index + 1 < static_cast<int>(fx.size())) std::swap(fx[static_cast<std::size_t>(index)], fx[static_cast<std::size_t>(index) + 1]); });
        });
        connect(remove, &QToolButton::clicked, this, [this, index] {
            editEffects(tr("Remove Effect"), prop::ResetGroup, [index](std::vector<Effect>& fx) { if (index < static_cast<int>(fx.size())) fx.erase(fx.begin() + index); });
        });
    }
}

void EffectsSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    std::vector<EffectType> shape;
    for (const Effect& e : c.effects) shape.push_back(e.type);
    if (shape != m_shape) rebuildRows(c.effects);
    for (std::size_t i = 0; i < m_rowWidgets.size() && i < c.effects.size(); ++i) {
        m_rowWidgets[i].enabled->setChecked(c.effects[i].enabled);
        m_rowWidgets[i].amount->setValue(c.effects[i].amount);
        m_rowWidgets[i].amount->setEnabled(c.effects[i].enabled);
    }
    setSubtitle(c.effects.empty() ? QString() : tr("%n effect(s)", nullptr, static_cast<int>(c.effects.size())));
}

}  // namespace lf::ui
