#include "lumaforge/project/Animation.h"

#include "lumaforge/project/Sequence.h"

#include <algorithm>
#include <cmath>

namespace lf::project {

namespace {
constexpr const char* kPropIds[] = {"positionX", "positionY", "scaleX", "scaleY", "rotation", "opacity",
                                    "cropLeft", "cropRight", "cropTop", "cropBottom", "volume"};
static_assert(std::size(kPropIds) == static_cast<std::size_t>(AnimProp::Count));

KeyTrack* trackFor(Clip& c, AnimProp prop) {
    for (auto& t : c.animation) if (t.prop == prop) return &t;
    return nullptr;
}
}  // namespace

const char* animPropId(AnimProp prop) noexcept {
    const auto i = static_cast<std::size_t>(prop);
    return i < std::size(kPropIds) ? kPropIds[i] : "";
}

AnimProp animPropFromId(const std::string& id) noexcept {
    for (std::size_t i = 0; i < std::size(kPropIds); ++i) if (id == kPropIds[i]) return static_cast<AnimProp>(i);
    return AnimProp::Count;
}

const char* interpId(Interp interp) noexcept {
    switch (interp) {
        case Interp::Hold: return "hold";
        case Interp::Smooth: return "smooth";
        default: return "linear";
    }
}

Interp interpFromId(const std::string& id) noexcept {
    if (id == "hold") return Interp::Hold;
    if (id == "smooth") return Interp::Smooth;
    return Interp::Linear;
}

float evaluate(const std::vector<Keyframe>& keys, TimePoint t) noexcept {
    if (keys.empty()) return 0.0f;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    // First key strictly after t.
    auto hi = std::upper_bound(keys.begin(), keys.end(), t, [](TimePoint v, const Keyframe& k) { return v < k.time; });
    const Keyframe& b = *hi;
    const Keyframe& a = *(hi - 1);
    if (a.interp == Interp::Hold) return a.value;
    double u = static_cast<double>(t.ticks - a.time.ticks) / static_cast<double>(b.time.ticks - a.time.ticks);
    if (a.interp == Interp::Smooth) u = u * u * (3.0 - 2.0 * u);   // ease in and out
    return static_cast<float>(a.value + (b.value - a.value) * u);
}

// ---------------------------------------------------------------- clip helpers (Sequence.h)

const KeyTrack* findKeyTrack(const Clip& c, AnimProp prop) noexcept {
    for (const auto& t : c.animation) if (t.prop == prop) return &t;
    return nullptr;
}

float baseValue(const Clip& c, AnimProp prop) noexcept {
    switch (prop) {
        case AnimProp::PositionX: return c.transform.positionX;
        case AnimProp::PositionY: return c.transform.positionY;
        case AnimProp::ScaleX: return c.transform.scaleX;
        case AnimProp::ScaleY: return c.transform.scaleY;
        case AnimProp::Rotation: return c.transform.rotation;
        case AnimProp::Opacity: return c.transform.opacity;
        case AnimProp::CropLeft: return c.crop.left;
        case AnimProp::CropRight: return c.crop.right;
        case AnimProp::CropTop: return c.crop.top;
        case AnimProp::CropBottom: return c.crop.bottom;
        case AnimProp::Volume: return c.audio.volume;
        case AnimProp::Count: break;
    }
    return 0.0f;
}

void setBaseValue(Clip& c, AnimProp prop, float v) noexcept {
    switch (prop) {
        case AnimProp::PositionX: c.transform.positionX = v; break;
        case AnimProp::PositionY: c.transform.positionY = v; break;
        case AnimProp::ScaleX: c.transform.scaleX = v; break;
        case AnimProp::ScaleY: c.transform.scaleY = v; break;
        case AnimProp::Rotation: c.transform.rotation = v; break;
        case AnimProp::Opacity: c.transform.opacity = v; break;
        case AnimProp::CropLeft: c.crop.left = v; break;
        case AnimProp::CropRight: c.crop.right = v; break;
        case AnimProp::CropTop: c.crop.top = v; break;
        case AnimProp::CropBottom: c.crop.bottom = v; break;
        case AnimProp::Volume: c.audio.volume = v; break;
        case AnimProp::Count: break;
    }
}

float valueAt(const Clip& c, AnimProp prop, TimePoint source) noexcept {
    const KeyTrack* t = findKeyTrack(c, prop);
    return t && !t->keys.empty() ? evaluate(t->keys, source) : baseValue(c, prop);
}

const Keyframe* keyAt(const Clip& c, AnimProp prop, TimePoint source) noexcept {
    const KeyTrack* t = findKeyTrack(c, prop);
    if (!t) return nullptr;
    for (const auto& k : t->keys) if (k.time == source) return &k;
    return nullptr;
}

void setKey(Clip& c, AnimProp prop, TimePoint source, float value, std::optional<Interp> interp) {
    KeyTrack* t = trackFor(c, prop);
    if (!t) {
        c.animation.push_back(KeyTrack{prop, {}});
        t = &c.animation.back();
    }
    auto it = std::lower_bound(t->keys.begin(), t->keys.end(), source, [](const Keyframe& k, TimePoint v) { return k.time < v; });
    if (it != t->keys.end() && it->time == source) {
        it->value = value;
        if (interp) it->interp = *interp;
    } else {
        // A new key inherits the interpolation of the key before it (Linear for the first).
        Interp chosen = interp ? *interp : (it != t->keys.begin() ? (it - 1)->interp : Interp::Linear);
        t->keys.insert(it, Keyframe{source, value, chosen});
    }
}

bool removeKey(Clip& c, AnimProp prop, TimePoint source) {
    KeyTrack* t = trackFor(c, prop);
    if (!t) return false;
    auto it = std::find_if(t->keys.begin(), t->keys.end(), [&](const Keyframe& k) { return k.time == source; });
    if (it == t->keys.end()) return false;
    const float value = it->value;
    t->keys.erase(it);
    if (t->keys.empty()) {
        // The last key goes: the property becomes static at that value.
        setBaseValue(c, prop, value);
        c.animation.erase(c.animation.begin() + (t - c.animation.data()));
    }
    return true;
}

void clearAnimation(Clip& c, AnimProp prop, TimePoint source) {
    if (!findKeyTrack(c, prop)) return;
    const float v = valueAt(c, prop, source);   // freeze what is showing now
    c.animation.erase(std::remove_if(c.animation.begin(), c.animation.end(), [prop](const KeyTrack& t) { return t.prop == prop; }), c.animation.end());
    setBaseValue(c, prop, v);
}

void setValueAt(Clip& c, AnimProp prop, TimePoint source, float value) {
    if (findKeyTrack(c, prop)) setKey(c, prop, source, value);
    else setBaseValue(c, prop, value);
}

void animatedTransform(const Clip& c, TimePoint source, Transform2D& tf, Crop& crop) noexcept {
    if (c.animation.empty()) return;
    for (const auto& t : c.animation) {
        if (t.keys.empty()) continue;
        const float v = evaluate(t.keys, source);
        switch (t.prop) {
            case AnimProp::PositionX: tf.positionX = v; break;
            case AnimProp::PositionY: tf.positionY = v; break;
            case AnimProp::ScaleX: tf.scaleX = std::max(0.0f, v); break;
            case AnimProp::ScaleY: tf.scaleY = std::max(0.0f, v); break;
            case AnimProp::Rotation: tf.rotation = v; break;
            case AnimProp::Opacity: tf.opacity = std::clamp(v, 0.0f, 1.0f); break;
            case AnimProp::CropLeft: crop.left = std::clamp(v, 0.0f, 0.98f); break;
            case AnimProp::CropRight: crop.right = std::clamp(v, 0.0f, 0.98f); break;
            case AnimProp::CropTop: crop.top = std::clamp(v, 0.0f, 0.98f); break;
            case AnimProp::CropBottom: crop.bottom = std::clamp(v, 0.0f, 0.98f); break;
            default: break;
        }
    }
    // Opposite edges may never overlap, whatever the keys say.
    if (crop.left + crop.right > 0.98f) crop.right = 0.98f - crop.left;
    if (crop.top + crop.bottom > 0.98f) crop.bottom = 0.98f - crop.top;
}

TimePoint animationTime(const Clip& c, TimePoint sequenceTime) noexcept {
    const TimePoint t{std::clamp(sequenceTime.ticks, c.start.ticks, std::max(c.start.ticks, c.end().ticks - 1))};
    return c.sourceTimeAt(t);
}

std::vector<TimePoint> keyTimes(const Clip& c) {
    std::vector<TimePoint> out;
    for (const auto& t : c.animation) for (const auto& k : t.keys) out.push_back(k.time);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace lf::project
