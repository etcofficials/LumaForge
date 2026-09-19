#include "lumaforge/diagnostics/SystemInfo.h"

#include "lumaforge/core/StringUtil.h"
#include "lumaforge/core/Version.h"

#include <windows.h>
#include <intrin.h>
#include <psapi.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <array>
#include <cstring>
#include <format>
#include <vector>

namespace lf::diag {

namespace {

// RtlGetVersion is the only reliable way to get the real OS version without a manifest dance.
using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

std::string readRegistryString(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[256]{};
    DWORD size = sizeof buf;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return wideToUtf8(buf);
    return {};
}
}  // namespace

CpuInfo queryCpu() {
    CpuInfo info;
    std::array<int, 4> regs{};
    char brand[49]{};
    for (unsigned i = 0; i < 3; ++i) {
        __cpuid(regs.data(), static_cast<int>(0x80000002 + i));
        std::memcpy(brand + i * 16, regs.data(), 16);
    }
    info.brand = brand;
    while (!info.brand.empty() && info.brand.front() == ' ') info.brand.erase(info.brand.begin());

    __cpuid(regs.data(), 1);
    info.sse4_2 = (regs[2] & (1 << 20)) != 0;
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    const bool avxBit = (regs[2] & (1 << 28)) != 0;
    if (osxsave && avxBit) {
        const auto xcr0 = _xgetbv(0);
        info.avx = (xcr0 & 0x6) == 0x6;
    }
    __cpuidex(regs.data(), 7, 0);
    info.avx2 = info.avx && (regs[1] & (1 << 5)) != 0;

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    info.logicalProcessors = si.dwNumberOfProcessors;

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<std::byte> buffer(len);
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &len)) {
        for (DWORD off = 0; off < len;) {
            auto* e = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + off);
            if (e->Relationship == RelationProcessorCore) ++info.physicalCores;
            off += e->Size;
        }
    }
    if (info.physicalCores == 0) info.physicalCores = info.logicalProcessors;
    return info;
}

MemoryInfo queryMemory() {
    MemoryInfo m;
    MEMORYSTATUSEX ms{sizeof ms};
    if (GlobalMemoryStatusEx(&ms)) {
        m.totalPhysical = ms.ullTotalPhys;
        m.availablePhysical = ms.ullAvailPhys;
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        m.processWorkingSet = pmc.WorkingSetSize;
        m.processPrivate = pmc.PrivateUsage;
    }
    return m;
}

OsInfo queryOs() {
    OsInfo os;
    RTL_OSVERSIONINFOW v{sizeof v};
    if (auto* ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) fn(&v);
    }
    os.version = std::format("{}.{}.{}", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    os.build = std::to_string(v.dwBuildNumber);
    os.name = readRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    if (v.dwBuildNumber >= 22000 && os.name.rfind("Windows 10", 0) == 0) os.name.replace(0, 10, "Windows 11");
    const auto display = readRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    if (!display.empty()) os.name += " " + display;
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    os.is64Bit = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
    return os;
}

BuildInfo queryBuild() {
    BuildInfo b;
    b.appVersion = version::string;
    b.buildType = version::buildType;
#if defined(_MSC_FULL_VER)
    b.compiler = std::format("MSVC {}.{}.{}", _MSC_FULL_VER / 10000000, (_MSC_FULL_VER / 100000) % 100, _MSC_FULL_VER % 100000);
#else
    b.compiler = "unknown";
#endif
    auto ver = [](unsigned v) { return std::format("{}.{}.{}", AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v)); };
    b.ffmpegVersion = std::format("avcodec {} / avformat {} / avutil {}", ver(avcodec_version()), ver(avformat_version()), ver(avutil_version()));
    b.ffmpegLicense = avcodec_license();
    b.ffmpegConfiguration = avcodec_configuration();
    return b;
}

SystemReport querySystem() { return SystemReport{queryCpu(), queryMemory(), queryOs(), queryBuild()}; }

}  // namespace lf::diag
