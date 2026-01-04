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
#include <vector>

#include "net_game.h"
#include "shared/event.h"
#include "shared/lua_runtime/lua_value_codec.h"

namespace gmp::gothic {

namespace {

struct LuaProxyArgs {
  std::any event;
  sol::protected_function callback;
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
* Triggered once when the Gothic engine initializes.
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
* Triggered when the Gothic engine is exiting.
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
* Triggered each frame when the game renders.
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
* Triggered each time the game time minute passes.
*
* @version  0.3.0
* @name     onTime
* @side     client
* @category Game
* @param    (int) day   The current in-game day.
* @param    (int) hour  The current in-game hour.
* @param    (int) min   The current in-game minute.
*
*/
  g_gothic_event_proxies[kEventOnTimeName] = [](LuaProxyArgs args) {
    OnTimeEvent event = std::any_cast<OnTimeEvent>(args.event);
    args.callback(event.day, event.hour, event.min);
  };

/* luagmp (event)
*
* Triggered when weather changes.
*
* @version  0.3.0
* @name     onWeatherChange
* @side     client
* @category Weather
* @param    (int) old_weather_type  Previous weather type.
* @param    (int) new_weather_type  New weather type.
*
*/
  g_gothic_event_proxies[kEventOnWeatherChangeName] = [](LuaProxyArgs args) {
    OnWeatherChangeEvent event = std::any_cast<OnWeatherChangeEvent>(args.event);
    args.callback(event.old_weather_type, event.new_weather_type);
  };

/* luagmp (event)
*
* Triggered when a key is pressed.
*
* @version  0.3.0
* @name     onKeyDown
* @side     client
* @category Input
* @param    (int) key    The key code pressed.
*
*/
  g_gothic_event_proxies[kEventOnKeyDownName] = [](LuaProxyArgs args) {
    OnKeyEvent event = std::any_cast<OnKeyEvent>(args.event);
    args.callback(event.key);
  };

/* luagmp (event)
*
* Triggered when a key is released.
*
* @version  0.3.0
* @name     onKeyUp
* @side     client
* @category Input
* @param    (int) key    The key code released.
*
*/
  g_gothic_event_proxies[kEventOnKeyUpName] = [](LuaProxyArgs args) {
    OnKeyEvent event = std::any_cast<OnKeyEvent>(args.event);
    args.callback(event.key);
  };

/* luagmp (event)
*
* Triggered when a mouse button is pressed.
*
* @version  0.3.0
* @name     onMouseDown
* @side     client
* @category Mouse
* @param    (int) button The mouse button pressed.
*
*/
  g_gothic_event_proxies[kEventOnMouseDownName] = [](LuaProxyArgs args) {
    OnMouseButtonEvent event = std::any_cast<OnMouseButtonEvent>(args.event);
    args.callback(event.button);
  };

/* luagmp (event)
*
* Triggered when a mouse button is released.
*
* @version  0.3.0
* @name     onMouseUp
* @side     client
* @category Mouse
* @param    (int) button The mouse button released.
*
*/
  g_gothic_event_proxies[kEventOnMouseUpName] = [](LuaProxyArgs args) {
    OnMouseButtonEvent event = std::any_cast<OnMouseButtonEvent>(args.event);
    args.callback(event.button);
  };

/* luagmp (event)
*
* Triggered when the mouse cursor is moved.
*
* @version  0.3.0
* @name     onMouseMove
* @side     client
* @category Mouse
* @param    (int) x Cursor X position.
* @param    (int) y Cursor Y position.
*
*/
  g_gothic_event_proxies[kEventOnMouseMoveName] = [](LuaProxyArgs args) {
    OnMouseMoveEvent event = std::any_cast<OnMouseMoveEvent>(args.event);
    args.callback(event.x, event.y);
  };

/* luagmp (event)
*
* Triggered when the mouse wheel is scrolled.
*
* @version  0.3.0
* @name     onMouseWheel
* @side     client
* @category Mouse
* @param    (int) z Mouse wheel delta.
*
*/
  g_gothic_event_proxies[kEventOnMouseWheelName] = [](LuaProxyArgs args) {
    OnMouseWheelEvent event = std::any_cast<OnMouseWheelEvent>(args.event);
    args.callback(event.z);
  };

/* luagmp (event)
*
* Triggered when the user opens the inventory.
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
* Triggered when the user closes the inventory.
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
* Triggered when the selected inventory slot changes.
*
* @version  0.3.0
* @name     onInventorySlotChange
* @side     client
* @category Inventory
* @param    (int) from  Previous slot number.
* @param    (int) to    Current slot number.
*
*/
  g_gothic_event_proxies[kEventOnInventorySlotChangeName] = [](LuaProxyArgs args) {
    OnInventorySlotChangeEvent event = std::any_cast<OnInventorySlotChangeEvent>(args.event);
    args.callback(event.from, event.to);
  };

/* luagmp (event)
*
* Triggered when the client requests a world change.
*
* @version  0.3.0
* @name     onWorldChange
* @side     client
* @category World
* @param    (string) world     World filename.
* @param    (string) waypoint  Waypoint name used for teleport.
*
*/
  g_gothic_event_proxies[kEventOnWorldChangeName] = [](LuaProxyArgs args) {
    OnWorldChangeEvent event = std::any_cast<OnWorldChangeEvent>(args.event);
    args.callback(event.world, event.waypoint);
  };

/* luagmp (event)
*
* Triggered when the active world is loaded and the hero is present in it.
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
* Triggered when the hero equips an item.
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
* Triggered when the hero drops an item.
*
* @version  0.3.0
* @name     onDropItem
* @side     client
* @category Hero
* @param    (string) item  Item instance.
*
*/
  g_gothic_event_proxies[kEventOnDropItemName] = [](LuaProxyArgs args) {
    OnItemEvent event = std::any_cast<OnItemEvent>(args.event);
    args.callback(event.item);
  };

/* luagmp (event)
*
* Triggered when the hero takes an item from the ground.
*
* @version  0.3.0
* @name     onTakeItem
* @side     client
* @category Hero
* @param    (string) item        Item instance.
* @param    (bool) synchronized  True when pickup is synchronized with the server.
*
*/
  g_gothic_event_proxies[kEventOnTakeItemName] = [](LuaProxyArgs args) {
    OnTakeItemEvent event = std::any_cast<OnTakeItemEvent>(args.event);
    args.callback(event.item, event.synchronized);
  };

/* luagmp (event)
*
* Triggered when the hero uses, interacts with, opens, or consumes an item.
*
* @version  0.3.0
* @name     onUseItem
* @side     client
* @category Hero
* @param    (string) item    Item instance.
* @param    (string) scheme  Item scheme name, if available.
* @param    (int) from     Previous interact state.
* @param    (int) to       Current interact state.
*
*/
  g_gothic_event_proxies[kEventOnUseItemName] = [](LuaProxyArgs args) {
    OnUseItemEvent event = std::any_cast<OnUseItemEvent>(args.event);
    args.callback(event.item, event.scheme, event.from, event.to);
  };

/* luagmp (event)
*
* Triggered when the hero unequips an item.
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
* Triggered when a player object is created locally.
*
* @version  0.3.0
* @name     onPlayerCreate
* @side     client
* @category Player
* @param    (int) player_id    The local player id.
*
*/
  g_gothic_event_proxies[kEventOnPlayerCreateName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* Triggered when a local player object is destroyed.
*
* @version  0.3.0
* @name     onPlayerDestroy
* @side     client
* @category Player
* @param    (int) player_id    The local player id.
*
*/
  g_gothic_event_proxies[kEventOnPlayerDestroyName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* Triggered when a chat message is received locally.
*
* @version  0.3.0
* @name     onPlayerMessage
* @side     client
* @category Player
* @param    (int) sender_id  Optional sender id (nil for system).
* @param    (int) r           Red color component.
* @param    (int) g           Green color component.
* @param    (int) b           Blue color component.
* @param    (string) message  Message text.
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
* Triggered when a player respawns after death.
*
* @version  0.3.0
* @name     onPlayerRespawn
* @side     client
* @category Player
* @param    (int) id  The id of the respawned player.
*
*/
  g_gothic_event_proxies[kEventOnPlayerRespawnName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* Triggered when a player is spawned into the world.
*
* @version  0.3.0
* @name     onPlayerSpawn
* @side     client
* @category Player
* @param    (int) id  The id of the spawned player.
*
*/
  g_gothic_event_proxies[kEventOnPlayerSpawnName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* Triggered when a player or NPC dies.
*
* @version  0.3.0
* @name     onPlayerDead
* @side     client
* @category Player
* @param    (int) id  The id of the player who died.
*
*/
  g_gothic_event_proxies[kEventOnPlayerDeadName] = [](LuaProxyArgs args) {
    PlayerLifecycleEvent event = std::any_cast<PlayerLifecycleEvent>(args.event);
    args.callback(event.player_id);
  };

/* luagmp (event)
*
* Triggered when the client receives a ping update for a player.
*
* @version  0.3.0
* @name     onPlayerChangePing
* @side     client
* @category Player
* @param    (int) id    The id of the player.
* @param    (int) ping  The player's ping.
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
      if (custom_event.source_element.has_value()) {
        lua["source"] = static_cast<int>(*custom_event.source_element);
      } else {
        lua["source"] = sol::lua_nil;
      }
      args.callback(sol::as_args(custom_event.args));
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

                     auto callback = [proxy, lua_callback](std::any event) {
                       LuaProxyArgs args;
                       args.event = event;
                       args.callback = lua_callback;
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
* Triggers a custom server-side event and optionally passes arguments.
* The first argument is always the event name.
* 
* @version  0.3.0
* @name     triggerServerEvent
* @side     client
* @category Network
* @note     You may optionally provide a numeric source element id as the next argument, followed by any number of additional arguments to send with the event.
* @note     `sourceElement` is an optional numeric identifier that represents the object or entity that caused the event. Its meaning is user-defined and depends on the game logic. 
* @param    (string) eventName Name of the server-side event to trigger.
* @param    (int|nil) sourceElement Optional source element id. Use nil or omit it if not needed.
* @param    (...) ... Optional arguments passed to the server event handler.
* @return   (bool) True if the event was sent successfully, otherwise false.
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
      if (source_obj.get_type() == sol::type::number && remaining >= 2) {
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
  event.source_element = source_element;
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
