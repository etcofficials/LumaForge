#include "lumaforge/hardware/HardwareDetector.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"
#include "lumaforge/hardware/GpuDevice.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

#include <chrono>

namespace lf::hw {

namespace {
constexpr const char* kLog = "hw";

struct ProfileEntry {
    const char* codec;
    const char* name;
    const GUID& guid;
};

// D3D11 video decoder profiles we care about for an editor. Order matters for display.
const ProfileEntry kProfiles[] = {
    {"H.264", "D3D11_DECODER_PROFILE_H264_VLD_NOFGT", D3D11_DECODER_PROFILE_H264_VLD_NOFGT},
    {"HEVC Main", "D3D11_DECODER_PROFILE_HEVC_VLD_MAIN", D3D11_DECODER_PROFILE_HEVC_VLD_MAIN},
    {"HEVC Main10", "D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10", D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10},
    {"VP9", "D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0", D3D11_DECODER_PROFILE_VP9_VLD_PROFILE0},
    {"VP9 10-bit", "D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2", D3D11_DECODER_PROFILE_VP9_VLD_10BIT_PROFILE2},
    {"AV1", "D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0", D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0},
    {"MPEG-2", "D3D11_DECODER_PROFILE_MPEG2_VLD", D3D11_DECODER_PROFILE_MPEG2_VLD},
    {"VC-1", "D3D11_DECODER_PROFILE_VC1_D2010", D3D11_DECODER_PROFILE_VC1_D2010},
};

std::string formatDriverVersion(LARGE_INTEGER v) {
    return std::format("{}.{}.{}.{}", HIWORD(v.HighPart), LOWORD(v.HighPart), HIWORD(v.LowPart), LOWORD(v.LowPart));
}
}  // namespace

std::vector<AdapterInfo> HardwareDetector::enumerateAdapters() {
    std::vector<AdapterInfo> out;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return out;

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i, adapter.Reset()) {
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        AdapterInfo info;
        info.name = wideToUtf8(desc.Description);
        info.vendorId = desc.VendorId;
        info.vendor = vendorFromId(desc.VendorId);
        info.deviceId = desc.DeviceId;
        info.subSysId = desc.SubSysId;
        info.revision = desc.Revision;
        info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
        info.dedicatedSystemMemory = desc.DedicatedSystemMemory;
        info.sharedSystemMemory = desc.SharedSystemMemory;
        info.isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        info.luid = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart;
        LARGE_INTEGER umd{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) info.driverVersion = formatDriverVersion(umd);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<DecoderProfileSupport> HardwareDetector::queryDecoderProfiles(const GpuDevice& device) {
    std::vector<DecoderProfileSupport> out;
    auto* video = device.videoDevice();
    for (const auto& p : kProfiles) {
        DecoderProfileSupport s{p.codec, p.name, false};
        if (video) {
            BOOL supported = FALSE;
            // NV12 is the output format every practical decoder pipeline uses; Main10 profiles use P010.
            const DXGI_FORMAT fmt = (std::string_view(p.codec).find("10") != std::string_view::npos) ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
            if (SUCCEEDED(video->CheckVideoDecoderFormat(&p.guid, fmt, &supported))) s.supported = supported == TRUE;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::string> HardwareDetector::ffmpegHwDeviceTypes() {
    std::vector<std::string> out;
    for (auto t = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE); t != AV_HWDEVICE_TYPE_NONE; t = av_hwdevice_iterate_types(t)) {
        out.emplace_back(av_hwdevice_get_type_name(t));
    }
    return out;
}

HwReport HardwareDetector::detect(const Options& options, const std::shared_ptr<GpuDevice>& existingDevice) {
    const auto t0 = std::chrono::steady_clock::now();
    HwReport report;
    report.adapters = enumerateAdapters();

    std::shared_ptr<GpuDevice> device = existingDevice;
    if (!device) {
        auto created = GpuDevice::create({.adapterIndex = options.adapterIndex});
        if (created) device = std::move(created.value());
        else report.d3d11Error = created.error().fullText();
    }

    if (device) {
        report.d3d11Available = true;
        report.d3d11FeatureLevel = device->featureLevelString();
        report.d3d11VideoDeviceAvailable = device->videoDevice() != nullptr;
        report.decoderProfiles = queryDecoderProfiles(*device);
        if (auto* adapter = device->adapter()) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            for (std::size_t i = 0; i < report.adapters.size(); ++i) {
                if (report.adapters[i].luid == ((static_cast<std::uint64_t>(static_cast<std::uint32_t>(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart)) {
                    report.activeAdapterIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    report.ffmpegHwDeviceTypes = ffmpegHwDeviceTypes();
    if (options.probeEncoders) report.encoders = probeH264Encoders();

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    LF_LOG_INFO(kLog, "hardware detection finished in {} ms: {}", ms, summary(report));
    for (const auto& a : report.adapters) {
        LF_LOG_INFO(kLog, "  adapter: {} [{}] vendor=0x{:04X} device=0x{:04X} rev={} vram={} driver={}{}", a.name, vendorName(a.vendor),
                    a.vendorId, a.deviceId, a.revision, formatBytes(a.dedicatedVideoMemory), a.driverVersion, a.isSoftware ? " (software)" : "");
    }
    for (const auto& p : report.decoderProfiles) LF_LOG_DEBUG(kLog, "  decoder {:<12} {}", p.codec, p.supported ? "yes" : "no");
    for (const auto& e : report.encoders) LF_LOG_INFO(kLog, "  encoder {:<12} {}{}", e.name, e.available ? "available" : "unavailable", e.detail.empty() ? "" : " - " + e.detail);
    return report;
}

std::string HardwareDetector::summary(const HwReport& r) {
    std::string s;
    if (const auto* a = r.activeAdapter()) s += a->name;
    else s += "No GPU";
    s += r.d3d11Available ? std::format(" | D3D11 {}", r.d3d11FeatureLevel) : " | D3D11 unavailable";
    s += r.supportsDecoder("H.264") ? " | HW decode: H.264" : " | HW decode: none";
    if (const auto* hw = r.firstAvailableEncoder(EncoderKind::Hardware)) s += " | HW encode: " + hw->displayName;
    else if (const auto* sw = r.firstAvailableEncoder(EncoderKind::Software)) s += " | Encode: " + sw->displayName;
    else s += " | Encode: none";
    return s;
}

}  // namespace lf::hw
