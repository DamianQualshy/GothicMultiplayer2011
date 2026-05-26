/*
MIT License

Copyright (c) 2026 Gothic Multiplayer Team.

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

#include "lua_music_theme.h"

#include <cstddef>

#include "ZenGin/zGothicAPI.h"

using namespace Gothic_II_Addon;

namespace gmp::gothic {
namespace {
struct MusicThemeState {
  zCMusicTheme* theme{nullptr};
  std::string file_name;
  float volume{zMUS_THEME_VOL_DEFAULT};
  bool loop{false};
  float reverb_mix{0.0f};
  float reverb_time{0.0f};
  int trans_type{zMUS_TR_DEFAULT};
  int trans_sub_type{zMUS_TRSUB_DEFAULT};
  std::string name;
  bool file_name_overridden{false};
  bool volume_overridden{false};
  bool loop_overridden{false};
  bool reverb_mix_overridden{false};
  bool reverb_time_overridden{false};
  bool trans_type_overridden{false};
  bool trans_sub_type_overridden{false};
  bool name_overridden{false};
};

MusicThemeState& State() {
  static MusicThemeState state;
  return state;
}

std::string ToString(const zSTRING& value) {
  return std::string(value.ToChar());
}

std::string ThemeName(zCMusicTheme* theme) {
  if (!theme) {
    return {};
  }

  if (!theme->name.IsEmpty()) {
    return ToString(theme->name);
  }

  return ToString(theme->fileName);
}

bool ReadStringArg(sol::variadic_args args, std::string& out) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    sol::object arg = args[i];
    if (arg.is<std::string>()) {
      out = arg.as<std::string>();
      return true;
    }
  }
  return false;
}

void ApplyState(zCMusicTheme* theme) {
  if (!theme) {
    return;
  }

  auto& state = State();
  if (state.file_name_overridden && !state.file_name.empty()) {
    theme->fileName = zSTRING(state.file_name.c_str());
  }
  if (state.volume_overridden) {
    theme->vol = state.volume;
  }
  if (state.loop_overridden) {
    theme->loop = state.loop ? 1 : 0;
  }
  if (state.reverb_mix_overridden) {
    theme->reverbMix = state.reverb_mix;
  }
  if (state.reverb_time_overridden) {
    theme->reverbTime = state.reverb_time;
  }
  if (state.trans_type_overridden) {
    theme->trType = static_cast<zTMus_TransType>(state.trans_type);
  }
  if (state.trans_sub_type_overridden) {
    theme->trSubType = static_cast<zTMus_TransSubType>(state.trans_sub_type);
  }
  if (state.name_overridden && !state.name.empty()) {
    theme->name = zSTRING(state.name.c_str());
  }
}

zCMusicTheme* CurrentTheme() {
  auto& state = State();
  if (state.theme) {
    return state.theme;
  }

  if (zmusic) {
    state.theme = zmusic->GetActiveTheme();
  }
  return state.theme;
}

bool LoadThemeFromState() {
  auto& state = State();
  if (!zmusic || state.file_name.empty()) {
    return false;
  }

  zSTRING file_name(state.file_name.c_str());
  state.theme = zmusic->LoadThemeByScript(file_name);
  if (!state.theme) {
    state.theme = zmusic->LoadTheme(file_name);
  }
  if (state.theme && !state.file_name_overridden) {
    state.file_name = ToString(state.theme->fileName);
  }
  ApplyState(state.theme);
  return state.theme != nullptr;
}

void SetFileNameRaw(const std::string& file_name) {
  auto& state = State();
  state.file_name = file_name;
  state.file_name_overridden = true;
  if (state.theme) {
    state.theme->fileName = zSTRING(file_name.c_str());
  }
}

std::string GetFileNameRaw() {
  if (auto* theme = CurrentTheme()) {
    return ToString(theme->fileName);
  }
  return State().file_name;
}

void SetVolumeRaw(float volume) {
  auto& state = State();
  state.volume = volume;
  state.volume_overridden = true;
  if (state.theme) {
    state.theme->vol = volume;
    if (zmusic && state.theme->IsActive()) {
      zmusic->SetVolume(volume);
    }
  }
}

float GetVolumeRaw() {
  if (auto* theme = CurrentTheme()) {
    return theme->vol;
  }
  return State().volume;
}

void SetLoopRaw(bool loop) {
  auto& state = State();
  state.loop = loop;
  state.loop_overridden = true;
  if (state.theme) {
    state.theme->loop = loop ? 1 : 0;
  }
}

bool GetLoopRaw() {
  if (auto* theme = CurrentTheme()) {
    return theme->loop != 0;
  }
  return State().loop;
}

void SetReverbMixRaw(float value) {
  auto& state = State();
  state.reverb_mix = value;
  state.reverb_mix_overridden = true;
  if (state.theme) {
    state.theme->reverbMix = value;
  }
}

float GetReverbMixRaw() {
  if (auto* theme = CurrentTheme()) {
    return theme->reverbMix;
  }
  return State().reverb_mix;
}

void SetReverbTimeRaw(float value) {
  auto& state = State();
  state.reverb_time = value;
  state.reverb_time_overridden = true;
  if (state.theme) {
    state.theme->reverbTime = value;
  }
}

float GetReverbTimeRaw() {
  if (auto* theme = CurrentTheme()) {
    return theme->reverbTime;
  }
  return State().reverb_time;
}

void SetTransTypeRaw(int value) {
  auto& state = State();
  state.trans_type = value;
  state.trans_type_overridden = true;
  if (state.theme) {
    state.theme->trType = static_cast<zTMus_TransType>(value);
  }
}

int GetTransTypeRaw() {
  if (auto* theme = CurrentTheme()) {
    return static_cast<int>(theme->trType);
  }
  return State().trans_type;
}

void SetTransSubTypeRaw(int value) {
  auto& state = State();
  state.trans_sub_type = value;
  state.trans_sub_type_overridden = true;
  if (state.theme) {
    state.theme->trSubType = static_cast<zTMus_TransSubType>(value);
  }
}

int GetTransSubTypeRaw() {
  if (auto* theme = CurrentTheme()) {
    return static_cast<int>(theme->trSubType);
  }
  return State().trans_sub_type;
}

void SetNameRaw(const std::string& name) {
  auto& state = State();
  state.name = name;
  state.name_overridden = true;
  if (state.theme) {
    state.theme->name = zSTRING(name.c_str());
  }
}

std::string GetNameRaw() {
  if (auto* theme = CurrentTheme()) {
    return ToString(theme->name);
  }
  return State().name;
}
}  // namespace

/* luagmp (class)
*
* Static access to the engine music theme system.
*
* @version  0.3.0
* @name     MusicTheme
* @side     client
* @category Game
*
*/

/* luagmp (method)
*
* Loads a music theme by script name, falling back to a raw file name.
*
* @name     loadTheme
* @param    (string) fileName    Theme script name or file name.
* @return   (boolean)            True if the theme was loaded.
*
*/
bool LuaMusicTheme::loadTheme(const std::string& file_name) {
  auto& state = State();
  state.file_name = file_name;
  state.file_name_overridden = false;
  return LoadThemeFromState();
}

/* luagmp (method)
*
* Plays the currently loaded theme.
*
* @name     playTheme
* @return   (boolean) True if playback was started.
*
*/
bool LuaMusicTheme::playTheme() {
  auto& state = State();
  if (!state.theme && !LoadThemeFromState()) {
    return false;
  }

  if (!zmusic || !state.theme) {
    return false;
  }

  ApplyState(state.theme);
  const float volume = state.theme->vol;
  const zTMus_TransType trans_type = state.theme->trType;
  const zTMus_TransSubType trans_sub_type = state.theme->trSubType;
  zmusic->PlayTheme(state.theme, volume, trans_type, trans_sub_type);
  return true;
}

/* luagmp (method)
*
* Stops music theme playback.
*
* @name     stopTheme
*
*/
void LuaMusicTheme::stopTheme() {
  if (zmusic) {
    zmusic->Stop();
  }
}

/* luagmp (property)
*
* Represents the loaded theme file name.
*
* @name     fileName
* @return   (string) Theme file name.
*
*/
std::string LuaMusicTheme::getFileName() {
  return GetFileNameRaw();
}

void LuaMusicTheme::setFileName(const std::string& file_name) {
  SetFileNameRaw(file_name);
}

/* luagmp (property)
*
* Represents the theme volume.
*
* @name     volume
* @return   (number) Theme volume.
*
*/
float LuaMusicTheme::getVolume() {
  return GetVolumeRaw();
}

void LuaMusicTheme::setVolume(float volume) {
  SetVolumeRaw(volume);
}

/* luagmp (property)
*
* Represents whether the theme should loop.
*
* @name     loop
* @return   (boolean) True if looping is enabled.
*
*/
bool LuaMusicTheme::getLoop() {
  return GetLoopRaw();
}

void LuaMusicTheme::setLoop(bool loop) {
  SetLoopRaw(loop);
}

/* luagmp (property)
*
* Represents whether the engine music system is disabled.
*
* @name     disabled
* @return   (boolean) True if music is disabled.
*
*/
bool LuaMusicTheme::getDisabled() {
  return zCMusicSystem::s_musicSystemDisabled != 0;
}

void LuaMusicTheme::setDisabled(bool disabled) {
  zCMusicSystem::DisableMusicSystem(disabled ? 1 : 0);
}

/* luagmp (property)
*
* Represents the active engine music theme name or file name.
*
* @name     activeTheme
* @return   (string|nil) Active theme identifier.
*
*/
sol::object LuaMusicTheme::getActiveTheme(sol::this_state ts) {
  sol::state_view lua(ts);
  zCMusicTheme* active = zmusic ? zmusic->GetActiveTheme() : nullptr;
  if (!active) {
    return sol::make_object(lua, sol::lua_nil);
  }

  return sol::make_object(lua, ThemeName(active));
}

void LuaMusicTheme::setActiveTheme(sol::object value) {
  if (value.get_type() == sol::type::nil) {
    stopTheme();
    return;
  }

  if (value.is<std::string>()) {
    loadTheme(value.as<std::string>());
    playTheme();
  }
}

/* luagmp (property)
*
* Represents the theme reverb mix.
*
* @name     reverbMix
* @return   (number) Reverb mix.
*
*/
float LuaMusicTheme::getReverbMix() {
  return GetReverbMixRaw();
}

void LuaMusicTheme::setReverbMix(float value) {
  SetReverbMixRaw(value);
}

/* luagmp (property)
*
* Represents the theme reverb time.
*
* @name     reverbTime
* @return   (number) Reverb time.
*
*/
float LuaMusicTheme::getReverbTime() {
  return GetReverbTimeRaw();
}

void LuaMusicTheme::setReverbTime(float value) {
  SetReverbTimeRaw(value);
}

/* luagmp (property)
*
* Represents the theme transition type.
*
* @name     transType
* @return   (number) Transition type.
*
*/
int LuaMusicTheme::getTransType() {
  return GetTransTypeRaw();
}

void LuaMusicTheme::setTransType(int value) {
  SetTransTypeRaw(value);
}

/* luagmp (property)
*
* Represents the theme transition subtype.
*
* @name     transSubType
* @return   (number) Transition subtype.
*
*/
int LuaMusicTheme::getTransSubType() {
  return GetTransSubTypeRaw();
}

void LuaMusicTheme::setTransSubType(int value) {
  SetTransSubTypeRaw(value);
}

/* luagmp (property)
*
* Represents the theme script name.
*
* @name     name
* @return   (string) Theme name.
*
*/
std::string LuaMusicTheme::getName() {
  return GetNameRaw();
}

void LuaMusicTheme::setName(const std::string& name) {
  SetNameRaw(name);
}

void BindMusicTheme(sol::state& lua) {
  sol::usertype<LuaMusicTheme> music_theme_type = lua.new_usertype<LuaMusicTheme>("MusicTheme", sol::no_constructor);

  music_theme_type["loadTheme"] = [](sol::variadic_args args) {
    std::string file_name;
    return ReadStringArg(args, file_name) && LuaMusicTheme::loadTheme(file_name);
  };
  music_theme_type["playTheme"] = [](sol::variadic_args) { return LuaMusicTheme::playTheme(); };
  music_theme_type["stopTheme"] = [](sol::variadic_args) { LuaMusicTheme::stopTheme(); };

  music_theme_type["fileName"] = sol::property(&LuaMusicTheme::getFileName, &LuaMusicTheme::setFileName);
  music_theme_type["volume"] = sol::property(&LuaMusicTheme::getVolume, &LuaMusicTheme::setVolume);
  music_theme_type["loop"] = sol::property(&LuaMusicTheme::getLoop, &LuaMusicTheme::setLoop);
  music_theme_type["disabled"] = sol::property(&LuaMusicTheme::getDisabled, &LuaMusicTheme::setDisabled);
  music_theme_type["activeTheme"] = sol::property(&LuaMusicTheme::getActiveTheme, &LuaMusicTheme::setActiveTheme);
  music_theme_type["reverbMix"] = sol::property(&LuaMusicTheme::getReverbMix, &LuaMusicTheme::setReverbMix);
  music_theme_type["reverbTime"] = sol::property(&LuaMusicTheme::getReverbTime, &LuaMusicTheme::setReverbTime);
  music_theme_type["transType"] = sol::property(&LuaMusicTheme::getTransType, &LuaMusicTheme::setTransType);
  music_theme_type["transSubType"] = sol::property(&LuaMusicTheme::getTransSubType, &LuaMusicTheme::setTransSubType);
  music_theme_type["name"] = sol::property(&LuaMusicTheme::getName, &LuaMusicTheme::setName);

  lua["MusicTheme"] = LuaMusicTheme();
}

}  // namespace gmp::gothic
