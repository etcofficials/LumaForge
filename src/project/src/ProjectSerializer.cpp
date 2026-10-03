#include "lumaforge/project/ProjectSerializer.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"
#include "lumaforge/core/Version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace lf::project::serializer {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr const char* kLog = "project";

fs::path toPath(const std::string& utf8) { return fs::path(utf8ToWide(utf8)); }
std::string fromPath(const fs::path& p) { return wideToUtf8(p.wstring()); }

// ------------------------------------------------------------ writers

json rational(Rational r) { return json::array({r.num, r.den}); }
json color(const ColorRgba& c) { return json::array({c.r, c.g, c.b, c.a}); }

json write(const Transform2D& t) {
    return {{"positionX", t.positionX}, {"positionY", t.positionY}, {"scaleX", t.scaleX}, {"scaleY", t.scaleY}, {"rotation", t.rotation},
            {"opacity", t.opacity}, {"flipH", t.flipH}, {"flipV", t.flipV}};
}
json write(const Crop& c) { return {{"left", c.left}, {"right", c.right}, {"top", c.top}, {"bottom", c.bottom}, {"feather", c.feather}}; }
json write(const ColorAdjust& c) {
    return {{"exposure", c.exposure}, {"brightness", c.brightness}, {"contrast", c.contrast}, {"saturation", c.saturation},
            {"temperature", c.temperature}, {"tint", c.tint}, {"highlights", c.highlights}, {"shadows", c.shadows},
            {"gamma", c.gamma}, {"vibrance", c.vibrance}, {"hue", c.hue}};
}
json write(const std::vector<Effect>& effects) {
    json arr = json::array();
    for (const Effect& e : effects) arr.push_back({{"type", effectInfo(e.type).id}, {"enabled", e.enabled}, {"amount", e.amount}});
    return arr;
}
// Keyframes: [{"prop": "positionX", "keys": [[ticks, value, "linear"], ...]}, ...]
json writeAnimation(const std::vector<KeyTrack>& tracks) {
    json arr = json::array();
    for (const KeyTrack& t : tracks) {
        json keys = json::array();
        for (const Keyframe& k : t.keys) keys.push_back(json::array({k.time.ticks, k.value, interpId(k.interp)}));
        arr.push_back({{"prop", animPropId(t.prop)}, {"keys", std::move(keys)}});
    }
    return arr;
}
json write(const AudioParams& a) {
    json j = {{"volume", a.volume}, {"muted", a.muted}, {"fadeIn", a.fadeIn.ticks}, {"fadeOut", a.fadeOut.ticks}, {"pan", a.pan}};
    if (!a.fx.isIdentity()) {
        j["fx"] = {{"eqLow", a.fx.eqLow}, {"eqMid", a.fx.eqMid}, {"eqHigh", a.fx.eqHigh}, {"compressor", a.fx.compressor}, {"compThreshold", a.fx.compThreshold},
                   {"compRatio", a.fx.compRatio}, {"gate", a.fx.gate}, {"gateThreshold", a.fx.gateThreshold}};
    }
    return j;
}
const char* alignName(TextAlign a) { return a == TextAlign::Left ? "left" : a == TextAlign::Right ? "right" : "center"; }
json write(const TextParams& t) {
    return {{"text", t.text}, {"fontFamily", t.fontFamily}, {"fontSize", t.fontSize}, {"bold", t.bold}, {"italic", t.italic},
            {"color", color(t.color)}, {"align", alignName(t.align)}, {"outlineWidth", t.outlineWidth}, {"outlineColor", color(t.outlineColor)},
            {"shadow", t.shadow}, {"shadowOffsetX", t.shadowOffsetX}, {"shadowOffsetY", t.shadowOffsetY}, {"shadowBlur", t.shadowBlur},
            {"shadowColor", color(t.shadowColor)}, {"backgroundOpacity", t.backgroundOpacity}, {"backgroundColor", color(t.backgroundColor)},
            {"backgroundPadding", t.backgroundPadding}, {"lineSpacing", t.lineSpacing}, {"letterSpacing", t.letterSpacing}};
}

json write(const media::VideoStreamInfo& v) {
    return {{"streamIndex", v.streamIndex}, {"codecName", v.codecName}, {"codecLongName", v.codecLongName}, {"profile", v.profile},
            {"width", v.width}, {"height", v.height}, {"frameRate", rational(v.frameRate)}, {"timeBase", rational(v.timeBase)},
            {"sampleAspectRatio", rational(v.sampleAspectRatio)}, {"pixelFormat", v.pixelFormat}, {"bitDepth", v.bitDepth},
            {"bitRate", v.bitRate}, {"rotationDegrees", v.rotationDegrees}, {"isFullRange", v.isFullRange}, {"frameCount", v.frameCount}};
}
json write(const media::AudioStreamInfo& a) {
    return {{"streamIndex", a.streamIndex}, {"codecName", a.codecName}, {"codecLongName", a.codecLongName}, {"channels", a.channels},
            {"channelLayout", a.channelLayout}, {"sampleRate", a.sampleRate}, {"sampleFormat", a.sampleFormat}, {"bitRate", a.bitRate},
            {"timeBase", rational(a.timeBase)}};
}
const char* mediaTypeKey(media::MediaType t) { return t == media::MediaType::Video ? "video" : t == media::MediaType::Audio ? "audio" : "image"; }
json write(const media::MediaInfo& i) {
    json j = {{"path", i.path}, {"fileName", i.fileName}, {"containerName", i.containerName}, {"containerLongName", i.containerLongName},
              {"fileSize", i.fileSize}, {"modifiedTimeUnix", i.modifiedTimeUnix}, {"type", mediaTypeKey(i.type)},
              {"duration", i.duration.ticks}, {"bitRate", i.bitRate}};
    if (i.video) j["video"] = write(*i.video);
    if (i.audio) j["audio"] = write(*i.audio);
    return j;
}

std::string relativeTo(const std::string& file, const std::string& dir) {
    if (dir.empty()) return {};
    std::error_code ec;
    const fs::path rel = fs::relative(toPath(file), toPath(dir), ec);
    return ec ? std::string{} : fromPath(rel);
}

const char* trackKindKey(TrackKind k) { return k == TrackKind::Video ? "video" : "audio"; }
const char* clipKindKey(ClipKind k) {
    switch (k) {
        case ClipKind::Video: return "video";
        case ClipKind::Audio: return "audio";
        case ClipKind::Image: return "image";
        case ClipKind::Text: return "text";
        case ClipKind::Solid: return "solid";
        case ClipKind::Adjustment: return "adjustment";
    }
    return "video";
}

// ------------------------------------------------------------ readers (lenient: missing keys keep defaults)

template <typename T>
void get(const json& j, const char* key, T& out) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        try { out = it->get<T>(); } catch (const json::exception&) { /* keep default */ }
    }
}
void getTicks(const json& j, const char* key, Duration& out) { Ticks t = out.ticks; get(j, key, t); out = Duration{t}; }
void getTicks(const json& j, const char* key, TimePoint& out) { Ticks t = out.ticks; get(j, key, t); out = TimePoint{t}; }
void getRational(const json& j, const char* key, Rational& out) {
    if (auto it = j.find(key); it != j.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number()) {
        const Rational r((*it)[0].get<std::int64_t>(), (*it)[1].get<std::int64_t>());
        if (r.isValid()) out = r;
    }
}
void getColor(const json& j, const char* key, ColorRgba& out) {
    if (auto it = j.find(key); it != j.end() && it->is_array() && it->size() == 4) {
        out = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
    }
}
template <typename Id>
void getId(const json& j, const char* key, Id& out) { std::uint64_t v = raw(out); get(j, key, v); out = static_cast<Id>(v); }

void read(const json& j, Transform2D& t) {
    get(j, "positionX", t.positionX); get(j, "positionY", t.positionY);
    float legacyScale = 1.0f;   // v1 files stored one uniform scale
    get(j, "scale", legacyScale);
    t.scaleX = t.scaleY = legacyScale;
    get(j, "scaleX", t.scaleX); get(j, "scaleY", t.scaleY);
    get(j, "rotation", t.rotation); get(j, "opacity", t.opacity); get(j, "flipH", t.flipH); get(j, "flipV", t.flipV);
}
void read(const json& j, Crop& c) { get(j, "left", c.left); get(j, "right", c.right); get(j, "top", c.top); get(j, "bottom", c.bottom); get(j, "feather", c.feather); }
void read(const json& j, ColorAdjust& c) {
    get(j, "exposure", c.exposure); get(j, "brightness", c.brightness); get(j, "contrast", c.contrast); get(j, "saturation", c.saturation);
    get(j, "temperature", c.temperature); get(j, "tint", c.tint); get(j, "highlights", c.highlights); get(j, "shadows", c.shadows);
    get(j, "gamma", c.gamma); get(j, "vibrance", c.vibrance); get(j, "hue", c.hue);
}
std::optional<Transition> readTransition(const json& clip, const char* key) {
    auto it = clip.find(key);
    if (it == clip.end() || !it->is_object()) return std::nullopt;
    std::string id;
    get(*it, "type", id);
    const TransitionType type = transitionTypeFromId(id);
    if (type == TransitionType::Count) return std::nullopt;   // from a newer version: dropped, not fatal
    Transition t;
    t.type = type;
    Ticks d = t.duration.ticks;
    get(*it, "duration", d);
    if (d <= 0) return std::nullopt;
    t.duration = Duration{d};
    return t;
}

void readAnimation(const json& j, std::vector<KeyTrack>& tracks) {
    tracks.clear();
    if (!j.is_array()) return;
    for (const auto& item : j) {
        if (!item.is_object()) continue;
        std::string id;
        get(item, "prop", id);
        const AnimProp prop = animPropFromId(id);
        if (prop == AnimProp::Count) continue;   // property from a newer version: dropped, not fatal
        KeyTrack t{prop, {}};
        if (auto keys = item.find("keys"); keys != item.end() && keys->is_array()) {
            for (const auto& k : *keys) {
                if (!k.is_array() || k.size() < 2 || !k[0].is_number_integer() || !k[1].is_number()) continue;
                Keyframe key;
                key.time = TimePoint{k[0].get<Ticks>()};
                key.value = k[1].get<float>();
                if (k.size() > 2 && k[2].is_string()) key.interp = interpFromId(k[2].get<std::string>());
                t.keys.push_back(key);
            }
        }
        std::sort(t.keys.begin(), t.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
        t.keys.erase(std::unique(t.keys.begin(), t.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time == b.time; }), t.keys.end());
        if (!t.keys.empty()) tracks.push_back(std::move(t));
    }
}
void read(const json& j, std::vector<Effect>& effects) {
    effects.clear();
    if (j.is_array()) {
        for (const auto& item : j) {
            std::string id;
            get(item, "type", id);
            const EffectType type = effectTypeFromId(id);
            if (type == EffectType::Count) continue;   // unknown effect from a newer version: dropped, not fatal
            Effect e = makeEffect(type);
            get(item, "enabled", e.enabled);
            get(item, "amount", e.amount);
            effects.push_back(e);
        }
        return;
    }
    if (!j.is_object()) return;
    // v1: a fixed struct {grayscale, sepia, vignette, blur, sharpen}
    bool grayscale = false, sepia = false;
    float vignette = 0, blur = 0, sharpen = 0;
    get(j, "grayscale", grayscale); get(j, "sepia", sepia); get(j, "vignette", vignette); get(j, "blur", blur); get(j, "sharpen", sharpen);
    if (grayscale) effects.push_back(makeEffect(EffectType::Grayscale));
    if (sepia) effects.push_back(makeEffect(EffectType::Sepia));
    if (blur > 0) { Effect e = makeEffect(EffectType::Blur); e.amount = blur; effects.push_back(e); }
    if (sharpen > 0) { Effect e = makeEffect(EffectType::Sharpen); e.amount = sharpen; effects.push_back(e); }
    if (vignette > 0) { Effect e = makeEffect(EffectType::Vignette); e.amount = vignette; effects.push_back(e); }
}
void read(const json& j, AudioParams& a) {
    get(j, "volume", a.volume); get(j, "muted", a.muted); getTicks(j, "fadeIn", a.fadeIn); getTicks(j, "fadeOut", a.fadeOut); get(j, "pan", a.pan);
    if (auto fx = j.find("fx"); fx != j.end() && fx->is_object()) {
        get(*fx, "eqLow", a.fx.eqLow); get(*fx, "eqMid", a.fx.eqMid); get(*fx, "eqHigh", a.fx.eqHigh);
        get(*fx, "compressor", a.fx.compressor); get(*fx, "compThreshold", a.fx.compThreshold); get(*fx, "compRatio", a.fx.compRatio);
        get(*fx, "gate", a.fx.gate); get(*fx, "gateThreshold", a.fx.gateThreshold);
    }
}
void read(const json& j, TextParams& t) {
    get(j, "text", t.text); get(j, "fontFamily", t.fontFamily); get(j, "fontSize", t.fontSize); get(j, "bold", t.bold); get(j, "italic", t.italic);
    getColor(j, "color", t.color);
    std::string align = "center";
    get(j, "align", align);
    t.align = align == "left" ? TextAlign::Left : align == "right" ? TextAlign::Right : TextAlign::Center;
    get(j, "outlineWidth", t.outlineWidth); getColor(j, "outlineColor", t.outlineColor);
    get(j, "shadow", t.shadow); get(j, "shadowOffsetX", t.shadowOffsetX); get(j, "shadowOffsetY", t.shadowOffsetY); get(j, "shadowBlur", t.shadowBlur);
    getColor(j, "shadowColor", t.shadowColor); get(j, "backgroundOpacity", t.backgroundOpacity); getColor(j, "backgroundColor", t.backgroundColor);
    get(j, "lineSpacing", t.lineSpacing); get(j, "letterSpacing", t.letterSpacing); get(j, "backgroundPadding", t.backgroundPadding);
}
void read(const json& j, media::VideoStreamInfo& v) {
    get(j, "streamIndex", v.streamIndex); get(j, "codecName", v.codecName); get(j, "codecLongName", v.codecLongName); get(j, "profile", v.profile);
    get(j, "width", v.width); get(j, "height", v.height); getRational(j, "frameRate", v.frameRate); getRational(j, "timeBase", v.timeBase);
    getRational(j, "sampleAspectRatio", v.sampleAspectRatio); get(j, "pixelFormat", v.pixelFormat); get(j, "bitDepth", v.bitDepth);
    get(j, "bitRate", v.bitRate); get(j, "rotationDegrees", v.rotationDegrees); get(j, "isFullRange", v.isFullRange); get(j, "frameCount", v.frameCount);
}
void read(const json& j, media::AudioStreamInfo& a) {
    get(j, "streamIndex", a.streamIndex); get(j, "codecName", a.codecName); get(j, "codecLongName", a.codecLongName); get(j, "channels", a.channels);
    get(j, "channelLayout", a.channelLayout); get(j, "sampleRate", a.sampleRate); get(j, "sampleFormat", a.sampleFormat); get(j, "bitRate", a.bitRate);
    getRational(j, "timeBase", a.timeBase);
}
void read(const json& j, media::MediaInfo& i) {
    get(j, "path", i.path); get(j, "fileName", i.fileName); get(j, "containerName", i.containerName); get(j, "containerLongName", i.containerLongName);
    get(j, "fileSize", i.fileSize); get(j, "modifiedTimeUnix", i.modifiedTimeUnix);
    std::string type = "video";
    get(j, "type", type);
    i.type = type == "audio" ? media::MediaType::Audio : type == "image" ? media::MediaType::Image : media::MediaType::Video;
    getTicks(j, "duration", i.duration); get(j, "bitRate", i.bitRate);
    if (auto it = j.find("video"); it != j.end() && it->is_object()) { media::VideoStreamInfo v; read(*it, v); i.video = v; }
    if (auto it = j.find("audio"); it != j.end() && it->is_object()) { media::AudioStreamInfo a; read(*it, a); i.audio = a; }
}

bool fileExists(const std::string& utf8) {
    std::error_code ec;
    return !utf8.empty() && fs::is_regular_file(toPath(utf8), ec);
}

}  // namespace

// ------------------------------------------------------------ public API

std::string toJson(const Project& project, const std::string& projectDir) {
    json root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;
    root["app"] = std::string("LumaForge ") + version::string;

    const auto& s = project.settings;
    root["settings"] = {{"name", s.name}, {"width", s.width}, {"height", s.height}, {"frameRate", rational(s.frameRate)}, {"audioSampleRate", s.audioSampleRate}};

    json media = json::array();
    for (const auto& m : project.media) {
        json j = {{"id", raw(m.id)}, {"path", m.path}, {"relativePath", relativeTo(m.path, projectDir)}, {"displayName", m.displayName},
                  {"fingerprintSize", m.fingerprintSize}, {"fingerprintMtime", m.fingerprintMtime}, {"addedUnixTime", m.addedUnixTime},
                  {"info", write(m.info)}};
        if (!m.bin.empty()) j["bin"] = m.bin;
        media.push_back(std::move(j));
    }
    root["media"] = std::move(media);
    root["bins"] = project.bins;
    json markers = json::array();
    for (const auto& m : project.sequence.markers) {
        json jm = {{"id", m.id}, {"time", m.time.ticks}, {"name", m.name}, {"color", m.color}};
        if (!m.note.empty()) jm["note"] = m.note;
        markers.push_back(std::move(jm));
    }

    json tracks = json::array();
    for (const auto& t : project.sequence.tracks) {
        json jt = {{"id", raw(t.id)}, {"kind", trackKindKey(t.kind)}, {"name", t.name}, {"muted", t.muted}, {"locked", t.locked}, {"solo", t.solo}, {"height", t.height}};
        if (t.subtitles) jt["subtitles"] = true;
        if (t.kind == TrackKind::Audio) { jt["volume"] = t.volume; jt["pan"] = t.pan; }
        tracks.push_back(std::move(jt));
    }
    json clips = json::array();
    for (const auto& c : project.sequence.clips) {
        json j = {{"id", raw(c.id)}, {"track", raw(c.track)}, {"kind", clipKindKey(c.kind)}, {"media", raw(c.media)}, {"name", c.name},
                  {"start", c.start.ticks}, {"duration", c.duration.ticks}, {"sourceIn", c.sourceIn.ticks}, {"speed", rational(c.speed)},
                  {"link", raw(c.link)}, {"transform", write(c.transform)}, {"crop", write(c.crop)}, {"color", write(c.color)},
                  {"effects", write(c.effects)}, {"audio", write(c.audio)}, {"enabled", c.enabled}};
        if (c.reverse) j["reverse"] = true;
        if (c.freeze) j["freeze"] = true;
        if (!c.animation.empty()) j["animation"] = writeAnimation(c.animation);
        if (c.mask.isActive()) {
            j["mask"] = {{"shape", c.mask.shape == MaskShape::Ellipse ? "ellipse" : "rectangle"}, {"centerX", c.mask.centerX}, {"centerY", c.mask.centerY},
                         {"width", c.mask.width}, {"height", c.mask.height}, {"feather", c.mask.feather}, {"invert", c.mask.invert}};
        }
        if (c.transitionIn) j["transitionIn"] = {{"type", transitionInfo(c.transitionIn->type).id}, {"duration", c.transitionIn->duration.ticks}};
        if (c.transitionOut) j["transitionOut"] = {{"type", transitionInfo(c.transitionOut->type).id}, {"duration", c.transitionOut->duration.ticks}};
        if (c.kind == ClipKind::Text) j["text"] = write(c.text);
        if (c.kind == ClipKind::Solid) j["solidColor"] = {c.solidColor.r, c.solidColor.g, c.solidColor.b, c.solidColor.a};
        clips.push_back(std::move(j));
    }
    root["sequence"] = {{"tracks", std::move(tracks)}, {"clips", std::move(clips)},
                        {"markIn", project.sequence.markIn.ticks}, {"markOut", project.sequence.markOut.ticks}, {"markers", std::move(markers)},
                        {"masterVolume", project.sequence.masterVolume}, {"masterLimiter", project.sequence.masterLimiter}};
    return root.dump(2);
}

Result<LoadResult> fromJson(const std::string& text, const std::string& projectDir) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception& e) {
        return Error{"The project file is not valid.", e.what()};
    }
    if (!root.is_object() || root.value("format", "") != kFormatName) return Error{"This is not a LumaForge project file."};
    const int version = root.value("version", 0);
    if (version > kFormatVersion) return Error{"This project was saved by a newer version of LumaForge.", "file version " + std::to_string(version)};
    if (version < 1) return Error{"The project file has no valid version number."};

    LoadResult result;
    result.fileVersion = version;
    Project& p = result.project;

    if (auto it = root.find("settings"); it != root.end() && it->is_object()) {
        get(*it, "name", p.settings.name); get(*it, "width", p.settings.width); get(*it, "height", p.settings.height);
        getRational(*it, "frameRate", p.settings.frameRate); get(*it, "audioSampleRate", p.settings.audioSampleRate);
        if (p.settings.width < 16 || p.settings.height < 16 || !p.settings.frameRate.isValid() || p.settings.frameRate.isZero()) return Error{"The project settings are invalid."};
    }

    p.bins.clear();
    if (auto it = root.find("bins"); it != root.end() && it->is_array()) {
        for (const auto& b : *it) if (b.is_string() && !b.get<std::string>().empty()) p.bins.push_back(b.get<std::string>());
    }
    p.media.clear();
    if (auto it = root.find("media"); it != root.end() && it->is_array()) {
        for (const auto& j : *it) {
            MediaItem m;
            getId(j, "id", m.id); get(j, "path", m.path); get(j, "displayName", m.displayName);
            get(j, "fingerprintSize", m.fingerprintSize); get(j, "fingerprintMtime", m.fingerprintMtime); get(j, "addedUnixTime", m.addedUnixTime);
            get(j, "bin", m.bin);
            if (auto info = j.find("info"); info != j.end() && info->is_object()) read(*info, m.info);
            if (!isValid(m.id)) continue;
            // Resolve: absolute path first, then relative to the project file.
            if (!fileExists(m.path)) {
                std::string rel;
                get(j, "relativePath", rel);
                if (!rel.empty() && !projectDir.empty()) {
                    std::error_code ec;
                    const fs::path candidate = fs::weakly_canonical(toPath(projectDir) / toPath(rel), ec);
                    const std::string cs = fromPath(candidate);
                    if (!ec && fileExists(cs)) { m.path = cs; m.info.path = cs; }
                }
            }
            m.offline = !fileExists(m.path);
            if (m.offline) result.offlineMedia.push_back(m.displayName.empty() ? m.path : m.displayName);
            p.media.push_back(std::move(m));
        }
    }

    p.sequence = Sequence{};
    if (auto seq = root.find("sequence"); seq != root.end() && seq->is_object()) {
        if (auto it = seq->find("tracks"); it != seq->end() && it->is_array()) {
            for (const auto& j : *it) {
                Track t;
                getId(j, "id", t.id); get(j, "name", t.name); get(j, "muted", t.muted); get(j, "locked", t.locked); get(j, "solo", t.solo);
                get(j, "subtitles", t.subtitles);
                get(j, "volume", t.volume);
                get(j, "pan", t.pan);
                get(j, "height", t.height);
                t.height = std::clamp(t.height, 0, 400);
                std::string kind = "video";
                get(j, "kind", kind);
                t.kind = kind == "audio" ? TrackKind::Audio : TrackKind::Video;
                if (isValid(t.id)) p.sequence.tracks.push_back(std::move(t));
            }
        }
        if (auto it = seq->find("clips"); it != seq->end() && it->is_array()) {
            for (const auto& j : *it) {
                Clip c;
                getId(j, "id", c.id); getId(j, "track", c.track); getId(j, "media", c.media); getId(j, "link", c.link); get(j, "name", c.name);
                std::string kind = "video";
                get(j, "kind", kind);
                c.kind = kind == "audio" ? ClipKind::Audio : kind == "image" ? ClipKind::Image : kind == "text" ? ClipKind::Text
                       : kind == "solid" ? ClipKind::Solid : kind == "adjustment" ? ClipKind::Adjustment : ClipKind::Video;
                if (auto sc = j.find("solidColor"); sc != j.end() && sc->is_array() && sc->size() == 4) {
                    c.solidColor = {(*sc)[0].get<float>(), (*sc)[1].get<float>(), (*sc)[2].get<float>(), (*sc)[3].get<float>()};
                }
                getTicks(j, "start", c.start); getTicks(j, "duration", c.duration); getTicks(j, "sourceIn", c.sourceIn); getRational(j, "speed", c.speed);
                get(j, "enabled", c.enabled);
                get(j, "reverse", c.reverse);
                get(j, "freeze", c.freeze);
                if (auto s = j.find("transform"); s != j.end()) read(*s, c.transform);
                if (auto s = j.find("crop"); s != j.end()) read(*s, c.crop);
                if (auto s = j.find("color"); s != j.end()) read(*s, c.color);
                if (auto s = j.find("effects"); s != j.end()) read(*s, c.effects);
                if (auto s = j.find("animation"); s != j.end()) readAnimation(*s, c.animation);
                if (auto mk = j.find("mask"); mk != j.end() && mk->is_object()) {
                    std::string shape;
                    get(*mk, "shape", shape);
                    c.mask.shape = shape == "ellipse" ? MaskShape::Ellipse : shape == "rectangle" ? MaskShape::Rectangle : MaskShape::None;
                    get(*mk, "centerX", c.mask.centerX); get(*mk, "centerY", c.mask.centerY);
                    get(*mk, "width", c.mask.width); get(*mk, "height", c.mask.height);
                    get(*mk, "feather", c.mask.feather); get(*mk, "invert", c.mask.invert);
                }
                c.transitionIn = readTransition(j, "transitionIn");
                c.transitionOut = readTransition(j, "transitionOut");
                if (auto s = j.find("audio"); s != j.end()) read(*s, c.audio);
                if (auto s = j.find("text"); s != j.end()) read(*s, c.text);
                // Drop clips that cannot be placed; never load an inconsistent sequence.
                if (!isValid(c.id) || !p.sequence.findTrack(c.track) || c.duration.ticks <= 0 || c.speed.num <= 0) {
                    LF_LOG_WARN(kLog, "skipping invalid clip {} in project file", raw(c.id));
                    continue;
                }
                if (!isGeneratedClip(c.kind) && !p.findMedia(c.media)) {
                    LF_LOG_WARN(kLog, "skipping clip {} referencing unknown media {}", raw(c.id), raw(c.media));
                    continue;
                }
                p.sequence.clips.push_back(std::move(c));
            }
        }
        p.sequence.markers.clear();
        if (auto ms = seq->find("markers"); ms != seq->end() && ms->is_array()) {
            for (const auto& jm : *ms) {
                if (!jm.is_object()) continue;
                Marker m;
                get(jm, "id", m.id);
                getTicks(jm, "time", m.time);
                get(jm, "name", m.name);
                get(jm, "color", m.color);
                get(jm, "note", m.note);
                if (m.time.ticks < 0) continue;
                p.sequence.markers.push_back(std::move(m));
            }
            std::stable_sort(p.sequence.markers.begin(), p.sequence.markers.end(), [](const Marker& a, const Marker& b) { return a.time < b.time; });
            // Ids must be unique: renumber duplicates after the highest one.
            std::uint64_t next = 1;
            for (const auto& m : p.sequence.markers) next = std::max(next, m.id + 1);
            for (std::size_t i = 0; i < p.sequence.markers.size(); ++i) {
                for (std::size_t k = 0; k < i; ++k) if (p.sequence.markers[k].id == p.sequence.markers[i].id || p.sequence.markers[i].id == 0) { p.sequence.markers[i].id = next++; break; }
            }
        }
        get(*seq, "masterVolume", p.sequence.masterVolume);
        get(*seq, "masterLimiter", p.sequence.masterLimiter);
        getTicks(*seq, "markIn", p.sequence.markIn);
        getTicks(*seq, "markOut", p.sequence.markOut);
        if (!p.sequence.hasInOut() && (p.sequence.markIn.ticks >= 0 || p.sequence.markOut.ticks >= 0)) {
            // A single mark is fine; an inverted pair is not.
            if (p.sequence.markIn.ticks >= 0 && p.sequence.markOut.ticks >= 0) p.sequence.markIn = p.sequence.markOut = TimePoint{-1};
        }
        // Links must be symmetric and point at existing clips.
        for (auto& c : p.sequence.clips) {
            const Clip* partner = p.sequence.findClip(c.link);
            if (!partner || partner->link != c.id) c.link = ClipId::Invalid;
        }
    }
    if (p.sequence.tracks.empty()) p.sequence = Sequence::withDefaultTracks();
    return result;
}

Result<void> saveToFile(const Project& project, const std::string& path) {
    const fs::path target = toPath(path);
    const std::string text = toJson(project, fromPath(target.parent_path()));
    const fs::path temp = target.parent_path() / (target.filename().wstring() + L".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{"The project could not be written.", "cannot create " + fromPath(temp)};
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) return Error{"The project could not be written.", "write failed for " + fromPath(temp)};
    }
    std::error_code ec;
    fs::rename(temp, target, ec);   // atomic replace on the same volume
    if (ec) {
        fs::remove(temp);
        return Error{"The project could not be saved.", ec.message()};
    }
    LF_LOG_INFO(kLog, "saved project to {} ({} bytes)", path, text.size());
    return {};
}

Result<LoadResult> loadFromFile(const std::string& path) {
    const fs::path file = toPath(path);
    std::ifstream in(file, std::ios::binary);
    if (!in) return Error{"The project file could not be opened.", path};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto result = fromJson(text, fromPath(file.parent_path()));
    if (result) LF_LOG_INFO(kLog, "loaded project {} (v{}, {} media, {} clips, {} offline)", path, result->fileVersion, result->project.media.size(), result->project.sequence.clips.size(), result->offlineMedia.size());
    else LF_LOG_WARN(kLog, "failed to load {}: {}", path, result.error().fullText());
    return result;
}

}  // namespace lf::project::serializer
