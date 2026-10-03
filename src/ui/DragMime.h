#pragma once

// MIME types for drags between LumaForge panels (media ids live in MediaModel).
namespace lf::ui::mime {

inline constexpr const char* kEffect = "application/x-lumaforge-effect";   // payload: effect id (UTF-8)
inline constexpr const char* kTitle = "application/x-lumaforge-title";     // payload: title preset index (ASCII)
inline constexpr const char* kTransition = "application/x-lumaforge-transition";   // payload: "<type id>;<duration ticks>"

}  // namespace lf::ui::mime
