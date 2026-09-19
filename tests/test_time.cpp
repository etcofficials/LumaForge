#define LF_TEST_MAIN
#include "TestMain.h"

#include "lumaforge/core/Time.h"

using namespace lf;

TEST_CASE("all common frame rates are exact in flicks") {
    for (auto r : {fps::f23_976, fps::f24, fps::f25, fps::f29_97, fps::f30, fps::f50, fps::f59_94, fps::f60}) {
        CHECK(isExactFrameRate(r));
        CHECK(frameDuration(r).ticks * r.num == kFlicksPerSecond * r.den);
    }
    CHECK(isExactFrameRate(Rational{48000, 1}));
    CHECK(isExactFrameRate(Rational{44100, 1}));
}

TEST_CASE("frame index and time round-trip without drift") {
    const Rational r = fps::f29_97;
    for (std::int64_t f = 0; f < 200000; f += 997) {
        const TimePoint t = timeOfFrame(f, r);
        CHECK(frameIndexAt(t, r) == f);
        CHECK(frameIndexAt(t + Duration{frameDuration(r).ticks - 1}, r) == f);
        CHECK(frameIndexAt(t + frameDuration(r), r) == f + 1);
    }
}

TEST_CASE("snapToFrame rounds to nearest boundary") {
    const Rational r = fps::f30;
    const Ticks fd = frameDuration(r).ticks;
    CHECK(snapToFrame(TimePoint{fd * 10 + fd / 2 - 1}, r).ticks == fd * 10);
    CHECK(snapToFrame(TimePoint{fd * 10 + fd / 2 + 1}, r).ticks == fd * 11);
    CHECK(snapToFrame(TimePoint{-fd / 3}, r).ticks == 0);
}

TEST_CASE("time base conversion is exact for FFmpeg-style bases") {
    // 1/90000 (MPEG-TS) and 1/48000 (audio) divide a second evenly.
    for (auto tb : {Rational{1, 90000}, Rational{1, 48000}, Rational{1001, 30000}}) {
        for (std::int64_t v : {0LL, 1LL, 12345LL, 987654321LL}) {
            const Ticks t = ticksFromTimeBase(v, tb);
            CHECK(timeBaseFromTicks(t, tb) == v);
        }
    }
}

TEST_CASE("Rational normalises sign and gcd") {
    CHECK(Rational(30000, 1001) == Rational(60000, 2002));
    CHECK(Rational(-1, -2) == Rational(1, 2));
    CHECK(Rational(1, -2).den == 2);
    CHECK(Rational(1, -2).num == -1);
    CHECK(!Rational(1, 0).isValid());
    CHECK(fps::f29_97 < fps::f30);
}

TEST_CASE("TimeRange semantics are half-open") {
    TimeRange a{TimePoint{100}, Duration{50}};
    CHECK(a.contains(TimePoint{100}));
    CHECK(!a.contains(TimePoint{150}));
    CHECK(a.overlaps(TimeRange{TimePoint{149}, Duration{10}}));
    CHECK(!a.overlaps(TimeRange{TimePoint{150}, Duration{10}}));
}
