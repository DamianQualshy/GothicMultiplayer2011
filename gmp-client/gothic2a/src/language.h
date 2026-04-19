
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

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "localization_utils.h"

#include "ZenGin/zGothicAPI.h"

// Manages available languages and their metadata
class LanguageManager {
public:
  struct LanguageInfo {
    std::string filename;  // e.g., "English.json"
    zSTRING displayName;   // Localized display name from JSON
    localization::LanguageEncoding encoding;
  };

  LanguageManager() = default;

  // Load all available languages from the specified directory
  // Also loads the active language based on languageIndex (or defaults to English if invalid)
  void LoadLanguages(const char* languageDir = ".\\Multiplayer\\Localization\\", int languageIndex = -1);

  // Check if languages have been loaded
  bool IsLoaded() const {
    return !availableLanguages_.empty();
  }

  // Get list of all available languages
  const std::vector<LanguageInfo>& GetAvailableLanguages() const {
    return availableLanguages_;
  }

  // Get a specific language by index
  const LanguageInfo* GetLanguage(int index) const;

  // Get the number of available languages
  size_t GetLanguageCount() const {
    return availableLanguages_.size();
  }

  // Get the language directory path
  const std::string& GetLanguageDir() const {
    return languageDir_;
  }

  // Clear all loaded data to prevent zSTRING destructor issues during shutdown
  void Clear() {
    availableLanguages_.clear();
  }

  // Singleton instance for convenience
  static LanguageManager& Instance();

private:
  std::vector<LanguageInfo> availableLanguages_;
  std::string languageDir_;
};

class Language {
public:
  enum STRING_ID {
    LANGUAGE = 0,
    WRITE_NICKNAME,
    MMENU_CHSERVER,
    MMENU_OPTIONS,
    MMENU_LEAVEGAME,
    MMENU_ONLINEOPTIONS,
    MMENU_BACK,
    MMENU_LOGCHAT,
    MMENU_WATCH,
    MMENU_SETWATCHPOS,
    WATCHPOS_INSTRUCTIONS,
    WATCHPOS_RETURN,
    CWATCH_REALTIME,
    CWATCH_GAMETIME,
    MMENU_NICKNAME,
    MMENU_ANTIALIASING,
    MMENU_JOYSTICK,
    MMENU_CHATLINES,
    MMENU_LANGUAGE,
    MMENU_INTROVIDEOS,
    MMENU_ON,
    MMENU_OFF,
    MMENU_YES,
    MMENU_NO,
    INGAMEM_BACKTOGAME,
    NOPLAYERS,
    EXITTOMAINMENU,
    DISCONNECTED,
    ITEM_TOOFAR,
    INV_HOWMUCH,
    SRVLIST_ALL,
    SRVLIST_FAVOURITES,
    SRVLIST_NAME,
    SRVLIST_MAP,
    SRVLIST_PLAYERNUMBER
  };

  bool LoadFromJsonFile(const std::filesystem::path& file);
  bool IsLoaded() const {
    return !data.empty();
  }

  localization::LanguageEncoding GetEncoding() const {
    return encoding_;
  }

  const std::string& GetFontPrefix() const {
    return fontPrefix_;
  }

  std::string ApplyFontPrefix(std::string_view fontName) const;


  // Clear all loaded data to prevent zSTRING destructor issues during shutdown
  void Clear() {
    data.clear();
    encoding_ = localization::LanguageEncoding::kNone;
    fontPrefix_.clear();
  }

  const zSTRING& operator[](unsigned long) const;
  void RemovePolishCharactersFromWideString(std::wstring& txt);

  static Language& Instance() {
    static Language instance;
    return instance;
  }

private:
  Language() = default;

  std::vector<zSTRING> data;
  localization::LanguageEncoding encoding_{localization::LanguageEncoding::kNone};
  std::string fontPrefix_;
};