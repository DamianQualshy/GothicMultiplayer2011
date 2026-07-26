
/*
MIT License

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

#include "function_bind.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

#include "game_server.h"
#include "shared/lua_runtime/shared_bind.h"
#include "shared/lua_runtime/timer_manager.h"
#include "shared/lua_runtime/lua_constants.h"

using namespace std;

namespace {

std::uint8_t ClampColorComponent(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::optional<float> GetOptionalFloat(const sol::table& table, const char* lowerKey, const char* upperKey) {
  if (auto value = table.get<sol::optional<float>>(lowerKey); value) {
    return std::optional<float>(*value);
  }
  if (auto value = table.get<sol::optional<float>>(upperKey); value) {
    return std::optional<float>(*value);
  }
  return std::nullopt;
}

std::optional<glm::vec3> ParseSpawnPosition(sol::variadic_args args) {
  if (args.size() == 0) {
    return std::nullopt;
  }

  if (args.size() == 1) {
    sol::object arg = args[0];
    if (arg.get_type() == sol::type::table) {
      sol::table tbl = arg;
      auto x = GetOptionalFloat(tbl, "x", "X");
      auto y = GetOptionalFloat(tbl, "y", "Y");
      auto z = GetOptionalFloat(tbl, "z", "Z");
      if (x && y && z) {
        return glm::vec3(*x, *y, *z);
      }
      SPDLOG_WARN("spawnPlayer table argument must contain x, y, z fields");
      return std::nullopt;
    }
    SPDLOG_WARN("spawnPlayer expects a table with coordinates or three numeric arguments");
    return std::nullopt;
  }

  if (args.size() == 3) {
    try {
      float x = args[0].as<float>();
      float y = args[1].as<float>();
      float z = args[2].as<float>();
      return glm::vec3(x, y, z);
    } catch (const sol::error& err) {
      SPDLOG_ERROR("spawnPlayer received invalid coordinate arguments: {}", err.what());
      return std::nullopt;
    }
  }

  SPDLOG_WARN("spawnPlayer called with unsupported arguments");
  return std::nullopt;
}

std::optional<glm::vec3> Function_ParsePositionTable(const sol::table& table) {
  auto x = GetOptionalFloat(table, "x", "X");
  auto y = GetOptionalFloat(table, "y", "Y");
  auto z = GetOptionalFloat(table, "z", "Z");
  if (x && y && z) {
    return glm::vec3(*x, *y, *z);
  }

  SPDLOG_WARN("Position table must contain x, y, z fields");
  return std::nullopt;
}

std::string ClampLuaText(const std::string& text, std::size_t max_len) {
  if (text.size() <= max_len) {
    return text;
  }
  return text.substr(0, max_len);
}

std::optional<std::reference_wrapper<PlayerManager::Player>> GetPlayerOrWarn(std::uint32_t player_id,
                                                                             const char* action) {
  auto player_opt = g_server->GetPlayerManager().GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("{} called for missing player id {}", action, player_id);
  }
  return player_opt;
}

sol::object EquipmentInstanceOrNil(std::int16_t index, sol::state_view lua) {
  if (index <= 0) {
    return sol::nil;
  }

  const auto* item = g_server->GetItemRegistry().FindByIndex(index);
  if (!item) {
    return sol::nil;
  }

  return sol::make_object(lua, item->instance);
}

std::optional<std::string> FindItemInstanceByIndex(std::int16_t index, const char* action) {
  if (index <= 0) {
    return std::nullopt;
  }

  const auto* item = g_server->GetItemRegistry().FindByIndex(index);
  if (!item) {
    SPDLOG_WARN("{} found unknown equipped item index {}", action, index);
    return std::nullopt;
  }

  return item->instance;
}

}  // namespace


/* luagmp (func)
*
* This function will send a colored chat message to all connected players.
*
* @version  0.3.0
* @name     sendMessageToAll
* @side     server
* @category Chat
* @param    (number) r        Red component (0-255).
* @param    (number) g        Green component (0-255).
* @param    (number) b        Blue component (0-255).
* @param    (string) text     Message text to send.
*
*/
bool Function_SendMessageToAll(int r, int g, int b, const std::string& text) {
  g_server->SendMessageToAll(ClampColorComponent(r), ClampColorComponent(g), ClampColorComponent(b), text);
  return true;
}

/* luagmp (func)
*
* This function will send a colored chat message to a specific player.
*
* @version  0.3.0
* @name     sendMessageToPlayer
* @side     server
* @category Chat
* @param    (number) player_id  Target player id.
* @param    (number) r          Red component (0-255).
* @param    (number) g          Green component (0-255).
* @param    (number) b          Blue component (0-255).
* @param    (string) text       Message text to send.
*
*/
bool Function_SendMessageToPlayer(std::uint32_t player_id, int r, int g, int b, const std::string& text) {
  if (!GetPlayerOrWarn(player_id, "sendMessageToPlayer")) {
    return false;
  }

  g_server->SendMessageToPlayer(player_id, ClampColorComponent(r), ClampColorComponent(g), ClampColorComponent(b), text);
  return true;
}

/* luagmp (func)
*
* This function will send a player-sourced colored message to all players (includes sender id).
*
* @version  0.3.0
* @name     sendPlayerMessageToAll
* @side     server
* @category Chat
* @param    (number) sender_id  Sender player id.
* @param    (number) r          Red component (0-255).
* @param    (number) g          Green component (0-255).
* @param    (number) b          Blue component (0-255).
* @param    (string) text       Message text.
*
*/
bool Function_SendPlayerMessageToAll(std::uint32_t sender_id, int r, int g, int b, const std::string& text) {
  if (!GetPlayerOrWarn(sender_id, "sendPlayerMessageToAll")) {
    return false;
  }

  g_server->SendPlayerMessageToAll(sender_id, ClampColorComponent(r), ClampColorComponent(g), ClampColorComponent(b), text);
  return true;
}

/* luagmp (func)
*
* This function will send a player-sourced colored message to a specific player.
*
* @version  0.3.0
* @name     sendPlayerMessageToPlayer
* @side     server
* @category Chat
* @param    (number) sender_id    Sender player id.
* @param    (number) receiver_id  Receiver player id.
* @param    (number) r            Red component (0-255).
* @param    (number) g            Green component (0-255).
* @param    (number) b            Blue component (0-255).
* @param    (string) text         Message text.
*
*/
bool Function_SendPlayerMessageToPlayer(std::uint32_t sender_id, std::uint32_t receiver_id, int r, int g, int b,
                                        const std::string& text) {
  if (!GetPlayerOrWarn(sender_id, "sendPlayerMessageToPlayer (sender)")) {
    return false;
  }
  if (!GetPlayerOrWarn(receiver_id, "sendPlayerMessageToPlayer (receiver)")) {
    return false;
  }

  g_server->SendPlayerMessageToPlayer(sender_id, receiver_id, ClampColorComponent(r), ClampColorComponent(g),
                                      ClampColorComponent(b), text);
  return true;
}

/* luagmp (func)
*
* This function will spawn the player, optionally overriding the spawn position.
*
* @version  0.3.0
* @name     spawnPlayer
* @side     server
* @category Player
* @note     If the player is not spawned, server doesn't recognize his presence at all.
* @param    (number) player_id    Player id to spawn.
* @param    ({x, y, z})           Optional position table or three numeric coords.
*
*/
bool Function_SpawnPlayer(std::uint32_t player_id, sol::variadic_args args) {
  if (!GetPlayerOrWarn(player_id, "spawnPlayer")) {
    return false;
  }

  auto position_override = ParseSpawnPosition(args);
  return g_server->SpawnPlayer(player_id, position_override);
}

/* luagmp (func)
*
* This function will unspawn the player without disconnecting him.
*
* @version  0.3.0
* @name     unspawnPlayer
* @side     server
* @category Player
* @param    (number) player_id    Player id to unspawn.
* @return   (boolean)             True on success.
*
*/
bool Function_UnspawnPlayer(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "unspawnPlayer")) {
    return false;
  }

  return g_server->UnspawnPlayer(player_id);
}

/* luagmp (func)
*
* This function will set the player's character instance.
*
* @version  0.3.0
* @name     setPlayerInstance
* @side     server
* @category Player
* @param    (number) player_id     Target player id.
* @param    (string) instance      Instance name.
*
*/
bool Function_SetPlayerInstance(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "setPlayerInstance")) {
    return false;
  }

  return g_server->SetPlayerInstance(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will return the player's instance name, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerInstance
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Instance name or nil.
*
*/
sol::object Function_GetPlayerInstance(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerInstance");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().instance);
}

/* luagmp (func)
*
* This function will set the player's character name.
*
* @version  0.3.0
* @name     setPlayerName
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (string) name       New player name.
*
*/
bool Function_SetPlayerName(std::uint32_t player_id, const std::string& name) {
  if (!GetPlayerOrWarn(player_id, "setPlayerName")) {
    return false;
  }

  return g_server->SetPlayerName(player_id, name);
}

/* luagmp (func)
*
* This function will return the player's name, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerName
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Player name or nil.
*
*/
sol::object Function_GetPlayerName(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerName");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().name);
}

/* luagmp (func)
*
* This function will return the player's IP address or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerIP
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Player IP or nil.
*
*/
sol::object Function_GetPlayerIP(std::uint32_t player_id, sol::this_state ts) {
  if (!GetPlayerOrWarn(player_id, "getPlayerIP")) {
    return sol::nil;
  }

  const auto ip = g_server->GetPlayerIp(player_id);
  if (ip.empty()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, ip);
}

/* luagmp (func)
*
* This function will return the player's MAC address or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMacAddress
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Player MAC address or nil.
*
*/
sol::object Function_GetPlayerMacAddress(std::uint32_t player_id, sol::this_state ts) {
  if (!GetPlayerOrWarn(player_id, "getPlayerMacAddress")) {
    return sol::nil;
  }

  const auto mac = g_server->GetPlayerMacAddress(player_id);
  if (mac.empty()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, mac);
}

/* luagmp (func)
*
* This function will return the player's UUID or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerUUID
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Player UUID or nil.
*
*/
sol::object Function_GetPlayerUUID(std::uint32_t player_id, sol::this_state ts) {
  if (!GetPlayerOrWarn(player_id, "getPlayerUUID")) {
    return sol::nil;
  }

  const auto uuid = g_server->GetPlayerUUID(player_id);
  if (uuid.empty()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, uuid);
}

/* luagmp (func)
*
* This function will return the player's current network ping.
*
* @version  0.3.0
* @name     getPlayerPing
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number)            Average of all ping times read, or -1 if unavailable.
*
*/
std::int32_t Function_GetPlayerPing(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "getPlayerPing")) {
    return -1;
  }

  return g_server->GetPlayerPing(player_id);
}

/* luagmp (func)
*
* This function will set the player's name color in RGB format.
*
* @version  0.3.0
* @name     setPlayerColor
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) r          Red (0-255).
* @param    (number) g          Green (0-255).
* @param    (number) b          Blue (0-255).
*
*/
bool Function_SetPlayerColor(std::uint32_t player_id, int r, int g, int b) {
  if (!GetPlayerOrWarn(player_id, "setPlayerColor")) {
    return false;
  }

  return g_server->SetPlayerColor(player_id, ClampColorComponent(r), ClampColorComponent(g), ClampColorComponent(b));
}

/* luagmp (func)
*
* This function will return the player's name color, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerColor
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @return   ({r, g, b}|nil)      RGB color table or nil.
*
*/
sol::object Function_GetPlayerColor(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerColor");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  sol::table color_table = lua.create_table();
  const auto& player = player_opt->get();
  color_table["r"] = static_cast<int>(player.name_color_r);
  color_table["g"] = static_cast<int>(player.name_color_g);
  color_table["b"] = static_cast<int>(player.name_color_b);
  return color_table;
}

/* luagmp (func)
*
* This function will set the player's current health.
*
* @version  0.3.0
* @name     setPlayerHealth
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) health     New health value.
*
*/
bool Function_SetPlayerHealth(std::uint32_t player_id, int health) {
  if (!GetPlayerOrWarn(player_id, "setPlayerHealth")) {
    return false;
  }

  return g_server->SetPlayerHealth(player_id, health);
}

/* luagmp (func)
*
* This function will return the player's current health, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerHealth
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Health value or nil.
*
*/
sol::object Function_GetPlayerHealth(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerHealth");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().health);
}

/* luagmp (func)
*
* This function will set the player's maximum health.
*
* @version  0.3.0
* @name     setPlayerMaxHealth
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @param    (number) max_health  New maximum health.
*
*/
bool Function_SetPlayerMaxHealth(std::uint32_t player_id, int max_health) {
  if (!GetPlayerOrWarn(player_id, "setPlayerMaxHealth")) {
    return false;
  }

  return g_server->SetPlayerMaxHealth(player_id, max_health);
}

/* luagmp (func)
*
* This function will return the player's maximum health, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMaxHealth
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Max health or nil.
*
*/
sol::object Function_GetPlayerMaxHealth(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerMaxHealth");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().max_health);
}

/* luagmp (func)
*
* This function will set the player's current mana.
*
* @version  0.3.0
* @name     setPlayerMana
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) mana       New mana value.
*
*/
bool Function_SetPlayerMana(std::uint32_t player_id, int mana) {
  if (!GetPlayerOrWarn(player_id, "setPlayerMana")) {
    return false;
  }

  return g_server->SetPlayerMana(player_id, mana);
}

/* luagmp (func)
*
* This function will return the player's current mana, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMana
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Mana value or nil.
*
*/
sol::object Function_GetPlayerMana(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerMana");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().mana);
}

/* luagmp (func)
*
* This function will set the player's maximum mana.
*
* @version  0.3.0
* @name     setPlayerMaxMana
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) max_mana   New maximum mana.
*
*/
bool Function_SetPlayerMaxMana(std::uint32_t player_id, int max_mana) {
  if (!GetPlayerOrWarn(player_id, "setPlayerMaxMana")) {
    return false;
  }

  return g_server->SetPlayerMaxMana(player_id, max_mana);
}

/* luagmp (func)
*
* This function will get the player's maximum mana, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMaxMana
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Max mana or nil.
*
*/
sol::object Function_GetPlayerMaxMana(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerMaxMana");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().max_mana);
}

/* luagmp (func)
*
* This function will set the player's strength attribute.
*
* @version  0.3.0
* @name     setPlayerStrength
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) strength   New strength value.
*
*/
bool Function_SetPlayerStrength(std::uint32_t player_id, int strength) {
  if (!GetPlayerOrWarn(player_id, "setPlayerStrength")) {
    return false;
  }

  return g_server->SetPlayerStrength(player_id, strength);
}

/* luagmp (func)
*
* This function will return the player's strength attribute, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerStrength
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Strength value or nil.
*
*/
sol::object Function_GetPlayerStrength(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerStrength");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().strength);
}

/* luagmp (func)
*
* This function will set the player's dexterity attribute.
*
* @version  0.3.0
* @name     setPlayerDexterity
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) dexterity  New dexterity value.
*
*/
bool Function_SetPlayerDexterity(std::uint32_t player_id, int dexterity) {
  if (!GetPlayerOrWarn(player_id, "setPlayerDexterity")) {
    return false;
  }

  return g_server->SetPlayerDexterity(player_id, dexterity);
}

/* luagmp (func)
*
* This function will return the player's dexterity attribute, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerDexterity
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Dexterity value or nil.
*
*/
sol::object Function_GetPlayerDexterity(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerDexterity");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().dexterity);
}

/* luagmp (func)
*
* This function will set the player's weapon skill hit chance.
*
* @version  0.3.0
* @name     setPlayerSkillWeapon
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) skill_id   Skill identifier, for more information check [Weapon Constants](../../shared-constants/Weapon.md).
* @param    (number) percentage Hit chance amount.
*
*/
bool Function_SetPlayerSkillWeapon(std::uint32_t player_id, int skill_id, int percentage) {
  if (!GetPlayerOrWarn(player_id, "setPlayerSkillWeapon")) {
    return false;
  }

  return g_server->SetPlayerSkillWeapon(player_id, skill_id, percentage);
}

/* luagmp (func)
*
* This function will return the player's weapon skill hit chance, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerSkillWeapon
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) skill_id   Skill identifier, for more information check [Weapon Constants](../../shared-constants/Weapon.md).
* @return   (number|nil)        Hit chance amount or nil.
*
*/
sol::object Function_GetPlayerSkillWeapon(std::uint32_t player_id, int skill_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerSkillWeapon");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  auto& skills = player_opt->get().weapon_skills;
  const auto it = skills.find(skill_id);
  const int value = (it != skills.end()) ? it->second : 0;
  sol::state_view lua(ts);
  return sol::make_object(lua, value);
}

/* luagmp (func)
*
* This function will set the player's talent value.
*
* @version  0.3.0
* @name     setPlayerTalent
* @side     server
* @category Player
* @param    (number) player_id     Target player id.
* @param    (number) talent_id     Talent identifier, for more information check [Talent Constants](../../shared-constants/Talent.md).
* @param    (number) talent_value  Talent value.
*
*/
bool Function_SetPlayerTalent(std::uint32_t player_id, int talent_id, int talent_value) {
  if (!GetPlayerOrWarn(player_id, "setPlayerTalent")) {
    return false;
  }

  return g_server->SetPlayerTalent(player_id, talent_id, talent_value);
}

/* luagmp (func)
*
* This function will return the player's talent value, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerTalent
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) talent_id  Talent identifier, for more information check [Talent Constants](../../shared-constants/Talent.md).
* @return   (number|nil)        Talent value or nil.
*
*/
sol::object Function_GetPlayerTalent(std::uint32_t player_id, int talent_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerTalent");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  auto& talents = player_opt->get().talents;
  const auto it = talents.find(talent_id);
  const int value = (it != talents.end()) ? it->second : 0;
  sol::state_view lua(ts);
  return sol::make_object(lua, value);
}

/* luagmp (func)
*
* This function will set the player's level.
*
* @version  0.3.0
* @name     setPlayerLevel
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) level      New level.
*
*/
bool Function_SetPlayerLevel(std::uint32_t player_id, int level) {
  if (!GetPlayerOrWarn(player_id, "setPlayerLevel")) {
    return false;
  }

  return g_server->SetPlayerLevel(player_id, level);
}

/* luagmp (func)
*
* This function will return the player's level, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerLevel
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Level or nil.
*
*/
sol::object Function_GetPlayerLevel(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerLevel");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().level);
}

/* luagmp (func)
*
* This function will set the player's experience points.
*
* @version  0.3.0
* @name     setPlayerExp
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) exp        New exp value.
*
*/
bool Function_SetPlayerExp(std::uint32_t player_id, int exp) {
  if (!GetPlayerOrWarn(player_id, "setPlayerExp")) {
    return false;
  }

  return g_server->SetPlayerExp(player_id, exp);
}

/* luagmp (func)
*
* This function will return the player's experience points, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerExp
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Exp value or nil.
*
*/
sol::object Function_GetPlayerExp(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerExp");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().exp);
}

/* luagmp (func)
*
* This function will set the experience required for the player's next level.
*
* @version  0.3.0
* @name     setPlayerNextLevelExp
* @side     server
* @category Player
* @param    (number) player_id      Target player id.
* @param    (number) next_level_exp Required exp for next level.
*
*/
bool Function_SetPlayerNextLevelExp(std::uint32_t player_id, int next_level_exp) {
  if (!GetPlayerOrWarn(player_id, "setPlayerNextLevelExp")) {
    return false;
  }

  return g_server->SetPlayerNextLevelExp(player_id, next_level_exp);
}

/* luagmp (func)
*
* This function will return the experience required for the player's next level, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerNextLevelExp
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Next level exp or nil.
*
*/
sol::object Function_GetPlayerNextLevelExp(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerNextLevelExp");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().next_level_exp);
}

/* luagmp (func)
*
* This function will set the player's learn points.
*
* @version  0.3.0
* @name     setPlayerLearnPoints
* @side     server
* @category Player
* @param    (number) player_id     Target player id.
* @param    (number) learn_points  New learn points value.
*
*/
bool Function_SetPlayerLearnPoints(std::uint32_t player_id, int learn_points) {
  if (!GetPlayerOrWarn(player_id, "setPlayerLearnPoints")) {
    return false;
  }

  return g_server->SetPlayerLearnPoints(player_id, learn_points);
}

/* luagmp (func)
*
* This function will return the player's learn points, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerLearnPoints
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Learn points or nil.
*
*/
sol::object Function_GetPlayerLearnPoints(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerLearnPoints");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().learn_points);
}

/* luagmp (func)
*
* This function will set the player's visual model and textures.
*
* @version  0.3.0
* @name     setPlayerVisual
* @side     server
* @category Player
* @param    (number) player_id       Target player id.
* @param    (string) body_model      Body model name.
* @param    (number) body_texture    Body texture index.
* @param    (string) head_model      Head model name.
* @param    (number) head_texture    Head texture index.
*
*/
bool Function_SetPlayerVisual(std::uint32_t player_id, const std::string& body_model, int body_texture, const std::string& head_model,
                              int head_texture) {
  if (!GetPlayerOrWarn(player_id, "setPlayerVisual")) {
    return false;
  }

  return g_server->SetPlayerVisual(player_id, body_model, static_cast<std::int16_t>(body_texture), head_model,
                                   static_cast<std::int16_t>(head_texture));
}

/* luagmp (func)
*
* This function will return the player's visual information as a table, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerVisual
* @side     server
* @category Player
* @param    (number) player_id       Target player id.
* @return   ({bodyModel, bodyTexture, headModel, headTexture}|nil)         Table with bodyModel, bodyTexture, headModel, headTexture or nil.
*
*/
sol::object Function_GetPlayerVisual(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerVisual");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  sol::table visual_table = lua.create_table();
  const auto& player = player_opt->get();
  visual_table["bodyModel"] = player.body_model;
  visual_table["bodyTexture"] = player.body_texture;
  visual_table["headModel"] = player.head_model;
  visual_table["headTexture"] = player.head_texture;
  return visual_table;
}

/* luagmp (func)
*
* This function will set the player's model fatness.
*
* @version  0.3.0
* @name     setPlayerFatness
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @param    (number) fatness     Fatness value.
*
*/
bool Function_SetPlayerFatness(std::uint32_t player_id, float fatness) {
  if (!GetPlayerOrWarn(player_id, "setPlayerFatness")) {
    return false;
  }

  return g_server->SetPlayerFatness(player_id, fatness);
}

/* luagmp (func)
*
* This function will return the player's model fatness, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerFatness
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @return   (float|nil)          Fatness value or nil.
*
*/
sol::object Function_GetPlayerFatness(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerFatness");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().fatness);
}

/* luagmp (func)
*
* This function will set the player's model scale.
*
* @version  0.3.0
* @name     setPlayerScale
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @param    (number) x           Scale factor on x axis.
* @param    (number) y           Scale factor on y axis.
* @param    (number) z           Scale factor on z axis.
*
*/
bool Function_SetPlayerScale(std::uint32_t player_id, float x, float y, float z) {
  if (!GetPlayerOrWarn(player_id, "setPlayerScale")) {
    return false;
  }

  return g_server->SetPlayerScale(player_id, glm::vec3{x, y, z});
}

/* luagmp (func)
*
* This function will return the player's model scale.
*
* @version  0.3.0
* @name     getPlayerScale
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @return   ({x, y, z}|nil)      Scale table or nil.
*
*/
sol::object Function_GetPlayerScale(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerScale");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  const auto& scale = player_opt->get().scale;
  sol::state_view lua(ts);
  sol::table scale_table = lua.create_table();
  scale_table["x"] = scale.x;
  scale_table["y"] = scale.y;
  scale_table["z"] = scale.z;
  return scale_table;
}

/* luagmp (func)
*
* This function will set the player's weapon mode.
*
* @version  0.3.0
* @name     setPlayerWeaponMode
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @param    (number) weapon_mode Weapon mode constant.
*
*/
bool Function_SetPlayerWeaponMode(std::uint32_t player_id, int weapon_mode) {
  if (!GetPlayerOrWarn(player_id, "setPlayerWeaponMode")) {
    return false;
  }

  return g_server->SetPlayerWeaponMode(player_id, weapon_mode);
}

/* luagmp (func)
*
* This function will return the player's weapon mode, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerWeaponMode
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @return   (number|nil)         Weapon mode or nil.
*
*/
sol::object Function_GetPlayerWeaponMode(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerWeaponMode");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, static_cast<int>(player_opt->get().state.weapon_mode));
}

/* luagmp (func)
*
* This function will return the last reported player animation id.
*
* @version  0.3.0
* @name     getPlayerAniId
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number)            Animation id, or -1 if unavailable.
*
*/
std::int32_t Function_GetPlayerAniId(std::uint32_t player_id) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerAniId");
  if (!player_opt.has_value()) {
    return -1;
  }

  return player_opt->get().state.animation;
}

/* luagmp (func)
*
* This function will return the player's equipped armor instance name.
*
* @version  0.3.0
* @name     getPlayerArmor
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Equipped armor instance, or nil if no armor is equipped.
*
*/
sol::object Function_GetPlayerArmor(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerArmor");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return EquipmentInstanceOrNil(player_opt->get().state.equipped_armor_instance, lua);
}

/* luagmp (func)
*
* This function will return the player's equipped melee weapon instance name.
*
* @version  0.3.0
* @name     getPlayerMeleeWeapon
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Equipped melee weapon instance, or nil if none is equipped.
*
*/
sol::object Function_GetPlayerMeleeWeapon(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerMeleeWeapon");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return EquipmentInstanceOrNil(player_opt->get().state.melee_weapon_instance, lua);
}

/* luagmp (func)
*
* This function will return the player's equipped ranged weapon instance name.
*
* @version  0.3.0
* @name     getPlayerRangedWeapon
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Equipped ranged weapon instance, or nil if none is equipped.
*
*/
sol::object Function_GetPlayerRangedWeapon(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerRangedWeapon");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return EquipmentInstanceOrNil(player_opt->get().state.ranged_weapon_instance, lua);
}

/* luagmp (func)
*
* This function will return the player's equipped helmet instance name.
*
* @version  0.3.0
* @name     getPlayerHelmet
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Equipped helmet instance, or nil if no helmet is equipped.
*
*/
sol::object Function_GetPlayerHelmet(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerHelmet");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return EquipmentInstanceOrNil(player_opt->get().state.equipped_helmet_instance, lua);
}

/* luagmp (func)
*
* This function will return the player's equipped shield instance name.
*
* @version  0.3.0
* @name     getPlayerShield
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Equipped shield instance, or nil if no shield is equipped.
*
*/
sol::object Function_GetPlayerShield(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerShield");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return EquipmentInstanceOrNil(player_opt->get().state.equipped_shield_instance, lua);
}

/* luagmp (func)
*
* This function will apply animation overlay on the player.
*
* @version  0.3.0
* @name     applyPlayerOverlay
* @side     server
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string) overlay      Overlay name.
*
*/
bool Function_ApplyPlayerOverlay(std::uint32_t player_id, const std::string& overlay) {
  if (!GetPlayerOrWarn(player_id, "applyPlayerOverlay")) {
    return false;
  }

  return g_server->ApplyPlayerOverlay(player_id, ClampLuaText(overlay, 255));
}

/* luagmp (func)
*
* This function will return the player's active animation overlays.
*
* @version  0.3.0
* @name     getPlayerOverlays
* @side     server
* @category Player
* @param    (number) player_id
* @return   ({...}|nil)         Array of overlay names or nil.
*
*/
sol::object Function_GetPlayerOverlays(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerOverlays");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  const auto& overlays = player_opt->get().overlays;
  if (overlays.empty()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  sol::table overlay_table = lua.create_table(overlays.size(), 0);
  for (std::size_t i = 0; i < overlays.size(); ++i) {
    overlay_table[i + 1] = overlays[i];
  }
  return overlay_table;
}

/* luagmp (func)
*
* This function will remove a specified animation overlay from the player.
*
* @version  0.3.0
* @name     removePlayerOverlay
* @side     server
* @category Player
* @param    (number) player_id     Target player id.
* @param    (string) overlay       Overlay name.
*
*/
bool Function_RemovePlayerOverlay(std::uint32_t player_id, const std::string& overlay) {
  if (!GetPlayerOrWarn(player_id, "removePlayerOverlay")) {
    return false;
  }

  return g_server->RemovePlayerOverlay(player_id, ClampLuaText(overlay, 255));
}

/* luagmp (func)
*
* This function will play an animation on the player's character.
*
* @version  0.3.0
* @name     playAni
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (string) aniName    Animation name (e.g. "T_STAND_2_SIT").
*
*/
bool Function_PlayAni(std::uint32_t player_id, const std::string& ani_name) {
  if (!GetPlayerOrWarn(player_id, "playAni")) {
    return false;
  }

  return g_server->PlayAnimation(player_id, ClampLuaText(ani_name, 255));
}

/* luagmp (func)
*
* This function will stop a played animation on the player's character.
*
* @version  0.3.0
* @name     stopAni
* @side     server
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string|nil) aniName  Animation name to stop. Defaults to "" for first active animation.
*
*/
bool Function_StopAni(std::uint32_t player_id, sol::optional<std::string> ani_name) {
  if (!GetPlayerOrWarn(player_id, "stopAni")) {
    return false;
  }

  return g_server->StopAnimation(player_id, ClampLuaText(ani_name.value_or(""), 255));
}

/* luagmp (func)
*
* This function will play a face animation on the player's character.
*
* @version  0.3.0
* @name     playFaceAni
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (string) aniName    Face animation name (e.g. "S_FRIENDLY").
*
*/
bool Function_PlayFaceAni(std::uint32_t player_id, const std::string& ani_name) {
  if (!GetPlayerOrWarn(player_id, "playFaceAni")) {
    return false;
  }

  return g_server->PlayFaceAnimation(player_id, ClampLuaText(ani_name, 255));
}

/* luagmp (func)
*
* This function will stop a played face animation on the player's character.
*
* @version  0.3.0
* @name     stopFaceAni
* @side     server
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string|nil) aniName  Face animation name to stop. Defaults to "" for first active animation.
*
*/
bool Function_StopFaceAni(std::uint32_t player_id, sol::optional<std::string> ani_name) {
  if (!GetPlayerOrWarn(player_id, "stopFaceAni")) {
    return false;
  }

  return g_server->StopFaceAnimation(player_id, ClampLuaText(ani_name.value_or(""), 255));
}

/* luagmp (func)
*
* This function will play gesticulation animation on the player's character.
*
* @version  0.3.0
* @name     playGesticulation
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
*
*/
bool Function_PlayGesticulation(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "playGesticulation")) {
    return false;
  }

  return g_server->PlayGesticulation(player_id);
}

/* luagmp (func)
*
* This function will set the player's world position.
*
* @version  0.3.0
* @name     setPlayerPosition
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) x          X coordinate.
* @param    (number) y          Y coordinate.
* @param    (number) z          Z coordinate.
*
*/
bool Function_SetPlayerPosition(std::uint32_t player_id, float x, float y, float z) {
  if (!GetPlayerOrWarn(player_id, "setPlayerPosition")) {
    return false;
  }

  return g_server->SetPlayerPosition(player_id, glm::vec3{x, y, z});
}

/* luagmp (func)
*
* This function will return the player's position, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerPosition
* @side     server
* @category Player
* @param    (number) player_id   Target player id.
* @return   ({x, y, z}|nil)      Table containing x,y,z or nil.
*
*/
sol::object Function_GetPlayerPosition(std::uint32_t player_id, sol::this_state ts) {
  if (!GetPlayerOrWarn(player_id, "getPlayerPosition")) {
    return sol::nil;
  }

  auto position = g_server->GetPlayerPosition(player_id);
  if (!position.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  sol::table position_table = lua.create_table();
  position_table["x"] = position->x;
  position_table["y"] = position->y;
  position_table["z"] = position->z;
  return position_table;
}

/* luagmp (func)
*
* This function will set the player's facing angle (degrees).
*
* @version  0.3.0
* @name     setPlayerAngle
* @side     server
* @category Player
* @param    (number) player_id        Target player id.
* @param    (number) angle_degrees    Angle in degrees.
*
*/
bool Function_SetPlayerAngle(std::uint32_t player_id, float angle_degrees) {
  if (!GetPlayerOrWarn(player_id, "setPlayerAngle")) {
    return false;
  }

  // Expose degrees in Lua (more ergonomic for script authors), convert to radians for the server.
  return g_server->SetPlayerAngle(player_id, glm::radians(angle_degrees));
}

/* luagmp (func)
*
* This function will return the player's facing angle in degrees, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerAngle
* @side     server
* @category Player
* @param    (number) player_id    Target player id.
* @return   (number|nil)          Angle in degrees or nil.
*
*/
sol::object Function_GetPlayerAngle(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerAngle");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  const auto& nrot = player_opt->get().state.nrot;
  const float angle_radians = std::atan2(nrot.x, nrot.z);
  const float angle_degrees = glm::degrees(angle_radians);
  sol::state_view lua(ts);
  return sol::make_object(lua, angle_degrees);
}

/* luagmp (func)
*
* This function will move the player to a different world, optionally specifying a start point.
*
* @version  0.3.0
* @name     setPlayerWorld
* @side     server
* @category Player
* @param    (number) player_id      Target player id.
* @param    (string) world          World name.
* @param    (string) start_point    Optional start point name.
*
*/
bool Function_SetPlayerWorld(std::uint32_t player_id, const std::string& world, std::optional<std::string> start_point) {
  if (!GetPlayerOrWarn(player_id, "setPlayerWorld")) {
    return false;
  }

  return g_server->SetPlayerWorld(player_id, world, start_point);
}

/* luagmp (func)
*
* This function will return the player's current world name, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerWorld
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        World name or nil.
*
*/
sol::object Function_GetPlayerWorld(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerWorld");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().world);
}

/* luagmp (func)
*
* This function will set the player's virtual world id.
*
* @version  0.3.0
* @name     setPlayerVirtualWorld
* @side     server
* @category Player
* @param    (number) player_id       Target player id.
* @param    (number) virtual_world   Virtual world id (0-65535).
*
*/
bool Function_SetPlayerVirtualWorld(std::uint32_t player_id, int virtual_world) {
  if (!GetPlayerOrWarn(player_id, "setPlayerVirtualWorld")) {
    return false;
  }

  return g_server->SetPlayerVirtualWorld(player_id, virtual_world);
}

/* luagmp (func)
*
* This function will return the player's virtual world id, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerVirtualWorld
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Virtual world id or nil.
*
*/
sol::object Function_GetPlayerVirtualWorld(std::uint32_t player_id, sol::this_state ts) {
  auto player_opt = GetPlayerOrWarn(player_id, "getPlayerVirtualWorld");
  if (!player_opt.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, player_opt->get().virtual_world);
}

/* luagmp (func)
*
* This function will ban the player on the server.
*
* @version  0.3.0
* @name     ban
* @side     server
* @category Player
* @note     The reason string can't be longer than 255 characters.
* @param    (number) player_id  Target player id.
* @param    (string) reason     Optional reason why the player was banned.
*
*/
void Function_Ban(std::uint32_t player_id, sol::optional<std::string> reason) {
  if (!GetPlayerOrWarn(player_id, "ban")) {
    return;
  }

  auto reason_text = reason.value_or(std::string{});
  auto clamped_reason = ClampLuaText(reason_text, 255);
  if (clamped_reason.size() != reason_text.size()) {
    SPDLOG_WARN("ban reason too long ({}), clamping to 255 characters", reason_text.size());
  }

  g_server->BanPlayer(player_id, clamped_reason);
}

/* luagmp (func)
*
* This function will kick the player from the server.
*
* @version  0.3.0
* @name     kick
* @side     server
* @category Player
* @note     The reason string can't be longer than 255 characters.
* @param    (number) player_id  Target player id.
* @param    (string) reason     Optional reason why the player was kicked.
*
*/
void Function_Kick(std::uint32_t player_id, sol::optional<std::string> reason) {
  if (!GetPlayerOrWarn(player_id, "kick")) {
    return;
  }

  auto reason_text = reason.value_or(std::string{});
  auto clamped_reason = ClampLuaText(reason_text, 255);
  if (clamped_reason.size() != reason_text.size()) {
    SPDLOG_WARN("kick reason too long ({}), clamping to 255 characters", reason_text.size());
  }

  g_server->KickPlayer(player_id, clamped_reason);
}

/* luagmp (func)
*
* This function will check whether player is connected to the server.
*
* @version  0.3.0
* @name     isPlayerConnected
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (boolean)           True when player is connected, otherwise false.
*
*/
bool Function_IsPlayerConnected(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "isPlayerConnected")) {
    return false;
  }

  return g_server->IsPlayerConnected(player_id);
}

/* luagmp (func)
*
* This function will check whether player is dead.
*
* @version  0.3.0
* @name     isPlayerDead
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (boolean)           True when player is dead, otherwise false.
*
*/
bool Function_IsPlayerDead(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "isPlayerDead")) {
    return false;
  }

  return g_server->IsPlayerDead(player_id);
}

/* luagmp (func)
*
* This function will check whether player is spawned.
*
* @version  0.3.0
* @name     isPlayerSpawned
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (boolean)           True when player is spawned, otherwise false.
*
*/
bool Function_IsPlayerSpawned(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "isPlayerSpawned")) {
    return false;
  }

  return g_server->IsPlayerSpawned(player_id);
}

/* luagmp (func)
*
* This function will check whether player is in unconscious state. The player will be unconscious, when it gets beaten up, but not killed.
*
* @version  0.3.0
* @name     isPlayerUnconscious
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (boolean)           True when player is unconscious, otherwise false.
*
*/
bool Function_IsPlayerUnconscious(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "isPlayerUnconscious")) {
    return false;
  }

  return g_server->IsPlayerUnconscious(player_id);
}

/* luagmp (func)
*
* This function will immediately respawn the player if he is dead.
*
* @version  0.3.0
* @name     respawnPlayer
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
*
*/
void Function_RespawnPlayer(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "respawnPlayer")) {
    return;
  }

  g_server->RespawnPlayer(player_id);
}

/* luagmp (func)
*
* This function will set the player time to respawn after death. If set to 0, respawn is disabled for selected player.
*
* @version  0.3.0
* @name     setPlayerRespawnTime
* @side     server
* @category Player
* @note     The respawnTime can't be smaller than 1001 miliseconds.
* @param    (number) player_id     Target player id.
* @param    (number) respawn_time  New respawn time in milliseconds.
*
*/
void Function_SetPlayerRespawnTime(std::uint32_t player_id, std::int32_t respawn_time_ms) {
  if (!GetPlayerOrWarn(player_id, "setPlayerRespawnTime")) {
    return;
  }

  constexpr std::int32_t kMinRespawnTimeMs = 1001;
  if (respawn_time_ms != 0 && respawn_time_ms < kMinRespawnTimeMs) {
    SPDLOG_WARN("setPlayerRespawnTime called with invalid value {} ms, clamping to {} ms", respawn_time_ms, kMinRespawnTimeMs);
    respawn_time_ms = kMinRespawnTimeMs;
  }

  g_server->SetPlayerRespawnTime(player_id, respawn_time_ms);
}

/* luagmp (func)
*
* This function will return the player time to respawn after death.
*
* @version  0.3.0
* @name     getPlayerRespawnTime
* @side     server
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        The player respawn time or nil if player isn't created.
*
*/
sol::object Function_GetPlayerRespawnTime(std::uint32_t player_id, sol::this_state ts) {
  if (!GetPlayerOrWarn(player_id, "getPlayerRespawnTime")) {
    return sol::nil;
  }

  auto respawn_time = g_server->GetPlayerRespawnTime(player_id);
  if (!respawn_time.has_value()) {
    return sol::nil;
  }

  sol::state_view lua(ts);
  return sol::make_object(lua, respawn_time.value());
}

/* luagmp (func)
*
* This function will give an item to the player.
*
* @version  0.3.0
* @name     giveItem
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Item instance name from scripts.
* @param    (number) amount       Amount to give.
*
*/
bool Function_GiveItem(std::uint32_t player_id, const std::string& instance, std::int32_t amount) {
  if (!GetPlayerOrWarn(player_id, "giveItem")) {
    return false;
  }

  return g_server->GiveItem(player_id, ClampLuaText(instance, 255), amount);
}

/* luagmp (func)
*
* This function will equip an item for the player.
*
* @version  0.3.0
* @name     equipItem
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Item instance name from scripts.
* @param    (number) slot_id      Optional slot id. Defaults to -1 for first free slot.
*
*/
bool Function_EquipItem(std::uint32_t player_id, const std::string& instance, sol::optional<std::int32_t> slot_id) {
  if (!GetPlayerOrWarn(player_id, "equipItem")) {
    return false;
  }

  return g_server->EquipItem(player_id, ClampLuaText(instance, 255), slot_id.value_or(-1));
}

/* luagmp (func)
*
* This function will unequip an item for the player.
*
* @version  0.3.0
* @name     unequipItem
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Item instance name from scripts.
*
*/
bool Function_UnequipItem(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "unequipItem")) {
    return false;
  }

  return g_server->UnequipItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will equip an armor item for the player.
*
* @version  0.3.0
* @name     equipArmor
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Armor item instance name.
* @return   (boolean)             True when the equip packet was sent.
*
*/
bool Function_EquipArmor(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "equipArmor")) {
    return false;
  }

  return g_server->EquipItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will unequip the player's current armor.
*
* @version  0.3.0
* @name     unequipArmor
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @return   (boolean)             True when an equipped armor item was found and unequipped.
*
*/
bool Function_UnequipArmor(std::uint32_t player_id) {
  auto player_opt = GetPlayerOrWarn(player_id, "unequipArmor");
  if (!player_opt.has_value()) {
    return false;
  }

  auto instance = FindItemInstanceByIndex(player_opt->get().state.equipped_armor_instance, "unequipArmor");
  return instance.has_value() && g_server->UnequipItem(player_id, *instance);
}

/* luagmp (func)
*
* This function will equip a melee weapon for the player.
*
* @version  0.3.0
* @name     equipMeleeWeapon
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Melee weapon item instance name.
* @return   (boolean)             True when the equip packet was sent.
*
*/
bool Function_EquipMeleeWeapon(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "equipMeleeWeapon")) {
    return false;
  }

  return g_server->EquipItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will unequip the player's current melee weapon.
*
* @version  0.3.0
* @name     unequipMeleeWeapon
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @return   (boolean)             True when an equipped melee weapon was found and unequipped.
*
*/
bool Function_UnequipMeleeWeapon(std::uint32_t player_id) {
  auto player_opt = GetPlayerOrWarn(player_id, "unequipMeleeWeapon");
  if (!player_opt.has_value()) {
    return false;
  }

  auto instance = FindItemInstanceByIndex(player_opt->get().state.melee_weapon_instance, "unequipMeleeWeapon");
  return instance.has_value() && g_server->UnequipItem(player_id, *instance);
}

/* luagmp (func)
*
* This function will equip a ranged weapon for the player.
*
* @version  0.3.0
* @name     equipRangedWeapon
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Ranged weapon item instance name.
* @return   (boolean)             True when the equip packet was sent.
*
*/
bool Function_EquipRangedWeapon(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "equipRangedWeapon")) {
    return false;
  }

  return g_server->EquipItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will unequip the player's current ranged weapon.
*
* @version  0.3.0
* @name     unequipRangedWeapon
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @return   (boolean)             True when an equipped ranged weapon was found and unequipped.
*
*/
bool Function_UnequipRangedWeapon(std::uint32_t player_id) {
  auto player_opt = GetPlayerOrWarn(player_id, "unequipRangedWeapon");
  if (!player_opt.has_value()) {
    return false;
  }

  auto instance = FindItemInstanceByIndex(player_opt->get().state.ranged_weapon_instance, "unequipRangedWeapon");
  return instance.has_value() && g_server->UnequipItem(player_id, *instance);
}

/* luagmp (func)
*
* This function will equip a helmet item for the player.
*
* @version  0.3.0
* @name     equipHelmet
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Helmet item instance name.
* @return   (boolean)             True when the equip packet was sent.
*
*/
bool Function_EquipHelmet(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "equipHelmet")) {
    return false;
  }

  return g_server->EquipItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will unequip the player's current helmet.
*
* @version  0.3.0
* @name     unequipHelmet
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @return   (boolean)             True when an equipped helmet item was found and unequipped.
*
*/
bool Function_UnequipHelmet(std::uint32_t player_id) {
  auto player_opt = GetPlayerOrWarn(player_id, "unequipHelmet");
  if (!player_opt.has_value()) {
    return false;
  }

  auto instance = FindItemInstanceByIndex(player_opt->get().state.equipped_helmet_instance, "unequipHelmet");
  return instance.has_value() && g_server->UnequipItem(player_id, *instance);
}

/* luagmp (func)
*
* This function will equip a shield item for the player.
*
* @version  0.3.0
* @name     equipShield
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Shield item instance name.
* @return   (boolean)             True when the equip packet was sent.
*
*/
bool Function_EquipShield(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "equipShield")) {
    return false;
  }

  return g_server->EquipItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will unequip the player's current shield.
*
* @version  0.3.0
* @name     unequipShield
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @return   (boolean)             True when an equipped shield item was found and unequipped.
*
*/
bool Function_UnequipShield(std::uint32_t player_id) {
  auto player_opt = GetPlayerOrWarn(player_id, "unequipShield");
  if (!player_opt.has_value()) {
    return false;
  }

  auto instance = FindItemInstanceByIndex(player_opt->get().state.equipped_shield_instance, "unequipShield");
  return instance.has_value() && g_server->UnequipItem(player_id, *instance);
}

/* luagmp (func)
*
* This function will return the amount of a specific item in the player's inventory.
*
* @version  0.3.0
* @name     hasItem
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Item instance name from scripts.
* @return   (number)              Item amount or 0 if missing.
*
*/
int Function_HasItem(std::uint32_t player_id, const std::string& instance) {
  if (!GetPlayerOrWarn(player_id, "hasItem")) {
    return 0;
  }

  return g_server->HasItem(player_id, ClampLuaText(instance, 255));
}

/* luagmp (func)
*
* This function will remove an item from the player's inventory.
*
* @version  0.3.0
* @name     removeItem
* @side     server
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Item instance name from scripts.
* @param    (number) amount       Amount to remove.
*
*/
bool Function_RemoveItem(std::uint32_t player_id, const std::string& instance, std::int32_t amount) {
  if (!GetPlayerOrWarn(player_id, "removeItem")) {
    return false;
  }

  return g_server->RemoveItem(player_id, ClampLuaText(instance, 255), amount);
}

/* luagmp (func)
*
* This function will set the server's current world name.
*
* @version  0.3.0
* @name     setServerWorld
* @side     server
* @category Game
* @param    (string) world      World name to set.
* @return   (boolean)           True on success.
*
*/
bool Function_SetServerWorld(const std::string& world) {
  return g_server->SetServerWorld(world);
}

/* luagmp (func)
*
* This function will return the server's current world name.
*
* @version  0.3.0
* @name     getServerWorld
* @side     server
* @category Game
* @return   (string)        Current server world or empty string.
*
*/
std::string Function_GetServerWorld() {
  return g_server->GetServerWorld();
}

/* luagmp (func)
*
* This function will return player ids within a radius of a given position in a world.
*
* @version  0.3.0
* @name     findNearbyPlayers
* @side     server
* @category Streamer
* @param    ({x, y, z}) position_table    Table with x,y,z coordinates.
* @param    (number) radius               Search radius.
* @param    (string) world                World name to search in.
* @param    (number) virtual_world        Optional virtual world id.
* @return   ({...})                       Array of player ids.
*
*/
std::vector<std::uint32_t> Function_FindNearbyPlayers(const sol::table& position_table, int radius,
                                                      const std::string& world, sol::optional<int> virtual_world) {
  auto position = Function_ParsePositionTable(position_table);
  if (!position.has_value()) {
    return {};
  }

  return g_server->FindNearbyPlayers(*position, static_cast<float>(radius), world, virtual_world.value_or(0));
}

/* luagmp (func)
*
* This function will return the list of players that have been spawned for the given player.
*
* @version  0.3.0
* @name     getSpawnedPlayersForPlayer
* @side     server
* @category Streamer
* @param    (number) player_id   Target player id.
* @return   ({...})              Array of player ids.
*
*/
std::vector<std::uint32_t> Function_GetSpawnedPlayersForPlayer(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "getSpawnedPlayersForPlayer")) {
    return {};
  }

  return g_server->GetSpawnedPlayersForPlayer(player_id);
}

/* luagmp (func)
*
* This function will return the list of players currently streamed to the given player.
*
* @version  0.3.0
* @name     getStreamedPlayersByPlayer
* @side     server
* @category Streamer
* @param    (number) player_id  Target player id.
* @return   ({...})             Array of player ids.
*
*/
std::vector<std::uint32_t> Function_GetStreamedPlayersByPlayer(std::uint32_t player_id) {
  if (!GetPlayerOrWarn(player_id, "getStreamedPlayersByPlayer")) {
    return {};
  }

  return g_server->GetStreamedPlayersByPlayer(player_id);
}

/* luagmp (func)
*
* This function sets the global player streaming radius.
*
* @version  0.3.0
* @name     setStreamerRadius
* @side     server
* @category Streamer
* @param    (number) radius   Streaming radius in world units.
* @return   (boolean)         True if the value was accepted.
*
*/
bool Function_SetStreamerRadius(int radius) {
  return g_server->SetStreamerRadius(radius);
}

/* luagmp (func)
*
* This function returns the global player streaming radius.
*
* @version  0.3.0
* @name     getStreamerRadius
* @side     server
* @category Streamer
* @return   (number)          Streaming radius in world units.
*
*/
int Function_GetStreamerRadius() {
  return g_server->GetStreamerRadius();
}

/* luagmp (func)
*
* This function sets the global player streaming height. A value of 0 disables the vertical limit.
*
* @version  0.3.0
* @name     setStreamerHeight
* @side     server
* @category Streamer
* @param    (number) height   Streaming height in world units, or 0 for unlimited.
* @return   (boolean)         True if the value was accepted.
*
*/
bool Function_SetStreamerHeight(int height) {
  return g_server->SetStreamerHeight(height);
}

/* luagmp (func)
*
* This function returns the global player streaming height.
*
* @version  0.3.0
* @name     getStreamerHeight
* @side     server
* @category Streamer
* @return   (number)          Streaming height in world units, or 0 for unlimited.
*
*/
int Function_GetStreamerHeight() {
  return g_server->GetStreamerHeight();
}

/* luagmp (func)
*
* This function will set the server time (hour, minute, optional day offset).
*
* @version  0.3.0
* @name     setTime
* @side     server
* @category Game
* @param    (number) hour      Hour (0-23).
* @param    (number) min       Minute (0-59).
* @param    (number) day       Optional day offset.
*
*/
bool Function_SetTime(int hour, int min, sol::optional<int> day) {
  return g_server->SetTime(hour, min, day.value_or(0));
}

/* luagmp (func)
*
* This function will return the current server time as a table {day,hour,min}.
*
* @version  0.3.0
* @name     getTime
* @side     server
* @category Game
* @return   ({day, hour, min})  Table containing day, hour, min.
*
*/
sol::object Function_GetTime(sol::this_state ts) {
  auto time = g_server->GetTime();
  sol::state_view lua(ts);
  sol::table time_table = lua.create_table();
  time_table["day"] = time.day_;
  time_table["hour"] = time.hour_;
  time_table["min"] = time.min_;
  return time_table;
}

/* luagmp (func)
*
* This function will set the duration of a full in-game day in milliseconds.
*
* @version  0.3.0
* @name     setDayLength
* @side     server
* @category Game
* @param    (number) miliseconds   Day length in milliseconds (min 10000 ms).
*
*/
bool Function_SetDayLength(float day_length_ms) {
  constexpr float kMinDayLengthMs = 10000.0f;
  if (day_length_ms < kMinDayLengthMs) {
    SPDLOG_WARN("setDayLength called with invalid value {} ms, clamping to {} ms", day_length_ms, kMinDayLengthMs);
    day_length_ms = kMinDayLengthMs;
  }

  return g_server->SetDayLength(day_length_ms);
}

/* luagmp (func)
*
* This function will return the current duration of a full in-game day in milliseconds.
*
* @version  0.3.0
* @name     getDayLength
* @side     server
* @category Game
* @return   (number)  Day length in milliseconds.
*
*/
sol::object Function_GetDayLength(sol::this_state ts) {
  sol::state_view lua(ts);
  return sol::make_object(lua, g_server->GetDayLength());
}


// Register Functions
void lua::bindings::BindFunctions(sol::state& lua, TimerManager& timer_manager) {
  SetServerInfoProvider({
      [] { return g_server ? g_server->GetHostname() : std::string{}; },
      [] { return g_server ? static_cast<int>(g_server->GetMaxSlots()) : 0; },
      [] {
        if (!g_server) {
          return std::vector<int>{};
        }

        std::vector<int> players;
        g_server->GetPlayerManager().ForEachIngamePlayer(
            [&players](const PlayerManager::Player& player) { players.push_back(player.player_id); });
        return players;
      },
      [] {
        if (!g_server) {
          return 0;
        }

        std::uint32_t count = 0;
        g_server->GetPlayerManager().ForEachIngamePlayer([&count](const PlayerManager::Player&) { ++count; });
        return static_cast<int>(count);
      },
  });

  lua["sendMessageToAll"] = Function_SendMessageToAll;
  lua["sendMessageToPlayer"] = Function_SendMessageToPlayer;
  lua["sendPlayerMessageToAll"] = Function_SendPlayerMessageToAll;
  lua["sendPlayerMessageToPlayer"] = Function_SendPlayerMessageToPlayer;

  lua["spawnPlayer"] = Function_SpawnPlayer;
  lua["unspawnPlayer"] = Function_UnspawnPlayer;

  lua["setPlayerInstance"] = Function_SetPlayerInstance;
  lua["getPlayerInstance"] = Function_GetPlayerInstance;
  lua["setPlayerName"] = Function_SetPlayerName;
  lua["getPlayerName"] = Function_GetPlayerName;
  lua["getPlayerIP"] = Function_GetPlayerIP;
  lua["getPlayerPing"] = Function_GetPlayerPing;
  lua["getPlayerMacAddress"] = Function_GetPlayerMacAddress;
  lua["getPlayerUUID"] = Function_GetPlayerUUID;
  lua["setPlayerColor"] = Function_SetPlayerColor;
  lua["getPlayerColor"] = Function_GetPlayerColor;
  lua["setPlayerHealth"] = Function_SetPlayerHealth;
  lua["getPlayerHealth"] = Function_GetPlayerHealth;
  lua["setPlayerMaxHealth"] = Function_SetPlayerMaxHealth;
  lua["getPlayerMaxHealth"] = Function_GetPlayerMaxHealth;
  lua["setPlayerMana"] = Function_SetPlayerMana;
  lua["getPlayerMana"] = Function_GetPlayerMana;
  lua["setPlayerMaxMana"] = Function_SetPlayerMaxMana;
  lua["getPlayerMaxMana"] = Function_GetPlayerMaxMana;
  lua["setPlayerStrength"] = Function_SetPlayerStrength;
  lua["getPlayerStrength"] = Function_GetPlayerStrength;
  lua["setPlayerDexterity"] = Function_SetPlayerDexterity;
  lua["getPlayerDexterity"] = Function_GetPlayerDexterity;
  lua["setPlayerSkillWeapon"] = Function_SetPlayerSkillWeapon;
  lua["getPlayerSkillWeapon"] = Function_GetPlayerSkillWeapon;
  lua["setPlayerTalent"] = Function_SetPlayerTalent;
  lua["getPlayerTalent"] = Function_GetPlayerTalent;
  lua["setPlayerLevel"] = Function_SetPlayerLevel;
  lua["getPlayerLevel"] = Function_GetPlayerLevel;
  lua["setPlayerExp"] = Function_SetPlayerExp;
  lua["getPlayerExp"] = Function_GetPlayerExp;
  lua["setPlayerNextLevelExp"] = Function_SetPlayerNextLevelExp;
  lua["getPlayerNextLevelExp"] = Function_GetPlayerNextLevelExp;
  lua["setPlayerLearnPoints"] = Function_SetPlayerLearnPoints;
  lua["getPlayerLearnPoints"] = Function_GetPlayerLearnPoints;
  lua["setPlayerVisual"] = Function_SetPlayerVisual;
  lua["getPlayerVisual"] = Function_GetPlayerVisual;
  lua["setPlayerFatness"] = Function_SetPlayerFatness;
  lua["getPlayerFatness"] = Function_GetPlayerFatness;
  lua["setPlayerScale"] = Function_SetPlayerScale;
  lua["getPlayerScale"] = Function_GetPlayerScale;
  lua["setPlayerWeaponMode"] = Function_SetPlayerWeaponMode;
  lua["getPlayerWeaponMode"] = Function_GetPlayerWeaponMode;
  lua["getPlayerAniId"] = Function_GetPlayerAniId;
  lua["getPlayerArmor"] = Function_GetPlayerArmor;
  lua["getPlayerMeleeWeapon"] = Function_GetPlayerMeleeWeapon;
  lua["getPlayerRangedWeapon"] = Function_GetPlayerRangedWeapon;
  lua["getPlayerHelmet"] = Function_GetPlayerHelmet;
  lua["getPlayerShield"] = Function_GetPlayerShield;
  lua["applyPlayerOverlay"] = Function_ApplyPlayerOverlay;
  lua["getPlayerOverlays"] = Function_GetPlayerOverlays;
  lua["removePlayerOverlay"] = Function_RemovePlayerOverlay;
  lua["playAni"] = Function_PlayAni;
  lua["stopAni"] = Function_StopAni;
  lua["playFaceAni"] = Function_PlayFaceAni;
  lua["stopFaceAni"] = Function_StopFaceAni;
  lua["playGesticulation"] = Function_PlayGesticulation;
  lua["setPlayerPosition"] = Function_SetPlayerPosition;
  lua["getPlayerPosition"] = Function_GetPlayerPosition;
  lua["setPlayerAngle"] = Function_SetPlayerAngle;
  lua["getPlayerAngle"] = Function_GetPlayerAngle;
  lua["setPlayerWorld"] = Function_SetPlayerWorld;
  lua["getPlayerWorld"] = Function_GetPlayerWorld;
  lua["setPlayerVirtualWorld"] = Function_SetPlayerVirtualWorld;
  lua["getPlayerVirtualWorld"] = Function_GetPlayerVirtualWorld;

  lua["ban"] = Function_Ban;
  lua["kick"] = Function_Kick;
  lua["isPlayerConnected"] = Function_IsPlayerConnected;
  lua["isPlayerDead"] = Function_IsPlayerDead;
  lua["isPlayerSpawned"] = Function_IsPlayerSpawned;
  lua["isPlayerUnconscious"] = Function_IsPlayerUnconscious;
  lua["respawnPlayer"] = Function_RespawnPlayer;
  lua["setPlayerRespawnTime"] = Function_SetPlayerRespawnTime;
  lua["getPlayerRespawnTime"] = Function_GetPlayerRespawnTime;

  lua["giveItem"] = Function_GiveItem;
  lua["equipItem"] = Function_EquipItem;
  lua["unequipItem"] = Function_UnequipItem;
  lua["equipArmor"] = Function_EquipArmor;
  lua["unequipArmor"] = Function_UnequipArmor;
  lua["equipMeleeWeapon"] = Function_EquipMeleeWeapon;
  lua["unequipMeleeWeapon"] = Function_UnequipMeleeWeapon;
  lua["equipRangedWeapon"] = Function_EquipRangedWeapon;
  lua["unequipRangedWeapon"] = Function_UnequipRangedWeapon;
  lua["equipHelmet"] = Function_EquipHelmet;
  lua["unequipHelmet"] = Function_UnequipHelmet;
  lua["equipShield"] = Function_EquipShield;
  lua["unequipShield"] = Function_UnequipShield;
  lua["hasItem"] = Function_HasItem;
  lua["removeItem"] = Function_RemoveItem;

  lua["setServerWorld"] = Function_SetServerWorld;
  lua["getServerWorld"] = Function_GetServerWorld;
  
  lua["findNearbyPlayers"] = Function_FindNearbyPlayers;
  lua["getSpawnedPlayersForPlayer"] = Function_GetSpawnedPlayersForPlayer;
  lua["getStreamedPlayersByPlayer"] = Function_GetStreamedPlayersByPlayer;
  lua["setStreamerRadius"] = Function_SetStreamerRadius;
  lua["getStreamerRadius"] = Function_GetStreamerRadius;
  lua["setStreamerHeight"] = Function_SetStreamerHeight;
  lua["getStreamerHeight"] = Function_GetStreamerHeight;

  lua["setTime"] = Function_SetTime;
  lua["getTime"] = Function_GetTime;
  lua["setDayLength"] = Function_SetDayLength;
  lua["getDayLength"] = Function_GetDayLength;
}
