// Subtitles: SRT / WebVTT parsing and writing, the subtitle track, import as
// one undo step, cues for a range, and the export sidecar.
#include "TestMain.h"

#include "lumaforge/export/Exporter.h"
#include "lumaforge/hardware/GpuDevice.h"
#include "lumaforge/project/Document.h"
#include "lumaforge/project/ProjectSerializer.h"
#include "lumaforge/project/Subtitles.h"
#include "lumaforge/project/TimelineCommands.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lf;
using namespace lf::project;

namespace {
TimePoint ms(long long v) { return TimePoint{v * (kFlicksPerSecond / 1000)}; }
}

TEST_CASE("subtitles: SRT parsing (CRLF, BOM, tags, multi-line, out of order) and writing") {
    const std::string srt =
        "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello <i>world</i>\r\n\r\n"
        "3\r\n00:00:05,000 --> 00:00:06,000\r\nLast\r\n\r\n"
        "2\r\n00:00:03,250 --> 00:00:04,000\r\nTwo\r\nlines\r\n\r\n"
        "4\r\n00:00:07,000 --> 00:00:06,000\r\nBackwards (dropped)\r\n";
    auto cues = parseSubtitles(srt);
    CHECK(static_cast<bool>(cues));
    if (!cues) return;
    CHECK(cues->size() == 3);
    CHECK((*cues)[0].start == ms(1000) && (*cues)[0].end == ms(2500) && (*cues)[0].text == "Hello world");
    CHECK((*cues)[1].text == "Two\nlines" && (*cues)[1].start == ms(3250));
    CHECK((*cues)[2].text == "Last");
    const std::string written = writeSrt(*cues);
    CHECK(written.rfind("1\n00:00:01,000 --> 00:00:02,500\nHello world\n\n", 0) == 0);
    auto again = parseSubtitles(written);
    CHECK(again && *again == *cues);
    CHECK(!parseSubtitles("just some text\nno cues here"));
}

TEST_CASE("subtitles: WebVTT parsing (header, notes, cue settings, short timestamps) and writing") {
    const std::string vtt =
        "WEBVTT - demo\nKind: captions\n\n"
        "NOTE a comment\nspanning lines\n\n"
        "intro\n00:01.000 --> 00:02.000 align:start line:90%\nFirst\n\n"
        "01:00:00.500 --> 01:00:01.000\n<v Speaker>Second</v>\n";
    auto cues = parseSubtitles(vtt);
    CHECK(static_cast<bool>(cues));
    if (!cues) return;
    CHECK(cues->size() == 2);
    CHECK((*cues)[0].start == ms(1000) && (*cues)[0].end == ms(2000) && (*cues)[0].text == "First");
    CHECK((*cues)[1].start == ms(3600500) && (*cues)[1].text == "Second");
    const std::string out = writeVtt(*cues);
    CHECK(out.rfind("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nFirst\n", 0) == 0);
    CHECK(parseSubtitles(out) && *parseSubtitles(out) == *cues);
}

TEST_CASE("subtitles: import creates the track and clips in one undo step; add uses the same track") {
    Document doc;
    const auto& seq = doc.project().sequence;
    const std::size_t tracksBefore = seq.tracks.size();
    std::vector<SubtitleCue> cues{{ms(0), ms(1500), "A"}, {ms(1400), ms(3000), "B"}};   // overlapping by 100 ms
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Import", importSubtitles(doc, cues)));
    CHECK(seq.tracks.size() == tracksBefore + 1);
    const Track* st = subtitleTrack(seq);
    CHECK(st && st->subtitles && st->name == "Subtitles" && st->id == seq.tracks.front().id);   // at the top
    CHECK(seq.clips.size() == 2);
    const auto back = subtitleCues(seq);
    CHECK(back.size() == 2 && back[0].end == ms(1400) && back[1].text == "B");   // overlap trimmed
    for (const auto& c : seq.clips) CHECK(c.kind == ClipKind::Text && c.text.outlineWidth > 0.0f);   // the Subtitle look

    doc.push(std::make_unique<SequenceEditCommand>(doc, "Add", addSubtitle(doc, ms(5000))));
    CHECK(seq.tracks.size() == tracksBefore + 1 && seq.clips.size() == 3);
    const auto ranged = subtitleCues(seq, TimeRange{ms(1000), Duration{ms(5000).ticks}});
    CHECK(ranged.size() == 3 && ranged[0].start == ms(0) && ranged[0].end == ms(400) && ranged[2].start == ms(4000) && ranged[2].end == ms(5000));

    doc.undoStack().undo();
    doc.undoStack().undo();
    CHECK(seq.tracks.size() == tracksBefore && seq.clips.empty());

    // The flag survives a save / load.
    doc.undoStack().redo();
    auto loaded = serializer::fromJson(serializer::toJson(doc.project(), ""), "");
    CHECK(loaded && subtitleTrack(loaded->project.sequence) != nullptr);
}

TEST_CASE("subtitles: export writes an .srt sidecar for the range") {
    auto gpu = hw::GpuDevice::create({});
    Document doc;
    doc.push(std::make_unique<SequenceEditCommand>(doc, "Import", importSubtitles(doc, {{ms(500), ms(1500), "Hi"}, {ms(2000), ms(2600), "Bye"}})));
    Project p = doc.project();
    p.settings.width = 320;
    p.settings.height = 180;
    const std::string dir = LF_TEST_TMP_DIR "/lf_export_tests";
    std::filesystem::create_directories(dir);
    exporting::ExportSettings s;
    s.outputPath = dir + "/subs.mp4";
    s.speedPreset = "ultrafast";
    s.subtitleSidecar = true;
    s.burnSubtitles = false;
    s.rangeStart = ms(1000);
    std::filesystem::remove(dir + "/subs.srt");
    exporting::Exporter exporter(gpu ? std::move(gpu.value()) : nullptr);
    std::atomic<bool> no{false};
    auto r = exporter.run(p, s, {}, no);
    CHECK(static_cast<bool>(r));
    std::ifstream in(dir + "/subs.srt", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    auto cues = parseSubtitles(text.str());
    CHECK(cues && cues->size() == 2 && (*cues)[0].start == ms(0) && (*cues)[0].end == ms(500) && (*cues)[1].start == ms(1000));
}
