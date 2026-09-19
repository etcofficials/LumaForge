#pragma once

#include <cstdint>
#include <vector>

namespace lf::media {

// Plain CPU image, 8-bit BGRA, tightly packed rows (stride = width * 4).
// Used for thumbnails and other small transfers; video frames never go
// through this type.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;

    [[nodiscard]] bool isNull() const noexcept { return width <= 0 || height <= 0 || bgra.empty(); }
    [[nodiscard]] int stride() const noexcept { return width * 4; }
};

}  // namespace lf::media
