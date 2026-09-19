#pragma once

#include <compare>
#include <cstdint>
#include <numeric>

namespace lf {

// Exact ratio of two 64-bit integers, always stored in lowest terms with a
// positive denominator. Used for frame rates (30000/1001), sample rates and
// time bases. Never use floating point for timeline math.
struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;

    constexpr Rational() = default;
    constexpr Rational(std::int64_t n, std::int64_t d) : num(n), den(d) { normalize(); }

    [[nodiscard]] constexpr bool isValid() const noexcept { return den != 0; }
    [[nodiscard]] constexpr bool isZero() const noexcept { return num == 0; }
    [[nodiscard]] constexpr double toDouble() const noexcept { return den ? static_cast<double>(num) / static_cast<double>(den) : 0.0; }
    [[nodiscard]] constexpr Rational inverse() const noexcept { return Rational(den, num); }

    friend constexpr bool operator==(const Rational& a, const Rational& b) noexcept { return a.num == b.num && a.den == b.den; }
    friend constexpr std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept {
        // Cross-multiply in 128-bit-safe fashion for the magnitudes we use (frame rates, time bases).
        const auto lhs = static_cast<long double>(a.num) * static_cast<long double>(b.den);
        const auto rhs = static_cast<long double>(b.num) * static_cast<long double>(a.den);
        if (lhs < rhs) return std::strong_ordering::less;
        if (lhs > rhs) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }
    friend constexpr Rational operator*(const Rational& a, const Rational& b) noexcept { return Rational(a.num * b.num, a.den * b.den); }
    friend constexpr Rational operator/(const Rational& a, const Rational& b) noexcept { return Rational(a.num * b.den, a.den * b.num); }

private:
    constexpr void normalize() noexcept {
        if (den < 0) { num = -num; den = -den; }
        if (den == 0) { num = 0; return; }
        const auto g = std::gcd(num < 0 ? -num : num, den);
        if (g > 1) { num /= g; den /= g; }
    }
};

// Common frame rates, exact.
namespace fps {
inline constexpr Rational f23_976{24000, 1001};
inline constexpr Rational f24{24, 1};
inline constexpr Rational f25{25, 1};
inline constexpr Rational f29_97{30000, 1001};
inline constexpr Rational f30{30, 1};
inline constexpr Rational f50{50, 1};
inline constexpr Rational f59_94{60000, 1001};
inline constexpr Rational f60{60, 1};
}  // namespace fps

}  // namespace lf
