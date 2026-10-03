#include "ui/inspector/AudioSection.h"

#include "lumaforge/playback/AudioAnalysis.h"
#include "lumaforge/project/TimelineOps.h"
#include "ui/inspector/KeyframeControls.h"
#include "ui/widgets/ParamSlider.h"

#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QToolButton>

#include <QPointer>

#include <cmath>
#include <thread>

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
    m_pan = new ParamSlider(-100, 100, 0, 0, this);
    m_pan->setObjectName(QStringLiteral("param.pan"));
    m_pan->setToolTip(tr("Left (-100) to right (+100); equal-power"));
    f->addRow(tr("Pan"), m_pan);

    // Clip processing: EQ, gate, compressor (applied before volume and pan).
    auto db = [this](double def) {
        auto* s = new ParamSlider(-12, 12, def, 1, this);
        s->setSuffix(QStringLiteral(" dB"));
        return s;
    };
    m_eqLow = db(0);
    m_eqMid = db(0);
    m_eqHigh = db(0);
    m_eqLow->setToolTip(tr("Low shelf at 120 Hz: rumble / warmth"));
    m_eqMid->setToolTip(tr("Peak at 1 kHz: presence of voices"));
    m_eqHigh->setToolTip(tr("High shelf at 8 kHz: air / hiss"));
    f->addRow(tr("EQ low"), m_eqLow);
    f->addRow(tr("EQ mid"), m_eqMid);
    f->addRow(tr("EQ high"), m_eqHigh);
    m_gate = new QCheckBox(tr("Noise gate"), this);
    m_gate->setFocusPolicy(Qt::NoFocus);
    f->addRow(QString(), m_gate);
    m_gateThreshold = new ParamSlider(-80, -10, -50, 0, this);
    m_gateThreshold->setSuffix(QStringLiteral(" dB"));
    m_gateThreshold->setToolTip(tr("Sound quieter than this is muted (room noise between words)"));
    f->addRow(tr("Gate below"), m_gateThreshold);
    m_comp = new QCheckBox(tr("Compressor"), this);
    m_comp->setFocusPolicy(Qt::NoFocus);
    f->addRow(QString(), m_comp);
    m_compThreshold = new ParamSlider(-50, 0, -18, 0, this);
    m_compThreshold->setSuffix(QStringLiteral(" dB"));
    m_compRatio = new ParamSlider(1, 20, 3, 1, this);
    m_compRatio->setSuffix(QStringLiteral(" : 1"));
    m_compThreshold->setToolTip(tr("Louder than this gets turned down"));
    m_compRatio->setToolTip(tr("How strongly: 3:1 gentle, 10:1 heavy"));
    f->addRow(tr("Threshold"), m_compThreshold);
    f->addRow(tr("Ratio"), m_compRatio);

    // Edits touch audio clips only (the section context filters), even when the linked video clip is selected too.
    m_volumeParam = new AnimatedParam(m_volume, m_ctx, m_loading, {AnimProp::Volume}, tr("Volume"), prop::Volume,
                                      [](float lin) { return std::round(linearToDb(lin) * 10.0) / 10.0; }, [](double db) { return dbToLinear(db); });
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
    auto bindFx = [this](ParamSlider* s, int id, const QString& text, std::function<void(AudioFx&, float)> set, double scale) {
        connect(s, &ParamSlider::valueEdited, this, [this, id, text, set, scale](double v) {
            if (m_loading) return;
            const float value = static_cast<float>(v / scale);
            m_ctx.editSelection(text, id, [set, value](Clip& c) { set(c.audio.fx, value); });
        });
        connect(s, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    };
    connect(m_pan, &ParamSlider::valueEdited, this, [this](double v) {
        if (m_loading) return;
        const float pan = static_cast<float>(v / 100.0);
        m_ctx.editSelection(tr("Pan"), prop::Pan, [pan](Clip& c) { c.audio.pan = pan; });
    });
    connect(m_pan, &ParamSlider::gestureFinished, this, [this] { m_ctx.endGesture(); });
    bindFx(m_eqLow, prop::EqLow, tr("EQ"), [](AudioFx& fx, float v) { fx.eqLow = v; }, 1.0);
    bindFx(m_eqMid, prop::EqMid, tr("EQ"), [](AudioFx& fx, float v) { fx.eqMid = v; }, 1.0);
    bindFx(m_eqHigh, prop::EqHigh, tr("EQ"), [](AudioFx& fx, float v) { fx.eqHigh = v; }, 1.0);
    bindFx(m_gateThreshold, prop::GateThreshold, tr("Gate"), [](AudioFx& fx, float v) { fx.gateThreshold = v; fx.gate = true; }, 1.0);
    bindFx(m_compThreshold, prop::CompThreshold, tr("Compressor"), [](AudioFx& fx, float v) { fx.compThreshold = v; fx.compressor = true; }, 1.0);
    bindFx(m_compRatio, prop::CompRatio, tr("Compressor"), [](AudioFx& fx, float v) { fx.compRatio = v; fx.compressor = true; }, 1.0);
    connect(m_gate, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(on ? tr("Noise Gate On") : tr("Noise Gate Off"), prop::Gate, [on](Clip& c) { c.audio.fx.gate = on; });
        m_ctx.endGesture();
    });
    connect(m_comp, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.editSelection(on ? tr("Compressor On") : tr("Compressor Off"), prop::Compressor, [on](Clip& c) { c.audio.fx.compressor = on; });
        m_ctx.endGesture();
    });
    m_normalize = addHeaderButton(tr("Normalize"), tr("Set the volume so the loudest peak of the clip reaches -1 dBFS"));
    connect(m_normalize, &QToolButton::clicked, this, &AudioSection::normalize);
    auto* reset = addHeaderButton(tr("Reset"), tr("Unity gain, no fades, unmuted"));
    connect(reset, &QToolButton::clicked, this, [this] {
        m_ctx.editSelection(tr("Reset Audio"), prop::ResetGroup, [](Clip& c) { if (c.kind == ClipKind::Audio) c.audio = AudioParams{}; });
        m_ctx.endGesture();
    });
}

void AudioSection::normalize() {
    // Measure every selected audio clip's peak on a worker, then set the volumes in one step.
    struct Job { ClipId id; std::string path; TimePoint start; Duration length; };
    std::vector<Job> jobs;
    const auto& project = m_ctx.project();
    for (auto id : ops::withLinked(project.sequence, m_ctx.selectionIds())) {
        const Clip* c = project.sequence.findClip(id);
        if (!c || c->kind != ClipKind::Audio) continue;
        if (const auto* m = project.findMedia(c->media); m && !m->offline) jobs.push_back({c->id, m->path, c->sourceIn, c->sourceDuration()});
    }
    if (jobs.empty()) return;
    m_normalize->setEnabled(false);
    QPointer<AudioSection> self(this);
    std::thread([self, jobs] {
        std::vector<std::pair<ClipId, float>> gains;
        for (const auto& j : jobs) {
            auto peak = playback::measurePeak(j.path, j.start, j.length);
            if (peak) gains.emplace_back(j.id, playback::normalizeGain(*peak, -1.0f));
        }
        QMetaObject::invokeMethod(qApp, [self, gains] {
            if (!self) return;
            self->m_ctx.editSelection(tr("Normalize"), prop::Volume, [&gains](Clip& c) {
                for (const auto& [id, g] : gains) if (c.id == id) c.audio.volume = std::clamp(g, 0.0f, 16.0f);
            });
            self->m_ctx.endGesture();
            self->m_normalize->setEnabled(true);
        }, Qt::QueuedConnection);
    }).detach();
}

void AudioSection::load(const Clip& c, int) {
    LoadGuard guard(m_loading);
    m_volumeParam->load(c);
    m_pan->setValue(std::round(c.audio.pan * 100.0));
    m_eqLow->setValue(c.audio.fx.eqLow);
    m_eqMid->setValue(c.audio.fx.eqMid);
    m_eqHigh->setValue(c.audio.fx.eqHigh);
    m_gate->setChecked(c.audio.fx.gate);
    m_gateThreshold->setValue(c.audio.fx.gateThreshold);
    m_comp->setChecked(c.audio.fx.compressor);
    m_compThreshold->setValue(c.audio.fx.compThreshold);
    m_compRatio->setValue(c.audio.fx.compRatio);
    m_normalize->setEnabled(true);
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
    m_reverse = new QCheckBox(tr("Reverse (play backwards)"), this);
    m_reverse->setObjectName(QStringLiteral("clipReverse"));
    m_reverse->setFocusPolicy(Qt::NoFocus);
    m_reverse->setToolTip(tr("Plays the clip's media range backwards; linked audio follows"));
    f->addRow(QString(), m_reverse);
    connect(m_reverse, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        m_ctx.pushEdit(on ? tr("Reverse Clip") : tr("Play Forwards"), [on](Document& doc, const std::vector<ClipId>& ids) { return ops::setReverse(doc.project().sequence, ids, on); });
        m_ctx.endGesture();
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
    m_reverse->setChecked(c.reverse);
    QString sub = pct == 100 ? QString() : QStringLiteral("%1%").arg(pct);
    if (c.reverse) sub = sub.isEmpty() ? tr("reversed") : sub + tr(" · reversed");
    setSubtitle(sub);
}

}  // namespace lf::ui
