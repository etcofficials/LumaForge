#include "TestMain.h"

#include "lumaforge/rendering/FrameLayout.h"

using namespace lf::render;

TEST_CASE("fitFrame letterboxes wide frames in tall viewports") {
    const IntRect r = fitFrame(1000, 1000, 1920, 1080);
    CHECK(r.width == 1000);
    CHECK(r.height == 562);
    CHECK(r.x == 0);
    CHECK(r.y == (1000 - 562) / 2);
}

TEST_CASE("fitFrame pillarboxes tall frames in wide viewports") {
    const IntRect r = fitFrame(1000, 500, 1080, 1920);
    CHECK(r.height == 500);
    CHECK(r.width == 281);
    CHECK(r.y == 0);
}

TEST_CASE("fitFrame handles degenerate sizes") {
    CHECK(fitFrame(0, 100, 16, 9).width == 0);
    CHECK(fitFrame(100, 100, 0, 9).width == 0);
    CHECK(fitFrame(3, 3, 1920, 1080).width >= 1);
    CHECK(fitFrame(3, 3, 1920, 1080).height >= 1);
}
