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

#include "lua_item.h"

#include <cstddef>
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>
#include <utility>

#include "../game_server.h"
#include "../resource_manager.h"
#include "Shared/lua_runtime/lua_diagnostics.h"

namespace lua {
namespace bindings {
namespace {

struct ItemCallbackRegistration {
  sol::protected_function function;
  std::string owner;
};
using ItemCallbacks = std::array<std::vector<ItemCallbackRegistration>, 3>;
std::unordered_map<std::string, ItemCallbacks> item_callbacks;
thread_local std::optional<std::uint32_t> active_callback_actor;
struct CallbackActorScope {
  std::optional<std::uint32_t> previous{active_callback_actor};
  explicit CallbackActorScope(std::optional<std::uint32_t> actor) { active_callback_actor = actor; }
  ~CallbackActorScope() { active_callback_actor = previous; }
};

sol::object GetCallback(std::string_view instance, ItemCallback kind, sol::this_state state) {
  sol::state_view lua(state);
  const auto found = item_callbacks.find(ItemRegistry::NormalizeInstanceName(instance));
  if (found == item_callbacks.end()) return sol::nil;
  const auto& callbacks = found->second[static_cast<std::size_t>(kind)];
  for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it) {
    if (it->function.lua_state() != lua.lua_state()) continue;
    auto registration = *it;
    return sol::make_object(lua, sol::as_function([registration](sol::variadic_args args) mutable {
      std::unique_ptr<ResourceManager::ScopedResourceContext> context;
      if (!registration.owner.empty()) {
        auto* manager = ResourceManager::GetActiveInstance();
        auto resource = manager ? manager->GetResource(registration.owner) : std::nullopt;
        if (!resource || !resource->get().IsLoaded()) throw sol::error("Item callback resource is not loaded");
        context = std::make_unique<ResourceManager::ScopedResourceContext>(resource->get());
      }
      std::optional<std::uint32_t> actor;
      if (args.size() > 0) {
        sol::stack_object first = args[0];
        if (first.is<std::uint32_t>()) actor = first.as<std::uint32_t>();
      }
      CallbackActorScope actor_scope(actor);
      auto result = registration.function(sol::as_args(args));
      if (!result.valid()) {
        sol::error error = result;
        throw error;
      }
      return result;
    }));
  }
  return sol::nil;
}

bool SetCallback(std::string_view instance, ItemCallback kind, sol::object callback) {
  if (!g_server || !g_server->GetItemRegistry().Contains(instance)) return false;
  if (callback.get_type() != sol::type::nil && callback.get_type() != sol::type::function)
    return false;
  const auto* resource = ResourceManager::GetCurrentResource();
  const auto owner = resource ? resource->GetName() : std::string{};
  auto& callbacks = item_callbacks[ItemRegistry::NormalizeInstanceName(instance)][static_cast<std::size_t>(kind)];
  callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(), [&](const auto& entry) {
    return entry.owner == owner && entry.function.lua_state() == callback.lua_state();
  }), callbacks.end());
  if (callback.get_type() == sol::type::function)
    callbacks.push_back({callback.as<sol::protected_function>(), owner});
  return true;
}

template <typename Predicate>
void RemoveCallbacks(Predicate predicate) {
  for (auto it = item_callbacks.begin(); it != item_callbacks.end();) {
    bool empty = true;
    for (auto& callbacks : it->second) {
      callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(), predicate), callbacks.end());
      empty = empty && callbacks.empty();
    }
    if (empty) it = item_callbacks.erase(it);
    else ++it;
  }
}

const ItemRegistry::Item* FindItem(std::string_view instance) {
  return g_server ? g_server->GetItemRegistry().Find(instance) : nullptr;
}

sol::table MakeNumberPairTable(sol::state_view lua, const char* first_name, std::int32_t first_value, const char* second_name,
                               std::int32_t second_value) {
  sol::table table = lua.create_table();
  table[first_name] = first_value;
  table[second_name] = second_value;
  return table;
}

const ItemRegistry::Requirement* GetRequirement(const ItemRegistry::Item& item, std::int32_t index) {
  if (index < 0) {
    return nullptr;
  }
  const auto array_index = static_cast<std::size_t>(index);
  if (array_index >= item.requirements.size()) {
    return nullptr;
  }
  return &item.requirements[array_index];
}

}  // namespace

/* luagmp (class)
*
* Represents a read-only Gothic item definition from the server item registry.
*
* @version  0.3.0
* @name     Item
* @side     server
* @category Item
*
*/
LuaItem::LuaItem(std::string instance) : instance_(std::move(instance)) {}

/* luagmp (property)
*
* The item's server-side use callback.
*
* @name     onUse
* @return   (function|nil)
* @note     Callback receives player_id, consumed quantity and optional target_id. Set through ItemManager.
*
*/
sol::object LuaItem::getOnUse(sol::this_state state) const { return GetCallback(instance_, ItemCallback::Use, state); }
/* luagmp (property)
*
* The item's server-side equip callback.
*
* @name     onEquip
* @return   (function|nil)
* @note     Callback receives player_id and equipment slot. Set through ItemManager.
*
*/
sol::object LuaItem::getOnEquip(sol::this_state state) const { return GetCallback(instance_, ItemCallback::Equip, state); }
/* luagmp (property)
*
* The item's server-side unequip callback.
*
* @name     onUnequip
* @return   (function|nil)
* @note     Callback receives player_id and equipment slot. Set through ItemManager.
*
*/
sol::object LuaItem::getOnUnequip(sol::this_state state) const { return GetCallback(instance_, ItemCallback::Unequip, state); }

ItemCallbackResult InvokeItemCallback(std::string_view instance, ItemCallback kind, std::uint32_t player_id,
                                    std::int32_t quantity_or_slot, std::optional<std::uint32_t> target_id) {
  const auto item_name = std::string(instance);
  const auto found = item_callbacks.find(ItemRegistry::NormalizeInstanceName(item_name));
  if (found == item_callbacks.end()) return {};
  const auto& callbacks = found->second[static_cast<std::size_t>(kind)];
  if (callbacks.empty()) return {};
  // Copy before calling Lua: the resource may replace/remove its callback or unload.
  auto registration = callbacks.back();
  std::unique_ptr<ResourceManager::ScopedResourceContext> context;
  if (!registration.owner.empty()) {
    auto* manager = ResourceManager::GetActiveInstance();
    auto resource = manager ? manager->GetResource(registration.owner) : std::nullopt;
    if (!resource || !resource->get().IsLoaded()) return {};
    context = std::make_unique<ResourceManager::ScopedResourceContext>(resource->get());
  }
  CallbackActorScope actor_scope(player_id);
  sol::state_view lua(registration.function.lua_state());
  const sol::object target = target_id ? sol::make_object(lua, *target_id) : sol::make_object(lua, sol::nil);
  auto result = kind == ItemCallback::Use ? registration.function(player_id, quantity_or_slot, target)
                                        : registration.function(player_id, quantity_or_slot);
  if (!result.valid()) {
    sol::error error = result;
    ::lua::diagnostics::LogRuntimeError(error.what(), {registration.owner, "item callback", item_name});
    return {true, false};
  }
  const bool accepted = result.return_count() == 0 || result.get_type() != sol::type::boolean || result.get<bool>();
  return {true, accepted};
}

void RemoveItemCallbacksForResource(const std::string& owner_name) {
  RemoveCallbacks([&](const auto& entry) { return entry.owner == owner_name; });
}

bool IsItemCallbackActiveFor(std::uint32_t player_id) { return active_callback_actor == player_id; }

void RemoveItemCallbacksForState(lua_State* state) {
  RemoveCallbacks([&](const auto& entry) { return entry.function.lua_state() == state; });
}

/* luagmp (property)
*
* Represents the canonical Gothic item instance name.
*
* @name     instance
* @return   (string)
*
*/
std::string LuaItem::getInstance() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->instance : std::string{};
}

/* luagmp (property)
*
* Represents the item's main category flag. For more information, see [Item Constants](../../shared-constants/Item.md).
*
* @name     mainflag
* @return   (number)
*
*/
/* luagmp (property)
*
* Represents the item's main category flag. For more information, see [Item Constants](../../shared-constants/Item.md).
*
* @name     mainFlag
* @return   (number)
*
*/
std::int32_t LuaItem::getMainflag() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->mainflag : 0;
}

/* luagmp (property)
*
* Represents the item's Gothic flags. For more information, see [Item Constants](../../shared-constants/Item.md).
*
* @name     flags
* @return   (number)
*
*/
std::int32_t LuaItem::getFlags() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->flags : 0;
}

/* luagmp (property)
*
* Represents the item's visual model file.
*
* @name     visual
* @return   (string)
*
*/
std::string LuaItem::getVisual() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->visual : std::string{};
}

/* luagmp (property)
*
* Represents the armor wear slot value. For more information, see [Item Constants](../../shared-constants/Item.md).
*
* @name     wear
* @return   (number)
*
*/
std::int32_t LuaItem::getWear() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->wear.has_value() ? item->wear.value() : 0;
}

/* luagmp (property)
*
* Represents the item's range value.
*
* @name     range
* @return   (number)
*
*/
std::int32_t LuaItem::getRange() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->range.has_value() ? item->range.value() : 0;
}

/* luagmp (property)
*
* Represents the item's value in currency units.
*
* @name     value
* @return   (number)
*
*/
std::int32_t LuaItem::getValue() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->value.has_value() ? item->value.value() : 0;
}

/* luagmp (property)
*
* Represents the item's damage information. Its `types` field uses [Damage Constants](../../shared-constants/Damage.md).
*
* @name     damage
* @return   ({total, types}|nil)
*
*/
sol::object LuaItem::getDamage(sol::this_state state) const {
  sol::state_view lua(state);
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr || !item->damage.has_value()) {
    return sol::nil;
  }
  return sol::make_object(lua, MakeNumberPairTable(lua, "total", item->damage->total, "types", item->damage->types));
}

/* luagmp (property)
*
* Represents the item's total damage.
*
* @name     damageTotal
* @return   (number)
*
*/
std::int32_t LuaItem::getDamageTotal() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->damage.has_value() ? item->damage->total : 0;
}

/* luagmp (property)
*
* Represents the item's Gothic damage type flags. For more information, see [Damage Constants](../../shared-constants/Damage.md).
*
* @name     damageTypes
* @return   (number)
*
*/
std::int32_t LuaItem::getDamageTypes() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->damage.has_value() ? item->damage->types : 0;
}

/* luagmp (property)
*
* Represents the required munition item instance name.
*
* @name     munition
* @return   (string)
*
*/
std::string LuaItem::getMunition() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->munition.has_value() ? item->munition.value() : std::string{};
}

/* luagmp (property)
*
* Represents the required munition item definition.
*
* @name     munitionItem
* @return   (Item|nil)
*
*/
sol::object LuaItem::getMunitionItem(sol::this_state state) const {
  sol::state_view lua(state);
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr || !item->munition.has_value()) {
    return sol::nil;
  }
  return MakeItemObject(lua, *item->munition);
}

/* luagmp (property)
*
* Represents the spell id associated with this item.
*
* @name     spell
* @return   (number)
*
*/
std::int32_t LuaItem::getSpell() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->spell.has_value() ? item->spell.value() : 0;
}

/* luagmp (property)
*
* Represents the item's Gothic sceme name.
*
* @name     scemename
* @return   (string)
*
*/
std::string LuaItem::getScemename() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->scemename : std::string{};
}

/* luagmp (property)
*
* Represents the magic circle required by this item.
*
* @name     mag_circle
* @return   (number)
*
*/
std::int32_t LuaItem::getMagCircle() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->mag_circle.has_value() ? item->mag_circle.value() : 0;
}

/* luagmp (property)
*
* Represents protection values exported for this item.
*
* @name     protections
* @return   ({...}) Array of tables with `type` and `value` fields; `type` uses [Damage Constants](../../shared-constants/Damage.md).
*
*/
sol::table LuaItem::getProtections(sol::this_state state) const {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr) {
    return result;
  }

  for (std::size_t i = 0; i < item->protections.size(); ++i) {
    const auto& protection = item->protections[i];
    result[static_cast<int>(i + 1)] = MakeNumberPairTable(lua, "type", protection.type, "value", protection.value);
  }
  return result;
}

/* luagmp (property)
*
* Represents attribute conditions exported for this item.
*
* @name     conditions
* @return   ({...}) Array of tables with `attribute` and `value` fields.
*
*/
sol::table LuaItem::getConditions(sol::this_state state) const {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr) {
    return result;
  }

  for (std::size_t i = 0; i < item->requirements.size(); ++i) {
    const auto& requirement = item->requirements[i];
    result[static_cast<int>(i + 1)] = MakeNumberPairTable(lua, "attribute", requirement.attribute, "value", requirement.value);
  }
  return result;
}

/* luagmp (method)
*
* Returns the protection value for a Gothic damage type.
*
* @name     getProtection
* @param    (number) damageType  Gothic damage type. For more information, see [Damage Constants](../../shared-constants/Damage.md).
* @return   (number)
*
*/
std::int32_t LuaItem::getProtection(std::int32_t damage_type) const {
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr) {
    return 0;
  }

  for (const auto& protection : item->protections) {
    if (protection.type == damage_type) {
      return protection.value;
    }
  }
  return 0;
}

/* luagmp (method)
*
* Returns the required attribute type at the given condition index.
*
* @name     getCondAtr
* @param    (number) index  Zero-based condition index.
* @return   (number)
*
*/
std::int32_t LuaItem::getCondAtr(std::int32_t index) const {
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr) {
    return 0;
  }
  const ItemRegistry::Requirement* requirement = GetRequirement(*item, index);
  return requirement ? requirement->attribute : 0;
}

/* luagmp (method)
*
* Returns the required attribute value at the given condition index.
*
* @name     getCondValue
* @param    (number) index  Zero-based condition index.
* @return   (number)
*
*/
std::int32_t LuaItem::getCondValue(std::int32_t index) const {
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr) {
    return 0;
  }
  const ItemRegistry::Requirement* requirement = GetRequirement(*item, index);
  return requirement ? requirement->value : 0;
}

sol::object MakeItemObject(sol::state_view lua, std::string_view instance) {
  const ItemRegistry::Item* item = FindItem(instance);
  if (item == nullptr) {
    return sol::nil;
  }
  return sol::make_object(lua, LuaItem(item->instance));
}

/* luagmp (method)
*
* Returns an item definition by Gothic instance name.
*
* @version  0.3.0
* @name     getByInstance
* @side     server
* @category Item
* @param    (string) instance  Gothic item instance name.
* @return   (Item|nil) Item definition or nil if missing.
*
*/
sol::object Function_Item_GetByInstance(std::string instance, sol::this_state state) {
  sol::state_view lua(state);
  return MakeItemObject(lua, instance);
}

/* luagmp (method)
*
* Returns whether an item definition exists for the given instance name.
*
* @version  0.3.0
* @name     exists
* @side     server
* @category Item
* @param    (string) instance  Gothic item instance name.
* @return   (boolean)
*
*/
bool Function_Item_Exists(std::string instance) {
  return g_server != nullptr && g_server->GetItemRegistry().Contains(instance);
}

/* luagmp (method)
*
* Returns the number of item definitions loaded by the server.
*
* @version  0.3.0
* @name     size
* @side     server
* @category Item
* @return   (number)
*
*/
std::size_t Function_Item_Size() {
  return g_server ? g_server->GetItemRegistry().Size() : 0;
}

/* luagmp (class)
*
* Manages server-side item callbacks.
*
* @version  0.3.0
* @name     ItemManager
* @side     server
* @category Item
*
*/
/* luagmp (method)
*
* Sets the item's use callback.
*
* @name     setOnUse
* @side     server
* @category Item
* @param    (string) instance       Item instance name.
* @param    (function|nil) callback Callback, or nil to remove it.
* @return   (boolean)              True on success.
*
*/
bool Function_ItemManager_SetOnUse(std::string instance, sol::object callback) {
  return SetCallback(instance, ItemCallback::Use, std::move(callback));
}

/* luagmp (method)
*
* Sets the item's equip callback.
*
* @name     setOnEquip
* @side     server
* @category Item
* @param    (string) instance       Item instance name.
* @param    (function|nil) callback Callback, or nil to remove it.
* @return   (boolean)              True on success.
*
*/
bool Function_ItemManager_SetOnEquip(std::string instance, sol::object callback) {
  return SetCallback(instance, ItemCallback::Equip, std::move(callback));
}

/* luagmp (method)
*
* Sets the item's unequip callback.
*
* @name     setOnUnequip
* @side     server
* @category Item
* @param    (string) instance       Item instance name.
* @param    (function|nil) callback Callback, or nil to remove it.
* @return   (boolean)              True on success.
*
*/
bool Function_ItemManager_SetOnUnequip(std::string instance, sol::object callback) {
  return SetCallback(instance, ItemCallback::Unequip, std::move(callback));
}

void BindItem(sol::state& lua) {
  auto item_type = lua.new_usertype<LuaItem>("Item", sol::no_constructor);
  item_type["onUse"] = sol::property(&LuaItem::getOnUse);
  item_type["onEquip"] = sol::property(&LuaItem::getOnEquip);
  item_type["onUnequip"] = sol::property(&LuaItem::getOnUnequip);
  item_type["instance"] = sol::property(&LuaItem::getInstance);
  item_type["mainflag"] = sol::property(&LuaItem::getMainflag);
  item_type["mainFlag"] = sol::property(&LuaItem::getMainflag);
  item_type["flags"] = sol::property(&LuaItem::getFlags);
  item_type["visual"] = sol::property(&LuaItem::getVisual);
  item_type["wear"] = sol::property(&LuaItem::getWear);
  item_type["range"] = sol::property(&LuaItem::getRange);
  item_type["value"] = sol::property(&LuaItem::getValue);
  item_type["damage"] = sol::property(&LuaItem::getDamage);
  item_type["damageTotal"] = sol::property(&LuaItem::getDamageTotal);
  item_type["damageTypes"] = sol::property(&LuaItem::getDamageTypes);
  item_type["munition"] = sol::property(&LuaItem::getMunition);
  item_type["munitionItem"] = sol::property(&LuaItem::getMunitionItem);
  item_type["spell"] = sol::property(&LuaItem::getSpell);
  item_type["scemename"] = sol::property(&LuaItem::getScemename);
  item_type["mag_circle"] = sol::property(&LuaItem::getMagCircle);
  item_type["protections"] = sol::property(&LuaItem::getProtections);
  item_type["conditions"] = sol::property(&LuaItem::getConditions);
  item_type["getProtection"] = &LuaItem::getProtection;
  item_type["getCondAtr"] = &LuaItem::getCondAtr;
  item_type["getCondValue"] = &LuaItem::getCondValue;
  item_type["getByInstance"] = Function_Item_GetByInstance;
  item_type["exists"] = Function_Item_Exists;
  item_type["size"] = Function_Item_Size;
}

void BindItemManager(sol::state& lua) {
  auto manager = lua.create_table();
  manager["setOnUse"] = Function_ItemManager_SetOnUse;
  manager["setOnEquip"] = Function_ItemManager_SetOnEquip;
  manager["setOnUnequip"] = Function_ItemManager_SetOnUnequip;
  lua["ItemManager"] = manager;
}

}  // namespace bindings
}  // namespace lua
