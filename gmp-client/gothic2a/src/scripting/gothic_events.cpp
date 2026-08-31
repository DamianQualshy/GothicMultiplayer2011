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

#include "gothic_events.h"

#include <spdlog/spdlog.h>

#include <any>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "item_ground.h"
#include "net_game.h"
#include "shared/event.h"
#include "shared/lua_runtime/lua_diagnostics.h"
#include "shared/lua_runtime/lua_value_codec.h"

namespace gmp::gothic {

namespace {

struct LuaEventCallback {
  sol::protected_function callback;
  std::string event_name;

  LuaEventCallback() = default;

  LuaEventCallback(sol::protected_function callback, std::string event_name)
      : callback(std::move(callback)), event_name(std::move(event_name)) {}

  lua_State* lua_state() {
    return callback.lua_state();
  }

  template <typename... Args>
  sol::protected_function_result operator()(Args&&... callback_args) {
    sol::protected_function_result result = callback(std::forward<Args>(callback_args)...);
    if (!result.valid()) {
      sol::error error = result;
      ::lua::diagnostics::LogRuntimeError(error.what(), {"", "event handler", event_name});
    }
    return result;
  }
};

struct LuaProxyArgs {
  std::any event;
  LuaEventCallback callback;
};
std::map<std::string, std::function<void(LuaProxyArgs)>> g_gothic_event_proxies;

struct LuaCustomEvent {
  std::vector<sol::object> args;
  std::optional<std::uint32_t> source_element;
};

struct HandlerRegistration {
  const void* identity = nullptr;
  EventManager::EventHandlerId id = 0;
};

std::unordered_map<std::string, std::vector<HandlerRegistration>> g_handler_registrations;
std::unordered_set<std::string> g_custom_events;
std::unordered_set<std::string> g_remote_events;

const void* GetFunctionIdentity(const sol::protected_function& function) {
  lua_State* state = function.lua_state();
  sol::stack::push(state, function);
  const void* identity = lua_topointer(state, -1);
  lua_pop(state, 1);
  return identity;
}

void RegisterGothicEventProxies() {
/* luagmp (event)
*
* This event is triggered once when the player connects to the server.
*
* @version  0.3.0
* @name     onInit
* @side     client
* @category Game
*
*/
  g_gothic_event_proxies[kEventOnInitName] = [](LuaProxyArgs args) { args.callback(); };

/* luagmp (event)
*
* This event is triggered when the player disconnects from the server.
*
* @version  0.3.0
* @name     onExit
* @side     client
* @category Game
*
*/
  g_gothic_event_proxies[kEventOnExitName] = [](LuaProxyArgs args) { args.callback(); };

/* luagmp (event)
*
* This event is triggered each frame.
*
* @version  0.3.0
* @name     onRender
* @side     client
* @category Game
*
*/
  g_gothic_event_proxies[kEventOnRenderName] = [](LuaProxyArgs args) {
    // onRender has no arguments for now
    args.callback();
  };

/* luagmp (event)
*
* This event is triggered each time the game time minute passes.
*
* @version  0.3.0
* @name     onTime
* @side     client
* @category Game
* @param    (number) day   The current in-game day.
* @param    (number) hour  The current in-game hour.
* @param    (number) min   The current in-game minute.
*
*/
  g_gothic_event_proxies[kEventOnTimeName] = [](LuaProxyArgs args) {
    OnTimeEvent event = std::any_cast<OnTimeEvent>(args.event);
    args.callback(event.day, event.hour, event.min);
  };

/* luagmp (event)
*
* This event is triggered when weather changes.
*
* @version  0.3.0
* @name     onWeatherChange
* @side     client
* @category Weather
* @param    (number) old_weather_type  Previous weather type. For more information, see [Weather Constants](../../shared-constants/Weather.md).
* @param    (number) new_weather_type  New weather type from the same table.
*
*/
  g_gothic_event_proxies[kEventOnWeatherChangeName] = [](LuaProxyArgs args) {
    OnWeatherChangeEvent event = std::any_cast<OnWeatherChangeEvent>(args.event);
    args.callback(event.old_weather_type, event.new_weather_type);
  };

/* luagmp (event)
*
* This event is triggered when a key is pressed.
*
* @version  0.3.0
* @name     onKeyDown
* @side     client
* @category Input
* @param    (number) key    The key code pressed. For more information, see [Key Constants](../../client-constants/Key.md).
*
*/
  g_gothic_event_proxies[kEventOnKeyDownName] = [](LuaProxyArgs args) {
    OnKeyEvent event = std::any_cast<OnKeyEvent>(args.event);
    args.callback(event.key);
  };

/* luagmp (event)
*
* This event is triggered when a key is released.
*
* @version  0.3.0
* @name     onKeyUp
* @side     client
* @category Input
* @param    (number) key    The key code released. For more information, see [Key Constants](../../client-constants/Key.md).
*
*/
  g_gothic_event_proxies[kEventOnKeyUpName] = [](LuaProxyArgs args) {
    OnKeyEvent event = std::any_cast<OnKeyEvent>(args.event);
    args.callback(event.key);
  };

/* luagmp (event)
*
* This event is triggered when the user presses Ctrl+V.
*
* @version  0.3.0
* @name     onPaste
* @side     client
* @category Input
* @param    (string) text  Current clipboard text.
*
*/
  g_gothic_event_proxies[kEventOnPasteName] = [](LuaProxyArgs args) {
    OnPasteEvent event = std::any_cast<OnPasteEvent>(args.event);
    args.callback(event.text);
  };

/* luagmp (event)
*
* This event is triggered when a slash command is submitted through chat input.
*
* @version  0.3.0
* @name     onCommand
* @side     client
* @category Input
* @param    (string) command  Command name without the leading slash.
* @param    (string) params   Raw command parameters after the command name.
*
*/
  g_gothic_event_proxies[kEventOnCommandName] = [](LuaProxyArgs args) {
    OnCommandEvent event = std::any_cast<OnCommandEvent>(args.event);
    args.callback(event.command, event.params);
  };

/* luagmp (event)
*
* This event is triggered when a mouse button is pressed.
*
* @version  0.3.0
* @name     onMouseDown
* @side     client
* @category Mouse
* @param    (number) button The mouse button pressed. For more information, see [Mouse Constants](../../client-constants/Mouse.md).
*
*/
  g_gothic_event_proxies[kEventOnMouseDownName] = [](LuaProxyArgs args) {
    OnMouseButtonEvent event = std::any_cast<OnMouseButtonEvent>(args.event);
    args.callback(event.button);
  };

/* luagmp (event)
*
* This event is triggered when a mouse button is released.
*
* @version  0.3.0
* @name     onMouseUp
* @side     client
* @category Mouse
* @param    (number) button The mouse button released. For more information, see [Mouse Constants](../../client-constants/Mouse.md).
*
*/
  g_gothic_event_proxies[kEventOnMouseUpName] = [](LuaProxyArgs args) {
    OnMouseButtonEvent event = std::any_cast<OnMouseButtonEvent>(args.event);
    args.callback(event.button);
  };

/* luagmp (event)
*
* This event is triggered when the mouse cursor is moved.
*
* @version  0.3.0
* @name     onMouseMove
* @side     client
* @category Mouse
* @param    (number) x Cursor X position.
* @param    (number) y Cursor Y position.
*
*/
  g_gothic_event_proxies[kEventOnMouseMoveName] = [](LuaProxyArgs args) {
    OnMouseMoveEvent event = std::any_cast<OnMouseMoveEvent>(args.event);
    args.callback(event.x, event.y);
  };

/* luagmp (event)
*
* This event is triggered when the mouse wheel is scrolled.
*
* @version  0.3.0
* @name     onMouseWheel
* @side     client
* @category Mouse
* @param    (number) z Mouse wheel delta.
*
*/
  g_gothic_event_proxies[kEventOnMouseWheelName] = [](LuaProxyArgs args) {
    OnMouseWheelEvent event = std::any_cast<OnMouseWheelEvent>(args.event);
    args.callback(event.z);
  };

/* luagmp (event)
*
* This event is triggered when the user opens the inventory.
*
* @version  0.3.0
* @name     onOpenInventory
* @side     client
* @category Inventory
*
*/
  g_gothic_event_proxies[kEventOnOpenInventoryName] = [](LuaProxyArgs args) { args.callback(); };

/* luagmp (event)
*
* This event is triggered when the user closes the inventory.
*
* @version  0.3.0
* @name     onCloseInventory
* @side     client
* @category Inventory
*
*/
  g_gothic_event_proxies[kEventOnCloseInventoryName] = [](LuaProxyArgs args) { args.callback(); };

/* luagmp (event)
*
* This event is triggered when the selected inventory slot changes.
*
* @version  0.3.0
* @name     onInventorySlotChange
* @side     client
* @category Inventory
* @param    (number) from  Previous slot number.
* @param    (number) to    Current slot number.
*
*/
  g_gothic_event_proxies[kEventOnInventorySlotChangeName] = [](LuaProxyArgs args) {
    OnInventorySlotChangeEvent event = std::any_cast<OnInventorySlotChangeEvent>(args.event);
    args.callback(event.from, event.to);
  };

/* luagmp (event)
*
* This event is triggered when the client requests a world change via oCTriggerChangeLevel vob.
*
* @version  0.3.0
* @name     onWorldChange
* @side     client
* @category World
* @param    (string) world     New world filename.
* @param    (string) waypoint  Waypoint name the player will be teleported to.
*
*/
  g_gothic_event_proxies[kEventOnWorldChangeName] = [](LuaProxyArgs args) {
    OnWorldChangeEvent event = std::any_cast<OnWorldChangeEvent>(args.event);
    args.callback(event.world, event.waypoint);
  };

/* luagmp (event)
*
* This event is triggered when the player enters a world.
*
* @version  0.3.0
* @name     onWorldEnter
* @side     client
* @category World
* @param    (string) world  World filename.
*
*/
  g_gothic_event_proxies[kEventOnWorldEnterName] = [](LuaProxyArgs args) {
    OnWorldEnterEvent event = std::any_cast<OnWorldEnterEvent>(args.event);
    args.callback(event.world);
  };

/* luagmp (event)
*
* This event is triggered when the hero equips an item.
*
* @version  0.3.0
* @name     onEquip
* @side     client
* @category Hero
* @param    (string) item  Item instance.
*
*/
  g_gothic_event_proxies[kEventOnEquipName] = [](LuaProxyArgs args) {
    OnItemEvent event = std::any_cast<OnItemEvent>(args.event);
    args.callback(event.item);
  };

/* luagmp (event)
*
* This event is triggered when the hero drops an item.
*
* @version  0.3.0
* @name     onDropItem
* @side     client
* @category Hero
* @param    (string) item    Item instance.
* @param    (number) amount  Item amount.
*
*/
  g_gothic_event_proxies[kEventOnDropItemName] = [](LuaProxyArgs args) {
    OnDropItemEvent event = std::any_cast<OnDropItemEvent>(args.event);
    args.callback(event.item, event.amount);
  };

/* luagmp (event)
*
* This event is triggered when the hero takes an item from the ground.
*
* @version  0.3.0
* @name     onTakeItem
* @side     client
* @category Hero
* @param    (string) item           Item instance.
* @param    (boolean) synchronized  True when pickup is synchronized with the server.
* @param    (number) amount         Item amount.
* @param    (number|nil) itemGroundId Ground item id, or nil for non-server items.
*
*/
  g_gothic_event_proxies[kEventOnTakeItemName] = [](LuaProxyArgs args) {
    OnTakeItemEvent event = std::any_cast<OnTakeItemEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object item_ground_id =
        event.item_ground_id.has_value() ? sol::make_object(lua, *event.item_ground_id) : sol::make_object(lua, sol::lua_nil);
    args.callback(event.item, event.synchronized, event.amount, item_ground_id);
  };

/* luagmp (event)
*
* This event is triggered when the hero uses, interacts with, opens, or consumes an item.
*
* @version  0.3.0
* @name     onUseItem
* @side     client
* @category Hero
* @param    (string) item     Item instance.
* @param    (string) scheme   Item scheme name, if available.
* @param    (number) from     Previous interact state.
* @param    (number) to       Current interact state.
*
*/
  g_gothic_event_proxies[kEventOnUseItemName] = [](LuaProxyArgs args) {
    OnUseItemEvent event = std::any_cast<OnUseItemEvent>(args.event);
    args.callback(event.item, event.scheme, event.from, event.to);
  };

/* luagmp (event)
*
* This event is triggered when a server-side ground item is created for the client.
*
* @version  0.3.0
* @name     onItemGroundCreate
* @side     client
* @category ItemGround
* @param    (ItemGround) itemGround  Ground item object.
*
*/
  g_gothic_event_proxies[kEventOnItemGroundCreateName] = [](LuaProxyArgs args) {
    LuaItemGround event = std::any_cast<LuaItemGround>(args.event);
    args.callback(event);
  };

/* luagmp (event)
*
* This event is triggered when a server-side ground item is destroyed for the client.
*
* @version  0.3.0
* @name     onItemGroundDestroy
* @side     client
* @category ItemGround
* @param    (ItemGround) itemGround  Ground item object.
*
*/
  g_gothic_event_proxies[kEventOnItemGroundDestroyName] = [](LuaProxyArgs args) {
    LuaItemGround event = std::any_cast<LuaItemGround>(args.event);
    args.callback(event);
  };

/* luagmp (event)
*
* This event is triggered when all server-side ground items are destroyed during world or virtual-world changes.
*
* @version  0.3.0
* @name     onItemsGroundDestroy
* @side     client
* @category ItemGround
*
*/
  g_gothic_event_proxies[kEventOnItemsGroundDestroyName] = [](LuaProxyArgs args) {
    args.callback();
  };

/* luagmp (event)
*
* This event is triggered when the hero unequips an item.
*
* @version  0.3.0
* @name     onUnequip
* @side     client
* @category Hero
* @param    (string) item  Item instance.
*
*/
  g_gothic_event_proxies[kEventOnUnequipName] = [](LuaProxyArgs args) {
    OnItemEvent event = std::any_cast<OnItemEvent>(args.event);
    args.callback(event.item);
  };

/* luagmp (event)
*
* This event is triggered when a player object is created locally.
*
* @version  0.3.0
* @name     onPlayerCreate
* @side     client
* @category Player
* @param    (number) player_id    The local player id.
*
*/
  g_gothic_event_proxies[kEventOnPlayerCreateName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* This event is triggered when a local player object is destroyed.
*
* @version  0.3.0
* @name     onPlayerDestroy
* @side     client
* @category Player
* @param    (number) player_id    The local player id.
*
*/
  g_gothic_event_proxies[kEventOnPlayerDestroyName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* This event is triggered when a chat message is received locally.
*
* @version  0.3.0
* @name     onPlayerMessage
* @side     client
* @category Player
* @param    (number|nil) sender_id Optional sender id (nil for system).
* @param    (number) r            The red color component in RGB model.
* @param    (number) g            The green color component in RGB model.
* @param    (number) b            The blue color component in RGB model.
* @param    (string) message      Message text.
*
*/
  g_gothic_event_proxies[kEventOnPlayerMessageName] = [](LuaProxyArgs args) {
    OnPlayerMessageEvent event = std::any_cast<OnPlayerMessageEvent>(args.event);
    sol::state_view lua(args.callback.lua_state());
    sol::object sender = event.sender_id.has_value() ? sol::make_object(lua, event.sender_id.value()) : sol::lua_nil;
    args.callback(sender, event.r, event.g, event.b, event.message);
  };

/* luagmp (event)
*
* This event is triggered when a player respawns after death.
*
* @version  0.3.0
* @name     onPlayerRespawn
* @side     client
* @category Player
* @param    (number) id  The id of the respawned player.
*
*/
  g_gothic_event_proxies[kEventOnPlayerRespawnName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* This event is triggered when a player is spawned into the world.
*
* @version  0.3.0
* @name     onPlayerSpawn
* @side     client
* @category Player
* @param    (number) id  The id of the spawned player.
*
*/
  g_gothic_event_proxies[kEventOnPlayerSpawnName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* This event is triggered when a player or NPC dies.
*
* @version  0.3.0
* @name     onPlayerDead
* @side     client
* @category Player
* @param    (number) id  The id of the player who died.
*
*/
  g_gothic_event_proxies[kEventOnPlayerDeadName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* This event is triggered when the client receives a ping update for a player.
*
* @version  0.3.0
* @name     onPlayerChangePing
* @side     client
* @category Player
* @param    (number) id    The id of the player.
* @param    (number) ping  The player's ping.
*
*/
  g_gothic_event_proxies[kEventOnPlayerChangePingName] = [](LuaProxyArgs args) {
    OnPlayerPingEvent event = std::any_cast<OnPlayerPingEvent>(args.event);
    args.callback(event.player_id, event.ping);
  };
}

void RegisterGothicEventsInManager() {
  EventManager::Instance().RegisterEvent(kEventOnInitName);
  EventManager::Instance().RegisterEvent(kEventOnExitName);
  EventManager::Instance().RegisterEvent(kEventOnRenderName);
  EventManager::Instance().RegisterEvent(kEventOnTimeName);
  EventManager::Instance().RegisterEvent(kEventOnWeatherChangeName);
  EventManager::Instance().RegisterEvent(kEventOnKeyDownName);
  EventManager::Instance().RegisterEvent(kEventOnKeyUpName);
  EventManager::Instance().RegisterEvent(kEventOnPasteName);
  EventManager::Instance().RegisterEvent(kEventOnCommandName);
  EventManager::Instance().RegisterEvent(kEventOnMouseDownName);
  EventManager::Instance().RegisterEvent(kEventOnMouseUpName);
  EventManager::Instance().RegisterEvent(kEventOnMouseMoveName);
  EventManager::Instance().RegisterEvent(kEventOnMouseWheelName);
  EventManager::Instance().RegisterEvent(kEventOnOpenInventoryName);
  EventManager::Instance().RegisterEvent(kEventOnCloseInventoryName);
  EventManager::Instance().RegisterEvent(kEventOnInventorySlotChangeName);
  EventManager::Instance().RegisterEvent(kEventOnWorldChangeName);
  EventManager::Instance().RegisterEvent(kEventOnWorldEnterName);
  EventManager::Instance().RegisterEvent(kEventOnEquipName);
  EventManager::Instance().RegisterEvent(kEventOnDropItemName);
  EventManager::Instance().RegisterEvent(kEventOnTakeItemName);
  EventManager::Instance().RegisterEvent(kEventOnUseItemName);
  EventManager::Instance().RegisterEvent(kEventOnItemGroundCreateName);
  EventManager::Instance().RegisterEvent(kEventOnItemGroundDestroyName);
  EventManager::Instance().RegisterEvent(kEventOnItemsGroundDestroyName);
  EventManager::Instance().RegisterEvent(kEventOnUnequipName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerCreateName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerDestroyName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerMessageName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerRespawnName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerSpawnName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerDeadName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerChangePingName);
}

std::optional<std::function<void(LuaProxyArgs)>> GetProxy(const std::string& event_name) {
  auto it = g_gothic_event_proxies.find(event_name);
  if (it != g_gothic_event_proxies.end()) {
    return it->second;
  }

  if (g_custom_events.find(event_name) != g_custom_events.end()) {
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

void BindGothicEvents(sol::state& lua) {
  RegisterGothicEventProxies();
  RegisterGothicEventsInManager();
  
  lua.set_function("addEventHandler",
                   [](std::string event_name, sol::protected_function lua_callback, sol::optional<int> priority_opt) -> bool {
                     SPDLOG_TRACE("addEventHandler({})", event_name);

                     auto proxy = GetProxy(event_name);
                     if (!proxy) {
                       SPDLOG_ERROR("addEventHandler: event with name {} doesn't exist!", event_name);
                       return false;
                     }

                     int priority = priority_opt.value_or(9999);
                     const void* identity = GetFunctionIdentity(lua_callback);

                     auto callback = [proxy, lua_callback, event_name](std::any event) {
                       LuaProxyArgs args;
                       args.event = event;
                       args.callback = LuaEventCallback(lua_callback, event_name);
                       (*proxy)(args);
                     };

                     auto handler_id = EventManager::Instance().SubscribeToEventWithPriority(event_name, callback, priority);
                     if (!handler_id) {
                       return false;
                     }

                     g_handler_registrations[event_name].push_back(HandlerRegistration{identity, *handler_id});
                     return true;
                   });

  lua.set_function("addEvent", [](std::string event_name, sol::optional<bool> allow_remote_trigger) -> bool {
    SPDLOG_TRACE("addEvent({})", event_name);
    if (!EventManager::Instance().RegisterEvent(event_name)) {
      return false;
    }

    g_custom_events.insert(event_name);
    if (allow_remote_trigger.value_or(false)) {
      g_remote_events.insert(event_name);
    } else {
      g_remote_events.erase(event_name);
    }
    return true;
  });

  lua.set_function("callEvent", [](std::string event_name, sol::variadic_args args) -> bool {
    SPDLOG_TRACE("callEvent({})", event_name);
    if (g_custom_events.find(event_name) == g_custom_events.end()) {
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
  });

/* luagmp (func)
*
* This function triggers a custom server-side event and optionally passes arguments.
* The first argument is always the event name.
* 
* @version  0.3.0
* @name     triggerServerEvent
* @side     client
* @category Network
* @note     You may optionally provide a numeric source element id as the next argument, followed by any number of additional arguments to send with the event.
* @note     `sourceElement` is an optional numeric identifier that represents the object or entity that caused the event. Its meaning is user-defined and depends on the game logic. 
* @param    (string) eventName Name of the server-side event to trigger.
* @param    (number|nil) sourceElement Optional source element id. Use nil if not needed.
* @param    (...) ... Optional arguments passed to the server event handler.
* @return   (boolean) True if the event was sent successfully, otherwise false.
*
*/
  lua.set_function("triggerServerEvent", [&lua](sol::variadic_args args) -> bool {
    if (args.size() < 1) {
      SPDLOG_ERROR("triggerServerEvent called without event name");
      return false;
    }

    sol::object event_name_obj = args[0];
    if (event_name_obj.get_type() != sol::type::string) {
      SPDLOG_ERROR("triggerServerEvent expected event name string");
      return false;
    }
    std::string event_name = event_name_obj.as<std::string>();
    SPDLOG_TRACE("triggerServerEvent({})", event_name);
    if (event_name.empty()) {
      SPDLOG_ERROR("triggerServerEvent called with empty event name");
      return false;
    }

    auto* game_client = NetGame::Instance().game_client.get();
    if (!game_client) {
      SPDLOG_WARN("triggerServerEvent called without active game client");
      return false;
    }

    std::size_t index = 1;
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
      SPDLOG_ERROR("triggerServerEvent failed to encode payload for '{}': {}", event_name, error);
      return false;
    }

    return game_client->SendLuaEventToServer(event_name, source_element, payload);
  });

  lua.set_function("cancelEvent", []() { EventManager::Instance().CancelCurrentEvent(); });
  lua.set_function("eventValue", [](int event_value) { EventManager::Instance().SetCurrentEventValue(event_value); });
  lua.set_function("isEventCancelled", []() -> bool { return EventManager::Instance().IsCurrentEventCancelled(); });

  lua.set_function("removeEvent", [](std::string event_name) {
    SPDLOG_TRACE("removeEvent({})", event_name);
    if (g_custom_events.find(event_name) == g_custom_events.end()) {
      SPDLOG_ERROR("removeEvent: event with name {} doesn't exist!", event_name);
      return;
    }

    EventManager::Instance().UnregisterEvent(event_name);
    g_custom_events.erase(event_name);
    g_remote_events.erase(event_name);
    g_handler_registrations.erase(event_name);
  });

  lua.set_function("removeEventHandler", [](std::string event_name, sol::protected_function lua_callback) -> bool {
    SPDLOG_TRACE("removeEventHandler({})", event_name);

    const void* identity = GetFunctionIdentity(lua_callback);
    auto it = g_handler_registrations.find(event_name);
    if (it == g_handler_registrations.end()) {
      return false;
    }

    auto& handlers = it->second;
    for (auto handler_it = handlers.begin(); handler_it != handlers.end(); ++handler_it) {
      if (handler_it->identity == identity) {
        bool removed = EventManager::Instance().UnsubscribeFromEvent(event_name, handler_it->id);
        handlers.erase(handler_it);
        return removed;
      }
    }

    return false;
  });

  lua.set_function("toggleEvent", [](std::string event_name, bool toggle) {
    SPDLOG_TRACE("toggleEvent({}, {})", event_name, toggle);
    if (!EventManager::Instance().ToggleEvent(event_name, toggle)) {
      SPDLOG_ERROR("toggleEvent: event with name {} doesn't exist!", event_name);
    }
  });
}

bool TriggerRemoteEvent(const std::string& event_name, std::uint32_t source_element, const std::vector<sol::object>& args) {
  if (g_custom_events.find(event_name) == g_custom_events.end()) {
    SPDLOG_ERROR("Remote event '{}' is not registered on the client", event_name);
    return false;
  }
  if (g_remote_events.find(event_name) == g_remote_events.end()) {
    SPDLOG_WARN("Remote event '{}' is not allowed for client triggering", event_name);
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

void ResetGothicEvents() {
  EventManager::Instance().Reset();
  g_custom_events.clear();
  g_remote_events.clear();
  g_handler_registrations.clear();
  RegisterGothicEventsInManager();
}

}  // namespace gmp::gothic
