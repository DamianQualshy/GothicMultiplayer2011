/*
MIT License

Copyright (c) 2022 Gothic Multiplayer Team (pampi, skejt23, mecio)

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

#include "CLanguage.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using Gothic_II_Addon::zSTRING;

namespace {
using Json = nlohmann::json;

std::optional<Json> LoadJsonFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    SPDLOG_ERROR("Failed to open localization file {}", path.string());
    return std::nullopt;
  }

  try {
    Json json;
    stream >> json;
    if (!json.is_object()) {
      SPDLOG_ERROR("Localization file {} must contain a JSON object at the root.", path.string());
      return std::nullopt;
    }
    return json;
  } catch (const std::exception& ex) {
    SPDLOG_ERROR("Failed to parse localization file {}: {}", path.string(), ex.what());
    return std::nullopt;
  }
}

const std::unordered_map<std::string_view, CLanguage::STRING_ID, std::hash<std::string_view>, std::equal_to<>>&
KeyLookup() {
  static const auto map = [] {
    std::unordered_map<std::string_view, CLanguage::STRING_ID, std::hash<std::string_view>, std::equal_to<>> lookup;
    lookup.reserve(CLanguage::AllStringIds().size());
    for (auto id : CLanguage::AllStringIds()) {
      lookup.emplace(CLanguage::ToLocalizationKey(id), id);
    }
    return lookup;
  }();
  return map;
}

void PopulateFromJson(const Json& json,
                      const std::filesystem::path& source,
                      CLanguage::TranslationMap& target,
                      std::unordered_set<CLanguage::STRING_ID, CLanguage::StringIdHash>* localizedKeys) {
  const auto& lookup = KeyLookup();
  for (const auto& [key, value] : json.items()) {
    if (!value.is_string()) {
      SPDLOG_WARN("Ignoring non-string value for key {} in {}", key, source.string());
      continue;
    }

    const auto it = lookup.find(key);
    if (it == lookup.end()) {
      SPDLOG_WARN("Unknown translation key {} in {}", key, source.string());
      continue;
    }

    target[it->second] = zSTRING(value.get<std::string>().c_str());
    if (localizedKeys) {
      localizedKeys->insert(it->second);
    }
  }
}

bool PathsEquivalent(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
  std::error_code ec;
  const bool equivalent = std::filesystem::equivalent(lhs, rhs, ec);
  if (ec) {
    return false;
  }
  return equivalent;
}

} // namespace

CLanguage::CLanguage(const char* file) { Load(std::filesystem::path(file)); }

CLanguage::CLanguage(const std::filesystem::path& file) { Load(file); }

CLanguage::~CLanguage() = default;

const zSTRING& CLanguage::GetString(STRING_ID id) const {
  static const zSTRING kEmpty = "EMPTY";
  const auto it = translations_.find(id);
  if (it != translations_.end()) {
    return it->second;
  }

  SPDLOG_ERROR("Missing translation for key {} even after falling back to English.",
               ToLocalizationKey(id));
  return kEmpty;
}

void CLanguage::Load(const std::filesystem::path& file) {
  translations_.clear();

  const auto requested = std::filesystem::weakly_canonical(file);
  const auto baseDir = requested.parent_path();
  const auto englishPath = baseDir / "en.json";

  if (auto englishJson = LoadJsonFile(englishPath)) {
    PopulateFromJson(*englishJson, englishPath, translations_, nullptr);
  } else {
    SPDLOG_ERROR("Unable to load fallback language file {}", englishPath.string());
  }

  for (auto id : AllStringIds()) {
    if (!translations_.contains(id)) {
      SPDLOG_ERROR("English localization missing key {}.", ToLocalizationKey(id));
    }
  }

  if (!std::filesystem::exists(requested)) {
    SPDLOG_ERROR("Unable to open language file {}", requested.string());
    return;
  }

  if (PathsEquivalent(requested, englishPath)) {
    return;
  }

  auto localizedJson = LoadJsonFile(requested);
  if (!localizedJson) {
    SPDLOG_ERROR("Failed to parse localization file {}. Using English fallback only.", requested.string());
    return;
  }

  std::unordered_set<STRING_ID, StringIdHash> localizedKeys;
  PopulateFromJson(*localizedJson, requested, translations_, &localizedKeys);

  for (auto id : AllStringIds()) {
    if (!localizedKeys.contains(id)) {
      SPDLOG_WARN("Missing translation {} in {}. Falling back to English.",
                  ToLocalizationKey(id), requested.string());
    }
  }
}

std::string_view CLanguage::ToLocalizationKey(STRING_ID id) noexcept {
  switch (id) {
#define CLANGUAGE_ENUM_CASE(name)                                                                    \
    case name:                                                                                        \
      return #name;
    CLANGUAGE_FOR_EACH_STRING_ID(CLANGUAGE_ENUM_CASE)
#undef CLANGUAGE_ENUM_CASE
  }
  return "";
}

std::span<const CLanguage::STRING_ID> CLanguage::AllStringIds() noexcept {
  static constexpr std::array<STRING_ID, 97> kIds = {
#define CLANGUAGE_ENUM_VALUE(name) name,
      CLANGUAGE_FOR_EACH_STRING_ID(CLANGUAGE_ENUM_VALUE)
#undef CLANGUAGE_ENUM_VALUE
  };
  return kIds;
}

std::string CLanguage::ReadLanguageName(const std::filesystem::path& path) {
  if (auto json = LoadJsonFile(path)) {
    const auto key = ToLocalizationKey(STRING_ID::LANGUAGE);
    if (auto it = json->find(key); it != json->end() && it->is_string()) {
      return it->get<std::string>();
    }
    SPDLOG_WARN("Language file {} does not define {}. Using file stem as display name.",
                path.string(), key);
  }
  return path.stem().string();
}

void CLanguage::RemovePolishCharactersFromWideString(std::wstring& txt) {
  wchar_t letter[1] = {0x22};
  size_t found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"Ż");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"Z");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"&#8221;");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 7, letter);
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"&#8211;");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 7, L"-");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ż");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"z");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ł");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"l");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"Ł");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"L");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ą");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"a");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ń");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"n");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ę");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"e");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ś");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"s");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ć");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"c");
  }
  found = 0;
  while (found != std::string::npos) {
    found = txt.find(L"ź");
    if (found == std::string::npos)
      break;
    else
      txt.replace(found, 1, L"z");
  }
}
