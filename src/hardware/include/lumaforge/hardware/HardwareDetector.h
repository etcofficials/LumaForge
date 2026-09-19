#pragma once

#include "lumaforge/hardware/HwCapabilities.h"

#include <memory>

namespace lf::hw {

class GpuDevice;

// Builds an HwReport from what the machine actually supports. Every fact in
// the report comes from a query or an attempt (device creation, decoder profile
// check, encoder open) — nothing is inferred from adapter names.
//
// detect() can take a few hundred milliseconds (encoder probes load vendor
// DLLs); call it from a worker thread, never the UI thread.
class HardwareDetector {
public:
    struct Options {
        bool probeEncoders = true;   // try to open each known H.264 encoder
        int adapterIndex = -1;
    };

    // Full detection. If a GpuDevice is supplied it is used for the D3D11
    // queries; otherwise a temporary device is created and discarded.
    static HwReport detect(const Options& options = {}, const std::shared_ptr<GpuDevice>& existingDevice = nullptr);

    static std::vector<AdapterInfo> enumerateAdapters();
    static std::vector<DecoderProfileSupport> queryDecoderProfiles(const GpuDevice& device);
    static std::vector<std::string> ffmpegHwDeviceTypes();
    static std::vector<EncoderProbe> probeH264Encoders();

    // One-line human summary for the status bar / diagnostics header.
    static std::string summary(const HwReport& report);
};

}  // namespace lf::hw
