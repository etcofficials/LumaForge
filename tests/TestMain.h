#pragma once

// Minimal test harness: TEST_CASE(name) { CHECK(expr); } — one main per test binary.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace lftest {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { cases().push_back({name, std::move(body)}); }
};

inline void check(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        ++failures();
        std::printf("  FAILED: %s  (%s:%d)\n", expr, file, line);
    }
}

}  // namespace lftest

#define LF_CONCAT2(a, b) a##b
#define LF_CONCAT(a, b) LF_CONCAT2(a, b)
#define TEST_CASE(name)                                                          \
    static void LF_CONCAT(lf_test_fn_, __LINE__)();                              \
    static ::lftest::Registrar LF_CONCAT(lf_test_reg_, __LINE__)(name, &LF_CONCAT(lf_test_fn_, __LINE__)); \
    static void LF_CONCAT(lf_test_fn_, __LINE__)()

#define CHECK(expr) ::lftest::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#ifdef LF_TEST_MAIN
#include <cstring>
#include <cstdlib>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
// Usage: lf_tests [--filter <substring>] [--list]
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // progress is visible even when piped or killed
#ifdef _MSC_VER
    // Debug-CRT assertions must fail the run, never block it behind a dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    const char* filter = nullptr;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else if (std::strcmp(argv[i], "--list") == 0) list = true;
    }
    int failedCases = 0;
    for (const auto& c : ::lftest::cases()) {
        if (filter && std::strstr(c.name, filter) == nullptr) continue;
        if (list) { std::printf("%s\n", c.name); continue; }
        std::printf("[ .. ] %s\r", c.name);
        const int before = ::lftest::failures();
        c.body();
        const bool ok = ::lftest::failures() == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
        if (!ok) ++failedCases;
    }
    std::printf("%zu test cases, %d failed\n", ::lftest::cases().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
#endif
