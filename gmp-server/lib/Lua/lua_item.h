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
#include <string>
#include <string_view>

#include <sol/sol.hpp>

namespace lua {
namespace bindings {

class LuaItem {
public:
  explicit LuaItem(std::string instance = {});

  std::string getInstance() const;
  std::int32_t getIndex() const;
  std::int32_t getMainflag() const;
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
  std::string getScemename() const;
  std::int32_t getMagCircle() const;
  sol::table getProtections(sol::this_state state) const;
  sol::table getConditions(sol::this_state state) const;
  std::int32_t getProtection(std::int32_t damage_type) const;
  std::int32_t getCondAtr(std::int32_t index) const;
  std::int32_t getCondValue(std::int32_t index) const;

private:
  std::string instance_;
};

sol::object MakeItemObject(sol::state_view lua, std::string_view instance);
sol::object MakeItemObjectByIndex(sol::state_view lua, std::int32_t index);

void BindItem(sol::state& lua);

}  // namespace bindings
}  // namespace lua
