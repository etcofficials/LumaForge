#pragma once

#include "Rational.h"

#include <cstdint>
#include <compare>

namespace lf {

// LumaForge measures timeline time in *flicks*: 1 flick = 1/705,600,000 s.
//
// 705,600,000 is divisible by 24, 25, 30, 48, 50, 60, 90, 100, 120 and by the
// NTSC rates 24000/1001, 30000/1001, 60000/1001, as well as by 8k/16k/22.05k/
// 24k/32k/44.1k/48k/88.2k/96k/192k Hz audio. Every frame boundary and every
// audio sample boundary of any common format is therefore an exact integer:
// no drift, no rounding, no floating point in the timeline.
//
// int64 flicks covers ~414 years, so overflow is not a practical concern.
using Ticks = std::int64_t;
inline constexpr Ticks kFlicksPerSecond = 705'600'000;

struct Duration;

struct TimePoint {
    Ticks ticks = 0;

    constexpr TimePoint() = default;
    constexpr explicit TimePoint(Ticks t) : ticks(t) {}

    static constexpr TimePoint zero() noexcept { return TimePoint{0}; }
    static constexpr TimePoint fromSeconds(double s) noexcept { return TimePoint{static_cast<Ticks>(s * kFlicksPerSecond + (s >= 0 ? 0.5 : -0.5))}; }
    [[nodiscard]] constexpr double toSeconds() const noexcept { return static_cast<double>(ticks) / kFlicksPerSecond; }

    friend constexpr auto operator<=>(const TimePoint&, const TimePoint&) noexcept = default;
    constexpr TimePoint& operator+=(Duration d) noexcept;
    constexpr TimePoint& operator-=(Duration d) noexcept;
};

struct Duration {
    Ticks ticks = 0;

    constexpr Duration() = default;
    constexpr explicit Duration(Ticks t) : ticks(t) {}

    static constexpr Duration zero() noexcept { return Duration{0}; }
    static constexpr Duration fromSeconds(double s) noexcept { return Duration{static_cast<Ticks>(s * kFlicksPerSecond + (s >= 0 ? 0.5 : -0.5))}; }
    static constexpr Duration fromMilliseconds(std::int64_t ms) noexcept { return Duration{ms * (kFlicksPerSecond / 1000)}; }
    [[nodiscard]] constexpr double toSeconds() const noexcept { return static_cast<double>(ticks) / kFlicksPerSecond; }
    [[nodiscard]] constexpr bool isZero() const noexcept { return ticks == 0; }
    [[nodiscard]] constexpr bool isNegative() const noexcept { return ticks < 0; }

    friend constexpr auto operator<=>(const Duration&, const Duration&) noexcept = default;
    friend constexpr Duration operator+(Duration a, Duration b) noexcept { return Duration{a.ticks + b.ticks}; }
    friend constexpr Duration operator-(Duration a, Duration b) noexcept { return Duration{a.ticks - b.ticks}; }
    friend constexpr Duration operator*(Duration a, std::int64_t k) noexcept { return Duration{a.ticks * k}; }
    friend constexpr Duration operator-(Duration a) noexcept { return Duration{-a.ticks}; }
    constexpr Duration& operator+=(Duration d) noexcept { ticks += d.ticks; return *this; }
    constexpr Duration& operator-=(Duration d) noexcept { ticks -= d.ticks; return *this; }
};

constexpr TimePoint operator+(TimePoint t, Duration d) noexcept { return TimePoint{t.ticks + d.ticks}; }
constexpr TimePoint operator-(TimePoint t, Duration d) noexcept { return TimePoint{t.ticks - d.ticks}; }
constexpr Duration operator-(TimePoint a, TimePoint b) noexcept { return Duration{a.ticks - b.ticks}; }
constexpr TimePoint& TimePoint::operator+=(Duration d) noexcept { ticks += d.ticks; return *this; }
constexpr TimePoint& TimePoint::operator-=(Duration d) noexcept { ticks -= d.ticks; return *this; }

// Exact duration of one frame at the given rate. For every common rate the
// division is exact; for exotic rates it truncates (documented behaviour).
constexpr Duration frameDuration(Rational fps) noexcept {
    if (!fps.isValid() || fps.isZero()) return Duration::zero();
    return Duration{(kFlicksPerSecond * fps.den) / fps.num};
}

// True when every frame boundary at this rate lands on an integer flick.
constexpr bool isExactFrameRate(Rational fps) noexcept {
    return fps.isValid() && !fps.isZero() && (kFlicksPerSecond * fps.den) % fps.num == 0;
}

// Frame index containing time t (floor).
constexpr std::int64_t frameIndexAt(TimePoint t, Rational fps) noexcept {
    const auto fd = frameDuration(fps).ticks;
    if (fd <= 0) return 0;
    return t.ticks >= 0 ? t.ticks / fd : -((-t.ticks + fd - 1) / fd);
}

constexpr TimePoint timeOfFrame(std::int64_t frame, Rational fps) noexcept {
    return TimePoint{frame * frameDuration(fps).ticks};
}

// Snap t to the nearest frame boundary at fps.
constexpr TimePoint snapToFrame(TimePoint t, Rational fps) noexcept {
    const auto fd = frameDuration(fps).ticks;
    if (fd <= 0) return t;
    const auto half = fd / 2;
    const auto shifted = t.ticks >= 0 ? t.ticks + half : t.ticks - half;
    return TimePoint{(shifted / fd) * fd};
}

// Convert a count in an arbitrary time base (e.g. an FFmpeg stream time_base)
// to flicks, rounding to nearest. Uses 128-bit-free arithmetic that is exact
// whenever the time base divides a second evenly (the common case).
constexpr Ticks ticksFromTimeBase(std::int64_t value, Rational timeBase) noexcept {
    if (!timeBase.isValid() || timeBase.isZero()) return 0;
    // value * timeBase.num * kFlicksPerSecond / timeBase.den
    const std::int64_t whole = value / timeBase.den;
    const std::int64_t rem = value % timeBase.den;
    return whole * timeBase.num * kFlicksPerSecond + (rem * timeBase.num * kFlicksPerSecond + timeBase.den / 2) / timeBase.den;
}

constexpr std::int64_t timeBaseFromTicks(Ticks ticks, Rational timeBase) noexcept {
    if (!timeBase.isValid() || timeBase.isZero()) return 0;
    // ticks * timeBase.den / (timeBase.num * kFlicksPerSecond)
    const std::int64_t denom = timeBase.num * kFlicksPerSecond;
    const std::int64_t whole = ticks / denom;
    const std::int64_t rem = ticks % denom;
    return whole * timeBase.den + (rem * timeBase.den + denom / 2) / denom;
}

// Half-open time interval [start, start + duration).
struct TimeRange {
    TimePoint start;
    Duration duration;

    [[nodiscard]] constexpr TimePoint end() const noexcept { return start + duration; }
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return duration.ticks <= 0; }
    [[nodiscard]] constexpr bool contains(TimePoint t) const noexcept { return t >= start && t < end(); }
    [[nodiscard]] constexpr bool overlaps(const TimeRange& o) const noexcept { return start < o.end() && o.start < end(); }
    friend constexpr bool operator==(const TimeRange&, const TimeRange&) noexcept = default;
};

}  // namespace lf
