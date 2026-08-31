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

#include "lua_voice.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "net_game.h"

namespace gmp::gothic {
namespace {

bool IsValidVoicePlayerId(std::int64_t player_id) {
  return player_id > 0 && static_cast<std::uint64_t>(player_id) <= std::numeric_limits<std::uint32_t>::max();
}

bool ReadBoolArg(sol::variadic_args args, bool& value) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    sol::object arg = args[i];
    if (arg.get_type() == sol::type::boolean) {
      value = arg.as<bool>();
      return true;
    }
  }
  return false;
}

bool ReadNumberArgs(sol::variadic_args args, std::vector<double>& values) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    sol::object arg = args[i];
    if (arg.get_type() == sol::type::number) {
      values.push_back(arg.as<double>());
    }
  }
  return !values.empty();
}

bool ReadIntegerArg(sol::variadic_args args, int& value) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    sol::object arg = args[i];
    if (arg.get_type() != sol::type::number) {
      continue;
    }

    const double number = arg.as<double>();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
      return false;
    }

    value = static_cast<int>(number);
    return true;
  }
  return false;
}

bool ReadStringArg(sol::variadic_args args, std::string& value) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    sol::object arg = args[i];
    if (arg.get_type() == sol::type::string) {
      value = arg.as<std::string>();
      return true;
    }
  }
  return false;
}

}  // namespace

/* luagmp (class)
*
* This singleton class controls native proximity voice chat for the local client.
*
* @version  0.3.0
* @name     Voice
* @side     client
* @category Game
*
*/

/* luagmp (method)
*
* This function checks whether the server offers voice chat.
*
* @name     isAvailable
* @return   (boolean) True when the server has proximity voice chat enabled.
*
*/
bool LuaVoice::isAvailable() {
  return NetGame::Instance().IsVoiceChatAvailable();
}

/* luagmp (method)
*
* This function checks whether voice chat is effectively enabled for this client.
*
* @name     isEnabled
* @return   (boolean) True when server, user, and script settings all allow voice chat.
*
*/
bool LuaVoice::isEnabled() {
  return NetGame::Instance().IsVoiceChatEnabled();
}

/* luagmp (method)
*
* This function changes the resource-controlled voice chat gate. Enabling it does not override a disabled user setting.
*
* @name     setEnabled
* @param    (boolean) enabled  Whether this resource permits voice chat.
* @return   (boolean) The resulting effective voice chat state.
*
*/
bool LuaVoice::setEnabled(bool enabled) {
  NetGame& net_game = NetGame::Instance();
  net_game.SetVoiceChatScriptEnabled(enabled);
  return net_game.IsVoiceChatEnabled();
}

/* luagmp (method)
*
* This function returns the effective local proximity voice range.
*
* @name     getRange
* @return   (number) Voice range in game units, or zero when unavailable.
*
*/
float LuaVoice::getRange() {
  return NetGame::Instance().GetVoiceChatRange();
}

/* luagmp (method)
*
* This function limits the local client's proximity voice range. The server's
* range remains authoritative, so this setting can only reduce the range.
*
* @name     setRange
* @param    (number) range  Maximum local voice range in game units.
* @return   (boolean)       True when the range was accepted.
*
*/
bool LuaVoice::setRange(float range) {
  return NetGame::Instance().SetVoiceChatRange(range);
}

/* luagmp (method)
*
* This function returns the resource output-volume multiplier.
*
* @name     getOutputVolume
* @return   (number) Multiplier from 0.0 to 1.0, applied after the user's volume setting.
*
*/
float LuaVoice::getOutputVolume() {
  return NetGame::Instance().GetVoiceChatOutputVolume();
}

/* luagmp (method)
*
* This function changes the resource output-volume multiplier.
*
* @name     setOutputVolume
* @param    (number) volume  Multiplier from 0.0 to 1.0, clamped to that range.
*
*/
void LuaVoice::setOutputVolume(float volume) {
  NetGame::Instance().SetVoiceChatOutputVolume(volume);
}

/* luagmp (method)
*
* This function returns the active push-to-talk key code.
*
* @name     getPushToTalkKey
* @return   (number) Key code used for push-to-talk.
*
*/
int LuaVoice::getPushToTalkKey() {
  return NetGame::Instance().GetVoiceChatPushToTalkKey();
}

/* luagmp (method)
*
* This function sets the local client's push-to-talk key for this resource.
*
* @name     setPushToTalkKey
* @param    (number) key  Keyboard key code in the range 1-255.
* @return   (boolean)      True when the key was accepted.
*
*/
bool LuaVoice::setPushToTalkKey(int key) {
  return NetGame::Instance().SetVoiceChatPushToTalkKey(key);
}

/* luagmp (method)
*
* This function returns the available microphone device names.
*
* @name     getInputDevices
* @return   ({string...}) Available microphone names.
*
*/
std::vector<std::string> LuaVoice::getInputDevices() {
  return NetGame::Instance().GetVoiceChatInputDevices();
}

/* luagmp (method)
*
* This function returns the selected microphone name. An empty string means the system default.
*
* @name     getInputDevice
* @return   (string) Selected microphone name.
*
*/
std::string LuaVoice::getInputDevice() {
  return NetGame::Instance().GetVoiceChatInputDevice();
}

/* luagmp (method)
*
* This function selects a microphone by name. Pass an empty string to restore the system default.
*
* @name     setInputDevice
* @param    (string) device_name  Microphone name returned by getInputDevices, or an empty string.
* @return   (boolean)              True when the device was selected.
*
*/
bool LuaVoice::setInputDevice(const std::string& device_name) {
  return NetGame::Instance().SetVoiceChatInputDevice(device_name);
}

/* luagmp (method)
*
* This function returns the available playback device names.
*
* @name     getOutputDevices
* @return   ({string...}) Available playback device names.
*
*/
std::vector<std::string> LuaVoice::getOutputDevices() {
  return NetGame::Instance().GetVoiceChatOutputDevices();
}

/* luagmp (method)
*
* This function returns the selected playback device name. An empty string means the system default.
*
* @name     getOutputDevice
* @return   (string) Selected playback device name.
*
*/
std::string LuaVoice::getOutputDevice() {
  return NetGame::Instance().GetVoiceChatOutputDevice();
}

/* luagmp (method)
*
* This function selects a playback device by name. Pass an empty string to restore the system default.
*
* @name     setOutputDevice
* @param    (string) device_name  Device name returned by getOutputDevices, or an empty string.
* @return   (boolean)              True when the device was selected.
*
*/
bool LuaVoice::setOutputDevice(const std::string& device_name) {
  return NetGame::Instance().SetVoiceChatOutputDevice(device_name);
}

/* luagmp (method)
*
* This function returns the local player's current voice channel.
*
* @name     getChannel
* @return   (string) Current channel name.
*
*/
std::string LuaVoice::getChannel() {
  return NetGame::Instance().GetVoiceChatChannel();
}

/* luagmp (method)
*
* This function changes the local player's voice channel.
*
* @name     setChannel
* @param    (string) channel  Printable channel name, up to 32 bytes.
* @return   (boolean)         True when the channel change was accepted locally and sent when connected.
*
*/
bool LuaVoice::setChannel(const std::string& channel) {
  return NetGame::Instance().SetVoiceChatChannel(channel);
}

/* luagmp (method)
*
* This function checks whether the local player is currently transmitting voice.
*
* @name     isTransmitting
* @return   (boolean) True while voice frames are being captured and sent.
*
*/
bool LuaVoice::isTransmitting() {
  return NetGame::Instance().IsVoiceChatTransmitting();
}

/* luagmp (func)
*
* This function checks whether a remote player is currently speaking.
*
* @version  0.3.0
* @name     isPlayerVoiceTalking
* @side     client
* @category Voice
* @param    (number) player_id  Player id.
* @return   (boolean) True while recent valid voice frames are being received.
*
*/
bool Function_IsPlayerVoiceTalking(std::int64_t player_id) {
  return IsValidVoicePlayerId(player_id) &&
         NetGame::Instance().IsPlayerVoiceTalking(static_cast<std::uint64_t>(player_id));
}

/* luagmp (func)
*
* This function checks whether a remote player's voice is muted locally.
*
* @version  0.3.0
* @name     isPlayerVoiceMuted
* @side     client
* @category Voice
* @param    (number) player_id  Player id.
* @return   (boolean) True when the player is muted.
*
*/
bool Function_IsPlayerVoiceMuted(std::int64_t player_id) {
  return IsValidVoicePlayerId(player_id) &&
         NetGame::Instance().IsPlayerVoiceMuted(static_cast<std::uint64_t>(player_id));
}

/* luagmp (func)
*
* This function mutes or unmutes a remote player locally.
*
* @version  0.3.0
* @name     setPlayerVoiceMuted
* @side     client
* @category Voice
* @param    (number) player_id  Player id.
* @param    (boolean) muted     New mute state.
* @return   (boolean) True when the player id was valid.
*
*/
bool Function_SetPlayerVoiceMuted(std::int64_t player_id, bool muted) {
  return IsValidVoicePlayerId(player_id) &&
         NetGame::Instance().SetPlayerVoiceMuted(static_cast<std::uint64_t>(player_id), muted);
}

/* luagmp (func)
*
* This function returns a remote player's local volume multiplier.
*
* @version  0.3.0
* @name     getPlayerVoiceVolume
* @side     client
* @category Voice
* @param    (number) player_id  Player id.
* @return   (number) Multiplier from 0.0 to 1.0.
*
*/
float Function_GetPlayerVoiceVolume(std::int64_t player_id) {
  if (!IsValidVoicePlayerId(player_id)) {
    return 1.0f;
  }
  return NetGame::Instance().GetPlayerVoiceVolume(static_cast<std::uint64_t>(player_id));
}

/* luagmp (func)
*
* This function changes a remote player's local volume multiplier.
*
* @version  0.3.0
* @name     setPlayerVoiceVolume
* @side     client
* @category Voice
* @param    (number) player_id  Player id.
* @param    (number) volume     Multiplier from 0.0 to 1.0, clamped to that range.
* @return   (boolean) True when the player id was valid.
*
*/
bool Function_SetPlayerVoiceVolume(std::int64_t player_id, float volume) {
  return IsValidVoicePlayerId(player_id) &&
         NetGame::Instance().SetPlayerVoiceVolume(static_cast<std::uint64_t>(player_id), volume);
}

void BindVoiceChat(sol::state& lua) {
  sol::usertype<LuaVoice> voice_type = lua.new_usertype<LuaVoice>("Voice", sol::no_constructor);

  voice_type["isAvailable"] = [](sol::variadic_args) { return LuaVoice::isAvailable(); };
  voice_type["isEnabled"] = [](sol::variadic_args) { return LuaVoice::isEnabled(); };
  voice_type["setEnabled"] = [](sol::variadic_args args) {
    bool enabled = false;
    return ReadBoolArg(args, enabled) && LuaVoice::setEnabled(enabled);
  };
  voice_type["getRange"] = [](sol::variadic_args) { return LuaVoice::getRange(); };
  voice_type["setRange"] = [](sol::variadic_args args) {
    std::vector<double> values;
    if (!ReadNumberArgs(args, values)) {
      return false;
    }
    return LuaVoice::setRange(static_cast<float>(values.front()));
  };
  voice_type["getOutputVolume"] = [](sol::variadic_args) { return LuaVoice::getOutputVolume(); };
  voice_type["setOutputVolume"] = [](sol::variadic_args args) {
    std::vector<double> values;
    if (ReadNumberArgs(args, values)) {
      LuaVoice::setOutputVolume(static_cast<float>(values.front()));
    }
  };
  voice_type["getPushToTalkKey"] = [](sol::variadic_args) { return LuaVoice::getPushToTalkKey(); };
  voice_type["setPushToTalkKey"] = [](sol::variadic_args args) {
    int key = 0;
    return ReadIntegerArg(args, key) && LuaVoice::setPushToTalkKey(key);
  };
  voice_type["getInputDevices"] = [&lua](sol::variadic_args) {
    const auto devices = LuaVoice::getInputDevices();
    sol::table result = lua.create_table(static_cast<int>(devices.size()), 0);
    for (std::size_t index = 0; index < devices.size(); ++index) {
      result[index + 1] = devices[index];
    }
    return result;
  };
  voice_type["getInputDevice"] = [](sol::variadic_args) { return LuaVoice::getInputDevice(); };
  voice_type["setInputDevice"] = [](sol::variadic_args args) {
    std::string device_name;
    return ReadStringArg(args, device_name) && LuaVoice::setInputDevice(device_name);
  };
  voice_type["getOutputDevices"] = [&lua](sol::variadic_args) {
    const auto devices = LuaVoice::getOutputDevices();
    sol::table result = lua.create_table(static_cast<int>(devices.size()), 0);
    for (std::size_t index = 0; index < devices.size(); ++index) {
      result[index + 1] = devices[index];
    }
    return result;
  };
  voice_type["getOutputDevice"] = [](sol::variadic_args) { return LuaVoice::getOutputDevice(); };
  voice_type["setOutputDevice"] = [](sol::variadic_args args) {
    std::string device_name;
    return ReadStringArg(args, device_name) && LuaVoice::setOutputDevice(device_name);
  };
  voice_type["getChannel"] = [](sol::variadic_args) { return LuaVoice::getChannel(); };
  voice_type["setChannel"] = [](sol::variadic_args args) {
    std::string channel;
    return ReadStringArg(args, channel) && LuaVoice::setChannel(channel);
  };
  voice_type["isTransmitting"] = [](sol::variadic_args) { return LuaVoice::isTransmitting(); };

  lua["Voice"] = LuaVoice{};
  lua["isPlayerVoiceTalking"] = Function_IsPlayerVoiceTalking;
  lua["isPlayerVoiceMuted"] = Function_IsPlayerVoiceMuted;
  lua["setPlayerVoiceMuted"] = Function_SetPlayerVoiceMuted;
  lua["getPlayerVoiceVolume"] = Function_GetPlayerVoiceVolume;
  lua["setPlayerVoiceVolume"] = Function_SetPlayerVoiceVolume;
}

}  // namespace gmp::gothic
