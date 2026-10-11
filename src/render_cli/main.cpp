// lf_render: renders a LumaForge project without the GUI.
//
// Same engine path as the Export dialog (SequenceProgram -> LayerFactory ->
// Compositor -> libx264/AAC), so a project renders identically from here and
// from the application. Used for automated/batch production and for checking
// renders frame by frame.
//
//   lf_render <project.lfproj> --info
//   lf_render <project.lfproj> --out <file.mp4> [--crf 18] [--preset medium] [--size WxH]
//             [--fps N] [--audio-kbps 256] [--start s] [--end s] [--no-audio] [--software]
//   lf_render <project.lfproj> --frames t1,t2,... --frames-dir <dir> [--size WxH]   (24-bit BMP stills)
//   lf_render <project.lfproj> --wav <file.wav> [--start s] [--end s]             (24-bit PCM final mix)
//
// Exit codes: 0 ok, 1 usage, 2 project could not be loaded or has offline media, 3 render failed.
#include "lumaforge/core/Log.h"
#include "lumaforge/export/ExportSettings.h"
#include "lumaforge/export/Exporter.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/playback/AudioMixer.h"
#include "lumaforge/playback/SequenceProgram.h"
#include "lumaforge/project/ProjectSerializer.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace lf;

std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(std::max(0, n - 1)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::filesystem::path toPath(const std::string& u) { return std::filesystem::path(std::u8string(u.begin(), u.end())); }

std::string absolute(const std::string& u) {
    std::error_code ec;
    const auto p = std::filesystem::absolute(toPath(u), ec);
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

int usage() {
    std::fputs("usage: lf_render <project.lfproj> (--info | --out <file.mp4> | --frames t1,t2,... --frames-dir <dir> | --wav <file.wav>)\n"
               "       [--crf N] [--preset P] [--size WxH] [--fps N] [--audio-kbps N] [--start s] [--end s] [--no-audio] [--software]\n",
               stderr);
    return 1;
}

bool writeBmp(const std::filesystem::path& path, const media::Image& img) {
    const int rowBytes = (img.width * 3 + 3) & ~3;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(rowBytes * img.height);
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + dataSize;
    ih.biSize = sizeof ih;
    ih.biWidth = img.width;
    ih.biHeight = img.height;   // bottom-up
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biSizeImage = dataSize;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fh), sizeof fh);
    out.write(reinterpret_cast<const char*>(&ih), sizeof ih);
    std::vector<char> row(static_cast<std::size_t>(rowBytes), 0);
    for (int y = img.height - 1; y >= 0; --y) {
        const std::uint8_t* src = img.bgra.data() + static_cast<std::size_t>(y) * img.stride();
        for (int x = 0; x < img.width; ++x) std::memcpy(&row[static_cast<std::size_t>(x) * 3], src + x * 4, 3);
        out.write(row.data(), rowBytes);
    }
    return static_cast<bool>(out);
}

// 24-bit PCM WAV of the final mix (the same AudioMixer the exporter encodes from).
int writeWav(const project::Project& project, const std::string& path, TimePoint start, TimePoint end) {
    auto program = std::make_shared<playback::SequenceProgram>(project);
    if (!program->hasAudio()) {
        std::fputs("error: the sequence has no audio\n", stderr);
        return 3;
    }
    const int rate = project.settings.audioSampleRate;
    const int channels = 2;
    playback::AudioMixer mixer(rate, channels);
    mixer.setProgram(program);
    mixer.setTime(start);
    mixer.setPlaying(true);
    const std::int64_t total = static_cast<std::int64_t>((end - start).toSeconds() * rate + 0.5);
    std::ofstream out(toPath(path), std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return 3;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(total * channels * 3);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + dataBytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(1); u16(static_cast<std::uint16_t>(channels)); u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * channels * 3)); u16(static_cast<std::uint16_t>(channels * 3)); u16(24);
    out.write("data", 4); u32(dataBytes);
    std::vector<float> block;
    std::vector<char> bytes;
    float peak = 0.0f;
    std::int64_t clipped = 0;
    for (std::int64_t done = 0; done < total;) {
        const int n = static_cast<int>(std::min<std::int64_t>(4800, total - done));
        block.assign(static_cast<std::size_t>(n) * channels, 0.0f);
        mixer.render(block.data(), n);
        bytes.resize(block.size() * 3);
        for (std::size_t i = 0; i < block.size(); ++i) {
            const float v = block[i];
            peak = std::max(peak, std::abs(v));
            if (std::abs(v) > 1.0f) ++clipped;
            const auto s = static_cast<std::int32_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 8388607.0f));
            bytes[i * 3] = static_cast<char>(s & 0xFF);
            bytes[i * 3 + 1] = static_cast<char>((s >> 8) & 0xFF);
            bytes[i * 3 + 2] = static_cast<char>((s >> 16) & 0xFF);
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        done += n;
    }
    if (!out) {
        std::fprintf(stderr, "error: writing %s failed\n", path.c_str());
        return 3;
    }
    std::printf("wav: %s | %lld samples @ %d Hz | peak %.2f dBFS | %lld samples over full scale (clamped)\n", path.c_str(), static_cast<long long>(total), rate,
                peak > 0 ? 20.0 * std::log10(peak) : -150.0, static_cast<long long>(clipped));
    return 0;
}

void printInfo(const project::Project& p) {
    const auto& seq = p.sequence;
    std::printf("project: %s | %dx%d @ %d/%d fps | %d Hz | duration %.3f s\n", p.settings.name.c_str(), p.settings.width, p.settings.height,
                static_cast<int>(p.settings.frameRate.num), static_cast<int>(p.settings.frameRate.den), p.settings.audioSampleRate, seq.duration().toSeconds());
    std::printf("media: %zu items\n", p.media.size());
    for (const auto& m : p.media) std::printf("  [%s] %s\n", m.offline ? "OFFLINE" : "ok", m.path.c_str());
    for (const auto& t : seq.tracks) {
        int n = 0;
        for (const auto& c : seq.clips) n += c.track == t.id ? 1 : 0;
        std::printf("track %s (%s): %d clips%s\n", t.name.c_str(), t.kind == project::TrackKind::Video ? "video" : "audio", n, t.muted ? " [muted]" : "");
    }
    std::printf("markers: %zu\n", seq.markers.size());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(utf8(argv[i]));
    if (args.empty()) return usage();

    std::string projectPath, out, framesDir, wav, framesList, preset = "medium";
    int crf = 18, width = 0, height = 0, audioKbps = 256, fps = 0;
    double start = 0.0, end = -1.0;
    bool info = false, noAudio = false, software = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string{}; };
        if (a == "--info") info = true;
        else if (a == "--out") out = next();
        else if (a == "--frames") framesList = next();
        else if (a == "--frames-dir") framesDir = next();
        else if (a == "--wav") wav = next();
        else if (a == "--crf") crf = std::stoi(next());
        else if (a == "--preset") preset = next();
        else if (a == "--fps") fps = std::stoi(next());
        else if (a == "--audio-kbps") audioKbps = std::stoi(next());
        else if (a == "--start") start = std::stod(next());
        else if (a == "--end") end = std::stod(next());
        else if (a == "--no-audio") noAudio = true;
        else if (a == "--software") software = true;
        else if (a == "--size") {
            const std::string v = next();
            if (std::sscanf(v.c_str(), "%dx%d", &width, &height) != 2) return usage();
        } else if (!a.starts_with("--") && projectPath.empty()) projectPath = a;
        else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return usage();
        }
    }
    if (projectPath.empty() || (!info && out.empty() && framesList.empty() && wav.empty())) return usage();

    auto& logger = log::Logger::instance();
    logger.setMinimumLevel(log::Level::Info);
    const auto logPath = toPath(absolute(projectPath)).parent_path() / "lf_render.log";
    auto sink = std::make_shared<log::FileSink>(logPath.wstring());
    if (sink->isOpen()) logger.addSink(sink);

    auto loaded = project::serializer::loadFromFile(absolute(projectPath));
    if (!loaded) {
        std::fprintf(stderr, "error: %s\n", loaded.error().fullText().c_str());
        return 2;
    }
    project::Project project = std::move(loaded->project);
    if (info) printInfo(project);
    if (!loaded->offlineMedia.empty()) {
        for (const auto& m : loaded->offlineMedia) std::fprintf(stderr, "offline media: %s\n", m.c_str());
        return 2;
    }

    const TimePoint rangeStart = TimePoint::fromSeconds(start);
    const TimePoint rangeEnd = end > 0 ? TimePoint::fromSeconds(end) : TimePoint{project.sequence.duration().ticks};

    std::shared_ptr<hw::GpuDevice> gpu;
    if (!software && (!out.empty() || !framesList.empty())) {
        if (auto created = hw::GpuDevice::create({})) {
            gpu = std::move(created.value());
            std::printf("gpu: %s%s\n", gpu->description().c_str(), gpu->isWarp() ? " (WARP)" : "");
        } else {
            std::printf("gpu: unavailable (%s); rendering with WARP\n", created.error().fullText().c_str());
        }
    }

    int rc = 0;
    if (!wav.empty()) rc = std::max(rc, writeWav(project, absolute(wav), rangeStart, rangeEnd));

    if (!framesList.empty()) {
        if (framesDir.empty()) return usage();
        std::error_code ec;
        std::filesystem::create_directories(toPath(framesDir), ec);
        exporting::Exporter exporter(gpu);
        std::stringstream ss(framesList);
        for (std::string item; std::getline(ss, item, ',');) {
            if (item.empty()) continue;
            const double t = std::stod(item);
            auto img = exporter.renderFrame(project, TimePoint::fromSeconds(t), width, height);
            if (!img) {
                std::fprintf(stderr, "error: frame %.3f: %s\n", t, img.error().fullText().c_str());
                rc = 3;
                continue;
            }
            char name[64];
            std::snprintf(name, sizeof name, "frame_%07.3f.bmp", t);
            if (!writeBmp(toPath(framesDir) / name, *img)) rc = 3;
        }
        std::printf("frames: written to %s\n", framesDir.c_str());
    }

    if (!out.empty()) {
        exporting::ExportSettings s;
        s.outputPath = absolute(out);
        s.crf = crf;
        s.speedPreset = preset;
        s.width = width;
        s.height = height;
        s.audioKbps = audioKbps;
        s.includeAudio = !noAudio;
        s.rangeStart = rangeStart;
        s.rangeEnd = rangeEnd;
        if (fps > 0) s.frameRate = Rational{fps, 1};
        exporting::Exporter exporter(gpu);
        std::atomic<bool> cancel{false};
        int lastPct = -1;
        auto result = exporter.run(project, s, [&](const exporting::ExportProgress& p) {
            const int pct = static_cast<int>(p.fraction() * 100.0);
            if (pct / 5 != lastPct / 5) {
                std::printf("  %3d%%  frame %lld/%lld  %.1f fps\n", pct, static_cast<long long>(p.frame), static_cast<long long>(p.totalFrames), p.framesPerSecond);
                std::fflush(stdout);
                lastPct = pct;
            }
        }, cancel);
        if (!result) {
            std::fprintf(stderr, "error: %s\n", result.error().fullText().c_str());
            return 3;
        }
        std::printf("export: %s | %dx%d | %lld frames in %.1f s | %s + %s | %llu bytes%s%s\n", result->path.c_str(), result->width, result->height,
                    static_cast<long long>(result->frames), result->seconds, result->videoEncoder.c_str(),
                    result->audioEncoder.empty() ? "no audio" : result->audioEncoder.c_str(), static_cast<unsigned long long>(result->bytes),
                    result->note.empty() ? "" : " | ", result->note.c_str());
    }
    log::Logger::instance().flush();
    return rc;
}
