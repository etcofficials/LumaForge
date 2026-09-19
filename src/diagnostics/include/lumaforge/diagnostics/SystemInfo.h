#pragma once

#include <cstdint>
#include <string>

namespace lf::diag {

struct CpuInfo {
    std::string brand;              // "Intel(R) Core(TM) i5-2400S CPU @ 2.50GHz"
    unsigned physicalCores = 0;
    unsigned logicalProcessors = 0;
    bool avx = false;
    bool avx2 = false;
    bool sse4_2 = false;
};

struct MemoryInfo {
    std::uint64_t totalPhysical = 0;
    std::uint64_t availablePhysical = 0;
    std::uint64_t processWorkingSet = 0;
    std::uint64_t processPrivate = 0;
};

struct OsInfo {
    std::string name;       // "Windows 10 Pro"
    std::string version;    // "10.0.19045"
    std::string build;      // "19045"
    bool is64Bit = true;
};

struct BuildInfo {
    std::string appVersion;
    std::string buildType;
    std::string compiler;       // "MSVC 19.44.35229"
    std::string qtVersion;      // filled in by the UI layer
    std::string ffmpegVersion;  // "avcodec 62.28.102 / avformat 62.12.102"
    std::string ffmpegLicense;
    std::string ffmpegConfiguration;
};

struct SystemReport {
    CpuInfo cpu;
    MemoryInfo memory;
    OsInfo os;
    BuildInfo build;
};

[[nodiscard]] CpuInfo queryCpu();
[[nodiscard]] MemoryInfo queryMemory();
[[nodiscard]] OsInfo queryOs();
[[nodiscard]] BuildInfo queryBuild();
[[nodiscard]] SystemReport querySystem();

}  // namespace lf::diag
