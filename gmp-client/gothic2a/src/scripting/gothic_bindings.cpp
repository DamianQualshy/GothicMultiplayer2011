/*
MIT License

Copyright (c) 2025 Gothic Multiplayer Team.

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

#include "gothic_bindings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <spdlog/spdlog.h>

#include "ZenGin/zGothicAPI.h"
#include "process_input.h"
#include "net_game.h"
#include "patch.h"
#include "gmp_core.h"
#include "Interface.h"
#include "item_ground.h"

#include "lua_discord.h"
#include "lua_chat.h"
#include "lua_draw.h"
#include "lua_draw3d.h"
#include "lua_texture.h"
#include "lua_sound.h"
#include "lua_sound3d.h"
#include "lua_music_theme.h"
#include "lua_camera.h"
#include "audio/lua_music.h"
#include "lua_cursor.h"
#include "lua_vob.h"
#include "lua_sky.h"
#include "lua_way.h"
#include "lua_interface.h"

using namespace Gothic_II_Addon;

namespace gmp::gothic {
namespace {
struct ClientNpc {
  oCNpc* npc{nullptr};
  std::string name;
  bool spawned{false};
};

std::unordered_map<int, ClientNpc> g_client_npcs;
int g_next_npc_id = -1;

oCSpawnManager* GetSpawnManager() {
  return ogame ? ogame->GetSpawnManager() : nullptr;
}

bool HasFactoryAndParser() { return zfactory && zCParser::GetParser(); }

bool HasSpawnPrerequisites() {
  return HasFactoryAndParser() && ogame && GetSpawnManager();
}

Gothic2APlayer* GetPlayerById(std::uint64_t id) {
  auto& players = NetGame::Instance().players;
  auto it = std::find_if(players.begin(), players.end(), [id](Gothic2APlayer* player) {
    return player && player->base_player().id() == id;
  });

  if (it == players.end()) {
    return nullptr;
  }

  return *it;
}

Gothic2APlayer* GetPlayerByIdSigned(std::int64_t id) {
  if (id < 0) {
    return nullptr;
  }

  return GetPlayerById(static_cast<std::uint64_t>(id));
}

oCNpc* GetNpcById(std::int64_t id) {
  if (id < 0) {
    auto it = g_client_npcs.find(static_cast<int>(id));
    if (it != g_client_npcs.end()) {
      return it->second.npc;
    }
    return nullptr;
  }

  if (auto* player = GetPlayerById(static_cast<std::uint64_t>(id))) {
    return player->GetNpc();
  }

  return nullptr;
}

enum class EquipmentSlot {
  Armor,
  MeleeWeapon,
  RangedWeapon,
  Helmet,
  Shield,
};

std::optional<std::string> ItemInstanceNameByIndex(int index) {
  if (index <= 0) {
    return std::nullopt;
  }

  auto* parser = zCParser::GetParser();
  if (!parser) {
    return std::nullopt;
  }

  auto* symbol = parser->GetSymbol(index);
  if (!symbol || symbol->name.IsEmpty()) {
    return std::nullopt;
  }

  return std::string(symbol->name.ToChar());
}

std::optional<std::string> ItemInstanceName(oCItem* item) {
  return item ? ItemInstanceNameByIndex(item->GetInstance()) : std::nullopt;
}

oCItem* GetNpcSlotItem(oCNpc* npc, const char* slot_name) {
  if (!npc) {
    return nullptr;
  }

  zSTRING slot(slot_name);
  return npc->GetSlotItem(slot);
}

oCItem* GetEquippedSlotItem(oCNpc* npc, EquipmentSlot slot) {
  if (!npc) {
    return nullptr;
  }

  switch (slot) {
    case EquipmentSlot::Armor:
      return npc->GetEquippedArmor();
    case EquipmentSlot::MeleeWeapon:
      return npc->GetEquippedMeleeWeapon();
    case EquipmentSlot::RangedWeapon:
      return npc->GetEquippedRangedWeapon();
    case EquipmentSlot::Helmet:
      return GetNpcSlotItem(npc, NPC_NODE_HELMET);
    case EquipmentSlot::Shield:
      return GetNpcSlotItem(npc, NPC_NODE_SHIELD);
  }

  return nullptr;
}

std::optional<std::string> CachedEquipmentSlotInstance(Gothic2APlayer* player, EquipmentSlot slot) {
  if (!player) {
    return std::nullopt;
  }

  auto& base = player->base_player();
  switch (slot) {
    case EquipmentSlot::Armor:
      return ItemInstanceNameByIndex(base.equipped_armor());
    case EquipmentSlot::MeleeWeapon:
      return ItemInstanceNameByIndex(base.melee_weapon());
    case EquipmentSlot::RangedWeapon:
      return ItemInstanceNameByIndex(base.ranged_weapon());
    case EquipmentSlot::Helmet:
      return ItemInstanceNameByIndex(base.equipped_helmet());
    case EquipmentSlot::Shield:
      return ItemInstanceNameByIndex(base.equipped_shield());
  }

  return std::nullopt;
}

std::optional<std::string> GetEquipmentSlotInstance(std::int64_t id, EquipmentSlot slot) {
  if (auto* player = GetPlayerByIdSigned(id)) {
    auto live_instance = ItemInstanceName(GetEquippedSlotItem(player->GetNpc(), slot));
    return live_instance.has_value() ? live_instance : CachedEquipmentSlotInstance(player, slot);
  }

  return ItemInstanceName(GetEquippedSlotItem(GetNpcById(id), slot));
}

oCItem* FindInventoryItem(oCNpc* npc, const std::string& instance, bool create_one) {
  if (!npc) {
    return nullptr;
  }

  auto* parser = zCParser::GetParser();
  if (!parser) {
    return nullptr;
  }

  zSTRING instance_name(instance.c_str());
  const int instance_id = parser->GetIndex(instance_name);
  if (instance_id < 0) {
    return nullptr;
  }

  if (create_one) {
    npc->CreateItems(instance_id, 1);
  }

  return npc->inventory2.IsIn(instance_id, 1);
}

bool EquipInventoryInstance(oCNpc* npc, const std::string& instance) {
  if (auto* item = FindInventoryItem(npc, instance, true)) {
    npc->EquipItem(item);
    return true;
  }

  return false;
}

bool UnequipSlot(std::int64_t id, EquipmentSlot slot) {
  auto* npc = GetNpcById(id);
  if (auto* item = GetEquippedSlotItem(npc, slot)) {
    npc->UnequipItem(item);
    return true;
  }

  return false;
}

bool ReplaceStandaloneNpcInstance(oCNpc*& npc, int instance_id) {
  if (!npc || !zfactory || !ogame) {
    return false;
  }

  auto* spawn_manager = GetSpawnManager();
  if (!spawn_manager) {
    return false;
  }

  oCNpc* old_npc = npc;
  oCNpc* new_npc = zfactory->CreateNpc(instance_id);
  if (!new_npc) {
    return false;
  }
  
  new_npc->idx = -1;

  zVEC3 position = old_npc->GetPositionWorld();
  zVEC3 heading = old_npc->GetAtVectorWorld();
  zSTRING name = old_npc->GetName();

  new_npc->startAIState = 0;
  new_npc->Enable(position);
  new_npc->SetHeadingYWorld(heading);
  new_npc->SetAnimationsEnabled(true);
  new_npc->name[0] = name;

  new_npc->SetAsPlayer();
  old_npc->Disable();
  old_npc->RemoveVobFromWorld();
  old_npc->Release();
  old_npc = nullptr;
  return true;
}

oCMenu_Status* GetStatusMenu() {
  zSTRING status_menu_name("MENU_STATUS");
  if (auto* menu = dynamic_cast<oCMenu_Status*>(zCMenu::GetByName(status_menu_name))) {
    return menu;
  }

  zSTRING fallback_name("STATUS");
  return dynamic_cast<oCMenu_Status*>(zCMenu::GetByName(fallback_name));
}
}  // namespace

/* luagmp (global)
*
* Represents the client player id.
* 
* Use it, to manipulate the hero (local player) with player functions, e.g: [setPlayerPosition](../client-functions/player/setPlayerPosition.md).
* 
* @side		client
* @name		heroId
* @return (number)
*
*/

/* luagmp (func)
*
* This function will change the player/npc in-game instance.
*
* @version  0.3.0
* @name     setPlayerInstance
* @side     client
* @category Player
* @param    (number) player_id      Target player id.
* @param    (string) instance       Instance name.
* @return   (boolean)               True on success, false otherwise.
*
*/
bool Function_SetPlayerInstance(std::int64_t id, const std::string& instance) {
  if (auto* parser = zCParser::GetParser()) {
    zSTRING instance_name(instance.c_str());
    const int instance_id = parser->GetIndex(instance_name);
    if (instance_id < 0) {
      return false;
    }

    if (auto* player = GetPlayerByIdSigned(id)) {
      if (player->ReplaceNpcInstance(instance_id)) {
        player->base_player().set_instance(instance);
        return true;
      }
      return false;
    }

    if (id < 0) {
      auto it = g_client_npcs.find(static_cast<int>(id));
      if (it == g_client_npcs.end()) {
        return false;
      }
      return ReplaceStandaloneNpcInstance(it->second.npc, instance_id);
    }
  }
  return false;
}

/* luagmp (func)
*
* This function will return the player/npc current instance, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerInstance
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @return   (string|nil)          Instance name or nil.
*
*/
sol::object Function_GetPlayerInstance(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    std::string name = npc->GetInstanceName().ToChar();
    return sol::make_object(lua, std::move(name));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will change the player/npc character name.
*
* @version  0.3.0
* @name     setPlayerName
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (string) name       New player name.
* @return   (boolean)           True on success.
*
*/
bool Function_SetPlayerName(std::int64_t id, const std::string& name) {
  if (auto* player = GetPlayerByIdSigned(id)) {
    zSTRING new_name(name.c_str());

    for (auto* other_player : NetGame::Instance().players) {
      if (!other_player || !other_player->GetNpc()) {
        continue;
      }

      if (other_player->base_player().id() != static_cast<std::uint64_t>(id) && other_player->GetNpc()->GetName() == new_name) {
        return false;
      }
    }

    player->SetName(new_name);
    player->base_player().set_name(new_name.ToChar());
    return true;
  }

  if (auto* npc = GetNpcById(id)) {
    npc->name[0] = name.c_str();
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will return the player/npc current character name, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerName
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (string|nil)        Player name or nil.
*
*/
sol::object Function_GetPlayerName(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    std::string name = npc->GetName().ToChar();
    return sol::make_object(lua, std::move(name));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will change the player/npc character name color.
*
* @version  0.3.0
* @name     setPlayerColor
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) r          The red color component in RGB model.
* @param    (number) g          The green color component in RGB model.
* @param    (number) b          The blue color component in RGB model.
*
*/
bool Function_SetPlayerColor(std::int64_t id, int r, int g, int b) {
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    if (auto* player = GetPlayerByIdSigned(id)) {
      player->SetNameColor(zCOLOR(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                                  static_cast<unsigned char>(b), 255));
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc current character name color, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerColor
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   ({r, g, b}|nil)     Table containing color in RGB model or nil.
*
*/
sol::object Function_GetPlayerColor(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* player = GetPlayerByIdSigned(id)) {
    const auto& color = player->GetNameColor();

    sol::table tbl = lua.create_table();
    tbl["r"] = static_cast<int>(color.r);
    tbl["g"] = static_cast<int>(color.g);
    tbl["b"] = static_cast<int>(color.b);

    return sol::make_object(lua, tbl);
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc health, not exceeding his current max health.
*
* @version  0.3.0
* @name     setPlayerHealth
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) health     New health value.
*
*/
bool Function_SetPlayerHealth(std::int64_t id, int health) {
  if (health < 0) {
    health = 0;
  }

  if (auto* npc = GetNpcById(id)) {
    const int max_health = npc->GetAttribute(NPC_ATR_HITPOINTSMAX);
    const int clamped_health = std::min(health, max_health);
    npc->SetAttribute(NPC_ATR_HITPOINTS, clamped_health);

    if (auto* player = GetPlayerByIdSigned(id)) {
      player->base_player().set_health(static_cast<short>(clamped_health));
    }

    return true;
  }
  return false;
}

/* luagmp (func)
*
* This function will return the player/npc current health, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerHealth
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Current health or nil.
*
*/
sol::object Function_GetPlayerHealth(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetAttribute(NPC_ATR_HITPOINTS));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc maximum health. If the current health exceeds the new maximum, it will be clamped down to the new value.
*
* @version  0.3.0
* @name     setPlayerMaxHealth
* @side     client
* @category Player
* @param    (number) player_id   Target player id.
* @param    (number) max_health  New maximum health.
*
*/
bool Function_SetPlayerMaxHealth(std::int64_t id, int max_health) {
  if (max_health < 0) {
    max_health = 0;
  }

  if (auto* npc = GetNpcById(id)) {
    npc->SetAttribute(NPC_ATR_HITPOINTSMAX, max_health);
    const int current_health = npc->GetAttribute(NPC_ATR_HITPOINTS);
    if (current_health > max_health) {
      npc->SetAttribute(NPC_ATR_HITPOINTS, max_health);
    }
    return true;
  }
  return false;
}

/* luagmp (func)
*
* This function will return the player/npc current maximum health, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMaxHealth
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Max health or nil.
*
*/
sol::object Function_GetPlayerMaxHealth(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetAttribute(NPC_ATR_HITPOINTSMAX));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc mana, not exceeding his current max mana.
*
* @version  0.3.0
* @name     setPlayerMana
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) mana       Mana value.
*
*/
bool Function_SetPlayerMana(std::int64_t id, int mana) {
    if (mana < 0) {
      mana = 0;
    }

    if (auto* npc = GetNpcById(id)) {
      const int max_mana = npc->GetAttribute(NPC_ATR_MANAMAX);
      const int clamped_mana = std::min(mana, max_mana);
      npc->SetAttribute(NPC_ATR_MANA, clamped_mana);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc current mana, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMana
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Current mana or nil.
*
*/
sol::object Function_GetPlayerMana(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetAttribute(NPC_ATR_MANA));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc maximum mana. If the current mana exceeds the new maximum, it will be clamped down to the new value.
*
* @version  0.3.0
* @name     setPlayerMaxMana
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) max_mana   New maximum mana.
*
*/
bool Function_SetPlayerMaxMana(std::int64_t id, int max_mana) {
    if (max_mana < 0) {
      max_mana = 0;
    }

    if (auto* npc = GetNpcById(id)) {
      npc->SetAttribute(NPC_ATR_MANAMAX, max_mana);
      const int current_mana = npc->GetAttribute(NPC_ATR_MANA);
      if (current_mana > max_mana) {
        npc->SetAttribute(NPC_ATR_MANA, max_mana);
      }
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc current max mana, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerMaxMana
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Max mana or nil.
*
*/
sol::object Function_GetPlayerMaxMana(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetAttribute(NPC_ATR_MANAMAX));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc strength attribute.
*
* @version  0.3.0
* @name     setPlayerStrength
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) strength   Strength value.
*
*/
bool Function_SetPlayerStrength(std::int64_t id, int strength) {
    if (strength < 0) {
      strength = 0;
    }

    if (auto* npc = GetNpcById(id)) {
      npc->SetAttribute(NPC_ATR_STRENGTH, strength);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc strength attribute, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerStrength
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Strength value or nil.
*
*/
sol::object Function_GetPlayerStrength(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetAttribute(NPC_ATR_STRENGTH));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc dexterity attribute.
*
* @version  0.3.0
* @name     setPlayerDexterity
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) dexterity  Dexterity value.
*
*/
bool Function_SetPlayerDexterity(std::int64_t id, int dexterity) {
    if (dexterity < 0) {
      dexterity = 0;
    }

    if (auto* npc = GetNpcById(id)) {
      npc->SetAttribute(NPC_ATR_DEXTERITY, dexterity);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc dexterity attribute, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerDexterity
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Dexterity value or nil.
*
*/
sol::object Function_GetPlayerDexterity(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetAttribute(NPC_ATR_DEXTERITY));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc weapon skill hit chance.
*
* @version  0.3.0
* @name     setPlayerSkillWeapon
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) skill_id   Skill identifier.
* @param    (number) percentage Hit chance (0-100).
*
*/
bool Function_SetPlayerSkillWeapon(std::int64_t id, int skill_id, int percentage) {
    percentage = std::clamp(percentage, 0, 100);

    if (auto* npc = GetNpcById(id)) {
      npc->SetHitChance(skill_id, percentage);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc weapon skill hit chance, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerSkillWeapon
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) skill_id   Skill identifier.
* @return   (number|nil)        Hit chance (0-100) or nil.
*
*/
sol::object Function_GetPlayerSkillWeapon(std::int64_t id, int skill_id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetHitChance(skill_id));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc talent value.
*
* @version  0.3.0
* @name     setPlayerTalent
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (number) talent_id    Talent identifier.
* @param    (number) talent_value Talent value.
*
*/
bool Function_SetPlayerTalent(std::int64_t id, int talent_id, int talent_value) {
    if (auto* npc = GetNpcById(id)) {
      npc->SetTalentSkill(talent_id, talent_value);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc talent value, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerTalent
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) talent_id  Talent identifier.
* @return   (number|nil)        Talent value or nil.
*
*/
sol::object Function_GetPlayerTalent(std::int64_t id, int talent_id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->GetTalentSkill(talent_id));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc level.
*
* @version  0.3.0
* @name     setPlayerLevel
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) level      New level.
*
*/
bool Function_SetPlayerLevel(std::int64_t id, int level) {
    if (auto* npc = GetNpcById(id)) {
      npc->level = static_cast<int>(std::max(level, 0));
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return player/npc current level, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerLevel
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Level or nil.
*
*/
sol::object Function_GetPlayerLevel(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, static_cast<int>(npc->level));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the hero's experience points.
*
* @version  0.3.0
* @name     setExp
* @side     client
* @category Hero
* @param    (number) exp       New exp value.
*
*/
bool Function_SetExp(int exp) {
  if (auto* player = Gothic2APlayer::GetLocalPlayer()) {
    if (auto* npc = player->GetNpc()) {
      const unsigned long clamped_exp = static_cast<unsigned long>(std::max(exp, 0));
      npc->experience_points = clamped_exp;

      if (auto* status_menu = GetStatusMenu()) {
        status_menu->SetExperience(clamped_exp, 0, npc->experience_points_next_level);
      }
      return true;
    }
  }
  return false;
}


/* luagmp (func)
*
* This function will return the hero's current experience point amount, or nil if unavailable.
*
* @version  0.3.0
* @name     getExp
* @side     client
* @category Hero
* @return   (number|nil)        Exp value or nil.
*
*/
sol::object Function_GetExp(sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* player = Gothic2APlayer::GetLocalPlayer()) {
    if (auto* npc = player->GetNpc()) {
      return sol::make_object(lua, static_cast<int>(npc->experience_points));
    }
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the experience required for the hero's next level.
*
* @version  0.3.0
* @name     setNextLevelExp
* @side     client
* @category Hero
* @param    (number) next_level_exp   Required exp for next level.
*
*/
bool Function_SetNextLevelExp(int next_level_exp) {
  if (auto* player = Gothic2APlayer::GetLocalPlayer()) {
    if (auto* npc = player->GetNpc()) {
      const unsigned long clamped_next_level = static_cast<unsigned long>(std::max(next_level_exp, 0));
      npc->experience_points_next_level = clamped_next_level;

      if (auto* status_menu = GetStatusMenu()) {
        status_menu->SetExperience(npc->experience_points, 0, clamped_next_level);
      }
      return true;
    }
  }
  return false;
}

/* luagmp (func)
*
* This function will return the current experience required for the hero's next level, or nil if unavailable.
*
* @version  0.3.0
* @name     getNextLevelExp
* @side     client
* @category Hero
* @return   (number|nil)        Next level exp or nil.
*
*/
sol::object Function_GetNextLevelExp(sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* player = Gothic2APlayer::GetLocalPlayer()) {
    if (auto* npc = player->GetNpc()) {
      return sol::make_object(lua, static_cast<int>(npc->experience_points_next_level));
    }
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the hero's learn points.
*
* @version  0.3.0
* @name     setLearnPoints
* @side     client
* @category Hero
* @param    (number) learn_points   New learn points value.
*
*/
bool Function_SetLearnPoints(int learn_points) {
  if (auto* player = Gothic2APlayer::GetLocalPlayer()) {
    if (auto* npc = player->GetNpc()) {
      const unsigned long clamped_points = static_cast<unsigned long>(std::max(learn_points, 0));
      npc->learn_points = clamped_points;

      if (auto* status_menu = GetStatusMenu()) {
        status_menu->SetLearnPoints(clamped_points);
      }
      return true;
    }
  }
  return false;
}

/* luagmp (func)
*
* This function will return the hero's current learn points, or nil if unavailable.
*
* @version  0.3.0
* @name     getLearnPoints
* @side     client
* @category Hero
* @return   (number|nil)        Learn points or nil.
*
*/
sol::object Function_GetLearnPoints(sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* player = Gothic2APlayer::GetLocalPlayer()) {
    if (auto* npc = player->GetNpc()) {
      return sol::make_object(lua, static_cast<int>(npc->learn_points));
    }
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc visual model and textures.
*
* @version  0.3.0
* @name     setPlayerVisual
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string) body_model   Body model name.
* @param    (number) body_texture    Body texture index.
* @param    (string) head_model   Head model name.
* @param    (number) head_texture    Head texture index.
*
*/
bool Function_SetPlayerVisual(std::int64_t id, const std::string& body_model, int body_texture, const std::string& head_model, int head_texture,
         sol::optional<int> teeth_texture, sol::optional<int> skin_color) {
        if (auto* npc = GetNpcById(id); npc) {
          zSTRING body(body_model.c_str());
          zSTRING head(head_model.c_str());
          const int color_variant = skin_color.value_or(0);
          const int teeth_variant = teeth_texture.value_or(0);
          npc->SetAdditionalVisuals(body, body_texture, color_variant, head, head_texture, teeth_variant, color_variant);
          return true;
        }
        return false;
      }

/* luagmp (func)
*
* This function will return the player/npc current visual model and textures, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerVisual
* @side     client
* @category Player
* @param    (number) player_id                                           Target player id.
* @return   ({body_model, body_texture, head_model, head_texture}|nil)   Player visual or nil.
*
*/
sol::object Function_GetPlayerVisual(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id); npc) {
    sol::table tbl = lua.create_table();

    tbl["body_model"] = std::string(npc->body_visualName.ToChar());
    tbl["body_texture"] = static_cast<int>(npc->body_TexVarNr);
    tbl["head_model"] = std::string(npc->head_visualName.ToChar());
    tbl["head_texture"] = static_cast<int>(npc->head_TexVarNr);
    return sol::make_object(lua, tbl);
  }
  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc model fatness.
*
* @version  0.3.0
* @name     setPlayerFatness
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) fatness    Fatness value.
*
*/
bool Function_SetPlayerFatness(std::int64_t id, float fatness) {
    if (auto* npc = GetNpcById(id); npc) {
      npc->SetFatness(fatness);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc current model fatness, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerFatness
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @return   (number|nil)          Player fatness or nil.
*
*/
sol::object Function_GetPlayerFatness(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->model_fatness);
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc model scale.
*
* @version  0.3.0
* @name     setPlayerScale
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (number) x            Scale factor on x axis.
* @param    (number) y            Scale factor on y axis.
* @param    (number) z            Scale factor on z axis.
*
*/
bool Function_SetPlayerScale(std::int64_t id, float x, float y, float z) {
    if (auto* npc = GetNpcById(id); npc) {
      npc->SetModelScale(zVEC3{x, y, z});
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc current model scale, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerScale
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @return   ({x, y, z}|nil)       Player scale or nil.
*
*/
sol::object Function_GetPlayerScale(std::int64_t id, sol::this_state ts){
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id); npc) {
    const zVEC3 scale = npc->model_scale;
    sol::table tbl = lua.create_table();
    tbl["x"] = scale[VX];
    tbl["y"] = scale[VY];
    tbl["z"] = scale[VZ];
    return sol::make_object(lua, tbl);
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc weapon mode for all players.
*
* @version  0.3.0
* @name     setPlayerWeaponMode
* @side     client
* @category Player
* @param    (number) player_id   Target player id.
* @param    (number) weapon_mode Weapon mode constant.
*
*/
bool Function_SetPlayerWeaponMode(std::int64_t id, int weapon_mode) {
  if (auto* npc = GetNpcById(id); npc) {
    npc->SetWeaponMode(weapon_mode);
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will return the player/npc current weapon mode, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerWeaponMode
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Weapon mode or nil.
*
*/
sol::object Function_GetPlayerWeaponMode(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id); npc) {
    return sol::make_object(lua, npc->GetWeaponMode());
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will return the active animation name for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerAni
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (string|nil)        Active animation name, or nil if unavailable.
*
*/
sol::object Function_GetPlayerAni(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    if (auto* model = npc->GetModel(); model && model->numActiveAnis > 0) {
      if (auto* active = model->GetActiveAni(0); active && active->protoAni) {
        return sol::make_object(lua, std::string(active->protoAni->GetAniName().ToChar()));
      }
    }
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will return the active animation id for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerAniId
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (number)            Active animation id, or -1 if unavailable.
*
*/
std::int32_t Function_GetPlayerAniId(std::int64_t id) {
  if (auto* npc = GetNpcById(id)) {
    if (auto* model = npc->GetModel(); model && model->numActiveAnis > 0) {
      if (auto* active = model->GetActiveAni(0); active && active->protoAni) {
        return active->protoAni->GetAniID();
      }
    }
  }

  if (auto* player = GetPlayerByIdSigned(id)) {
    return player->base_player().animation();
  }

  return -1;
}

/* luagmp (func)
*
* This function will return the equipped armor instance name for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerArmor
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (string|nil)        Equipped armor instance, or nil if no armor is equipped.
*
*/
sol::object Function_GetPlayerArmor(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  auto instance = GetEquipmentSlotInstance(id, EquipmentSlot::Armor);
  return instance.has_value() ? sol::make_object(lua, *instance) : sol::make_object(lua, sol::lua_nil);
}

/* luagmp (func)
*
* This function will return the equipped melee weapon instance name for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerMeleeWeapon
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (string|nil)        Equipped melee weapon instance, or nil if none is equipped.
*
*/
sol::object Function_GetPlayerMeleeWeapon(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  auto instance = GetEquipmentSlotInstance(id, EquipmentSlot::MeleeWeapon);
  return instance.has_value() ? sol::make_object(lua, *instance) : sol::make_object(lua, sol::lua_nil);
}

/* luagmp (func)
*
* This function will return the equipped ranged weapon instance name for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerRangedWeapon
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (string|nil)        Equipped ranged weapon instance, or nil if none is equipped.
*
*/
sol::object Function_GetPlayerRangedWeapon(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  auto instance = GetEquipmentSlotInstance(id, EquipmentSlot::RangedWeapon);
  return instance.has_value() ? sol::make_object(lua, *instance) : sol::make_object(lua, sol::lua_nil);
}

/* luagmp (func)
*
* This function will return the equipped helmet instance name for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerHelmet
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (string|nil)        Equipped helmet instance, or nil if no helmet is equipped.
*
*/
sol::object Function_GetPlayerHelmet(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  auto instance = GetEquipmentSlotInstance(id, EquipmentSlot::Helmet);
  return instance.has_value() ? sol::make_object(lua, *instance) : sol::make_object(lua, sol::lua_nil);
}

/* luagmp (func)
*
* This function will return the equipped shield instance name for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerShield
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (string|nil)        Equipped shield instance, or nil if no shield is equipped.
*
*/
sol::object Function_GetPlayerShield(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  auto instance = GetEquipmentSlotInstance(id, EquipmentSlot::Shield);
  return instance.has_value() ? sol::make_object(lua, *instance) : sol::make_object(lua, sol::lua_nil);
}

/* luagmp (func)
*
* This function will apply an animation overlay on player (eg. "HUMANS_MILITIA.MDS").
*
* @version  0.3.0
* @name     applyPlayerOverlay
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string) overlay      The name of overlay.
*
*/
bool Function_ApplyPlayerOverlay(std::int64_t id, const std::string& overlay) {
    if (auto* npc = GetNpcById(id); npc) {
      zSTRING overlay_name(overlay.c_str());
      return npc->ApplyOverlay(overlay_name) != 0;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc active animation overlays, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerOverlays
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @return   ({...}|nil)           Array of overlay names or nil.
*
*/
sol::object Function_GetPlayerOverlays(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    const zCArray<zSTRING>& overlays = npc->activeOverlays;

    if (overlays.GetNum() == 0) {
      return sol::nil; // or return empty table if you prefer
    }

    sol::table tbl = lua.create_table(overlays.GetNum(), 0);

    for (int i = 0; i < overlays.GetNum(); ++i) {
      tbl[i + 1] = overlays[i].ToChar();
    }

    return tbl;
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will remove specified animation overlay from the player.
*
* @version  0.3.0
* @name     removePlayerOverlay
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string) overlay      The name of overlay.
*
*/
bool Function_RemovePlayerOverlay(std::int64_t id, const std::string& overlay) {
    if (auto* npc = GetNpcById(id); npc) {
      zSTRING overlay_name(overlay.c_str());
      const bool has_overlay = npc->GetOverlay(overlay_name) != 0;
      if (has_overlay) {
        npc->RemoveOverlay(overlay_name);
      }
      return has_overlay;
    }
    return false;
  }

/* luagmp (func)
*
* This function will play an animation on the player/npc character.
*
* @version  0.3.0
* @name     playAni
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (string) aniName    Animation name (e.g. "T_STAND_2_SIT").
*
*/
bool Function_PlayAni(std::int64_t id, const std::string& ani_name) {
  if (auto* npc = GetNpcById(id)) {
    if (auto* model = npc->GetModel()) {
      model->StartAnimation(zSTRING(ani_name.c_str()));
      return true;
    }
  }

  return false;
}

/* luagmp (func)
*
* This function will stop a played animation on the player/npc character.
*
* @version  0.3.0
* @name     stopAni
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string|nil) aniName  Animation name to stop. Defaults to "" for first active animation.
*
*/
bool Function_StopAni(std::int64_t id, sol::optional<std::string> ani_name) {
  if (auto* npc = GetNpcById(id)) {
    if (auto* model = npc->GetModel()) {
      const std::string name = ani_name.value_or("");
      if (!name.empty()) {
        model->StopAnimation(zSTRING(name.c_str()));
        return true;
      }
      if (model->numActiveAnis > 0) {
        if (auto* active = model->GetActiveAni(0)) {
          model->StopAni(active);
          return true;
        }
      }
    }
  }

  return false;
}

/* luagmp (func)
*
* This function will play a face animation on the player/npc character.
*
* @version  0.3.0
* @name     playFaceAni
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (string) aniName    Face animation name (e.g. "S_FRIENDLY").
*
*/
bool Function_PlayFaceAni(std::int64_t id, const std::string& ani_name) {
  if (auto* npc = GetNpcById(id)) {
    zSTRING face_name(ani_name.c_str());
    npc->StartFaceAni(face_name, 1.0f, 1.0f);
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will stop a played face animation on the player/npc character.
*
* @version  0.3.0
* @name     stopFaceAni
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (string|nil) aniName  Face animation name to stop. Defaults to "" for first active animation.
*
*/
bool Function_StopFaceAni(std::int64_t id, sol::optional<std::string> ani_name) {
  if (auto* npc = GetNpcById(id)) {
    zSTRING face_name(ani_name.value_or("").c_str());
    npc->StopFaceAni(face_name);
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will play a gesticulation animation on the player/npc character.
* Calls for the same id are throttled to once every 3500 ms.
*
* @version  0.3.0
* @name     playGesticulation
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
*
*/
bool Function_PlayGesticulation(std::int64_t id) {
  static std::unordered_map<std::int64_t, std::chrono::steady_clock::time_point> next_allowed_by_player;

  if (auto* npc = GetNpcById(id); npc && !npc->IsDead() && !npc->IsUnconscious()) {
    const auto now = std::chrono::steady_clock::now();
    auto& next_allowed = next_allowed_by_player[id];
    if (now < next_allowed) {
      return false;
    }

    npc->StartDialogAni();
    next_allowed = now + std::chrono::milliseconds(3500);
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will set the player/npc world position.
*
* @version  0.3.0
* @name     setPlayerPosition
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @param    (number) x          X coordinate.
* @param    (number) y          Y coordinate.
* @param    (number) z          Z coordinate.
*
*/
bool Function_SetPlayerPosition(std::int64_t id, float x, float y, float z) {
  if (auto* npc = GetNpcById(id)) {
    zVEC3 position{x, y, z};
    npc->SetPositionWorld(position);

    if (auto* player = GetPlayerByIdSigned(id)) {
      player->SetPosition(position);
      player->base_player().set_position(x, y, z);
    }

    return true;
  }
  return false;
}

/* luagmp (func)
*
* This function will return the player/npc current world position.
*
* @version  0.3.0
* @name     getPlayerPosition
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   ({x,y,z}|nil)       Table with keys `x`,`y`,`z` or nil.
*
*/
sol::object Function_GetPlayerPosition(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    const zVEC3 position = npc->GetPositionWorld();
    sol::table tbl = lua.create_table();
    tbl["x"] = position[VX];
    tbl["y"] = position[VY];
    tbl["z"] = position[VZ];
    return sol::make_object(lua, tbl);
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will set the player/npc facing angle in world space.
*
* @version  0.3.0
* @name     setPlayerAngle
* @side     client
* @category Player
* @param    (number) player_id    Target player id.
* @param    (number) angle        Angle in radians.
*
*/
bool Function_SetPlayerAngle(std::int64_t id, float angle) {
    if (auto* npc = GetNpcById(id)) {
      const float radians = angle;
      const zVEC3 heading_vector(std::sin(radians), 0.0F, std::cos(radians));
      npc->SetHeadingYWorld(heading_vector);
      return true;
    }
    return false;
  }

/* luagmp (func)
*
* This function will return the player/npc facing angle in world space, or nil if unavailable.
*
* @version  0.3.0
* @name     getPlayerAngle
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number|nil)        Angle in radians or nil.
*
*/
sol::object Function_GetPlayerAngle(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (auto* npc = GetNpcById(id)) {
    const zVEC3 forward = npc->GetAtVectorWorld();
    return sol::make_object(lua, std::atan2(forward[VX], forward[VZ]));
  }

  return sol::nil;
}

/* luagmp (func)
*
* This function will give an item to the player or NPC on the client.
*
* @version  0.3.0
* @name     giveItem
* @side     client
* @category Inventory
* @param    (number) player_id    Target player id.
* @param    (string) instance     Item instance name.
* @param    (number) amount       Amount to give.
*
*/
bool Function_GiveItem(std::int64_t id, const std::string& instance, std::int32_t amount) {
  if (amount <= 0) {
    return false;
  }

  if (auto* npc = GetNpcById(id)) {
    if (auto* parser = zCParser::GetParser()) {
      zSTRING instance_name(instance.c_str());
      const int instance_id = parser->GetIndex(instance_name);
      if (instance_id >= 0) {
        npc->CreateItems(instance_id, amount);
        return true;
      }
    }
  }

  return false;
}

/* luagmp (func)
*
* This function will equip an item for the player/NPC.
*
* @version  0.3.0
* @name     equipItem
* @side     client
* @category Inventory
* @param    (number) player_id      Target player id.
* @param    (string) instance       Item instance name.
*
*/
bool Function_EquipItem(std::int64_t id, const std::string& instance) {
  return EquipInventoryInstance(GetNpcById(id), instance);
}

/* luagmp (func)
*
* This function will unequip an item from the player/NPC.
*
* @version  0.3.0
* @name     unequipItem
* @side     client
* @category Inventory
* @param    (number) player_id      Target player id.
* @param    (string) instance       Item instance name.
*
*/
bool Function_UnequipItem(std::int64_t id, const std::string& instance) {
  if (auto* npc = GetNpcById(id)) {
    if (auto* parser = zCParser::GetParser()) {
      zSTRING instance_name(instance.c_str());
      const int instance_id = parser->GetIndex(instance_name);
      if (instance_id >= 0) {
        if (auto* item = npc->inventory2.IsIn(instance_id, 1)) {
          npc->UnequipItem(item);
          return true;
        }
      }
    }
  }

  return false;
}

/* luagmp (func)
*
* This function will equip an armor item for a player or NPC.
*
* @version  0.3.0
* @name     equipArmor
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @param    (string) instance     Armor item instance name.
* @return   (boolean)             True on success.
*
*/
bool Function_EquipArmor(std::int64_t id, const std::string& instance) {
  return EquipInventoryInstance(GetNpcById(id), instance);
}

/* luagmp (func)
*
* This function will unequip the current armor from a player or NPC.
*
* @version  0.3.0
* @name     unequipArmor
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @return   (boolean)             True when an armor item was equipped and unequipped.
*
*/
bool Function_UnequipArmor(std::int64_t id) {
  return UnequipSlot(id, EquipmentSlot::Armor);
}

/* luagmp (func)
*
* This function will equip a melee weapon for a player or NPC.
*
* @version  0.3.0
* @name     equipMeleeWeapon
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @param    (string) instance     Melee weapon item instance name.
* @return   (boolean)             True on success.
*
*/
bool Function_EquipMeleeWeapon(std::int64_t id, const std::string& instance) {
  return EquipInventoryInstance(GetNpcById(id), instance);
}

/* luagmp (func)
*
* This function will unequip the current melee weapon from a player or NPC.
*
* @version  0.3.0
* @name     unequipMeleeWeapon
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @return   (boolean)             True when a melee weapon was equipped and unequipped.
*
*/
bool Function_UnequipMeleeWeapon(std::int64_t id) {
  return UnequipSlot(id, EquipmentSlot::MeleeWeapon);
}

/* luagmp (func)
*
* This function will equip a ranged weapon for a player or NPC.
*
* @version  0.3.0
* @name     equipRangedWeapon
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @param    (string) instance     Ranged weapon item instance name.
* @return   (boolean)             True on success.
*
*/
bool Function_EquipRangedWeapon(std::int64_t id, const std::string& instance) {
  return EquipInventoryInstance(GetNpcById(id), instance);
}

/* luagmp (func)
*
* This function will unequip the current ranged weapon from a player or NPC.
*
* @version  0.3.0
* @name     unequipRangedWeapon
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @return   (boolean)             True when a ranged weapon was equipped and unequipped.
*
*/
bool Function_UnequipRangedWeapon(std::int64_t id) {
  return UnequipSlot(id, EquipmentSlot::RangedWeapon);
}

/* luagmp (func)
*
* This function will equip a helmet item for a player or NPC.
*
* @version  0.3.0
* @name     equipHelmet
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @param    (string) instance     Helmet item instance name.
* @return   (boolean)             True on success.
*
*/
bool Function_EquipHelmet(std::int64_t id, const std::string& instance) {
  return EquipInventoryInstance(GetNpcById(id), instance);
}

/* luagmp (func)
*
* This function will unequip the current helmet from a player or NPC.
*
* @version  0.3.0
* @name     unequipHelmet
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @return   (boolean)             True when a helmet item was equipped and unequipped.
*
*/
bool Function_UnequipHelmet(std::int64_t id) {
  return UnequipSlot(id, EquipmentSlot::Helmet);
}

/* luagmp (func)
*
* This function will equip a shield item for a player or NPC.
*
* @version  0.3.0
* @name     equipShield
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @param    (string) instance     Shield item instance name.
* @return   (boolean)             True on success.
*
*/
bool Function_EquipShield(std::int64_t id, const std::string& instance) {
  return EquipInventoryInstance(GetNpcById(id), instance);
}

/* luagmp (func)
*
* This function will unequip the current shield from a player or NPC.
*
* @version  0.3.0
* @name     unequipShield
* @side     client
* @category Inventory
* @param    (number) player_id    Target player or client NPC id.
* @return   (boolean)             True when a shield item was equipped and unequipped.
*
*/
bool Function_UnequipShield(std::int64_t id) {
  return UnequipSlot(id, EquipmentSlot::Shield);
}

/* luagmp (func)
*
* This function will return the amount of a specific item in the player/npc inventory on the client.
*
* @version  0.3.0
* @name     hasItem
* @side     client
* @category Inventory
* @param    (number) player_id      Target player id.
* @param    (string) instance       Item instance name.
* @return   (number)                Item amount or 0 if missing.
*
*/
int Function_HasItem(std::int64_t id, const std::string& instance) {
  if (auto* npc = GetNpcById(id)) {
    if (auto* parser = zCParser::GetParser()) {
      zSTRING instance_name(instance.c_str());
      const int instance_id = parser->GetIndex(instance_name);
      if (instance_id >= 0) {
        if (auto* item = npc->inventory2.IsIn(instance_id, 1)) {
          return std::max(0, item->amount);
        }
      }
    }
  }

  return 0;
}

/* luagmp (func)
*
* This function will remove an item from the player/npc inventory on the client.
*
* @version  0.3.0
* @name     removeItem
* @side     client
* @category Inventory
* @param    (number) player_id      Target player id.
* @param    (string) instance       Item instance name.
* @param    (number) amount         Amount to remove.
*
*/
bool Function_RemoveItem(std::int64_t id, const std::string& instance, std::int32_t amount) {
  if (amount <= 0) {
    return 0;
  }

  if (auto* npc = GetNpcById(id)) {
    if (auto* parser = zCParser::GetParser()) {
      zSTRING instance_name(instance.c_str());
      const int instance_id = parser->GetIndex(instance_name);
      if (instance_id >= 0) {
        if (auto* item = npc->inventory2.IsIn(instance_id, 1)) {
          const int remove_amount = std::min(amount, item->amount);
          if (remove_amount > 0) {
            return npc->inventory2.Remove(instance_id, remove_amount);
          }
        }
      }
    }
  }

  return false;
}

/* luagmp (func)
*
* This function will remove all items from the hero's inventory.
*
* @version  0.3.0
* @name     clearInventory
* @side     client
* @category Inventory
*
*/
void Function_ClearInventory() {
  if (auto* equippedArmor = player->GetEquippedArmor()) {
    player->UnequipItem(equippedArmor);
  }
  if (auto* rangedWeapon = player->GetEquippedRangedWeapon()) {
    player->UnequipItem(rangedWeapon);
  }
  if (auto* meleeWeapon = player->GetEquippedMeleeWeapon()) {
    player->UnequipItem(meleeWeapon);
  }

  player->inventory2.ClearInventory();
}

/* luagmp (func)
*
* This function will open the local player's inventory.
*
* @version  0.3.0
* @name     openInventory
* @side     client
* @category Inventory
* @return   (boolean)  True on success.
*
*/
bool Function_OpenInventory() {
  if (!player) {
    return false;
  }

  player->OpenInventory(1);
  return true;
}

/* luagmp (func)
*
* This function will close the local player's inventory.
*
* @version  0.3.0
* @name     closeInventory
* @side     client
* @category Inventory
* @return   (boolean)  True on success.
*
*/
bool Function_CloseInventory() {
  if (!player) {
    return false;
  }

  player->CloseInventory();
  return true;
}

/* luagmp (func)
*
* This function will check whether the local player's inventory is open.
*
* @version  0.3.0
* @name     isInventoryOpen
* @side     client
* @category Inventory
* @return   (boolean)  True when the inventory is open.
*
*/
bool Function_IsInventoryOpen() {
  return player && player->inventory2.IsOpen() != 0;
}

/* luagmp (func)
*
* This function will change the player's current game world.
*
* @version  0.3.0
* @name     changeWorld
* @side     client
* @category World
* @param    (string) world             World filename.
* @param    (string|nil) start_point   Optional start point name.
*
*/
void Function_ChangeWorld(const std::string& world, sol::optional<std::string> start_point) {
  zSTRING z_world(world.c_str());
  zSTRING z_start_point = start_point ? zSTRING(start_point->c_str()) : zSTRING("");
    Patch::ChangeLevelEnabled(true);
    ogame->ChangeLevel(z_world, z_start_point);
    Patch::ChangeLevelEnabled(false);
}

/* luagmp (func)
*
* This function will return the current game world filename.
*
* @version  0.3.0
* @name     getWorld
* @side     client
* @category World
* @return   (string)           World filename.
*
*/
sol::object Function_GetWorld(sol::this_state ts) {
  sol::state_view lua(ts);
  return sol::make_object(lua, std::string(ogame->GetGameWorld()->GetWorldFilename().ToChar()));
}

/* luagmp (func)
*
* This function will create a client-side NPC entry and return an internal npc id (<0).
*
* @version  0.3.0
* @name     createNpc
* @side     client
* @category NPC
* @note     By default, the NPC won't be added to the world. It's necessary to call [`spawnNpc`](./spawnNpc.md) afterwards.
* @param    (string) name       Name for the created NPC.
* @return   (number)            NPC id (starting from -1) or 0 on failure.
*
*/
int Function_CreateNpc(const std::string& name) {
    if (!HasFactoryAndParser()) {
      SPDLOG_WARN("createNpc: missing game engine components");
      return 0;
    }

    auto* parser = zCParser::GetParser();
    const int default_instance = parser->GetIndex("PC_HERO");
    if (default_instance < 0) {
      SPDLOG_WARN("createNpc: failed to resolve default instance 'PC_HERO'");
      return 0;
    }

    oCNpc* npc = zfactory->CreateNpc(default_instance);
    if (!npc) {
      SPDLOG_WARN("createNpc: failed to allocate npc");
      return 0;
    }

    npc->name[0] = name.c_str();

    const int npc_id = g_next_npc_id--;
    g_client_npcs.emplace(npc_id, ClientNpc{npc, name});
    return npc_id;
  }

/* luagmp (func)
*
* This function will destroy a client-side NPC`.
*
* @version  0.3.0
* @name     destroyNpc
* @side     client
* @category NPC
* @param    (number) npc_id     NPC id.
* @return   (boolean)           True on success.
*
*/
bool Function_DestroyNpc(int npc_id) {
    auto it = g_client_npcs.find(npc_id);
    if (it == g_client_npcs.end()) {
      return false;
    }

    oCNpc* npc = it->second.npc;
    if (npc && npc->GetHomeWorld()) {
      npc->Disable();
      npc->RemoveVobFromWorld();
    }

    if (auto* spawn_manager = GetSpawnManager()) {
      spawn_manager->DeleteNpc(npc);
    }

    g_client_npcs.erase(it);
    return true;
  }

/* luagmp (func)
*
* This function will spawn a previously created NPC into the world.
*
* @version  0.3.0
* @name     spawnNpc
* @side     client
* @category NPC
* @param    (number) npc_id             NPC id.
* @param    (string|nil) instance_name  Optional instance name (defaults to "PC_HERO").
* @return   (boolean)                   True if spawn attached to world.
*
*/
bool Function_SpawnNpc(int npc_id, sol::optional<std::string> instance_name) {
  auto it = g_client_npcs.find(npc_id);
  if (it == g_client_npcs.end()) {
    return false;
  }

  ClientNpc& entry = it->second;
  if (entry.spawned || entry.npc->GetHomeWorld() != nullptr) {
    SPDLOG_WARN("spawnNpc: npc {} is already spawned", npc_id);
    return false;
  }

  if (!HasSpawnPrerequisites()) {
    SPDLOG_WARN("spawnNpc: missing game engine components");
    return false;
  }

  const std::string instance = instance_name.value_or("PC_HERO");
  auto* parser = zCParser::GetParser();
  const int instance_index = parser->GetIndex(instance.c_str());
  if (instance_index < 0) {
    SPDLOG_WARN("spawnNpc: instance '{}' not found", instance);
    return false;
  }

  entry.npc->InitByScript(instance_index, 0);
  entry.npc->startAIState = 0;
  entry.npc->name[0] = entry.name.c_str();

  zVEC3 spawn_position{0.0f, 0.0f, 0.0f};
  GetSpawnManager()->SpawnNpc(entry.npc, spawn_position, 0.0f);

  if (entry.npc->GetHomeWorld() == nullptr) {
    entry.npc->Enable(spawn_position);
  }

  const bool attached = entry.npc->GetHomeWorld() != nullptr;
  if (!attached) {
    SPDLOG_WARN("spawnNpc: failed to attach npc to world");
  }

  entry.spawned = attached;

  return attached;
}

/* luagmp (func)
*
* This function will unspawn a client NPC from the world.
*
* @version  0.3.0
* @name     unspawnNpc
* @side     client
* @category NPC
* @param    (number) npc_id   Internal npc id.
* @return   (boolean)         True on success.
*
*/
bool Function_UnspawnNpc(int npc_id) {
  auto it = g_client_npcs.find(npc_id);
  if (it == g_client_npcs.end()) {
    return false;
  }

  oCNpc* npc = it->second.npc;
  if (!it->second.spawned || npc == nullptr) {
    SPDLOG_WARN("unspawnNpc: npc {} is not spawned", npc_id);
    return false;
  }
  if (npc && npc->GetHomeWorld()) {
    npc->Disable();
    npc->RemoveVobFromWorld();
  }

  it->second.spawned = false;

  return true;
}

/* luagmp (func)
*
* This function will check whether a player or client NPC object exists.
*
* @version  0.3.0
* @name     isPlayerCreated
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (boolean)           True when the object exists.
*
*/
bool Function_IsPlayerCreated(std::int64_t id) {
  if (id < 0) {
    return g_client_npcs.find(static_cast<int>(id)) != g_client_npcs.end();
  }

  return GetPlayerByIdSigned(id) != nullptr;
}

/* luagmp (func)
*
* This function will check whether a player or client NPC is spawned into the world.
*
* @version  0.3.0
* @name     isPlayerStreamed
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (boolean|nil)       True when spawned, false when known but not spawned, nil if unknown.
*
*/
sol::object Function_IsPlayerStreamed(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);

  if (id < 0) {
    auto it = g_client_npcs.find(static_cast<int>(id));
    if (it == g_client_npcs.end()) {
      return sol::nil;
    }
    return sol::make_object(lua, it->second.spawned && it->second.npc != nullptr);
  }

  auto* player = GetPlayerByIdSigned(id);
  if (!player) {
    return sol::nil;
  }

  return sol::make_object(lua, player->base_player().has_spawned() && player->GetNpc() != nullptr);
}

/* luagmp (func)
*
* This function will check whether a player or NPC is dead.
*
* @version  0.3.0
* @name     isPlayerDead
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (boolean|nil)       Dead state, or nil if unavailable.
*
*/
sol::object Function_IsPlayerDead(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->IsDead() != 0);
  }
  return sol::nil;
}

/* luagmp (func)
*
* This function will check whether a player or NPC is unconscious.
*
* @version  0.3.0
* @name     isPlayerUnconscious
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (boolean|nil)       Unconscious state, or nil if unavailable.
*
*/
sol::object Function_IsPlayerUnconscious(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->IsUnconscious() != 0);
  }
  return sol::nil;
}

/* luagmp (func)
*
* This function will return the local player's current network ping.
*
* @version  0.3.0
* @name     getPlayerPing
* @side     client
* @category Player
* @param    (number) player_id  Target player id.
* @return   (number)            Player ping replicated by the server, or -1 if unavailable.
*
*/
std::int32_t Function_GetPlayerPing(std::int64_t id) {
  auto& net_game = NetGame::Instance();
  if (!net_game.game_client || !net_game.IsConnected() || id < 0) {
    return -1;
  }

  return net_game.game_client->GetPlayerPing(static_cast<std::uint64_t>(id));
}

/* luagmp (func)
*
* This function will return the focused multiplayer player id for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerFocus
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (number)            Focused player id, or -1 if no multiplayer player is focused.
*
*/
std::int32_t Function_GetPlayerFocus(std::int64_t id) {
  auto* npc = GetNpcById(id);
  if (!npc) {
    return -1;
  }

  oCNpc* focus_npc = npc->GetFocusNpc();
  if (!focus_npc) {
    return -1;
  }

  auto focused_player_id = NetGame::Instance().GetPlayerIdByNpc(focus_npc);
  return focused_player_id.has_value() ? static_cast<std::int32_t>(*focused_player_id) : -1;
}

/* luagmp (func)
*
* This function will return the multiplayer player type.
*
* @version  0.3.0
* @name     getPlayerType
* @side     client
* @category Player
* @param    (number) player_id  Target multiplayer player id.
* @return   (number|nil)        Player type, or nil if unavailable.
*
*/
sol::object Function_GetPlayerType(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  if (auto* player = GetPlayerByIdSigned(id)) {
    return sol::make_object(lua, static_cast<int>(player->Type));
  }
  return sol::nil;
}

/* luagmp (func)
*
* This function will place a player or NPC on the floor at its current position.
*
* @version  0.3.0
* @name     setPlayerOnFloor
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (boolean)           True on success.
*
*/
bool Function_SetPlayerOnFloor(std::int64_t id) {
  if (auto* npc = GetNpcById(id)) {
    zVEC3 position = npc->GetPositionWorld();
    npc->SetOnFloor(position);
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will enable or disable collision and physics for a player or NPC.
*
* @version  0.3.0
* @name     setPlayerCollision
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @param    (boolean) enabled   Collision state.
* @return   (boolean)           True on success.
*
*/
bool Function_SetPlayerCollision(std::int64_t id, bool enabled) {
  if (auto* npc = GetNpcById(id)) {
    npc->SetCollDet(enabled ? 1 : 0);
    if (auto* anictrl = npc->GetAnictrl()) {
      anictrl->SetPhysicsEnabled(enabled ? 1 : 0);
    }
    return true;
  }

  return false;
}

/* luagmp (func)
*
* This function will return whether collision is enabled for a player or NPC.
*
* @version  0.3.0
* @name     getPlayerCollision
* @side     client
* @category Player
* @param    (number) player_id  Target player or client NPC id.
* @return   (boolean|nil)       Collision state, or nil if unavailable.
*
*/
sol::object Function_GetPlayerCollision(std::int64_t id, sol::this_state ts) {
  sol::state_view lua(ts);
  if (auto* npc = GetNpcById(id)) {
    return sol::make_object(lua, npc->collDetectionStatic != 0 || npc->collDetectionDynamic != 0);
  }
  return sol::nil;
}

/* luagmp (func)
*
* This function will set the in-game world time (hour, minute).
*
* @version  0.3.0
* @name     setTime
* @side     client
* @category Game
* @param    (number) hour    Hour component.
* @param    (number) minute  Minute component.
*
*/
bool Function_SetTime(int hour, int minute) {
  if (!ogame || !ogame->GetWorldTimer()) {
    return false;
  }

  ogame->GetWorldTimer()->SetTime(hour, minute);
  return true;
}


/* luagmp (func)
*
* This function will return the current in-game world time.
*
* @version  0.3.0
* @name     getTime
* @side     client
* @category Game
* @return   ({hour, minute})  Table containing hour and minute.
*
*/
sol::object Function_GetTime(sol::this_state ts) {
  sol::state_view lua(ts);

  int hour = 0;
  int minute = 0;

  if (ogame && ogame->GetWorldTimer()) {
    ogame->GetWorldTimer()->GetTime(hour, minute);
  }

  sol::table tbl = lua.create_table();
  tbl["hour"]   = hour;
  tbl["minute"] = minute;
  return sol::make_object(lua, tbl);
}

/* luagmp (func)
*
* This function will set the duration of an in-game day in milliseconds.
*
* @version  0.3.0
* @name     setDayLength
* @side     client
* @category Game
* @param    (number) miliseconds  Day length in milliseconds (min 10000 ms).
*
*/
bool Function_SetDayLength(float day_length_ms) {
  if (day_length_ms <= 0.0f) {
    return false;
  }

  constexpr float kMinDayLengthMs = 10000.0f;
  if (day_length_ms < kMinDayLengthMs) {
    SPDLOG_WARN("setDayLength called with invalid value {} ms, clamping to {} ms", day_length_ms, kMinDayLengthMs);
    day_length_ms = kMinDayLengthMs;
  }

  NetGame::Instance().SetDayLengthMs(day_length_ms);
  return true;
}

/* luagmp (func)
*
* This function will return the current configured in-game day length in milliseconds.
*
* @version  0.3.0
* @name     getDayLength
* @side     client
* @category Game
* @return   (number)  Day length in milliseconds.
*
*/
sol::object Function_GetDayLength(sol::this_state ts) {
  sol::state_view lua(ts);
  return sol::make_object(lua, NetGame::Instance().GetDayLengthMs());
}

/* luagmp (func)
*
* This function will return the current game resolution.
*
* @version  0.3.0
* @name     getResolution
* @side     client
* @category Game
* @return   ({x, y})  Table containing width and height.
*
*/
sol::object Function_GetResolution(sol::this_state ts) {
  sol::state_view lua(ts);
  sol::table resolution = lua.create_table();

  int width = 800;
  int height = 600;
  if (zrenderer) {
    width = zrenderer->vid_xdim;
    height = zrenderer->vid_ydim;
  }

  resolution["x"] = width;
  resolution["y"] = height;
  return resolution;
}

/* luagmp (func)
*
* This function will return the current frame rate estimate.
*
* @version  0.3.0
* @name     getFpsRate
* @side     client
* @category Game
* @return   (number)  Frames per second, or 0 if unavailable.
*
*/
std::int32_t Function_GetFpsRate() {
  if (!ztimer || ztimer->frameTimeFloatSecs <= 0.0f) {
    return 0;
  }

  return static_cast<std::int32_t>(std::lround(1.0f / ztimer->frameTimeFloatSecs));
}

/* luagmp (func)
*
* This function will return client network statistics.
*
* @version  0.3.0
* @name     getNetworkStats
* @side     client
* @category Game
* @note     Packet loss fields are ratios from 0.0 to 1.0.
* @note     Send buffer fields aggregate low, medium, and high priority packets.
* @return   ({packetReceived, packetlossTotal, packetlossLastSecond, messagesInResendBuffer, messageInSendBuffer, bytesInResendBuffer, bytesInSendBuffer})  Table containing current network statistics.
*
*/
sol::object Function_GetNetworkStats(sol::this_state ts) {
  sol::state_view lua(ts);

  Net::NetworkStats stats;
  auto& net_game = NetGame::Instance();
  if (net_game.game_client) {
    stats = net_game.game_client->GetNetworkStats();
  }

  sol::table tbl = lua.create_table();
  tbl["packetReceived"] = stats.packetReceived;
  tbl["packetlossTotal"] = stats.packetlossTotal;
  tbl["packetlossLastSecond"] = stats.packetlossLastSecond;
  tbl["messagesInResendBuffer"] = stats.messagesInResendBuffer;
  tbl["messageInSendBuffer"] = stats.messageInSendBuffer;
  tbl["bytesInResendBuffer"] = stats.bytesInResendBuffer;
  tbl["bytesInSendBuffer"] = stats.bytesInSendBuffer;

  return sol::make_object(lua, tbl);
}

/* luagmp (func)
*
* This function will close the game immediately.
*
* @version  0.3.0
* @name     exitGame
* @side     client
* @category Game
*
*/
void Function_ExitGame() {
  GMPCore::ExitGame(0);
}

/* luagmp (func)
*
* This function will clear multiplayer status messages shown while joining the server.
*
* @version  0.3.0
* @name     clearMultiplayerMessages
* @side     client
* @category Game
*
*/
void Function_ClearMultiplayerMessages() {
  NetGame::Instance().ClearMultiplayerMessages();
}

/* luagmp (func)
*
* This function will enable/disable opening GMP menu with ESC.
*
* @version  0.3.0
* @name     enableGMPMenu
* @side     client
* @category Game
* @param    (boolean) enable  True to allow ESC to open/close GMP menu, false to ignore ESC for GMP menu.
*
*/
void Function_EnableGMPMenu(bool enable) {
  EnableGMPMenu(enable);
}

/* luagmp (func)
*
* This function will force open GMP menu.
*
* @version  0.3.0
* @name     openGMPMenu
* @side     client
* @category Game
*
*/
void Function_OpenGMPMenu() {
  OpenGMPMenu();
}

/* luagmp (func)
*
* This function will force close GMP menu.
*
* @version  0.3.0
* @name     closeGMPMenu
* @side     client
* @category Game
*
*/
void Function_CloseGMPMenu() {
  CloseGMPMenu();
}

void BindGothicSpecific(sol::state& lua) {
  SPDLOG_TRACE("Initializing Gothic 2 Addon 2.6 specific bindings...");

  lua["setPlayerInstance"] = Function_SetPlayerInstance;
  lua["getPlayerInstance"] = Function_GetPlayerInstance;
  lua["setPlayerName"] = Function_SetPlayerName;
  lua["getPlayerName"] = Function_GetPlayerName;
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
  lua["setLearnPoints"] = Function_SetLearnPoints;
  lua["getLearnPoints"] = Function_GetLearnPoints;
  lua["setExp"] = Function_SetExp;
  lua["getExp"] = Function_GetExp;
  lua["setPlayerFatness"] = Function_SetPlayerFatness;
  lua["getPlayerFatness"] = Function_GetPlayerFatness;
  lua["setPlayerVisual"] = Function_SetPlayerVisual;
  lua["getPlayerVisual"] = Function_GetPlayerVisual;
  lua["setPlayerScale"] = Function_SetPlayerScale;
  lua["getPlayerScale"] = Function_GetPlayerScale;
  lua["setPlayerWeaponMode"] = Function_SetPlayerWeaponMode;
  lua["getPlayerWeaponMode"] = Function_GetPlayerWeaponMode;
  lua["getPlayerAni"] = Function_GetPlayerAni;
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
  lua["clearInventory"] = Function_ClearInventory;
  lua["openInventory"] = Function_OpenInventory;
  lua["closeInventory"] = Function_CloseInventory;
  lua["isInventoryOpen"] = Function_IsInventoryOpen;

  lua["changeWorld"] = Function_ChangeWorld;
  lua["getWorld"] = Function_GetWorld;

  lua["createNpc"] = Function_CreateNpc;
  lua["destroyNpc"] = Function_DestroyNpc;
  lua["spawnNpc"] = Function_SpawnNpc;
  lua["unspawnNpc"] = Function_UnspawnNpc;

  lua["isPlayerCreated"] = Function_IsPlayerCreated;
  lua["isPlayerStreamed"] = Function_IsPlayerStreamed;
  lua["isPlayerDead"] = Function_IsPlayerDead;
  lua["isPlayerUnconscious"] = Function_IsPlayerUnconscious;
  lua["getPlayerPing"] = Function_GetPlayerPing;
  lua["getPlayerFocus"] = Function_GetPlayerFocus;
  lua["getPlayerType"] = Function_GetPlayerType;
  lua["setPlayerOnFloor"] = Function_SetPlayerOnFloor;
  lua["setPlayerCollision"] = Function_SetPlayerCollision;
  lua["getPlayerCollision"] = Function_GetPlayerCollision;

  lua["setTime"] = Function_SetTime;
  lua["getTime"] = Function_GetTime;
  lua["setDayLength"] = Function_SetDayLength;
  lua["getDayLength"] = Function_GetDayLength;

  lua["getResolution"] = Function_GetResolution;
  lua["getFpsRate"] = Function_GetFpsRate;
  lua["getNetworkStats"] = Function_GetNetworkStats;

  lua["exitGame"] = Function_ExitGame;
  lua["clearMultiplayerMessages"] = Function_ClearMultiplayerMessages;
  lua["enableGMPMenu"] = Function_EnableGMPMenu;
  lua["openGMPMenu"] = Function_OpenGMPMenu;
  lua["closeGMPMenu"] = Function_CloseGMPMenu;

  BindInputConstants(lua);
  BindCursor(lua);
  BindChat(lua);
  BindDiscord(lua);
  BindDraw(lua);
  BindDraw3d(lua);
  BindTexture(lua);
  BindSound(lua);
  BindSound3d(lua);
  gmp::lua::BindMusic(lua);
  BindMusicTheme(lua);
  BindVob(lua);
  BindCamera(lua);
  BindWay(lua);
  BindSky(lua);
  BindInterface(lua);
  BindItemGround(lua);

  // Constants
  lua["PLANET_SUN"] = 0;
  lua["PLANET_MOON"] = 1;
}

void CleanupGothicViews() {
  LuaDraw::CleanupViews();
  LuaDraw3d::CleanupViews();
  LuaTexture::CleanupViews();
  LuaCursor::CleanupViews();
}

}  // namespace gmp::gothic


/* luagmp (const)
*
* Represents planet sun type.
*
* @category Sky
* @side     client
* @name     PLANET_SUN
*
*/

/* luagmp (const)
*
* Represents planet moon type.
*
* @category Sky
* @side     client
* @name     PLANET_MOON
*
*/
