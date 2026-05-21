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
#include <utility>

#include "../game_server.h"

namespace lua {
namespace bindings {
namespace {

const ItemRegistry::Item* FindItem(std::string_view instance) {
  return g_server ? g_server->GetItemRegistry().Find(instance) : nullptr;
}

const ItemRegistry::Item* FindItemByIndex(std::int32_t index) {
  return g_server ? g_server->GetItemRegistry().FindByIndex(index) : nullptr;
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

LuaItem::LuaItem(std::string instance) : instance_(std::move(instance)) {}

std::string LuaItem::getInstance() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->instance : std::string{};
}

std::int32_t LuaItem::getIndex() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->index : 0;
}

std::int32_t LuaItem::getMainflag() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->mainflag : 0;
}

std::int32_t LuaItem::getFlags() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->flags : 0;
}

std::string LuaItem::getVisual() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->visual : std::string{};
}

std::int32_t LuaItem::getWear() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->wear.has_value() ? item->wear.value() : 0;
}

std::int32_t LuaItem::getRange() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->range.has_value() ? item->range.value() : 0;
}

std::int32_t LuaItem::getValue() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->value.has_value() ? item->value.value() : 0;
}

sol::object LuaItem::getDamage(sol::this_state state) const {
  sol::state_view lua(state);
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr || !item->damage.has_value()) {
    return sol::nil;
  }
  return sol::make_object(lua, MakeNumberPairTable(lua, "total", item->damage->total, "types", item->damage->types));
}

std::int32_t LuaItem::getDamageTotal() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->damage.has_value() ? item->damage->total : 0;
}

std::int32_t LuaItem::getDamageTypes() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->damage.has_value() ? item->damage->types : 0;
}

std::string LuaItem::getMunition() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->munition.has_value() ? item->munition.value() : std::string{};
}

sol::object LuaItem::getMunitionItem(sol::this_state state) const {
  sol::state_view lua(state);
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr || !item->munition.has_value()) {
    return sol::nil;
  }
  return MakeItemObject(lua, *item->munition);
}

std::int32_t LuaItem::getSpell() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->spell.has_value() ? item->spell.value() : 0;
}

std::string LuaItem::getScemename() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item ? item->scemename : std::string{};
}

std::int32_t LuaItem::getMagCircle() const {
  const ItemRegistry::Item* item = FindItem(instance_);
  return item && item->mag_circle.has_value() ? item->mag_circle.value() : 0;
}

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

std::int32_t LuaItem::getCondAtr(std::int32_t index) const {
  const ItemRegistry::Item* item = FindItem(instance_);
  if (item == nullptr) {
    return 0;
  }
  const ItemRegistry::Requirement* requirement = GetRequirement(*item, index);
  return requirement ? requirement->attribute : 0;
}

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

sol::object MakeItemObjectByIndex(sol::state_view lua, std::int32_t index) {
  const ItemRegistry::Item* item = FindItemByIndex(index);
  if (item == nullptr) {
    return sol::nil;
  }
  return sol::make_object(lua, LuaItem(item->instance));
}

void BindItem(sol::state& lua) {
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
  auto item_type = lua.new_usertype<LuaItem>("Item", sol::no_constructor);

/* luagmp (property)
*
* Represents the canonical Gothic item instance name.
*
* @name     instance
* @return   (string)
*
*/
  item_type["instance"] = sol::property(&LuaItem::getInstance);

/* luagmp (property)
*
* Represents the Gothic parser symbol index for this item instance.
*
* @name     index
* @return   (number)
*
*/
  item_type["index"] = sol::property(&LuaItem::getIndex);

/* luagmp (property)
*
* Represents the item's main category flag.
*
* @name     mainflag
* @return   (number)
*
*/
  item_type["mainflag"] = sol::property(&LuaItem::getMainflag);

/* luagmp (property)
*
* Represents the item's main category flag.
*
* @name     mainFlag
* @return   (number)
*
*/
  item_type["mainFlag"] = sol::property(&LuaItem::getMainflag);

/* luagmp (property)
*
* Represents the item's Gothic flags.
*
* @name     flags
* @return   (number)
*
*/
  item_type["flags"] = sol::property(&LuaItem::getFlags);

/* luagmp (property)
*
* Represents the item's visual model file.
*
* @name     visual
* @return   (string)
*
*/
  item_type["visual"] = sol::property(&LuaItem::getVisual);

/* luagmp (property)
*
* Represents the armor wear slot value.
*
* @name     wear
* @return   (number)
*
*/
  item_type["wear"] = sol::property(&LuaItem::getWear);

/* luagmp (property)
*
* Represents the item's range value.
*
* @name     range
* @return   (number)
*
*/
  item_type["range"] = sol::property(&LuaItem::getRange);

/* luagmp (property)
*
* Represents the item's value in currency units.
*
* @name     value
* @return   (number)
*
*/
  item_type["value"] = sol::property(&LuaItem::getValue);

/* luagmp (property)
*
* Represents the item's damage information.
*
* @name     damage
* @return   ({total, types}|nil)
*
*/
  item_type["damage"] = sol::property([](const LuaItem& item, sol::this_state state) { return item.getDamage(state); });

/* luagmp (property)
*
* Represents the item's total damage.
*
* @name     damageTotal
* @return   (number)
*
*/
  item_type["damageTotal"] = sol::property(&LuaItem::getDamageTotal);

/* luagmp (property)
*
* Represents the item's Gothic damage type flags.
*
* @name     damageTypes
* @return   (number)
*
*/
  item_type["damageTypes"] = sol::property(&LuaItem::getDamageTypes);

/* luagmp (property)
*
* Represents the required munition item instance name.
*
* @name     munition
* @return   (string)
*
*/
  item_type["munition"] = sol::property(&LuaItem::getMunition);

/* luagmp (property)
*
* Represents the required munition item definition.
*
* @name     munitionItem
* @return   (Item|nil)
*
*/
  item_type["munitionItem"] = sol::property([](const LuaItem& item, sol::this_state state) { return item.getMunitionItem(state); });

/* luagmp (property)
*
* Represents the spell id associated with this item.
*
* @name     spell
* @return   (number)
*
*/
  item_type["spell"] = sol::property(&LuaItem::getSpell);

/* luagmp (property)
*
* Represents the item's Gothic sceme name.
*
* @name     scemename
* @return   (string)
*
*/
  item_type["scemename"] = sol::property(&LuaItem::getScemename);

/* luagmp (property)
*
* Represents the magic circle required by this item.
*
* @name     mag_circle
* @return   (number)
*
*/
  item_type["mag_circle"] = sol::property(&LuaItem::getMagCircle);

/* luagmp (property)
*
* Represents protection values exported for this item.
*
* @name     protections
* @return   ({...}) Array of tables with `type` and `value` fields.
*
*/
  item_type["protections"] = sol::property([](const LuaItem& item, sol::this_state state) { return item.getProtections(state); });

/* luagmp (property)
*
* Represents attribute conditions exported for this item.
*
* @name     conditions
* @return   ({...}) Array of tables with `attribute` and `value` fields.
*
*/
  item_type["conditions"] = sol::property([](const LuaItem& item, sol::this_state state) { return item.getConditions(state); });

/* luagmp (method)
*
* Returns the protection value for a Gothic damage type.
*
* @name     getProtection
* @param    (number) damageType  Gothic damage type.
* @return   (number)
*
*/
  item_type["getProtection"] = &LuaItem::getProtection;

/* luagmp (method)
*
* Returns the required attribute type at the given condition index.
*
* @name     getCondAtr
* @param    (number) index  Zero-based condition index.
* @return   (number)
*
*/
  item_type["getCondAtr"] = &LuaItem::getCondAtr;

/* luagmp (method)
*
* Returns the required attribute value at the given condition index.
*
* @name     getCondValue
* @param    (number) index  Zero-based condition index.
* @return   (number)
*
*/
  item_type["getCondValue"] = &LuaItem::getCondValue;

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
  item_type["getByInstance"] = [](std::string instance, sol::this_state state) {
    sol::state_view lua(state);
    return MakeItemObject(lua, instance);
  };

/* luagmp (method)
*
* Returns an item definition by Gothic parser symbol index.
*
* @version  0.3.0
* @name     getByIndex
* @side     server
* @category Item
* @param    (number) index  Gothic parser symbol index.
* @return   (Item|nil) Item definition or nil if missing.
*
*/
  item_type["getByIndex"] = [](std::int32_t index, sol::this_state state) {
    sol::state_view lua(state);
    return MakeItemObjectByIndex(lua, index);
  };

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
  item_type["exists"] = [](std::string instance) -> bool {
    return g_server != nullptr && g_server->GetItemRegistry().Contains(instance);
  };

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
  item_type["size"] = []() -> std::size_t {
    return g_server ? g_server->GetItemRegistry().Size() : 0;
  };
}

}  // namespace bindings
}  // namespace lua
