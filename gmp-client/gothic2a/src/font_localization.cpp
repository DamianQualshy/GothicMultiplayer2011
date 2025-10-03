
/*
MIT License

Copyright (c) 2025 Gothic Multiplayer Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "font_localization.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "ZenGin/zGothicAPI.h"

namespace font_localization {
namespace {
struct FontMetrics {
  int height = 0;
  int font_y = 0;
  int letter_distance = 0;
  std::array<unsigned char, Gothic_II_Addon::zFONT_MAX_LETTER> widths{};

  unsigned char GlyphWidth(unsigned char ch) const {
    return widths[ch];
  }
};

std::string NormalizePrefix(std::string prefix) {
  prefix.erase(std::remove_if(prefix.begin(), prefix.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), prefix.end());
  if (!prefix.empty() && prefix.back() != '_' && prefix.back() != '/' && prefix.back() != '\\') {
    prefix.push_back('_');
  }
  return prefix;
}

std::string ComposeFontName(std::string_view baseFont, const std::string& prefix) {
  if (prefix.empty()) {
    return std::string(baseFont);
  }
  std::string fontName;
  fontName.reserve(prefix.size() + baseFont.size());
  fontName.append(prefix);
  fontName.append(baseFont.begin(), baseFont.end());
  return fontName;
}

std::unordered_map<std::string, std::string> g_font_cache;
std::string g_prefix;
std::unordered_map<std::string, FontMetrics> g_previous_metrics;
std::string g_previous_prefix;

const std::array<std::string_view, 3> kKnownFonts = {kFontDefault, kFontOld20White, kFontOld10White};

void RefreshCache() {
  g_font_cache.clear();
  g_font_cache.reserve(kKnownFonts.size());
  for (const auto base : kKnownFonts) {
    g_font_cache.emplace(std::string(base), ComposeFontName(base, g_prefix));
  }
}

bool CaptureMetricsForFont(const std::string& resolvedFontName, FontMetrics& outMetrics) {
  if (Gothic_II_Addon::zfontman == nullptr) {
    return false;
  }

  Gothic_II_Addon::zSTRING zName(resolvedFontName.c_str());
  int fontIndex = Gothic_II_Addon::zfontman->SearchFont(zName);
  if (fontIndex < 0) {
    if (Gothic_II_Addon::zfontman->Load(zName) < 0) {
      return false;
    }
    fontIndex = Gothic_II_Addon::zfontman->SearchFont(zName);
    if (fontIndex < 0) {
      return false;
    }
  }

  Gothic_II_Addon::zCFont* font = Gothic_II_Addon::zfontman->GetFont(fontIndex);
  if (font == nullptr) {
    return false;
  }

  outMetrics.height = font->height;
  outMetrics.font_y = font->GetFontY();
  outMetrics.letter_distance = font->GetLetterDistance();
  std::copy_n(font->width, Gothic_II_Addon::zFONT_MAX_LETTER, outMetrics.widths.begin());
  return true;
}

std::unordered_map<std::string, FontMetrics> CaptureMetricsForPrefix(const std::string& prefix) {
  std::unordered_map<std::string, FontMetrics> metrics;
  metrics.reserve(kKnownFonts.size());

  if (Gothic_II_Addon::zfontman == nullptr) {
    return metrics;
  }

  for (const auto base : kKnownFonts) {
    const std::string base_key(base);
    FontMetrics fontMetrics;
    if (CaptureMetricsForFont(ComposeFontName(base, prefix), fontMetrics)) {
      metrics.emplace(base_key, fontMetrics);
    }
  }

  return metrics;
}

void VerifyFontMetrics(const std::unordered_map<std::string, FontMetrics>& previous, const std::unordered_map<std::string, FontMetrics>& current) {
  if (current.empty()) {
    return;
  }

  bool all_consistent = true;
  bool compared_any = false;
  for (const auto base : kKnownFonts) {
    const std::string base_key(base);
    const auto prevIt = previous.find(base_key);
    const auto currIt = current.find(base_key);

    if (prevIt == previous.end()) {
      continue;
    }

    if (currIt == current.end()) {
      SPDLOG_WARN("Font '{}' missing after reload (previous prefix '{}', new prefix '{}')", ComposeFontName(base, g_previous_prefix),
                  g_previous_prefix, g_prefix);
      all_consistent = false;
      continue;
    }

    const FontMetrics& prevMetrics = prevIt->second;
    const FontMetrics& currMetrics = currIt->second;
    compared_any = true;
    if (prevMetrics.height != currMetrics.height || prevMetrics.font_y != currMetrics.font_y ||
        prevMetrics.GlyphWidth(static_cast<unsigned char>('W')) != currMetrics.GlyphWidth(static_cast<unsigned char>('W')) ||
        prevMetrics.letter_distance != currMetrics.letter_distance ||
        !std::equal(prevMetrics.widths.begin(), prevMetrics.widths.end(), currMetrics.widths.begin())) {
      SPDLOG_WARN("Font metrics changed when switching '{}' -> '{}': height {} -> {}, fontY {} -> {}, width('W') {} -> {}, letter distance {} -> {}",
                  ComposeFontName(base, g_previous_prefix), ComposeFontName(base, g_prefix), prevMetrics.height, currMetrics.height,
                  prevMetrics.font_y, currMetrics.font_y, prevMetrics.GlyphWidth(static_cast<unsigned char>('W')),
                  currMetrics.GlyphWidth(static_cast<unsigned char>('W')), prevMetrics.letter_distance, currMetrics.letter_distance);
      all_consistent = false;
    }
  }

  if (all_consistent && compared_any) {
    SPDLOG_INFO("Font metrics unchanged when switching from prefix '{}' to '{}'", g_previous_prefix, g_prefix);
  }
}

const std::string& ResolveFontInternal(std::string_view baseFont) {
  auto it = g_font_cache.find(std::string(baseFont));
  if (it != g_font_cache.end()) {
    return it->second;
  }
  auto [insertIt, _] = g_font_cache.emplace(std::string(baseFont), ComposeFontName(baseFont, g_prefix));
  return insertIt->second;
}

void RestoreMetricsForFont(std::string_view base_key_view, const char* resolved_name) {
  if (Gothic_II_Addon::zfontman == nullptr) {
    return;
  }

  const auto previousIt = g_previous_metrics.find(std::string(base_key_view));
  if (previousIt == g_previous_metrics.end()) {
    return;
  }

  Gothic_II_Addon::zSTRING zName(resolved_name);
  const int fontIndex = Gothic_II_Addon::zfontman->SearchFont(zName);
  if (fontIndex < 0) {
    return;
  }

  Gothic_II_Addon::zCFont* font = Gothic_II_Addon::zfontman->GetFont(fontIndex);
  if (font == nullptr) {
    return;
  }

  const FontMetrics& baseline = previousIt->second;

  bool metrics_adjusted = false;
  if (font->height != baseline.height) {
    font->height = baseline.height;
    metrics_adjusted = true;
  }

  if (!std::equal(baseline.widths.begin(), baseline.widths.end(), font->width)) {
    std::copy_n(baseline.widths.begin(), baseline.widths.size(), font->width);
    metrics_adjusted = true;
  }

  if (metrics_adjusted) {
    SPDLOG_INFO("Restored baseline draw metrics for font '{}' (prefix '{}' -> '{}')", ComposeFontName(base_key_view, g_prefix), g_previous_prefix,
                g_prefix);
  }
}

}  // namespace

void SetLanguagePrefix(std::string prefix) {
  g_previous_prefix = g_prefix;
  g_previous_metrics = CaptureMetricsForPrefix(g_prefix);
  g_prefix = NormalizePrefix(std::move(prefix));
  RefreshCache();
  SPDLOG_INFO("Font localization prefix set to '{}' (default='{}', old20='{}', old10='{}')", g_prefix, GetFont(kFontDefault),
              GetFont(kFontOld20White), GetFont(kFontOld10White));
}

const char* GetFont(std::string_view baseFont) {
  const auto& resolved = ResolveFontInternal(baseFont);
  return resolved.c_str();
}

void ReloadFonts() {
  if (Gothic_II_Addon::zfontman == nullptr) {
    return;
  }
  for (const auto base : kKnownFonts) {
    const auto* fontName = GetFont(base);
    Gothic_II_Addon::zfontman->Load(Gothic_II_Addon::zSTRING(fontName));
    RestoreMetricsForFont(base, fontName);
  }
  auto current_metrics = CaptureMetricsForPrefix(g_prefix);
  VerifyFontMetrics(g_previous_metrics, current_metrics);
  g_previous_metrics = std::move(current_metrics);
  g_previous_prefix = g_prefix;
}

}  // namespace font_localization