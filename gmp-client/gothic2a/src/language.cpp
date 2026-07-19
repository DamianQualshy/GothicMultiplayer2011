
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

#include "language.h"

#include <spdlog/spdlog.h>

#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>

#include "localization_utils.h"

// ============================================================================
// LanguageManager Implementation
// ============================================================================

LanguageManager& LanguageManager::Instance() {
  static LanguageManager instance;
  return instance;
}

void LanguageManager::LoadLanguages(const char* languageDir, int languageIndex) {
  languageDir_ = languageDir;
  availableLanguages_.clear();

  const std::string indexPath = languageDir_ + "index";
  std::ifstream ifs(indexPath, std::ifstream::in);

  if (!ifs.is_open()) {
    SPDLOG_ERROR("LanguageManager: Couldn't find language index file {}", indexPath);
    // Add a fallback language
    LanguageInfo fallback;
    fallback.filename = "English.json";
    fallback.displayName = zSTRING("English");
    fallback.encoding = localization::LanguageEncoding::kCp1252;
    availableLanguages_.push_back(fallback);
    SPDLOG_INFO("LanguageManager: Added fallback English language");

    // Load the fallback language
    std::string langPath = languageDir_ + fallback.filename;
    Language::Instance().LoadFromJsonFile(langPath);
    return;
  }

  // Read language file names from index
  std::vector<std::string> languageFiles;
  std::string line;
  while (std::getline(ifs, line)) {
    // Remove carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    languageFiles.push_back(line);
  }

  // Remove duplicate at the end if exists
  if (languageFiles.size() >= 2 && languageFiles.back() == languageFiles[languageFiles.size() - 2]) {
    languageFiles.pop_back();
  }

  // Load metadata for each language file
  for (const auto& filename : languageFiles) {
    const std::string langPath = languageDir_ + filename;
    std::ifstream langFile(langPath, std::ifstream::in);

    if (!langFile.is_open()) {
      SPDLOG_ERROR("LanguageManager: Couldn't open language file {}", langPath);
      // Add with filename as fallback
      LanguageInfo info;
      info.filename = filename;
      info.displayName = zSTRING(filename.c_str());
      info.encoding = localization::LanguageEncoding::kNone;
      availableLanguages_.push_back(info);
      continue;
    }

    try {
      nlohmann::json jsonData;
      langFile >> jsonData;

      LanguageInfo info;
      info.filename = filename;

      // Get display name from JSON
      auto rawLanguageName = jsonData.value("LANGUAGE", std::string{});
      if (rawLanguageName.empty()) {
        rawLanguageName = filename;
      }

      // Detect encoding and convert from UTF-8
      info.encoding = localization::DetectLanguageEncoding(rawLanguageName, langPath);
      const auto localizedName = localization::ConvertFromUtf8(rawLanguageName, info.encoding);
      info.displayName = zSTRING(localizedName.c_str());

      availableLanguages_.push_back(info);
      SPDLOG_DEBUG("LanguageManager: Loaded language {} ({})", filename, localizedName);

    } catch (const std::exception& ex) {
      SPDLOG_ERROR("LanguageManager: Failed to parse language file {}: {}", langPath, ex.what());
      // Add with filename as fallback
      LanguageInfo info;
      info.filename = filename;
      info.displayName = zSTRING(filename.c_str());
      info.encoding = localization::LanguageEncoding::kNone;
      availableLanguages_.push_back(info);
    }
  }

  SPDLOG_INFO("LanguageManager: Loaded {} languages", availableLanguages_.size());

  // Load the active language
  if (availableLanguages_.empty()) {
    SPDLOG_ERROR("LanguageManager: No languages available to load");
    return;
  }

  // Default to English (index 0) if not specified or invalid
  int targetIndex = languageIndex;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(availableLanguages_.size())) {
    SPDLOG_WARN("LanguageManager: Invalid language index {}, defaulting to first language", languageIndex);
    targetIndex = 0;
  }

  const auto& langInfo = availableLanguages_[targetIndex];
  std::string langPath = languageDir_ + langInfo.filename;

  SPDLOG_INFO("LanguageManager: Loading active language: {} from {}", langInfo.displayName.ToChar(), langPath);

  bool success = Language::Instance().LoadFromJsonFile(langPath);
  if (!success) {
    SPDLOG_ERROR("LanguageManager: Failed to load language file {}", langPath);
  }
}

const LanguageManager::LanguageInfo* LanguageManager::GetLanguage(int index) const {
  if (index < 0 || index >= static_cast<int>(availableLanguages_.size())) {
    return nullptr;
  }
  return &availableLanguages_[index];
}

// ============================================================================
// Language Implementation
// ============================================================================

namespace {
constexpr std::size_t kStringCount = static_cast<std::size_t>(Language::SRVLIST_PLAYERNUMBER) + 1;
constexpr std::array<std::string_view, 4> kKnownFontPrefixes = {"CP1250_", "CP1251_", "CP1252_", "CP1254_"};

const std::array<const char*, kStringCount> kStringKeys = {"LANGUAGE",
                                                           "WRITE_NICKNAME",
                                                           "MMENU_CHSERVER",
                                                           "MMENU_OPTIONS",
                                                           "MMENU_LEAVEGAME",
                                                           "MMENU_ONLINEOPTIONS",
                                                           "MMENU_BACK",
                                                           "MMENU_LOGCHAT",
                                                           "MMENU_WATCH",
                                                           "MMENU_SETWATCHPOS",
                                                           "WATCHPOS_INSTRUCTIONS",
                                                           "WATCHPOS_RETURN",
                                                           "CWATCH_REALTIME",
                                                           "CWATCH_GAMETIME",
                                                           "MMENU_NICKNAME",
                                                           "MMENU_ANTIALIASING",
                                                           "MMENU_JOYSTICK",
                                                           "MMENU_CHATLINES",
                                                           "MMENU_LANGUAGE",
                                                           "MMENU_INTROVIDEOS",
                                                           "MMENU_ON",
                                                           "MMENU_OFF",
                                                           "MMENU_YES",
                                                           "MMENU_NO",
                                                           "INGAMEM_BACKTOGAME",
                                                           "EXITTOMAINMENU",
                                                           "DISCONNECTED",
                                                           "ITEM_TOOFAR",
                                                           "SRVLIST_ALL",
                                                           "SRVLIST_FAVOURITES",
                                                           "SRVLIST_NAME",
                                                           "SRVLIST_MAP",
                                                           "SRVLIST_PLAYERNUMBER"};


std::string_view GetFontPrefixForEncoding(localization::LanguageEncoding encoding) {
  switch (encoding) {
    case localization::LanguageEncoding::kCp1250:
      return "CP1250_";
    case localization::LanguageEncoding::kCp1251:
      return "CP1251_";
    case localization::LanguageEncoding::kCp1252:
      return "CP1252_";
    case localization::LanguageEncoding::kCp1254:
      return "CP1254_";
    case localization::LanguageEncoding::kNone:
    default:
      return {};
  }
}

bool HasKnownFontPrefix(std::string_view font_name) {
  for (const auto prefix : kKnownFontPrefixes) {
    if (!prefix.empty() && font_name.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool Language::LoadFromJsonFile(const std::filesystem::path& file) {
  std::ifstream ifs(file);
  if (!ifs.good()) {
    SPDLOG_ERROR("Failed to open language file {}: {}", file.string(), strerror(errno));
    return false;
  }

  nlohmann::json json_data;
  try {
    ifs >> json_data;
  } catch (const std::exception& ex) {
    SPDLOG_ERROR("Failed to parse language file {}: {}", file.string(), ex.what());
    return false;
  }

  data.clear();
  const std::string language_field = json_data.value("LANGUAGE", std::string{});
  const auto encoding = localization::DetectLanguageEncoding(language_field, file);
  encoding_ = encoding;
  fontPrefix_ = GetFontPrefixForEncoding(encoding);

  data.resize(kStringCount);
  for (std::size_t i = 0; i < kStringKeys.size(); ++i) {
    const auto key = kStringKeys[i];
    std::string value;
    try {
      if (!json_data.contains(key)) {
        SPDLOG_WARN("Missing language key '{}' in file {}", key, file.string());
      }
      value = json_data.value(key, std::string{});
    } catch (const std::exception& ex) {
      SPDLOG_WARN("Language key '{}' in file {} has incompatible type: {}", key, file.string(), ex.what());
    }
    value = localization::ConvertFromUtf8(value, encoding);
    data[i] = value.c_str();
  }

  return true;
}

std::string Language::ApplyFontPrefix(std::string_view fontName) const {
  if (fontName.empty() || fontPrefix_.empty() || HasKnownFontPrefix(fontName)) {
    return std::string(fontName);
  }
  std::string prefixed;
  prefixed.reserve(fontPrefix_.size() + fontName.size());
  prefixed.append(fontPrefix_);
  prefixed.append(fontName);
  return prefixed;
}

void Language::RemovePolishCharactersFromWideString(std::wstring& txt) {
  wchar_t letter[1] = {0x22};
  size_t found;
  found = 0;
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
};

const zSTRING& Language::operator[](unsigned long i) const {
  thread_local zSTRING tl_unknown = "UNKNOWN TRANSLATION STRING";
  return (i < data.size()) ? data[i] : tl_unknown;
}
