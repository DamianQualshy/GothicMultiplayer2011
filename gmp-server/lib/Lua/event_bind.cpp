
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

#include "event_bind.h"

#include <spdlog/spdlog.h>

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game_server.h"
#include "../server_events.h"
#include "../resource_manager.h"
#include "lua.h"
#include "shared/event.h"
#include "shared/lua_runtime/lua_value_codec.h"

namespace lua {
namespace bindings {

namespace {

struct LuaProxyArgs {
  std::any event;
  sol::protected_function callback;
};
static std::map<std::string, std::function<void(LuaProxyArgs)>> kLuaEventProxies;

struct HandlerRegistration {
  const void* identity = nullptr;
  EventManager::EventHandlerId id = 0;
  std::string owner_name;
};

static std::unordered_map<std::string, std::vector<HandlerRegistration>> kHandlerRegistrations;
static std::unordered_set<std::string> kCustomEvents;
static std::unordered_set<std::string> kRemoteEvents;

const void* GetFunctionIdentity(const sol::protected_function& function) {
  lua_State* state = function.lua_state();
  sol::stack::push(state, function);
  const void* identity = lua_topointer(state, -1);
  lua_pop(state, 1);
  return identity;
}

void RegisterProxies() {

/* luagmp (event)
*
* This event is triggered in every server main loop iteration.
*
* @version  0.3.0
* @name     onTick
* @side     server
* @category Game
*
*/
  kLuaEventProxies[kEventOnTickName] = {[](LuaProxyArgs args) {
    std::any_cast<OnTickEvent>(args.event);
    args.callback();
  }};

/* luagmp (event)
*
* This event is triggered every time the server clock updates.
*
* @version  0.3.0
* @name     onClockUpdate
* @side     server
* @category Game
* @param    (number) day               The current ingame day.
* @param    (number) hour              The current ingame hour.
* @param    (number) min               The current ingame minute.
*
*/
  kLuaEventProxies[kEventOnClockUpdateName] = {[](LuaProxyArgs args) {
    OnClockUpdateEvent gametime_event = std::any_cast<OnClockUpdateEvent>(args.event);
    args.callback(gametime_event.day, gametime_event.hour, gametime_event.min);
  }};

/* luagmp (event)
*
* Triggered when a player connects to the server.
*
* @version  0.3.0
* @name     onPlayerConnect
* @side     server
* @category Player
* @param    (number) player_id    The id of the player that connected.
*
*/
  kLuaEventProxies[kEventOnPlayerConnectName] = {[](LuaProxyArgs args) {
    std::uint32_t player_id = std::any_cast<std::uint32_t>(args.event);
    args.callback(player_id);
  }};

/* luagmp (event)
*
* Triggered when a player disconnects from the server.
*
* @version  0.3.0
* @name     onPlayerDisconnect
* @side     server
* @category Player
* @param    (number) player_id    The id of the player that disconnected.
* @param    (number) reason       The reason why player got disconnected. See Network constants.
*
*/
  kLuaEventProxies[kEventOnPlayerDisconnectName] = {[](LuaProxyArgs args) {
    OnPlayerDisconnectEvent player_disconnect_event = std::any_cast<OnPlayerDisconnectEvent>(args.event);
    args.callback(player_disconnect_event.player_id, player_disconnect_event.reason);
  }};

/* luagmp (event)
*
* Triggered when a player sends a chat message.
*
* @version  0.3.0
* @name     onPlayerMessage
* @side     server
* @category Player
* @param    (number) player_id    The id of the player who sent the message.
* @param    (string) text      The message text.
*
*/
  kLuaEventProxies[kEventOnPlayerMessageName] = {[](LuaProxyArgs args) {
    OnPlayerMessageEvent player_message_event = std::any_cast<OnPlayerMessageEvent>(args.event);
    args.callback(player_message_event.pid, player_message_event.text);
  }};

/* luagmp (event)
*
* Triggered when a player issues a command.
*
* @version  0.3.0
* @name     onPlayerCommand
* @side     server
* @category Player
* @param    (number) player_id    The id of the player issuing the command.
* @param    (string) command   The command name.
* @param    ({...}) params     Command parameters.
*
*/
  kLuaEventProxies[kEventOnPlayerCommandName] = {[](LuaProxyArgs args) {
    OnPlayerCommandEvent player_command_event = std::any_cast<OnPlayerCommandEvent>(args.event);
    args.callback(player_command_event.pid, player_command_event.command, player_command_event.params);
  }};

/* luagmp (event)
*
* Triggered when a player dies.
*
* @version  0.3.0
* @name     onPlayerDeath
* @side     server
* @category Player
* @param    (number) player_id    The id of the player who died.
* @param    (number) killer_id   Optional id of the killer (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerDeathName] = {[](LuaProxyArgs args) {
    OnPlayerDeathEvent player_death_event = std::any_cast<OnPlayerDeathEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object killer = player_death_event.killer_id.has_value() ? sol::make_object(lua, player_death_event.killer_id.value()) : sol::lua_nil;
    args.callback(player_death_event.player_id, killer);
  }};

/* luagmp (event)
*
* Triggered when a player stands up after being unconscious.
*
* @version  0.3.0
* @name     onPlayerStandUp
* @side     server
* @category Player
* @param    (number) player_id    The id of the player who stood up.
*
*/
  kLuaEventProxies[kEventOnPlayerStandUpName] = {[](LuaProxyArgs args) {
    OnPlayerStandUpEvent player_standup_event = std::any_cast<OnPlayerStandUpEvent>(args.event);
    args.callback(player_standup_event.player_id);
  }};

/* luagmp (event)
*
* Triggered when a player becomes unconscious.
*
* @version  0.3.0
* @name     onPlayerUnconscious
* @side     server
* @category Player
* @param    (number) attacker_id  Optional attacker id (nil if none).
* @param    (number) victim_id     Victim player id.
*
*/
  kLuaEventProxies[kEventOnPlayerUnconsciousName] = {[](LuaProxyArgs args) {
    OnPlayerUnconsciousEvent player_unconscious_event = std::any_cast<OnPlayerUnconsciousEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object attacker =
        player_unconscious_event.attacker_id.has_value() ? sol::make_object(lua, player_unconscious_event.attacker_id.value()) : sol::lua_nil;
    args.callback(attacker, player_unconscious_event.victim_id);
  }};

/* luagmp (event)
*
* Triggered when a player drops an item.
*
* @version  0.3.0
* @name     onPlayerDropItem
* @side     server
* @category Player
* @param    (number) player_id        Player id who dropped the item.
* @param    (number) item_instance    Item instance id.
* @param    (number) amount           Amount dropped.
*
*/
  kLuaEventProxies[kEventOnPlayerDropItemName] = {[](LuaProxyArgs args) {
    OnPlayerDropItemEvent drop_item_event = std::any_cast<OnPlayerDropItemEvent>(args.event);
    args.callback(drop_item_event.pid, drop_item_event.item_instance, drop_item_event.amount);
  }};

/* luagmp (event)
*
* Triggered when a player picks up an item.
*
* @version  0.3.0
* @name     onPlayerTakeItem
* @side     server
* @category Player
* @param    (number) player_id        Player id who took the item.
* @param    (number) item_instance    Item instance id.
*
*/
  kLuaEventProxies[kEventOnPlayerTakeItemName] = {[](LuaProxyArgs args) {
    OnPlayerTakeItemEvent take_item_event = std::any_cast<OnPlayerTakeItemEvent>(args.event);
    args.callback(take_item_event.pid, take_item_event.item_instance);
  }};

/* luagmp (event)
*
* Triggered when a player casts a spell.
*
* @version  0.3.0
* @name     onPlayerCastSpell
* @side     server
* @category Player
* @param    (number) caster_id    Caster player id.
* @param    (number) spell_id     Spell identifier.
* @param    (number) target_id   Optional target player id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerCastSpellName] = {[](LuaProxyArgs args) {
    OnPlayerCastSpellEvent cast_spell_event = std::any_cast<OnPlayerCastSpellEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object target = cast_spell_event.target_id.has_value() ? sol::make_object(lua, cast_spell_event.target_id.value()) : sol::lua_nil;
    args.callback(cast_spell_event.caster_id, cast_spell_event.spell_id, target);
  }};

/* luagmp (event)
*
* Triggered when player health changes.
*
* @version  0.3.0
* @name     onPlayerChangeHealth
* @side     server
* @category Player
* @param    (number) player_id    The id of the player whose health points got changed.
* @param    (number) oldHP        The previous health points of the player.
* @param    (number) newHP        The new health points of the player.
*
*/
  kLuaEventProxies[kEventOnPlayerChangeHealthName] = {[](LuaProxyArgs args) {
    OnPlayerChangeHealthEvent health_event = std::any_cast<OnPlayerChangeHealthEvent>(args.event);
    args.callback(health_event.player_id, health_event.old_hp, health_event.new_hp);
  }};

/* luagmp (event)
*
* Triggered when player mana changes.
*
* @version  0.3.0
* @name     onPlayerChangeMana
* @side     server
* @category Player
* @param    (number) player_id    The id of the player whose mana points got changed.
* @param    (number) previous     The previous mana points of the player.
* @param    (number) current      The current mana points of the player.
*
*/
  kLuaEventProxies[kEventOnPlayerChangeManaName] = {[](LuaProxyArgs args) {
    OnPlayerChangeManaEvent mana_event = std::any_cast<OnPlayerChangeManaEvent>(args.event);
    args.callback(mana_event.player_id, mana_event.previous, mana_event.current);
  }};

/* luagmp (event)
*
* Triggered when player tries to change the played world (ZEN).
*
* @version  0.3.0
* @name     onPlayerChangeWorld
* @side     server
* @category Player
* @param    (number) player_id    The id of the player who tries to change the played world.
* @param    (string) world     The filename of the world.
* @param    (string) waypoint  The name of the waypoint that the player will be teleported to.
*
*/
  kLuaEventProxies[kEventOnPlayerChangeWorldName] = {[](LuaProxyArgs args) {
    OnPlayerChangeWorldEvent world_event = std::any_cast<OnPlayerChangeWorldEvent>(args.event);
    args.callback(world_event.player_id, world_event.world, world_event.waypoint);
  }};

/* luagmp (event)
*
* Triggered when a player's weapon mode changes.
*
* @version  0.3.0
* @name     onPlayerWeaponModeChange
* @side     server
* @category Player
* @param    (number) player_id  Player id.
* @param    (number) old_mode   Previous weapon mode.
* @param    (number) new_mode   New weapon mode.
*
*/
  kLuaEventProxies[kEventOnPlayerWeaponModeChangeName] = {[](LuaProxyArgs args) {
    OnPlayerWeaponModeChangeEvent weapon_mode_event = std::any_cast<OnPlayerWeaponModeChangeEvent>(args.event);
    args.callback(weapon_mode_event.player_id, weapon_mode_event.old_mode, weapon_mode_event.new_mode);
  }};

/* luagmp (event)
*
* Triggered when a player's amulet changes.
*
* @version  0.3.0
* @name     onPlayerAmuletChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New amulet instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerAmuletChangeName] = {[](LuaProxyArgs args) {
    OnPlayerAmuletChangeEvent amulet_event = std::any_cast<OnPlayerAmuletChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = amulet_event.instance_id.has_value() ? sol::make_object(lua, amulet_event.instance_id.value()) : sol::lua_nil;
    args.callback(amulet_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's armor changes.
*
* @version  0.3.0
* @name     onPlayerArmorChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New armor instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerArmorChangeName] = {[](LuaProxyArgs args) {
    OnPlayerArmorChangeEvent armor_event = std::any_cast<OnPlayerArmorChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = armor_event.instance_id.has_value() ? sol::make_object(lua, armor_event.instance_id.value()) : sol::lua_nil;
    args.callback(armor_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's belt changes.
*
* @version  0.3.0
* @name     onPlayerBeltChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New belt instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerBeltChangeName] = {[](LuaProxyArgs args) {
    OnPlayerBeltChangeEvent belt_event = std::any_cast<OnPlayerBeltChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = belt_event.instance_id.has_value() ? sol::make_object(lua, belt_event.instance_id.value()) : sol::lua_nil;
    args.callback(belt_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's hand item changes.
*
* @version  0.3.0
* @name     onPlayerHandItemChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number) hand           Hand id (0 = left, 1 = right).
* @param    (number|nil) instance   New hand item instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerHandItemChangeName] = {[](LuaProxyArgs args) {
    OnPlayerHandItemChangeEvent hand_item_event = std::any_cast<OnPlayerHandItemChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = hand_item_event.instance_id.has_value() ? sol::make_object(lua, hand_item_event.instance_id.value()) : sol::lua_nil;
    args.callback(hand_item_event.player_id, hand_item_event.hand, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's helmet changes.
*
* @version  0.3.0
* @name     onPlayerHelmetChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New helmet instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerHelmetChangeName] = {[](LuaProxyArgs args) {
    OnPlayerHelmetChangeEvent helmet_event = std::any_cast<OnPlayerHelmetChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = helmet_event.instance_id.has_value() ? sol::make_object(lua, helmet_event.instance_id.value()) : sol::lua_nil;
    args.callback(helmet_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's melee weapon changes.
*
* @version  0.3.0
* @name     onPlayerMeleeWeaponChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New melee weapon instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerMeleeWeaponChangeName] = {[](LuaProxyArgs args) {
    OnPlayerMeleeWeaponChangeEvent melee_weapon_event = std::any_cast<OnPlayerMeleeWeaponChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = melee_weapon_event.instance_id.has_value() ? sol::make_object(lua, melee_weapon_event.instance_id.value()) : sol::lua_nil;
    args.callback(melee_weapon_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's ranged weapon changes.
*
* @version  0.3.0
* @name     onPlayerRangedWeaponChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New ranged weapon instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerRangedWeaponChangeName] = {[](LuaProxyArgs args) {
    OnPlayerRangedWeaponChangeEvent ranged_weapon_event = std::any_cast<OnPlayerRangedWeaponChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = ranged_weapon_event.instance_id.has_value() ? sol::make_object(lua, ranged_weapon_event.instance_id.value()) : sol::lua_nil;
    args.callback(ranged_weapon_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's ring changes.
*
* @version  0.3.0
* @name     onPlayerRingChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number) hand_id        Hand id (0 = left, 1 = right).
* @param    (number|nil) instance   New ring instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerRingChangeName] = {[](LuaProxyArgs args) {
    OnPlayerRingChangeEvent ring_event = std::any_cast<OnPlayerRingChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = ring_event.instance_id.has_value() ? sol::make_object(lua, ring_event.instance_id.value()) : sol::lua_nil;
    args.callback(ring_event.player_id, ring_event.hand_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's shield changes.
*
* @version  0.3.0
* @name     onPlayerShieldChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number|nil) instance   New shield instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerShieldChangeName] = {[](LuaProxyArgs args) {
    OnPlayerShieldChangeEvent shield_event = std::any_cast<OnPlayerShieldChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = shield_event.instance_id.has_value() ? sol::make_object(lua, shield_event.instance_id.value()) : sol::lua_nil;
    args.callback(shield_event.player_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player's active spell slot changes.
*
* @version  0.3.0
* @name     onPlayerSpellSlotChange
* @side     server
* @category Player
* @param    (number) player_id      Player id.
* @param    (number) slot_id        Active spell slot id.
* @param    (number|nil) instance   Spell instance id (nil if none).
*
*/
  kLuaEventProxies[kEventOnPlayerSpellSlotChangeName] = {[](LuaProxyArgs args) {
    OnPlayerSpellSlotChangeEvent spell_slot_event = std::any_cast<OnPlayerSpellSlotChangeEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object instance = spell_slot_event.instance_id.has_value() ? sol::make_object(lua, spell_slot_event.instance_id.value()) : sol::lua_nil;
    args.callback(spell_slot_event.player_id, spell_slot_event.slot_id, instance);
  }};

/* luagmp (event)
*
* Triggered when a player spawns (initial spawn).
*
* @version  0.3.0
* @name     onPlayerSpawn
* @side     server
* @category Player
* @param    (number) player_id    Player id spawned.
* @param    (number) x         X coordinate of spawn.
* @param    (number) y         Y coordinate of spawn.
* @param    (number) z         Z coordinate of spawn.
*
*/
  kLuaEventProxies[kEventOnPlayerSpawnName] = {[](LuaProxyArgs args) {
    OnPlayerSpawnEvent player_spawn_event = std::any_cast<OnPlayerSpawnEvent>(args.event);
    args.callback(player_spawn_event.player_id, player_spawn_event.position.x, player_spawn_event.position.y, player_spawn_event.position.z);
  }};

/* luagmp (event)
*
* Triggered when a player respawns.
*
* @version  0.3.0
* @name     onPlayerRespawn
* @side     server
* @category Player
* @param    (number) player_id    Player id respawned.
* @param    (number) x         X coordinate of respawn.
* @param    (number) y         Y coordinate of respawn.
* @param    (number) z         Z coordinate of respawn.
*
*/
  kLuaEventProxies[kEventOnPlayerRespawnName] = {[](LuaProxyArgs args) {
    OnPlayerRespawnEvent player_respawn_event = std::any_cast<OnPlayerRespawnEvent>(args.event);
    args.callback(player_respawn_event.player_id, player_respawn_event.position.x, player_respawn_event.position.y, player_respawn_event.position.z);
  }};

/* luagmp (event)
*
* Triggered when a player is spawned for another player (streaming in).
*
* @version  0.3.0
* @name     onPlayerSpawnFor
* @side     server
* @category Player
* @param    (number) player_id    Player id receiving the spawn.
* @param    (number) spawn_id     Player id spawned for the receiver.
*
*/
  kLuaEventProxies[kEventOnPlayerSpawnForName] = {[](LuaProxyArgs args) {
    OnPlayerSpawnForEvent player_spawn_for_event = std::any_cast<OnPlayerSpawnForEvent>(args.event);
    args.callback(player_spawn_for_event.player_id, player_spawn_for_event.spawn_id);
  }};

/* luagmp (event)
*
* Triggered when a player is unspawned for another player (streaming out).
*
* @version  0.3.0
* @name     onPlayerUnspawnFor
* @side     server
* @category Player
* @param    (number) player_id    Player id losing the spawn.
* @param    (number) spawn_id     Player id removed for the receiver.
*
*/
  kLuaEventProxies[kEventOnPlayerUnspawnForName] = {[](LuaProxyArgs args) {
    OnPlayerUnspawnForEvent player_unspawn_for_event = std::any_cast<OnPlayerUnspawnForEvent>(args.event);
    args.callback(player_unspawn_for_event.player_id, player_unspawn_for_event.spawn_id);
  }};

/* luagmp (event)
*
* Triggered when a player is hit.
*
* @version  0.3.0
* @name     onPlayerHit
* @side     server
* @category Player
* @param    (number) attacker_id  Optional attacker id (nil if none).
* @param    (number) victim_id     Victim player id.
* @param    (number) damage        Damage dealt.
*
*/
  kLuaEventProxies[kEventOnPlayerHitName] = {[](LuaProxyArgs args) {
    OnPlayerHitEvent player_hit_event = std::any_cast<OnPlayerHitEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object attacker = player_hit_event.attacker_id.has_value() ? sol::make_object(lua, player_hit_event.attacker_id.value()) : sol::lua_nil;
    args.callback(attacker, player_hit_event.victim_id, player_hit_event.damage);
  }};
}

std::optional<std::function<void(LuaProxyArgs)>> GetProxy(std::string event_name) {
  auto it = kLuaEventProxies.find(event_name);
  if (it != kLuaEventProxies.end()) {
    return it->second;
  }

  if (kCustomEvents.find(event_name) != kCustomEvents.end()) {
    return {[](LuaProxyArgs args) {
      LuaCustomEvent custom_event = std::any_cast<LuaCustomEvent>(args.event);
      sol::state_view lua(args.callback.lua_state());
      sol::object previous_source = lua["source"];
      sol::object source_object = sol::make_object(lua, sol::lua_nil);
      if (custom_event.source_element.has_value()) {
        source_object = sol::make_object(lua, static_cast<int>(*custom_event.source_element));
        lua["source"] = source_object;
      } else {
        lua["source"] = sol::lua_nil;
      }
      std::vector<sol::object> callback_args;
      callback_args.reserve(custom_event.args.size() + 1);
      callback_args.emplace_back(std::move(source_object));
      callback_args.insert(callback_args.end(), custom_event.args.begin(), custom_event.args.end());
      args.callback(sol::as_args(callback_args));
      lua["source"] = previous_source;
    }};
  }

  return std::nullopt;
}

}  // namespace

void BindEvents(sol::state& lua) {
  RegisterProxies();

  lua["addEventHandler"] = [&lua](std::string event_name, sol::protected_function lua_callback,
                                  sol::optional<int> priority_opt) -> bool {
    SPDLOG_TRACE("addEventHandler({})", event_name);

    auto proxy = GetProxy(event_name);
    if (!proxy) {
      SPDLOG_ERROR("addEventHandler: event with name {} doesn't exist!", event_name);
      return false;
    }

    auto* resource_manager = ResourceManager::GetActiveInstance();
    if (resource_manager == nullptr) {
      SPDLOG_ERROR("addEventHandler called before ResourceManager initialization for event '{}'", event_name);
      return false;
    }

    Resource* owner = ResourceManager::GetCurrentResource();
    if (owner == nullptr) {
      SPDLOG_ERROR("addEventHandler: no active resource context when subscribing to event '{}'", event_name);
      return false;
    }

    std::string owner_name = owner->GetName();
    int priority = priority_opt.value_or(9999);
    const void* identity = GetFunctionIdentity(lua_callback);

    auto callback = [proxy, lua_callback, owner_name, event_name](std::any event) {
      auto* manager = ResourceManager::GetActiveInstance();
      if (manager == nullptr) {
        SPDLOG_ERROR("Cannot dispatch event '{}' because ResourceManager is unavailable", event_name);
        return;
      }

      auto resource_opt = manager->GetResource(owner_name);
      if (!resource_opt || !resource_opt->get().IsLoaded()) {
        SPDLOG_DEBUG("Skipping event '{}' for resource '{}' because it is not loaded", event_name, owner_name);
        return;
      }

      ResourceManager::ScopedResourceContext ctx(resource_opt->get());
      LuaProxyArgs args;
      args.event = event;
      args.callback = lua_callback;
      (*proxy)(args);
    };
    

    auto handler_id = EventManager::Instance().SubscribeToEventWithPriority(event_name, callback, priority);
    if (!handler_id) {
      return false;
    }

    kHandlerRegistrations[event_name].push_back(HandlerRegistration{identity, *handler_id, owner_name});
    return true;
  };

  lua["addEvent"] = [](std::string event_name, sol::optional<bool> allow_remote_trigger) -> bool {
    SPDLOG_TRACE("addEvent({})", event_name);
    if (!EventManager::Instance().RegisterEvent(event_name)) {
      return false;
    }

    kCustomEvents.insert(event_name);
    if (allow_remote_trigger.value_or(false)) {
      kRemoteEvents.insert(event_name);
    } else {
      kRemoteEvents.erase(event_name);
    }
    return true;
  };
/* luagmp (func)
*
* Triggers a custom client-side event for one or more players and optionally passes arguments.
*
* @version  0.3.0
* @name     triggerClientEvent
* @side     server
* @category Network
* @note     If sendTo is omitted or `nil`, the event is sent to all connected players. If sendTo is a number, it is treated as a single player id. If sendTo is a table, it must contain player ids.
* @note     You may optionally provide a numeric source element id after the event name, followed by any number of additional arguments to send with the event. 
* @param    (number|{...}|nil) sendTo Target player id, table of player ids, or nil to send to all players.
* @param    (string) eventName Name of the client-side event to trigger.
* @param    (number|nil) sourceElement Optional source element id. Use nil if not needed.
* @param    (...) ... Optional arguments passed to the client event handler.
* @return   (boolean) True if the event was sent successfully, otherwise false.
*
*/
  lua["triggerClientEvent"] = [&lua](sol::variadic_args args) -> bool {
    SPDLOG_TRACE("triggerClientEvent");

    std::optional<sol::object> send_to;
    std::size_t index = 0;
    sol::object first = args[0];
    sol::object event_name_obj;

    if (first.get_type() == sol::type::string) {
      event_name_obj = first;
      index = 1;
    } else if (first.get_type() == sol::type::nil) {
      send_to = first;
      if (args.size() < 3) {
        SPDLOG_ERROR("triggerClientEvent missing event name");
        return false;
      }
      event_name_obj = args[1];
      index = 2;
    } else {
      send_to = first;
      if (args.size() < 3) {
        SPDLOG_ERROR("triggerClientEvent missing event name");
        return false;
      }
      event_name_obj = args[1];
      index = 2;
    }

    if (event_name_obj.get_type() != sol::type::string) {
      SPDLOG_ERROR("triggerClientEvent expected event name string");
      return false;
    }
    std::string event_name = event_name_obj.as<std::string>();

    std::uint32_t source_element = 0;
    if (index < args.size()) {
      sol::object source_obj = args[index];
      auto remaining = args.size() - index;
      if (source_obj.get_type() == sol::type::number && remaining >= 1) {
        source_element = source_obj.as<std::uint32_t>();
        index++;
      } else if (source_obj.get_type() == sol::type::nil) {
        index++;
      }
    }

    std::vector<sol::object> event_args;
    for (std::size_t i = index; i < args.size(); ++i) {
      event_args.emplace_back(sol::make_object(lua, args[i]));
    }

    std::string payload;
    std::string error;
    if (!gmp::lua::EncodeLuaArgs(lua, event_args, payload, error)) {
      SPDLOG_ERROR("triggerClientEvent failed to encode payload for '{}': {}", event_name, error);
      return false;
    }

    std::vector<GameServer::PlayerId> targets;
    if (!send_to.has_value() || send_to->get_type() == sol::type::nil) {
      const auto& players = g_server->GetPlayerManager().GetAllPlayers();
      targets.reserve(players.size());
      for (const auto& entry : players) {
        targets.push_back(entry.first);
      }
    } else if (send_to->get_type() == sol::type::number) {
      targets.push_back(send_to->as<std::uint32_t>());
    } else if (send_to->get_type() == sol::type::table) {
      sol::table tbl = send_to->as<sol::table>();
      for (const auto& kv : tbl) {
        sol::object value = kv.second;
        if (value.get_type() != sol::type::number) {
          SPDLOG_ERROR("triggerClientEvent sendTo table must contain player ids");
          return false;
        }
        targets.push_back(value.as<std::uint32_t>());
      }
    } else {
      SPDLOG_ERROR("triggerClientEvent sendTo must be a player id or table of player ids");
      return false;
    }

    if (targets.empty()) {
      SPDLOG_WARN("triggerClientEvent did not target any players");
      return false;
    }

    return g_server->TriggerClientEvent(targets, event_name, source_element, payload);
  };

  lua["callEvent"] = [](std::string event_name, sol::variadic_args args) -> bool {
    SPDLOG_TRACE("callEvent({})", event_name);
    if (kCustomEvents.find(event_name) == kCustomEvents.end()) {
      SPDLOG_ERROR("callEvent: event with name {} doesn't exist!", event_name);
      return false;
    }

    LuaCustomEvent event;
    event.args.reserve(args.size());
    for (auto arg : args) {
      event.args.emplace_back(arg);
    }

    auto result = EventManager::Instance().DispatchEvent(event_name, event);
    return result.dispatched && !result.cancelled;
  };

  lua["cancelEvent"] = []() { EventManager::Instance().CancelCurrentEvent(); };

  lua["eventValue"] = [](int event_value) { EventManager::Instance().SetCurrentEventValue(event_value); };

  lua["isEventCancelled"] = []() -> bool { return EventManager::Instance().IsCurrentEventCancelled(); };

  lua["removeEvent"] = [](std::string event_name) {
    SPDLOG_TRACE("removeEvent({})", event_name);
    if (kCustomEvents.find(event_name) == kCustomEvents.end()) {
      SPDLOG_ERROR("removeEvent: event with name {} doesn't exist!", event_name);
      return;
    }

    EventManager::Instance().UnregisterEvent(event_name);
    kCustomEvents.erase(event_name);
    kRemoteEvents.erase(event_name);
    kHandlerRegistrations.erase(event_name);
  };

  lua["removeEventHandler"] = [](std::string event_name, sol::protected_function lua_callback) -> bool {
    SPDLOG_TRACE("removeEventHandler({})", event_name);

    auto* resource_manager = ResourceManager::GetActiveInstance();
    if (resource_manager == nullptr) {
      SPDLOG_ERROR("removeEventHandler called before ResourceManager initialization for event '{}'", event_name);
      return false;
    }

    Resource* owner = ResourceManager::GetCurrentResource();
    if (owner == nullptr) {
      SPDLOG_ERROR("removeEventHandler: no active resource context when unsubscribing from event '{}'", event_name);
      return false;
    }

    const void* identity = GetFunctionIdentity(lua_callback);
    auto it = kHandlerRegistrations.find(event_name);
    if (it == kHandlerRegistrations.end()) {
      return false;
    }

    auto& handlers = it->second;
    auto owner_name = owner->GetName();
    for (auto handler_it = handlers.begin(); handler_it != handlers.end(); ++handler_it) {
      if (handler_it->identity == identity && handler_it->owner_name == owner_name) {
        bool removed = EventManager::Instance().UnsubscribeFromEvent(event_name, handler_it->id);
        handlers.erase(handler_it);
        return removed;
      }
    }

    return false;
  };

  lua["toggleEvent"] = [](std::string event_name, bool toggle) {
    SPDLOG_TRACE("toggleEvent({}, {})", event_name, toggle);
    if (!EventManager::Instance().ToggleEvent(event_name, toggle)) {
      SPDLOG_ERROR("toggleEvent: event with name {} doesn't exist!", event_name);
    }
  };
}

  bool TriggerRemoteEvent(sol::state_view lua, const std::string& event_name, std::uint32_t source_element,
                          const std::vector<sol::object>& args) {
    (void)lua;
    if (kCustomEvents.find(event_name) == kCustomEvents.end()) {
      SPDLOG_ERROR("Remote event '{}' is not registered on the server", event_name);
      return false;
    }
    if (kRemoteEvents.find(event_name) == kRemoteEvents.end()) {
      SPDLOG_WARN("Remote event '{}' is not allowed for server triggering", event_name);
      return false;
    }

    LuaCustomEvent event;
    event.args = args;
    if (source_element != 0) {
      event.source_element = source_element;
    }

    auto result = EventManager::Instance().DispatchEvent(event_name, event);
    return result.dispatched && !result.cancelled;
  }
}  // namespace bindings
}  // namespace lua