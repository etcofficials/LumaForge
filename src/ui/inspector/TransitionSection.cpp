#include "ui/inspector/TransitionSection.h"

#include "lumaforge/project/TimelineOps.h"
#include "ui/widgets/ParamSlider.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>

namespace lf::ui {

using namespace lf::project;

TransitionSection::TransitionSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Transitions"), QStringLiteral("transitions"), ctx, parent) {
    buildEdge(m_start, true, tr("At start"));
    buildEdge(m_end, false, tr("At end"));
}

void TransitionSection::buildEdge(Edge& edge, bool atStart, const QString& title) {
    auto* f = form();
    auto* heading = new QLabel(title, this);
    heading->setProperty("sectionHeading", true);
    f->addRow(heading);
    edge.type = new QComboBox(this);
    edge.type->setObjectName(atStart ? QStringLiteral("transitionStartType") : QStringLiteral("transitionEndType"));
    edge.type->setFocusPolicy(Qt::NoFocus);
    edge.type->addItem(tr("None (cut)"), -1);
    for (int i = 0; i < static_cast<int>(TransitionType::Count); ++i) edge.type->addItem(tr(transitionInfo(static_cast<TransitionType>(i)).name), i);
    f->addRow(tr("Type"), edge.type);
    edge.duration = new ParamSlider(0.1, 10.0, 1.0, 2, this);
    edge.duration->setObjectName(atStart ? QStringLiteral("transitionStartDuration") : QStringLiteral("transitionEndDuration"));
    edge.duration->setSliderRange(0.1, 4.0);
    edge.duration->setSuffix(tr(" s"));
    f->addRow(tr("Duration"), edge.duration);
    edge.note = new QLabel(this);
    edge.note->setProperty("muted", true);
    edge.note->setWordWrap(true);
    f->addRow(QString(), edge.note);
    connect(edge.type, &QComboBox::activated, this, [this, atStart] { if (!m_loading) apply(atStart); });
    // One undo step per finished change (drag release / typed value), not per slider tick.
    connect(edge.duration, &ParamSlider::gestureFinished, this, [this, atStart] { if (!m_loading) apply(atStart); });
}

void TransitionSection::apply(bool atStart) {
    const Edge& edge = atStart ? m_start : m_end;
    const int typeIndex = edge.type->currentData().toInt();
    std::optional<Transition> t;
    if (typeIndex >= 0) t = Transition{static_cast<TransitionType>(typeIndex), Duration::fromSeconds(edge.duration->value())};
    const QString text = t ? tr("Set Transition") : tr("Remove Transition");
    m_ctx.pushEdit(text, [atStart, t](Document& doc, const std::vector<ClipId>& ids) {
        const auto& seq = doc.project().sequence;
        SequenceEdit merged;
        for (auto id : ids) {
            for (auto& m : ops::setTransition(seq, id, atStart, t).modified) {
                bool dup = false;
                for (const auto& e : merged.modified) dup |= e.id == m.id;
                if (!dup) merged.modify(std::move(m));
            }
        }
        return merged;
    });
    m_ctx.endGesture();
}

void TransitionSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    const auto& seq = m_ctx.project().sequence;
    const auto onTrack = seq.clipsOnTrack(c.track);
    for (bool atStart : {true, false}) {
        Edge& edge = atStart ? m_start : m_end;
        const auto t = ops::transitionAt(seq, c.id, atStart);
        edge.type->setCurrentIndex(t ? static_cast<int>(t->type) + 1 : 0);
        edge.duration->setValue(t ? t->duration.toSeconds() : kDefaultTransitionDuration.toSeconds());
        edge.duration->setEnabled(t.has_value());
        const Clip* neighbour = touchingNeighbour(onTrack, c, atStart);
        const Duration limit = atStart ? maxTransitionDuration(onTrack, c, true) : (neighbour ? maxTransitionDuration(onTrack, *neighbour, true) : c.duration);
        QString note = neighbour ? (atStart ? tr("Across the cut from \"%1\"").arg(QString::fromStdString(neighbour->name))
                                            : tr("Across the cut into \"%1\"").arg(QString::fromStdString(neighbour->name)))
                                 : (atStart ? tr("From what lies underneath (lower tracks or black)") : tr("To what lies underneath (lower tracks or black)"));
        note += tr(" · up to %1 s").arg(limit.toSeconds(), 0, 'f', 2);
        edge.note->setText(note);
    }
}

}  // namespace lf::ui
