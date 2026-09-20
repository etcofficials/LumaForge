#include "ui/inspector/AudioSection.h"

#include "lumaforge/project/TimelineOps.h"
#include "ui/widgets/ParamSlider.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QToolButton>

#include <cmath>

namespace lf::ui {

using namespace lf::project;

namespace {
float dbToLinear(double db) { return db <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
double linearToDb(float lin) { return lin <= 0.001f ? -60.0 : 20.0 * std::log10(lin); }
}  // namespace

// ---------------------------------------------------------------- audio

AudioSection::AudioSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Audio"), QStringLiteral("audio"), ctx, parent) {
    auto* f = form();
    m_volume = new ParamSlider(-60, 12, 0, 1, this);
    m_volume->setSuffix(QStringLiteral(" dB"));
    f->addRow(tr("Volume"), m_volume);
    m_mute = new QCheckBox(tr("Mute"), this);
    m_mute->setFocusPolicy(Qt::NoFocus);
    f->addRow(QString(), m_mute);
    m_fadeIn = new ParamSlider(0, 30, 0, 2, this);
    m_fadeIn->setSliderRange(0, 5);
    m_fadeIn->setSuffix(QStringLiteral(" s"));
    f->addRow(tr("Fade in"), m_fadeIn);
    m_fadeOut = new ParamSlider(0, 30, 0, 2, this);
    m_fadeOut->setSliderRange(0, 5);
    m_fadeOut->setSuffix(QStringLiteral(" s"));
    f->addRow(tr("Fade out"), m_fadeOut);

    // Edits touch audio clips only, even when the linked video clip is selected too.
    connect(m_volume, &ParamSlider::valueEdited, this, [this](double db) {
        if (m_loading) return;
        const float lin = dbToLinear(db);
        m_ctx.editSelection(tr("Volume"), prop::Volume, [lin](Clip& c) { if (c.kind == ClipKind::Audio) c.audio.volume = lin; });
    });
    connect(m_volume, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    connect(m_mute, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(on ? tr("Mute Clip") : tr("Unmute Clip"), prop::Mute, [on](Clip& c) { if (c.kind == ClipKind::Audio) c.audio.muted = on; });
        m_ctx.endGesture();
    });
    auto bindFade = [this](ParamSlider* s, int propertyId, Duration AudioParams::*member, const QString& text) {
        connect(s, &ParamSlider::valueEdited, this, [this, propertyId, member, text](double secs) {
            if (m_loading) return;
            const Duration d = Duration::fromSeconds(secs);
            m_ctx.editSelection(text, propertyId, [d, member](Clip& c) {
                if (c.kind != ClipKind::Audio) return;
                c.audio.*member = Duration{std::min(d.ticks, c.duration.ticks)};   // never longer than the clip
            });
        });
        connect(s, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    };
    bindFade(m_fadeIn, prop::FadeIn, &AudioParams::fadeIn, tr("Fade In"));
    bindFade(m_fadeOut, prop::FadeOut, &AudioParams::fadeOut, tr("Fade Out"));
    auto* reset = addHeaderButton(tr("Reset"), tr("Unity gain, no fades, unmuted"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Audio"), prop::ResetGroup, [](Clip& c) { if (c.kind == ClipKind::Audio) c.audio = AudioParams{}; });
        m_ctx.endGesture();
    });
}

void AudioSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_volume->setValue(std::round(linearToDb(c.audio.volume) * 10.0) / 10.0);
    m_mute->setChecked(c.audio.muted);
    m_fadeIn->setValue(c.audio.fadeIn.toSeconds());
    m_fadeOut->setValue(c.audio.fadeOut.toSeconds());
    setSubtitle(c.audio.muted ? tr("muted") : QString());
}

// ---------------------------------------------------------------- speed

SpeedSection::SpeedSection(InspectorContext& ctx, QWidget* parent) : InspectorSection(tr("Speed"), QStringLiteral("speed"), ctx, parent) {
    auto* layout = static_cast<QVBoxLayout*>(content()->layout());
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(3);
    const int presets[] = {25, 50, 75, 100, 125, 150, 200};
    for (int pct : presets) {
        auto* b = new QToolButton(this);
        b->setText(QStringLiteral("%1%").arg(pct));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(22);
        b->setToolTip(tr("Play at %1% speed").arg(pct));
        b->setProperty("pct", pct);
        row->addWidget(b, 1);
        connect(b, &QToolButton::clicked, this, [this, pct] { apply(Rational{pct, 100}); });
        m_presets.push_back(b);
    }
    layout->addLayout(row);
    auto* f = form();
    m_custom = new ParamSlider(10, 800, 100, 0, this);
    m_custom->setSliderRange(10, 400);
    m_custom->setSuffix(QStringLiteral(" %"));
    f->addRow(tr("Custom"), m_custom);
    connect(m_custom, &ParamSlider::gestureFinished, this, [this] {
        if (m_loading) return;
        apply(Rational{static_cast<std::int64_t>(std::lround(m_custom->value())), 100});
    });
}

void SpeedSection::apply(Rational speed) {
    // Speed changes clip duration (and may overwrite neighbours): a full sequence edit.
    m_ctx.pushEdit(tr("Speed %1%").arg(std::llround(speed.toDouble() * 100)), [speed](Document& doc, const std::vector<ClipId>& ids) {
        return ops::setSpeed(doc, ids, speed);
    });
}

void SpeedSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    const int pct = static_cast<int>(std::lround(c.speed.toDouble() * 100));
    for (auto* b : m_presets) b->setChecked(b->property("pct").toInt() == pct);
    m_custom->setValue(pct);
    setSubtitle(pct == 100 ? QString() : QStringLiteral("%1%").arg(pct));
}

}  // namespace lf::ui
