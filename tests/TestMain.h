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
int main() {
    int failedCases = 0;
    for (const auto& c : ::lftest::cases()) {
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
