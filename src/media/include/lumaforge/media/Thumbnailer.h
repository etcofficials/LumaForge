#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/core/Time.h"
#include "lumaforge/media/Image.h"

#include <string>

namespace lf::media {

// Produces a single small BGRA image from a media file. Uses software
// decoding on purpose: a thumbnail is one frame, and keeping this path free
// of GPU state lets it run on any worker thread.
class Thumbnailer {
public:
    struct Options {
        int maxWidth = 320;
        int maxHeight = 180;
        // Position inside the file; ignored for images. Negative = pick a
        // representative frame automatically (~10% in, but at least 0.5 s).
        TimePoint at{-1};
    };

    static Result<Image> render(const std::string& utf8Path, const Options& options = {});
};

}  // namespace lf::media
