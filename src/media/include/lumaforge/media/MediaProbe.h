#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/media/MediaInfo.h"

#include <string>
#include <string_view>
#include <vector>

namespace lf::media {

// Reads container and stream metadata without decoding. Blocking and
// allocation-heavy (opens the file, reads headers, may read a few packets):
// call it from a worker thread.
class MediaProbe {
public:
    static Result<MediaInfo> probe(const std::string& utf8Path);

    // Extensions LumaForge offers in file dialogs. Probing is the real test;
    // this list only drives UI filters and drag/drop pre-screening.
    static const std::vector<std::string>& videoExtensions();
    static const std::vector<std::string>& audioExtensions();
    static const std::vector<std::string>& imageExtensions();
    static bool looksLikeMedia(std::string_view utf8Path);
};

}  // namespace lf::media
