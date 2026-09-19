#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace lf {

// Error carries a user-presentable message plus optional technical detail
// (FFmpeg/HRESULT text) that belongs in the log and the diagnostics view, not
// in a dialog. Keeping the two apart is what lets the UI stay free of
// "meaningless technical errors" while diagnostics stay useful.
struct Error {
    std::string message;   // e.g. "The file could not be opened."
    std::string detail;    // e.g. "avformat_open_input: Invalid data found when processing input (-1094995529)"
    int code = 0;          // native code where one exists (HRESULT, AVERROR, errno)

    [[nodiscard]] std::string fullText() const {
        return detail.empty() ? message : message + " [" + detail + "]";
    }
};

// Minimal expected-like type (we stay on C++20; std::expected is C++23).
template <typename T>
class Result {
public:
    Result(T value) : m_data(std::move(value)) {}          // NOLINT(google-explicit-constructor)
    Result(Error error) : m_data(std::move(error)) {}      // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return std::get<T>(m_data); }
    [[nodiscard]] const T& value() const& { return std::get<T>(m_data); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(m_data)); }
    [[nodiscard]] const Error& error() const { return std::get<Error>(m_data); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::variant<T, Error> m_data;
};

// Result<void>: success or an Error.
template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)) {}     // NOLINT(google-explicit-constructor)

    static Result success() { return Result{}; }

    [[nodiscard]] bool ok() const noexcept { return !m_error.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const { return *m_error; }

private:
    std::optional<Error> m_error;
};

using Status = Result<void>;

}  // namespace lf
