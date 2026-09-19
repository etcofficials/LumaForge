#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lf::hw {

enum class Vendor { Unknown, Nvidia, Amd, Intel, Microsoft, Qualcomm };

[[nodiscard]] constexpr Vendor vendorFromId(std::uint32_t vendorId) noexcept {
    switch (vendorId) {
        case 0x10DE: return Vendor::Nvidia;
        case 0x1002: return Vendor::Amd;
        case 0x8086: return Vendor::Intel;
        case 0x1414: return Vendor::Microsoft;
        case 0x4D4F4351: return Vendor::Qualcomm;
        default: return Vendor::Unknown;
    }
}
[[nodiscard]] constexpr const char* vendorName(Vendor v) noexcept {
    switch (v) {
        case Vendor::Nvidia: return "NVIDIA";
        case Vendor::Amd: return "AMD";
        case Vendor::Intel: return "Intel";
        case Vendor::Microsoft: return "Microsoft";
        case Vendor::Qualcomm: return "Qualcomm";
        case Vendor::Unknown: return "Unknown";
    }
    return "Unknown";
}

struct AdapterInfo {
    std::string name;
    Vendor vendor = Vendor::Unknown;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t subSysId = 0;
    std::uint32_t revision = 0;
    std::uint64_t dedicatedVideoMemory = 0;
    std::uint64_t dedicatedSystemMemory = 0;
    std::uint64_t sharedSystemMemory = 0;
    std::string driverVersion;      // user-mode driver version, e.g. "23.21.13.9135"
    bool isSoftware = false;        // WARP / Basic Render Driver
    std::uint64_t luid = 0;
};

// One D3D11 video decoder profile check, e.g. "H.264" -> supported/not.
struct DecoderProfileSupport {
    std::string codec;      // "H.264", "HEVC Main", "HEVC Main10", "VP9", "AV1", "MPEG-2", "VC-1"
    std::string profile;    // GUID name, e.g. "D3D11_DECODER_PROFILE_H264_VLD_NOFGT"
    bool supported = false;
};

enum class EncoderKind { Hardware, Software };

// Result of actually trying to open an FFmpeg encoder. "compiled in" is not
// the same as "works on this machine": h264_nvenc exists in the FFmpeg build
// on every PC, but only opens successfully with a capable GPU + driver.
struct EncoderProbe {
    std::string name;           // FFmpeg codec name, e.g. "h264_nvenc"
    std::string displayName;    // "NVIDIA NVENC (H.264)"
    EncoderKind kind = EncoderKind::Software;
    bool available = false;
    std::string detail;         // error text when unavailable
};

struct HwReport {
    // Adapters as seen by DXGI, in enumeration order (index 0 = primary).
    std::vector<AdapterInfo> adapters;
    int activeAdapterIndex = -1;

    // Direct3D 11 device on the active adapter.
    bool d3d11Available = false;
    std::string d3d11FeatureLevel;      // "11_0"
    std::string d3d11Error;             // when !d3d11Available
    bool d3d11VideoDeviceAvailable = false;
    std::vector<DecoderProfileSupport> decoderProfiles;

    // FFmpeg-side facts.
    std::vector<std::string> ffmpegHwDeviceTypes;   // compiled-in hwaccel device types
    std::vector<EncoderProbe> encoders;             // probed H.264 encoders, hardware first

    // Convenience accessors.
    [[nodiscard]] bool supportsDecoder(std::string_view codec) const noexcept {
        for (const auto& p : decoderProfiles)
            if (p.supported && p.codec == codec) return true;
        return false;
    }
    [[nodiscard]] const EncoderProbe* firstAvailableEncoder(EncoderKind kind) const noexcept {
        for (const auto& e : encoders)
            if (e.available && e.kind == kind) return &e;
        return nullptr;
    }
    [[nodiscard]] const AdapterInfo* activeAdapter() const noexcept {
        return activeAdapterIndex >= 0 && activeAdapterIndex < static_cast<int>(adapters.size()) ? &adapters[activeAdapterIndex] : nullptr;
    }
};

}  // namespace lf::hw
