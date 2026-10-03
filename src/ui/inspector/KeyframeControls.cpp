#include "ui/inspector/KeyframeControls.h"

#include "ui/Theme.h"
#include "ui/widgets/ParamSlider.h"

#include <QActionGroup>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

namespace lf::ui {

using namespace lf::project;

// ---------------------------------------------------------------- button

KeyframeButton::KeyframeButton(QWidget* parent) : QToolButton(parent) {
    setAutoRaise(true);
    setFixedSize(18, 18);
    setFocusPolicy(Qt::NoFocus);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setState(State::Static);
}

void KeyframeButton::setState(State s) {
    m_state = s;
    switch (s) {
        case State::Static: setToolTip(tr("Add a keyframe at the playhead (starts animating this property)")); break;
        case State::Animated: setToolTip(tr("Add a keyframe at the playhead · right-click for more")); break;
        case State::OnKey: setToolTip(tr("Remove the keyframe at the playhead · right-click for interpolation")); break;
    }
    update();
}

void KeyframeButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& pal = Theme::palette();
    const QPointF c = QRectF(rect()).center();
    const double r = 4.5;
    QPainterPath diamond;
    diamond.moveTo(c.x(), c.y() - r);
    diamond.lineTo(c.x() + r, c.y());
    diamond.lineTo(c.x(), c.y() + r);
    diamond.lineTo(c.x() - r, c.y());
    diamond.closeSubpath();
    QColor color = m_state == State::Static ? pal.textMuted : pal.accent;
    if (!isEnabled()) color.setAlpha(80);
    else if (underMouse()) color = color.lighter(130);
    p.setPen(QPen(color, 1.4));
    p.setBrush(m_state == State::OnKey ? QBrush(color) : Qt::NoBrush);
    p.drawPath(diamond);
}

// ---------------------------------------------------------------- binding

AnimatedParam::AnimatedParam(ParamSlider* slider, InspectorContext& ctx, const bool& loading, std::vector<AnimProp> props, QString undoText,
                             int propertyId, ToUi toUi, FromUi fromUi)
    : QObject(slider), m_slider(slider), m_ctx(ctx), m_loading(loading), m_props(std::move(props)), m_text(std::move(undoText)),
      m_propertyId(propertyId), m_toUi(std::move(toUi)), m_fromUi(std::move(fromUi)) {
    m_button = new KeyframeButton(slider);
    slider->addLeadingWidget(m_button);

    connect(slider, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float model = m_fromUi(v);
        const TimePoint playhead = m_ctx.playhead();
        m_ctx.editSelection(m_text, m_propertyId, [this, model, playhead](Clip& c) {
            const TimePoint at = animationTime(c, playhead);
            bool anyStatic = false;
            for (AnimProp p : m_props) {
                anyStatic |= !isAnimated(c, p);
                setValueAt(c, p, at, model);
            }
            if (anyStatic && m_constraint) m_constraint(c);
        });
        // A new key may have appeared under the playhead.
        if (const Clip* c = primary()) m_button->setState(keyAt(*c, m_props.front(), animationTime(*c, m_ctx.playhead())) ? KeyframeButton::State::OnKey
                                                           : isAnimated(*c, m_props.front()) ? KeyframeButton::State::Animated
                                                                                             : KeyframeButton::State::Static);
    });
    connect(slider, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_button, &QToolButton::clicked, this, [this] { toggleKey(); });
    connect(m_button, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showMenu(m_button->mapToGlobal(pos)); });
}

const Clip* AnimatedParam::primary() const { return m_ctx.project().sequence.findClip(m_primary); }

void AnimatedParam::load(const Clip& c) {
    m_primary = c.id;
    const TimePoint at = animationTime(c, m_ctx.playhead());
    m_slider->setValue(m_toUi(valueAt(c, m_props.front(), at)));
    const bool inside = c.contains(m_ctx.playhead());
    m_button->setEnabled(inside);
    m_button->setState(keyAt(c, m_props.front(), at) ? KeyframeButton::State::OnKey
                       : isAnimated(c, m_props.front()) ? KeyframeButton::State::Animated
                                                        : KeyframeButton::State::Static);
    if (!inside) m_button->setToolTip(tr("Move the playhead over the clip to set keyframes"));
}

void AnimatedParam::toggleKey() {
    const Clip* c = primary();
    if (!c) return;
    const bool remove = keyAt(*c, m_props.front(), animationTime(*c, m_ctx.playhead())) != nullptr;
    const TimePoint playhead = m_ctx.playhead();
    m_ctx.editSelection(remove ? tr("Remove Keyframe") : tr("Add Keyframe"), prop::Keyframe, [this, remove, playhead](Clip& clip) {
        if (!clip.contains(playhead)) return;
        const TimePoint at = animationTime(clip, playhead);
        for (AnimProp p : m_props) {
            if (remove) removeKey(clip, p, at);
            else setKey(clip, p, at, valueAt(clip, p, at));
        }
    });
    m_ctx.endGesture();
}

void AnimatedParam::showMenu(const QPoint& globalPos) {
    const Clip* c = primary();
    if (!c) return;
    const TimePoint at = animationTime(*c, m_ctx.playhead());
    const Keyframe* key = keyAt(*c, m_props.front(), at);
    QMenu menu;
    auto* toggle = menu.addAction(key ? tr("Remove Keyframe") : tr("Add Keyframe"));
    toggle->setEnabled(c->contains(m_ctx.playhead()));
    connect(toggle, &QAction::triggered, this, [this] { toggleKey(); });
    menu.addSeparator();
    auto* group = new QActionGroup(&menu);
    const std::pair<Interp, QString> interps[] = {{Interp::Linear, tr("Linear")}, {Interp::Smooth, tr("Smooth (ease in/out)")}, {Interp::Hold, tr("Hold")}};
    for (const auto& [interp, label] : interps) {
        auto* a = menu.addAction(label);
        a->setCheckable(true);
        a->setEnabled(key != nullptr);
        a->setChecked(key && key->interp == interp);
        group->addAction(a);
        const Interp chosen = interp;
        const TimePoint playhead = m_ctx.playhead();
        connect(a, &QAction::triggered, this, [this, chosen, playhead] {
            m_ctx.editSelection(tr("Keyframe Interpolation"), prop::Keyframe, [this, chosen, playhead](Clip& clip) {
                const TimePoint t = animationTime(clip, playhead);
                for (AnimProp p : m_props) if (const Keyframe* k = keyAt(clip, p, t)) setKey(clip, p, t, k->value, chosen);
            });
            m_ctx.endGesture();
        });
    }
    menu.addSeparator();
    // Navigation between this property's keys (sequence time, within the clip).
    std::vector<TimePoint> times;
    if (const KeyTrack* t = findKeyTrack(*c, m_props.front())) for (const auto& k : t->keys) times.push_back(c->sequenceTimeAt(k.time));
    const TimePoint now = m_ctx.playhead();
    std::optional<TimePoint> prev, next;
    for (TimePoint t : times) {
        if (t < now && t >= c->start) prev = t;
        if (t > now && t < c->end() && !next) next = t;
    }
    auto* prevA = menu.addAction(tr("Previous Keyframe"));
    prevA->setEnabled(prev.has_value());
    if (prev) connect(prevA, &QAction::triggered, this, [this, t = *prev] { m_ctx.seekTo(t); });
    auto* nextA = menu.addAction(tr("Next Keyframe"));
    nextA->setEnabled(next.has_value());
    if (next) connect(nextA, &QAction::triggered, this, [this, t = *next] { m_ctx.seekTo(t); });
    menu.addSeparator();
    auto* clear = menu.addAction(tr("Clear Animation"));
    clear->setEnabled(isAnimated(*c, m_props.front()));
    connect(clear, &QAction::triggered, this, [this] {
        const TimePoint playhead = m_ctx.playhead();
        m_ctx.editSelection(tr("Clear Animation"), prop::Keyframe, [this, playhead](Clip& clip) {
            for (AnimProp p : m_props) clearAnimation(clip, p, animationTime(clip, playhead));
        });
        m_ctx.endGesture();
    });
    menu.exec(globalPos);
}

}  // namespace lf::ui
