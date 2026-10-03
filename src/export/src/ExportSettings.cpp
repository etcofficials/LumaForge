#include "lumaforge/export/ExportSettings.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace lf::exporting {

namespace {
constexpr ExportPreset kPresets[] = {
    {"h264-high",   "H.264 – High Quality",     "Project size, near-transparent quality. Good master for sharing.",       Container::Mp4, VideoCodec::H264, 0,    RateControl::Quality, 18, 0,    "medium",   320},
    {"youtube1080", "YouTube 1080p",            "1920×1080 H.264 + AAC, quality tuned for upload.",                        Container::Mp4, VideoCodec::H264, 1080, RateControl::Quality, 20, 0,    "medium",   192},
    {"youtube720",  "YouTube 720p",             "1280×720, smaller file, faster to upload.",                               Container::Mp4, VideoCodec::H264, 720,  RateControl::Quality, 21, 0,    "medium",   160},
    {"web-small",   "Small Web Video (480p)",   "854×480, about 1.5 Mbit/s — messages and email.",                          Container::Mp4, VideoCodec::H264, 480,  RateControl::Bitrate, 0,  1500, "medium",   128},
    {"fast-draft",  "Fast Draft",               "Project size, very fast encode, larger file — for quick reviews.",        Container::Mp4, VideoCodec::H264, 0,    RateControl::Quality, 23, 0,    "veryfast", 160},
    {"hevc",        "HEVC (H.265) – Smaller",   "About half the size of H.264 at similar quality; slower to encode, needs a newer player.", Container::Mp4, VideoCodec::Hevc, 0, RateControl::Quality, 24, 0, "medium", 192},
    {"mkv-archive", "MKV Archive",              "Project size, H.264 high quality in Matroska.",                           Container::Mkv, VideoCodec::H264, 0,    RateControl::Quality, 16, 0,    "slow",     320},
};

std::filesystem::path toPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
}  // namespace

std::span<const ExportPreset> exportPresets() noexcept { return kPresets; }

const char* containerExtension(Container c) noexcept {
    switch (c) {
        case Container::Mov: return ".mov";
        case Container::Mkv: return ".mkv";
        default: return ".mp4";
    }
}

const char* containerName(Container c) noexcept {
    switch (c) {
        case Container::Mov: return "QuickTime (MOV)";
        case Container::Mkv: return "Matroska (MKV)";
        default: return "MP4";
    }
}

const char* defaultEncoder(VideoCodec c) noexcept { return c == VideoCodec::Hevc ? "libx265" : "libx264"; }

void applyPreset(ExportSettings& s, const ExportPreset& p, const project::Project& project) {
    s.container = p.container;
    s.codec = p.codec;
    s.encoder.clear();
    s.rateControl = p.rateControl;
    if (p.crf > 0) s.crf = p.crf;
    if (p.videoKbps > 0) s.videoKbps = p.videoKbps;
    s.speedPreset = p.speedPreset;
    s.audioKbps = p.audioKbps;
    if (p.height <= 0 || p.height >= project.settings.height) {
        s.width = s.height = 0;
    } else {
        s.height = p.height;
        // Nearest even width for the aspect (4:2:0 needs even sizes): 16:9 at 480 -> 854.
        s.width = 2 * static_cast<int>(std::lround(static_cast<double>(project.settings.width) * p.height / project.settings.height / 2.0));
    }
    // Keep the file name, switch the extension.
    if (!s.outputPath.empty()) {
        auto path = toPath(s.outputPath);
        path.replace_extension(containerExtension(s.container));
        const auto u8 = path.u8string();
        s.outputPath.assign(u8.begin(), u8.end());
    }
}

void outputSize(const ExportSettings& s, const project::Project& project, int& w, int& h) {
    w = s.width > 0 ? s.width : project.settings.width;
    h = s.height > 0 ? s.height : project.settings.height;
    if (s.width > 0 && s.height <= 0) h = 2 * static_cast<int>(std::lround(static_cast<double>(project.settings.height) * w / project.settings.width / 2.0));
    w = std::max(16, w & ~1);
    h = std::max(16, h & ~1);
}

TimeRange exportRange(const ExportSettings& s, const project::Project& project) {
    const Ticks end = project.sequence.duration().ticks;
    const Ticks a = std::clamp<Ticks>(s.rangeStart.ticks, 0, end);
    const Ticks b = s.rangeEnd.ticks < 0 ? end : std::clamp<Ticks>(s.rangeEnd.ticks, a, end);
    return TimeRange{TimePoint{a}, Duration{b - a}};
}

std::int64_t frameCount(const ExportSettings& s, const project::Project& project) {
    const TimeRange r = exportRange(s, project);
    const Ticks fd = frameDuration(project.settings.frameRate).ticks;
    return fd > 0 ? (r.duration.ticks + fd - 1) / fd : 0;
}

std::uint64_t estimatedBytes(const ExportSettings& s, const project::Project& project) {
    const double seconds = exportRange(s, project).duration.toSeconds();
    int w = 0, h = 0;
    outputSize(s, project, w, h);
    double videoKbps = s.videoKbps;
    if (s.rateControl == RateControl::Quality) {
        // Empirical: ~0.1 bit per pixel per frame at CRF 20 for typical footage, x2 per -6 CRF.
        const double fps = project.settings.frameRate.toDouble();
        const double bpp = 0.1 * std::pow(2.0, (20.0 - s.crf) / 6.0) * (s.codec == VideoCodec::Hevc ? 0.6 : 1.0);
        videoKbps = w * h * fps * bpp / 1000.0;
    }
    const double audio = s.includeAudio ? s.audioKbps : 0;
    return static_cast<std::uint64_t>((videoKbps + audio) * 1000.0 / 8.0 * seconds);
}

std::vector<std::string> validate(const ExportSettings& s, const project::Project& project) {
    std::vector<std::string> problems;
    if (project.sequence.clips.empty()) problems.push_back("The timeline is empty: add some clips first.");
    else if (exportRange(s, project).duration.ticks <= 0) problems.push_back("The export range is empty.");
    if (s.outputPath.empty()) {
        problems.push_back("Choose where to save the video.");
    } else {
        const auto path = toPath(s.outputPath);
        std::error_code ec;
        if (!path.has_filename()) problems.push_back("The output file name is missing.");
        else if (!std::filesystem::is_directory(path.parent_path(), ec)) problems.push_back("The output folder does not exist.");
        const auto ext = path.extension().u8string();
        std::string lower(ext.begin(), ext.end());
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower != containerExtension(s.container)) problems.push_back(std::string("The file name should end in ") + containerExtension(s.container) + ".");
    }
    int w = 0, h = 0;
    outputSize(s, project, w, h);
    if (w > 8192 || h > 8192) problems.push_back("The output size is larger than 8192 pixels.");
    if (s.rateControl == RateControl::Bitrate && (s.videoKbps < 100 || s.videoKbps > 200000)) problems.push_back("The video bitrate must be between 100 and 200000 kbit/s.");
    if (s.crf < 0 || s.crf > 51) problems.push_back("Quality must be between 0 and 51.");
    return problems;
}

std::vector<std::string> offlineMediaInRange(const ExportSettings& s, const project::Project& project) {
    const TimeRange r = exportRange(s, project);
    std::vector<std::string> names;
    for (const auto& c : project.sequence.clips) {
        if (project::isGeneratedClip(c.kind) || !c.enabled) continue;
        if (!(c.start < r.end() && r.start < c.end())) continue;
        const auto* m = project.findMedia(c.media);
        if (!m || !m->offline) continue;
        const std::string name = m->displayName.empty() ? m->path : m->displayName;
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

}  // namespace lf::exporting
