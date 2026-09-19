#include "ui/Timecode.h"

#include <cstdlib>

namespace lf::ui {

namespace {
struct Parts {
    bool negative;
    std::int64_t hours, minutes, seconds, frames, millis;
};

Parts split(Ticks ticks, Rational fps) {
    Parts p{ticks < 0, 0, 0, 0, 0, 0};
    const Ticks abs = ticks < 0 ? -ticks : ticks;
    const Ticks wholeSeconds = abs / kFlicksPerSecond;
    const Ticks rem = abs % kFlicksPerSecond;
    p.hours = wholeSeconds / 3600;
    p.minutes = (wholeSeconds / 60) % 60;
    p.seconds = wholeSeconds % 60;
    const Ticks fd = frameDuration(fps).ticks;
    p.frames = fd > 0 ? rem / fd : 0;
    p.millis = rem / (kFlicksPerSecond / 1000);
    return p;
}
}  // namespace

QString formatTimecode(TimePoint t, Rational fps) {
    const Parts p = split(t.ticks, fps);
    return QStringLiteral("%1%2:%3:%4:%5")
        .arg(p.negative ? QStringLiteral("-") : QString())
        .arg(p.hours, 2, 10, QLatin1Char('0'))
        .arg(p.minutes, 2, 10, QLatin1Char('0'))
        .arg(p.seconds, 2, 10, QLatin1Char('0'))
        .arg(p.frames, 2, 10, QLatin1Char('0'));
}

QString formatClock(Duration d) {
    const Parts p = split(d.ticks, fps::f30);
    return QStringLiteral("%1%2:%3:%4.%5")
        .arg(p.negative ? QStringLiteral("-") : QString())
        .arg(p.hours, 2, 10, QLatin1Char('0'))
        .arg(p.minutes, 2, 10, QLatin1Char('0'))
        .arg(p.seconds, 2, 10, QLatin1Char('0'))
        .arg(p.millis, 3, 10, QLatin1Char('0'));
}

QString formatRulerLabel(TimePoint t, bool includeFrames, Rational fps) {
    const Parts p = split(t.ticks, fps);
    QString s;
    if (p.hours > 0) s = QStringLiteral("%1:%2:%3").arg(p.hours).arg(p.minutes, 2, 10, QLatin1Char('0')).arg(p.seconds, 2, 10, QLatin1Char('0'));
    else s = QStringLiteral("%1:%2").arg(p.minutes).arg(p.seconds, 2, 10, QLatin1Char('0'));
    if (includeFrames) s += QStringLiteral(":%1").arg(p.frames, 2, 10, QLatin1Char('0'));
    return p.negative ? QLatin1Char('-') + s : s;
}

}  // namespace lf::ui
