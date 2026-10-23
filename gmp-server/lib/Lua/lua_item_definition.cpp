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

#include "lua_item_definition.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

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
  explicit CallbackActorScope(std::optional<std::uint32_t> actor) {
    active_callback_actor = actor;
  }
  ~CallbackActorScope() {
    active_callback_actor = previous;
  }
};

// Invokes an item callback with its owning resource and actor context restored.
struct ItemCallbackFunction {
  ItemCallbackRegistration registration;
  sol::protected_function_result operator()(sol::variadic_args args) {
    std::unique_ptr<ResourceManager::ScopedResourceContext> context;
    if (!registration.owner.empty()) {
      auto* manager = ResourceManager::GetActiveInstance();
      auto resource = manager ? manager->GetResource(registration.owner) : std::nullopt;
      if (!resource || !resource->get().IsLoaded())
        throw sol::error("Item callback resource is not loaded");
      context = std::make_unique<ResourceManager::ScopedResourceContext>(resource->get());
    }
    std::optional<std::uint32_t> actor;
    if (args.size() > 0) {
      sol::stack_object first = args[0];
      if (first.is<std::uint32_t>())
        actor = first.as<std::uint32_t>();
    }
    CallbackActorScope actor_scope(actor);
    auto result = registration.function(sol::as_args(args));
    if (!result.valid()) {
      sol::error error = result;
      throw error;
    }
    return result;
  }
};

sol::object GetCallback(std::string_view instance, ItemCallback kind, sol::this_state state) {
  sol::state_view lua(state);
  const auto found = item_callbacks.find(ItemRegistry::NormalizeInstanceName(instance));
  if (found == item_callbacks.end())
    return sol::nil;
  const auto& callbacks = found->second[static_cast<std::size_t>(kind)];
  for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it) {
    if (it->function.lua_state() != lua.lua_state())
      continue;
    auto registration = *it;
    return sol::make_object(lua, sol::as_function(ItemCallbackFunction{std::move(registration)}));
  }
  return sol::nil;
}

bool SetCallback(std::string_view instance, ItemCallback kind, sol::object callback) {
  if (!g_server || !g_server->GetItemRegistry().Contains(instance))
    return false;
  if (callback.get_type() != sol::type::nil && callback.get_type() != sol::type::function)
    return false;
  const auto* resource = ResourceManager::GetCurrentResource();
  const auto owner = resource ? resource->GetName() : std::string{};
  auto& callbacks = item_callbacks[ItemRegistry::NormalizeInstanceName(instance)][static_cast<std::size_t>(kind)];
  callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                 [&](const auto& entry) { return entry.owner == owner && entry.function.lua_state() == callback.lua_state(); }),
                  callbacks.end());
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
    if (empty)
      it = item_callbacks.erase(it);
    else
      ++it;
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

}  // namespace

/* luagmp (class)
 *
 * Represents a read-only Gothic item definition from the server item registry.
 *
 * @version  0.3.0
 * @name     ItemDefinition
 * @side     server
 * @category Item
 *
 */
LuaItemDefinition::LuaItemDefinition(std::string instance) : instance_(std::move(instance)) {
}

/* luagmp (property)
 *
 * The item's server-side use callback.
 *
 * @name     onUse
 * @return   (function|nil)
 * @note     Callback receives player_id, consumed quantity and optional target_id. Set through [ItemCallbacks](ItemCallbacks.md).
 *
 */
sol::object LuaItemDefinition::getOnUse(sol::this_state state) const {
  return GetCallback(instance_, ItemCallback::Use, state);
}

/* luagmp (property)
 *
 * The item's server-side equip callback.
 *
 * @name     onEquip
 * @return   (function|nil)
 * @note     Callback receives player_id and equipment slot. Set through [ItemCallbacks](ItemCallbacks.md).
 *
 */
sol::object LuaItemDefinition::getOnEquip(sol::this_state state) const {
  return GetCallback(instance_, ItemCallback::Equip, state);
}

/* luagmp (property)
 *
 * The item's server-side unequip callback.
 *
 * @name     onUnequip
 * @return   (function|nil)
 * @note     Callback receives player_id and equipment slot. Set through [ItemCallbacks](ItemCallbacks.md).
 *
 */
sol::object LuaItemDefinition::getOnUnequip(sol::this_state state) const {
  return GetCallback(instance_, ItemCallback::Unequip, state);
}

ItemCallbackResult InvokeItemCallback(std::string_view instance, ItemCallback kind, std::uint32_t player_id, std::int32_t quantity_or_slot,
                                      std::optional<std::uint32_t> target_id) {
  const auto item_name = std::string(instance);
  const auto found = item_callbacks.find(ItemRegistry::NormalizeInstanceName(item_name));
  if (found == item_callbacks.end())
    return {};
  const auto& callbacks = found->second[static_cast<std::size_t>(kind)];
  if (callbacks.empty())
    return {};
  // Copy before calling Lua: the resource may replace/remove its callback or unload.
  auto registration = callbacks.back();
  std::unique_ptr<ResourceManager::ScopedResourceContext> context;
  if (!registration.owner.empty()) {
    auto* manager = ResourceManager::GetActiveInstance();
    auto resource = manager ? manager->GetResource(registration.owner) : std::nullopt;
    if (!resource || !resource->get().IsLoaded())
      return {};
    context = std::make_unique<ResourceManager::ScopedResourceContext>(resource->get());
  }
  CallbackActorScope actor_scope(player_id);
  sol::state_view lua(registration.function.lua_state());
  const sol::object target = target_id ? sol::make_object(lua, *target_id) : sol::make_object(lua, sol::nil);
  auto result =
      kind == ItemCallback::Use ? registration.function(player_id, quantity_or_slot, target) : registration.function(player_id, quantity_or_slot);
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

bool IsItemCallbackActiveFor(std::uint32_t player_id) {
  return active_callback_actor == player_id;
}

void RemoveItemCallbacksForState(lua_State* state) {
  RemoveCallbacks([&](const auto& entry) { return entry.function.lua_state() == state; });
}

/* luagmp (property)
 *
 * Represents the canonical Gothic item instance name.
 *
 * @name     instance
 * @return   (string)
 */
std::string LuaItemDefinition::getInstance() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->instance : std::string{};
}

/* luagmp (property)
 *
 * Represents the item's main category flag. For more information, see [Item Constants](../../shared-constants/Item.md).
 *
 * @name     mainFlag
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getMainFlag() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->mainflag : 0;
}

/* luagmp (property)
 *
 * Represents the item's Gothic flags. For more information, see [Item Constants](../../shared-constants/Item.md).
 *
 * @name     flags
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getFlags() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->flags : 0;
}

/* luagmp (property)
 *
 * Represents the item's visual model file.
 *
 * @name     visual
 * @return   (string)
 */
std::string LuaItemDefinition::getVisual() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->visual : std::string{};
}

/* luagmp (property)
 *
 * Represents the armor wear slot value. For more information, see [Item Constants](../../shared-constants/Item.md).
 *
 * @name     wear
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getWear() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->wear.has_value() ? item->wear.value() : 0;
}

/* luagmp (property)
 *
 * Represents the item's range value.
 *
 * @name     range
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getRange() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->range.has_value() ? item->range.value() : 0;
}

/* luagmp (property)
 *
 * Represents the item's value in currency units.
 *
 * @name     value
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getValue() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->value.has_value() ? item->value.value() : 0;
}

/* luagmp (property)
 *
 * Represents the item's damage information. Its `types` field uses [Damage Constants](../../shared-constants/Damage.md).
 *
 * @name     damage
 * @return   ({total, types}|nil)
 */
sol::object LuaItemDefinition::getDamage(sol::this_state state) const {
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
 */
std::int32_t LuaItemDefinition::getDamageTotal() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->damage.has_value() ? item->damage->total : 0;
}

/* luagmp (property)
 *
 * Represents the item's Gothic damage type flags. For more information, see [Damage Constants](../../shared-constants/Damage.md).
 *
 * @name     damageTypes
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getDamageTypes() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->damage.has_value() ? item->damage->types : 0;
}

/* luagmp (property)
 *
 * Represents the required munition item instance name.
 *
 * @name     munition
 * @return   (string)
 */
std::string LuaItemDefinition::getMunition() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->munition.has_value() ? item->munition.value() : std::string{};
}

/* luagmp (property)
 *
 * Represents the required munition item definition.
 *
 * @name     munitionItem
 * @return   (ItemDefinition|nil)
 */
sol::object LuaItemDefinition::getMunitionItem(sol::this_state state) const {
  sol::state_view lua(state);
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr || !item->munition.has_value()) {
    return sol::nil;
  }
  return MakeItemDefinitionObject(lua, *item->munition);
}

/* luagmp (property)
 *
 * Represents the spell id associated with this item.
 *
 * @name     spell
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getSpell() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->spell.has_value() ? item->spell.value() : 0;
}

/* luagmp (property)
 *
 * Represents the item's Gothic scheme name.
 *
 * @name     schemeName
 * @return   (string)
 */
std::string LuaItemDefinition::getSchemeName() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->scemename : std::string{};
}

/* luagmp (property)
 *
 * Represents the magic circle required by this item.
 *
 * @name     magicCircle
 * @return   (number)
 */
std::int32_t LuaItemDefinition::getMagicCircle() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->mag_circle.has_value() ? item->mag_circle.value() : 0;
}

/* luagmp (property)
 * One-based array of protection tables with `type` and `value` fields. The type uses [Damage Constants](../../shared-constants/Damage.md).
 *
 * @name     protections
 * @return   ({...})
 *
 */
sol::table LuaItemDefinition::getProtections(sol::this_state state) const {
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
 * One-based array of requirement tables with `attribute` and `value` fields.
 *
 * @name     requirements
 * @return   ({...})
 *
 */
sol::table LuaItemDefinition::getRequirements(sol::this_state state) const {
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
std::int32_t LuaItemDefinition::getProtection(std::int32_t damage_type) const {
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

sol::object MakeItemDefinitionObject(sol::state_view lua, std::string_view instance) {
  const ItemRegistry::Item* item = FindItem(instance);
  if (item == nullptr) {
    return sol::nil;
  }
  return sol::make_object(lua, LuaItemDefinition(item->instance));
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
 * @return   (ItemDefinition|nil) Definition or nil if missing.
 *
 * @static
 */
sol::object Function_ItemDefinition_GetByInstance(std::string instance, sol::this_state state) {
  sol::state_view lua(state);
  return MakeItemDefinitionObject(lua, instance);
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
 * @static
 */
bool Function_ItemDefinition_Exists(std::string instance) {
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
 * @static
 */
std::size_t Function_ItemDefinition_Size() {
  return g_server ? g_server->GetItemRegistry().Size() : 0;
}

/* luagmp (method)
 * Returns a requirement by its one-based index.
 * @name getRequirement
 * @param (number) index Index starting at 1.
 * @return (table|nil) Requirement or nil if out of range.
 */
sol::object LuaItemDefinition::getRequirement(int index, sol::this_state state) const {
  sol::table requirements = getRequirements(state);
  if (index < 1)
    return sol::nil;
  return requirements.get<sol::object>(index);
}

/* luagmp (class)
 *
 * Manages server-side item callbacks.
 *
 * @version  0.3.0
 * @name     ItemCallbacks
 * @side     server
 * @category Item
 *
 * @static
 */
/* luagmp (method)
 *
 * Sets the item's use callback.
 *
 * @name     setOnUse
 * @side     server
 * @category Item
 * @param    (string) instance       [ItemDefinition](ItemDefinition.md) instance name.
 * @param    (function|nil) callback Callback, or nil to remove it.
 * @return   (boolean)              True on success.
 *
 * @static
 */
bool Function_ItemCallbacks_SetOnUse(std::string instance, sol::object callback) {
  return SetCallback(instance, ItemCallback::Use, std::move(callback));
}

/* luagmp (method)
 *
 * Sets the item's equip callback.
 *
 * @name     setOnEquip
 * @side     server
 * @category Item
 * @param    (string) instance       [ItemDefinition](ItemDefinition.md) instance name.
 * @param    (function|nil) callback Callback, or nil to remove it.
 * @return   (boolean)              True on success.
 *
 * @static
 */
bool Function_ItemCallbacks_SetOnEquip(std::string instance, sol::object callback) {
  return SetCallback(instance, ItemCallback::Equip, std::move(callback));
}

/* luagmp (method)
 *
 * Sets the item's unequip callback.
 *
 * @name     setOnUnequip
 * @side     server
 * @category Item
 * @param    (string) instance       [ItemDefinition](ItemDefinition.md) instance name.
 * @param    (function|nil) callback Callback, or nil to remove it.
 * @return   (boolean)              True on success.
 *
 * @static
 */
bool Function_ItemCallbacks_SetOnUnequip(std::string instance, sol::object callback) {
  return SetCallback(instance, ItemCallback::Unequip, std::move(callback));
}

void BindItemDefinition(sol::state& lua) {
  auto item_type = lua.new_usertype<LuaItemDefinition>("ItemDefinition", sol::no_constructor);
  item_type["onUse"] = sol::property(&LuaItemDefinition::getOnUse);
  item_type["onEquip"] = sol::property(&LuaItemDefinition::getOnEquip);
  item_type["onUnequip"] = sol::property(&LuaItemDefinition::getOnUnequip);
  item_type["instance"] = sol::property(&LuaItemDefinition::getInstance);
  item_type["mainFlag"] = sol::property(&LuaItemDefinition::getMainFlag);
  item_type["flags"] = sol::property(&LuaItemDefinition::getFlags);
  item_type["visual"] = sol::property(&LuaItemDefinition::getVisual);
  item_type["wear"] = sol::property(&LuaItemDefinition::getWear);
  item_type["range"] = sol::property(&LuaItemDefinition::getRange);
  item_type["value"] = sol::property(&LuaItemDefinition::getValue);
  item_type["damage"] = sol::property(&LuaItemDefinition::getDamage);
  item_type["damageTotal"] = sol::property(&LuaItemDefinition::getDamageTotal);
  item_type["damageTypes"] = sol::property(&LuaItemDefinition::getDamageTypes);
  item_type["munition"] = sol::property(&LuaItemDefinition::getMunition);
  item_type["munitionItem"] = sol::property(&LuaItemDefinition::getMunitionItem);
  item_type["spell"] = sol::property(&LuaItemDefinition::getSpell);
  item_type["protections"] = sol::property(&LuaItemDefinition::getProtections);
  item_type["getProtection"] = &LuaItemDefinition::getProtection;
  item_type["getByInstance"] = Function_ItemDefinition_GetByInstance;
  item_type["exists"] = Function_ItemDefinition_Exists;
  item_type["size"] = Function_ItemDefinition_Size;
  item_type["requirements"] = sol::property(&LuaItemDefinition::getRequirements);
  item_type["getRequirement"] = &LuaItemDefinition::getRequirement;
  item_type["schemeName"] = sol::property(&LuaItemDefinition::getSchemeName);
  item_type["magicCircle"] = sol::property(&LuaItemDefinition::getMagicCircle);
}

void BindItemCallbacks(sol::state& lua) {
  auto manager = lua.create_table();
  manager["setOnUse"] = Function_ItemCallbacks_SetOnUse;
  manager["setOnEquip"] = Function_ItemCallbacks_SetOnEquip;
  manager["setOnUnequip"] = Function_ItemCallbacks_SetOnUnequip;
  lua["ItemCallbacks"] = manager;
}

}  // namespace bindings
}  // namespace lua
