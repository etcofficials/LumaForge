// Toolchain check: plain Win32 C++20 executable (cl + link + Windows SDK headers/libs).
#include <windows.h>
#include <cstdio>
#include <string>
#include <format>

int main() {
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    MEMORYSTATUSEX ms{ sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    std::string arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "other";
    std::printf("%s\n", std::format("hello from MSVC {} | arch={} | cpus={} | ram={} MB | tick={}",
                                    _MSC_FULL_VER, arch, si.dwNumberOfProcessors,
                                    ms.ullTotalPhys / (1024 * 1024), GetTickCount64()).c_str());
    return 0;
}
