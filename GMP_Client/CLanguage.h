
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

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ZenGin/zGothicAPI.h"

class CLanguage {
public:
  #define CLANGUAGE_FOR_EACH_STRING_ID(X) \
    X(LANGUAGE) \
    X(WRITE_NICKNAME) \
    X(CHOOSE_APPERANCE) \
    X(FACE_APPERANCE) \
    X(HEAD_MODEL) \
    X(SKIN_TEXTURE) \
    X(CHOOSE_SERVER) \
    X(WRITE_SERVER_ADDR) \
    X(CHOOSE_SERVER_TIP) \
    X(MANUAL_IP_TIP) \
    X(MMENU_CHSERVER) \
    X(MMENU_OPTIONS) \
    X(MMENU_APPEARANCE) \
    X(MMENU_LEAVEGAME) \
    X(EMPTY_SERVER_LIST) \
    X(APP_INFO1) \
    X(ERR_CONN_NO_ERROR) \
    X(ERR_CONN_FAIL) \
    X(ERR_CONN_ALREADY_CONNECTED) \
    X(ERR_CONN_SRV_FULL) \
    X(ERR_CONN_BANNED) \
    X(ERR_CONN_INCOMP_TECHNIC) \
    X(MMENU_ONLINEOPTIONS) \
    X(MMENU_BACK) \
    X(CLASS_NAME) \
    X(TEAM_NAME) \
    X(SELECT_CONTROLS) \
    X(MMENU_LOGCHATYES) \
    X(MMENU_LOGCHATNO) \
    X(MMENU_WATCHON) \
    X(MMENU_WATCHOFF) \
    X(MMENU_SETWATCHPOS) \
    X(CWATCH_REALTIME) \
    X(CWATCH_GAMETIME) \
    X(MMENU_NICKNAME) \
    X(MMENU_ANTIALIASINGYES) \
    X(MMENU_ANTIAlIASINGNO) \
    X(MMENU_JOYSTICKYES) \
    X(MMENU_JOYSTICKNO) \
    X(MMENU_POTIONKEYSYES) \
    X(MMENU_POTIONKEYSNO) \
    X(MMENU_CHATLINES) \
    X(INGAMEM_HELP) \
    X(INGAMEM_BACKTOGAME) \
    X(HCONTROLS) \
    X(HCHAT) \
    X(HCHATMAIN) \
    X(HCHATWHISPER) \
    X(CHAT_WHISPERTONOONE) \
    X(CHAT_WHISPERTO) \
    X(CHAT_CANTWHISPERTOYOURSELF) \
    X(SOMEONEDISCONNECT_FROM_SERVER) \
    X(NOPLAYERS) \
    X(EXITTOMAINMENU) \
    X(SRV_IP) \
    X(SRV_NAME) \
    X(SRV_MAP) \
    X(SRV_PLAYERS) \
    X(DISCONNECTED) \
    X(SOMEONE_JOIN_GAME) \
    X(CHAT_PLAYER_DOES_NOT_EXIST) \
    X(ANIMS_MENU) \
    X(HPLAYERLIST) \
    X(HMAP) \
    X(HANIMSMENU) \
    X(SHOWHOW) \
    X(WHISPERSTOYOU) \
    X(PRESSFORWHISPER) \
    X(CHAT_WRONGWINDOW) \
    X(DEATHMATCH) \
    X(TEAM_DEATHMATCH) \
    X(WALK_STYLE) \
    X(UNMUTE_TIME) \
    X(PLIST_PM) \
    X(KILLEDSOMEONE_MSG) \
    X(WB_NEWMAP) \
    X(WB_LOADMAP) \
    X(WB_SAVEMAP) \
    X(KILL_PLAYER) \
    X(GOTO_PLAYER) \
    X(ITEM_TOOFAR) \
    X(KEYBOARD_POLISH) \
    X(KEYBOARD_GERMAN) \
    X(KEYBOARD_RUSSIAN) \
    X(INTRO_YES) \
    X(INTRO_NO) \
    X(MERRY_CHRISTMAS) \
    X(NEW_NEWS) \
    X(INV_HOWMUCH) \
    X(CLASS_DESCRIPTION) \
    X(START_OBSERVATION) \
    X(END_OBSERVATION) \
    X(SRVLIST_ALL) \
    X(SRVLIST_FAVOURITES) \
    X(SRVLIST_NAME) \
    X(SRVLIST_MAP) \
    X(SRVLIST_PLAYERNUMBER)

  enum STRING_ID {
#define CLANGUAGE_DECLARE_ENUM(name) name,
    CLANGUAGE_FOR_EACH_STRING_ID(CLANGUAGE_DECLARE_ENUM)
#undef CLANGUAGE_DECLARE_ENUM
  };

  struct StringIdHash {
    size_t operator()(STRING_ID id) const noexcept { return static_cast<size_t>(id); }
  };

  using TranslationMap = std::unordered_map<STRING_ID, Gothic_II_Addon::zSTRING, StringIdHash>;

  explicit CLanguage(const char* file);
  explicit CLanguage(const std::filesystem::path& file);
  ~CLanguage();

  const Gothic_II_Addon::zSTRING& GetString(STRING_ID id) const;
  void RemovePolishCharactersFromWideString(std::wstring& txt);

  /**
   * Maps an enum value to the string identifier used inside localization JSON files.
   * This is useful for tooling that needs to validate or generate translations.
   */
  static std::string_view ToLocalizationKey(STRING_ID id) noexcept;
  static std::span<const STRING_ID> AllStringIds() noexcept;
  static std::string ReadLanguageName(const std::filesystem::path& path);

private:
  void Load(const std::filesystem::path& file);

  TranslationMap translations_;
};