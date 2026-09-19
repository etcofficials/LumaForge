#pragma once

#include <cstdint>
#include <functional>

namespace lf::project {

// Stable identifiers for document objects. They are allocated once per
// document, never reused, and survive save/load so that undo history, UI
// selection and cache keys can refer to objects without pointers.
enum class MediaId : std::uint64_t { Invalid = 0 };
enum class ClipId : std::uint64_t { Invalid = 0 };
enum class TrackId : std::uint64_t { Invalid = 0 };

template <typename Id>
[[nodiscard]] constexpr bool isValid(Id id) noexcept { return id != Id::Invalid; }
template <typename Id>
[[nodiscard]] constexpr std::uint64_t raw(Id id) noexcept { return static_cast<std::uint64_t>(id); }

}  // namespace lf::project

template <> struct std::hash<lf::project::MediaId> { std::size_t operator()(lf::project::MediaId id) const noexcept { return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(id)); } };
template <> struct std::hash<lf::project::ClipId>  { std::size_t operator()(lf::project::ClipId id)  const noexcept { return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(id)); } };
template <> struct std::hash<lf::project::TrackId> { std::size_t operator()(lf::project::TrackId id) const noexcept { return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(id)); } };
