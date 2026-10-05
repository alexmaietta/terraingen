#pragma once

/// Lucide icon glyphs (lucide-static 1.52.0, private-use codepoints) as UTF-8 strings, for text
/// drawn with the viewer's merged icon font.
namespace ernie::gui::icons {

inline constexpr const char* kCamera = "\xee\x81\xa4";             ///< U+E064
inline constexpr const char* kEye = "\xee\x82\xba";                ///< U+E0BA
inline constexpr const char* kSun = "\xee\x85\xb8";                ///< U+E178
inline constexpr const char* kLightbulb = "\xee\x87\x82";          ///< U+E1C2
inline constexpr const char* kLayers = "\xee\x94\xa9";             ///< U+E529
inline constexpr const char* kSettings = "\xee\x85\x94";           ///< U+E154
inline constexpr const char* kBox = "\xee\x81\xa1";                ///< U+E061
inline constexpr const char* kFocus = "\xee\x8a\x9e";              ///< U+E29E
inline constexpr const char* kMonitor = "\xee\x84\x9d";            ///< U+E11D
inline constexpr const char* kChartLine = "\xee\x8a\xa5";          ///< U+E2A5
inline constexpr const char* kSlidersHorizontal = "\xee\x8a\x9a";  ///< U+E29A
inline constexpr const char* kListTree = "\xee\x90\x88";           ///< U+E408
inline constexpr const char* kAperture = "\xee\x81\x80";           ///< U+E040
inline constexpr const char* kImage = "\xee\x83\xb6";              ///< U+E0F6
inline constexpr const char* kRotateCcw = "\xee\x85\x88";          ///< U+E148
inline constexpr const char* kX = "\xee\x86\xb2";                  ///< U+E1B2
inline constexpr const char* kFlashlight = "\xee\x83\x93";         ///< U+E0D3
inline constexpr const char* kSave = "\xee\x85\x8d";               ///< U+E14D
inline constexpr const char* kFolderOpen = "\xee\x89\x87";         ///< U+E247

/// First and last codepoints of the icon font, for loading its glyphs.
inline constexpr unsigned kFirst = 0xE000;
inline constexpr unsigned kLast = 0xF8FF;

}  // namespace ernie::gui::icons
