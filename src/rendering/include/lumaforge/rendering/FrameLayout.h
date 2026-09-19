#pragma once

#include <algorithm>
#include <cstdint>

namespace lf::render {

struct IntRect {
    int x = 0, y = 0, width = 0, height = 0;
    friend constexpr bool operator==(const IntRect&, const IntRect&) noexcept = default;
};

// Where the project frame lands inside a viewport: aspect-preserving fit,
// centred, integer-aligned so the video never lands on half pixels. Both the
// D3D11 renderer and the software fallback use this, so they always agree.
constexpr IntRect fitFrame(int viewportW, int viewportH, int frameW, int frameH, int padding = 0) noexcept {
    IntRect r;
    const int availW = std::max(0, viewportW - 2 * padding);
    const int availH = std::max(0, viewportH - 2 * padding);
    if (availW == 0 || availH == 0 || frameW <= 0 || frameH <= 0) return r;

    // Compare aspect ratios with integer math: frameW/frameH vs availW/availH.
    const std::int64_t lhs = static_cast<std::int64_t>(frameW) * availH;
    const std::int64_t rhs = static_cast<std::int64_t>(availW) * frameH;
    if (lhs >= rhs) {
        r.width = availW;
        r.height = static_cast<int>((static_cast<std::int64_t>(availW) * frameH) / frameW);
    } else {
        r.height = availH;
        r.width = static_cast<int>((static_cast<std::int64_t>(availH) * frameW) / frameH);
    }
    r.width = std::max(r.width, 1);
    r.height = std::max(r.height, 1);
    r.x = padding + (availW - r.width) / 2;
    r.y = padding + (availH - r.height) / 2;
    return r;
}

}  // namespace lf::render
