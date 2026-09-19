#pragma once

#include "lumaforge/core/Result.h"
#include "lumaforge/project/Project.h"

#include <string>

namespace lf::project {

// The .lfproj file format: UTF-8 JSON, versioned. Media is referenced by
// absolute path plus a path relative to the project file, so a project folder
// that is moved as a whole still finds its media. Older versions are upgraded
// on load; a newer version than we know is refused with a clear message.
namespace serializer {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFormatName = "lumaforge-project";
inline constexpr const char* kFileExtension = ".lfproj";

struct LoadResult {
    Project project;
    int fileVersion = 0;
    std::vector<std::string> offlineMedia;   // display names of media that could not be found
};

// projectDir: directory the project file lives in (for relative media paths). UTF-8.
std::string toJson(const Project& project, const std::string& projectDir);
Result<LoadResult> fromJson(const std::string& json, const std::string& projectDir);

// File helpers (UTF-8 paths). Saving writes to a temporary file first and
// replaces the target only when the whole document was written.
Result<void> saveToFile(const Project& project, const std::string& path);
Result<LoadResult> loadFromFile(const std::string& path);

}  // namespace serializer

}  // namespace lf::project
