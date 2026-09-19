#pragma once

#include "lumaforge/core/Time.h"

#include <QString>

namespace lf::ui {

// HH:MM:SS:FF at the given frame rate (drop-frame notation is not applied;
// the frame count is exact frames since zero, which is what an editor needs).
QString formatTimecode(TimePoint t, Rational fps);

// HH:MM:SS.mmm for durations where frames are not meaningful.
QString formatClock(Duration d);

// Compact ruler label: "0:00", "1:30", "1:02:03" depending on magnitude.
QString formatRulerLabel(TimePoint t, bool includeFrames, Rational fps);

}  // namespace lf::ui
