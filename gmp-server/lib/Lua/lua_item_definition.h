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

#pragma once

#include <cstdint>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <string_view>

namespace lua {
namespace bindings {

class LuaItemDefinition {
public:
  explicit LuaItemDefinition(std::string instance = {});

  std::string getInstance() const;
  std::int32_t getMainFlag() const;
  std::int32_t getFlags() const;
  std::string getVisual() const;
  std::int32_t getWear() const;
  std::int32_t getRange() const;
  std::int32_t getValue() const;
  sol::object getDamage(sol::this_state state) const;
  std::int32_t getDamageTotal() const;
  std::int32_t getDamageTypes() const;
  std::string getMunition() const;
  sol::object getMunitionItem(sol::this_state state) const;
  std::int32_t getSpell() const;
  std::string getSchemeName() const;
  std::int32_t getMagicCircle() const;
  sol::table getProtections(sol::this_state state) const;
  sol::table getRequirements(sol::this_state state) const;
  sol::object getRequirement(int index, sol::this_state state) const;
  std::int32_t getProtection(std::int32_t damage_type) const;
  sol::object getOnUse(sol::this_state state) const;
  sol::object getOnEquip(sol::this_state state) const;
  sol::object getOnUnequip(sol::this_state state) const;

private:
  std::string instance_;
};

sol::object MakeItemDefinitionObject(sol::state_view lua, std::string_view instance);

void BindItemDefinition(sol::state& lua);
void BindItemCallbacks(sol::state& lua);

enum class ItemCallback { Use, Equip, Unequip };
struct ItemCallbackResult {
  bool handled{false};
  bool accepted{true};
};
ItemCallbackResult InvokeItemCallback(std::string_view instance, ItemCallback kind, std::uint32_t player_id, std::int32_t quantity_or_slot,
                                      std::optional<std::uint32_t> target_id = std::nullopt);
void RemoveItemCallbacksForResource(const std::string& owner_name);
void RemoveItemCallbacksForState(lua_State* state);
bool IsItemCallbackActiveFor(std::uint32_t player_id);

}  // namespace bindings
}  // namespace lua
