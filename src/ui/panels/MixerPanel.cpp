#include "ui/panels/MixerPanel.h"

#include "lumaforge/playback/AudioDsp.h"
#include "ui/Theme.h"
#include "ui/playback/PlaybackController.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace lf::ui {

using namespace lf::project;

namespace {
// Fader: tenths of a dB, -60.0 .. +12.0; the bottom stop means silence.
constexpr int kFaderMin = -600, kFaderMax = 120;
int faderFromGain(float g) { return g <= 0.001f ? kFaderMin : std::clamp(static_cast<int>(std::lround(playback::dsp::gainToDb(g) * 10.0f)), kFaderMin, kFaderMax); }
float gainFromFader(int v) { return v <= kFaderMin ? 0.0f : playback::dsp::dbToGain(v / 10.0f); }
QString dbText(float g) { return g <= 0.001f ? QStringLiteral("-∞") : QStringLiteral("%1 dB").arg(playback::dsp::gainToDb(g), 0, 'f', 1); }
float meterPosition(float linear) { return std::clamp((playback::dsp::gainToDb(linear) + 60.0f) / 60.0f, 0.0f, 1.0f); }

QToolButton* toggle(QWidget* parent, const QString& text, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setFixedSize(26, 20);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
}  // namespace

// ---------------------------------------------------------------- meter

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) {
    setMinimumSize(10, 80);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setToolTip(tr("Peak level (dBFS): red at the top means clipping risk"));
}

void LevelMeter::setLevel(float linearPeak) {
    const float target = meterPosition(linearPeak);
    // Instant rise, ~20 dB/s fall at 30 Hz polling.
    m_level = target > m_level ? target : std::max(target, m_level - 0.011f);
    if (target >= m_hold) { m_hold = target; m_holdAge = 0; }
    else if (++m_holdAge > 45) m_hold = std::max(target, m_hold - 0.02f);
    update();
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& pal = Theme::palette();
    const QRect r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, pal.bgInput);
    const int h = static_cast<int>(r.height() * m_level);
    // Green up to -12 dB, yellow to -3 dB, red above.
    auto y = [&](float db) { return r.bottom() - static_cast<int>(r.height() * ((db + 60.0f) / 60.0f)); };
    const QRect lit(r.left(), r.bottom() - h + 1, r.width(), h);
    p.save();
    p.setClipRect(lit);
    p.fillRect(QRect(r.left(), y(-12), r.width(), r.bottom() - y(-12) + 1), QColor(0x3F, 0xB9, 0x6B));
    p.fillRect(QRect(r.left(), y(-3), r.width(), y(-12) - y(-3)), QColor(0xF2, 0xC1, 0x3A));
    p.fillRect(QRect(r.left(), r.top(), r.width(), y(-3) - r.top()), QColor(0xE5, 0x53, 0x3D));
    p.restore();
    if (m_hold > 0.0f) {
        const int hy = r.bottom() - static_cast<int>(r.height() * m_hold);
        p.fillRect(QRect(r.left(), hy, r.width(), 2), pal.textPrimary);
    }
}

// ---------------------------------------------------------------- panel

MixerPanel::MixerPanel(Document& doc, PlaybackController& playback, QWidget* parent) : QWidget(parent), m_doc(doc), m_playback(playback) {
    setProperty("panel", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(8);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    m_strips = new QHBoxLayout(host);
    m_strips->setContentsMargins(0, 0, 0, 0);
    m_strips->setSpacing(6);
    m_strips->addStretch(1);
    scroll->setWidget(host);
    layout->addWidget(scroll, 1);

    // Master strip
    auto* master = new QWidget(this);
    master->setObjectName(QStringLiteral("mixerMaster"));
    auto* mv = new QVBoxLayout(master);
    mv->setContentsMargins(6, 4, 6, 4);
    auto* title = new QLabel(tr("Master"), master);
    title->setAlignment(Qt::AlignCenter);
    title->setProperty("sectionHeading", true);
    mv->addWidget(title);
    auto* row = new QHBoxLayout();
    m_masterFader = new QSlider(Qt::Vertical, master);
    m_masterFader->setObjectName(QStringLiteral("masterFader"));
    m_masterFader->setRange(kFaderMin, kFaderMax);
    m_masterFader->setToolTip(tr("Volume of the final mix (preview and export)"));
    m_masterMeter = new LevelMeter(master);
    row->addWidget(m_masterFader);
    row->addWidget(m_masterMeter);
    mv->addLayout(row, 1);
    m_masterDb = new QLabel(master);
    m_masterDb->setAlignment(Qt::AlignCenter);
    mv->addWidget(m_masterDb);
    m_limiter = new QCheckBox(tr("Limiter"), master);
    m_limiter->setToolTip(tr("Keeps the mix just below 0 dBFS without hard clipping"));
    mv->addWidget(m_limiter);
    layout->addWidget(master);

    connect(m_masterFader, &QSlider::valueChanged, this, [this](int v) { m_masterDb->setText(dbText(gainFromFader(v))); });
    auto commitMasterFader = [this] {
        if (m_updating) return;
        commitMaster(MasterBus{gainFromFader(m_masterFader->value()), m_limiter->isChecked()}, tr("Master Volume"));
    };
    connect(m_masterFader, &QSlider::sliderReleased, this, commitMasterFader);
    connect(m_masterFader, &QSlider::actionTriggered, this, [this, commitMasterFader](int action) {
        // Keyboard / wheel / page steps commit at once; drags commit on release.
        if (action != QAbstractSlider::SliderMove) QTimer::singleShot(0, this, commitMasterFader);
    });
    connect(m_limiter, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        commitMaster(MasterBus{gainFromFader(m_masterFader->value()), on}, on ? tr("Limiter On") : tr("Limiter Off"));
    });

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(33);
    connect(m_meterTimer, &QTimer::timeout, this, &MixerPanel::pollMeters);
    m_listenerToken = m_doc.addListener([this](const Change& c) {
        if (c.kind != Change::Kind::Reset && c.kind != Change::Kind::TracksChanged) return;
        // Same tracks: just show the new values (keeps the slider being used alive).
        std::vector<TrackId> now;
        for (const auto* t : m_doc.project().sequence.audioTracks()) now.push_back(t->id);
        std::vector<TrackId> shown;
        for (const auto& s : m_stripList) shown.push_back(s.track);
        if (now == shown) refreshValues();
        else rebuild();
    });
    rebuild();
}

MixerPanel::~MixerPanel() { m_doc.removeListener(m_listenerToken); }

void MixerPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    m_meterTimer->start();
}

void MixerPanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    m_meterTimer->stop();
}

void MixerPanel::commitTrack(const Track& track, const QString& text) {
    SequenceEdit edit;
    edit.tracks.push_back(track);
    m_doc.push(std::make_unique<SequenceEditCommand>(m_doc, text.toStdString(), std::move(edit)));
}

void MixerPanel::commitMaster(MasterBus bus, const QString& text) {
    const auto& seq = m_doc.project().sequence;
    if (bus.volume == seq.masterVolume && bus.limiter == seq.masterLimiter) return;
    SequenceEdit edit;
    edit.master = bus;
    m_doc.push(std::make_unique<SequenceEditCommand>(m_doc, text.toStdString(), std::move(edit)));
}

void MixerPanel::rebuild() {
    for (auto& s : m_stripList) s.widget->deleteLater();
    m_stripList.clear();
    for (const auto& t : m_doc.project().sequence.audioTracks()) {
        Strip s;
        s.track = t->id;
        s.widget = new QWidget(this);
        s.widget->setObjectName(QStringLiteral("mixerStrip"));
        s.widget->setAttribute(Qt::WA_StyledBackground, true);
        auto* v = new QVBoxLayout(s.widget);
        v->setContentsMargins(6, 4, 6, 4);
        v->setSpacing(4);
        s.name = new QLabel(s.widget);
        s.name->setAlignment(Qt::AlignCenter);
        v->addWidget(s.name);
        auto* row = new QHBoxLayout();
        s.fader = new QSlider(Qt::Vertical, s.widget);
        s.fader->setRange(kFaderMin, kFaderMax);
        s.fader->setToolTip(tr("Track volume"));
        s.meter = new LevelMeter(s.widget);
        row->addWidget(s.fader);
        row->addWidget(s.meter);
        v->addLayout(row, 1);
        s.db = new QLabel(s.widget);
        s.db->setAlignment(Qt::AlignCenter);
        v->addWidget(s.db);
        s.pan = new QSlider(Qt::Horizontal, s.widget);
        s.pan->setRange(-100, 100);
        s.pan->setFixedWidth(64);
        s.pan->setToolTip(tr("Track pan (double-click to centre)"));
        v->addWidget(s.pan, 0, Qt::AlignHCenter);
        auto* buttons = new QHBoxLayout();
        s.mute = toggle(s.widget, QStringLiteral("M"), tr("Mute track"));
        s.solo = toggle(s.widget, QStringLiteral("S"), tr("Solo track"));
        buttons->addWidget(s.mute);
        buttons->addWidget(s.solo);
        v->addLayout(buttons);
        m_strips->insertWidget(m_strips->count() - 1, s.widget);

        const TrackId id = t->id;
        auto withTrack = [this, id](const std::function<void(Track&)>& change, const QString& text) {
            if (m_updating) return;
            const Track* cur = m_doc.project().sequence.findTrack(id);
            if (!cur) return;
            Track n = *cur;
            change(n);
            if (n == *cur) return;
            commitTrack(n, text);
        };
        QSlider* fader = s.fader;
        QLabel* db = s.db;
        connect(fader, &QSlider::valueChanged, this, [db](int v) { db->setText(dbText(gainFromFader(v))); });
        auto commitFader = [withTrack, fader] { withTrack([fader](Track& t) { t.volume = gainFromFader(fader->value()); }, tr("Track Volume")); };
        connect(fader, &QSlider::sliderReleased, this, commitFader);
        connect(fader, &QSlider::actionTriggered, this, [this, commitFader](int action) { if (action != QAbstractSlider::SliderMove) QTimer::singleShot(0, this, commitFader); });
        QSlider* pan = s.pan;
        auto commitPan = [withTrack, pan] { withTrack([pan](Track& t) { t.pan = pan->value() / 100.0f; }, tr("Track Pan")); };
        connect(pan, &QSlider::sliderReleased, this, commitPan);
        connect(pan, &QSlider::actionTriggered, this, [this, commitPan](int action) { if (action != QAbstractSlider::SliderMove) QTimer::singleShot(0, this, commitPan); });
        connect(s.mute, &QToolButton::toggled, this, [withTrack](bool on) { withTrack([on](Track& t) { t.muted = on; }, on ? tr("Mute Track") : tr("Unmute Track")); });
        connect(s.solo, &QToolButton::toggled, this, [withTrack](bool on) { withTrack([on](Track& t) { t.solo = on; }, on ? tr("Solo Track") : tr("Unsolo Track")); });
        m_stripList.push_back(s);
    }
    refreshValues();
}

void MixerPanel::refreshValues() {
    m_updating = true;
    const auto& seq = m_doc.project().sequence;
    for (auto& s : m_stripList) {
        const Track* t = seq.findTrack(s.track);
        if (!t) continue;
        s.name->setText(QString::fromStdString(t->name));
        s.fader->setValue(faderFromGain(t->volume));
        s.db->setText(dbText(t->volume));
        s.pan->setValue(static_cast<int>(std::lround(t->pan * 100.0f)));
        s.mute->setChecked(t->muted);
        s.solo->setChecked(t->solo);
    }
    m_masterFader->setValue(faderFromGain(seq.masterVolume));
    m_masterDb->setText(dbText(seq.masterVolume));
    m_limiter->setChecked(seq.masterLimiter);
    m_updating = false;
}

void MixerPanel::pollMeters() {
    auto* engine = m_playback.engine();
    if (!engine) return;
    const auto levels = engine->takeAudioLevels();
    for (auto& s : m_stripList) {
        float peak = 0.0f;
        for (const auto& [key, value] : levels.tracks) if (key == raw(s.track)) peak = value;
        s.meter->setLevel(peak);
    }
    m_masterMeter->setLevel(levels.master);
}

}  // namespace lf::ui
