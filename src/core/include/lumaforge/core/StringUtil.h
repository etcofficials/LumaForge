#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lf {

[[nodiscard]] std::wstring utf8ToWide(std::string_view utf8);
[[nodiscard]] std::string wideToUtf8(std::wstring_view wide);

// Human-readable byte size: "1.5 GB", "312 MB".
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

// Text for a Windows HRESULT, e.g. "DXGI_ERROR_DEVICE_REMOVED (0x887A0005)".
[[nodiscard]] std::string hresultToString(long hr);

}  // namespace lf
