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

#include "lua_item_ground.h"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

#include "../game_server.h"

namespace lua {
namespace bindings {
namespace {

ItemGroundManager::ItemGround* GetItemGround(ItemGroundManager::ItemGroundId id) {
  return g_server ? g_server->GetItemGroundManager().Get(id) : nullptr;
}

template <typename T>
T GetValueOr(const sol::table& table, const char* field, T fallback) {
  sol::object value = table[field];
  if (!value.valid() || value.get_type() == sol::type::nil) {
    return fallback;
  }

  return value.as<T>();
}

glm::vec3 ReadVec3(const sol::table& table, const char* field, const glm::vec3& fallback) {
  sol::object value = table[field];
  if (value.get_type() != sol::type::table) {
    return fallback;
  }

  sol::table vec = value.as<sol::table>();
  return glm::vec3(vec.get_or("x", fallback.x), vec.get_or("y", fallback.y), vec.get_or("z", fallback.z));
}

sol::table MakeVec3Table(sol::this_state state, const glm::vec3& vec) {
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  result["x"] = vec.x;
  result["y"] = vec.y;
  result["z"] = vec.z;
  return result;
}

sol::object MakeItemGroundObject(ItemGroundManager::ItemGroundId id, sol::this_state state) {
  sol::state_view lua(state);
  if (GetItemGround(id) == nullptr) {
    return sol::nil;
  }
  return sol::make_object(lua, LuaItemGround(id));
}

ItemGroundManager::ItemGroundId ReadItemGroundId(const sol::object& object) {
  if (object.get_type() == sol::type::number) {
    return object.as<ItemGroundManager::ItemGroundId>();
  }
  if (object.is<LuaItemGround>()) {
    return object.as<LuaItemGround>().getId();
  }
  return 0;
}

}  // namespace

LuaItemGround::LuaItemGround(ItemGroundManager::ItemGroundId id) : id_(id) {}

ItemGroundManager::ItemGroundId LuaItemGround::getId() const {
  return id_;
}

std::string LuaItemGround::getInstance() const {
  auto* item_ground = GetItemGround(id_);
  return item_ground ? item_ground->instance : std::string{};
}

std::int32_t LuaItemGround::getAmount() const {
  auto* item_ground = GetItemGround(id_);
  return item_ground ? item_ground->amount : 0;
}

bool LuaItemGround::getPhysicsEnabled() const {
  auto* item_ground = GetItemGround(id_);
  return item_ground ? item_ground->physics_enabled : false;
}

void LuaItemGround::setPhysicsEnabled(bool enabled) {
  if (g_server) {
    g_server->SetItemGroundPhysicsEnabled(id_, enabled);
  }
}

std::string LuaItemGround::getWorld() const {
  auto* item_ground = GetItemGround(id_);
  return item_ground ? item_ground->world : std::string{};
}

std::int32_t LuaItemGround::getVirtualWorld() const {
  auto* item_ground = GetItemGround(id_);
  return item_ground ? item_ground->virtual_world : 0;
}

void LuaItemGround::setVirtualWorld(std::int32_t virtual_world) {
  if (g_server) {
    g_server->SetItemGroundVirtualWorld(id_, virtual_world);
  }
}

sol::table LuaItemGround::getPosition(sol::this_state state) const {
  auto* item_ground = GetItemGround(id_);
  return MakeVec3Table(state, item_ground ? item_ground->position : glm::vec3{0.0f});
}

void LuaItemGround::setPosition(float x, float y, float z) {
  if (g_server) {
    g_server->SetItemGroundPosition(id_, glm::vec3{x, y, z});
  }
}

sol::table LuaItemGround::getRotation(sol::this_state state) const {
  auto* item_ground = GetItemGround(id_);
  return MakeVec3Table(state, item_ground ? item_ground->rotation : glm::vec3{0.0f});
}

void LuaItemGround::setRotation(float x, float y, float z) {
  if (g_server) {
    g_server->SetItemGroundRotation(id_, glm::vec3{x, y, z});
  }
}

void BindItemGround(sol::state& lua) {
/* luagmp (class)
*
* Represents a synchronized item placed in the world.
*
* @version  0.3.0
* @name     ItemGround
* @side     server
* @category Item
*
*/
  auto item_ground_type = lua.new_usertype<LuaItemGround>("ItemGround", sol::no_constructor);

/* luagmp (property)
*
* Represents the unique ground item id.
*
* @name     id
* @return   (number)
*
*/
  item_ground_type["id"] = sol::property(&LuaItemGround::getId);

/* luagmp (property)
*
* Represents the Gothic item instance name.
*
* @name     instance
* @return   (string)
*
*/
  item_ground_type["instance"] = sol::property(&LuaItemGround::getInstance);

/* luagmp (property)
*
* Represents the item amount.
*
* @name     amount
* @return   (number)
*
*/
  item_ground_type["amount"] = sol::property(&LuaItemGround::getAmount);

/* luagmp (property)
*
* Enables or disables client-side physics for this ground item.
*
* @name     physicsEnabled
* @return   (boolean)
*
*/
  item_ground_type["physicsEnabled"] = sol::property(&LuaItemGround::getPhysicsEnabled, &LuaItemGround::setPhysicsEnabled);

/* luagmp (property)
*
* Represents the world name where this ground item exists.
*
* @name     world
* @return   (string)
*
*/
  item_ground_type["world"] = sol::property(&LuaItemGround::getWorld);

/* luagmp (property)
*
* Represents the virtual world id where this ground item exists.
*
* @name     virtualWorld
* @return   (number)
*
*/
  item_ground_type["virtualWorld"] = sol::property(&LuaItemGround::getVirtualWorld, &LuaItemGround::setVirtualWorld);

/* luagmp (method)
*
* Returns the ground item world position.
*
* @name     getPosition
* @return   ({x, y, z}) Position table.
*
*/
  item_ground_type["getPosition"] = &LuaItemGround::getPosition;

/* luagmp (method)
*
* Sets the ground item world position and synchronizes it to relevant players.
*
* @name     setPosition
* @param    (number) x  X coordinate.
* @param    (number) y  Y coordinate.
* @param    (number) z  Z coordinate.
*
*/
  item_ground_type["setPosition"] = &LuaItemGround::setPosition;

/* luagmp (method)
*
* Returns the ground item Euler rotation.
*
* @name     getRotation
* @return   ({x, y, z}) Rotation table.
*
*/
  item_ground_type["getRotation"] = &LuaItemGround::getRotation;

/* luagmp (method)
*
* Sets the ground item Euler rotation and synchronizes it to relevant players.
*
* @name     setRotation
* @param    (number) x  X rotation.
* @param    (number) y  Y rotation.
* @param    (number) z  Z rotation.
*
*/
  item_ground_type["setRotation"] = &LuaItemGround::setRotation;

/* luagmp (class)
*
* Provides server-side access to synchronized ground items.
*
* @version  0.3.0
* @name     ItemGroundManager
* @side     server
* @category Item
*
*/
  sol::table manager = lua.create_table();

/* luagmp (func)
*
* Returns a ground item by id.
*
* @version  0.3.0
* @name     getById
* @side     server
* @category Item
* @param    (number) id        Ground item id.
* @return   (ItemGround|nil)   Ground item object or nil if missing.
*
*/
  manager["getById"] = [](ItemGroundManager::ItemGroundId id, sol::this_state state) { return MakeItemGroundObject(id, state); };

/* luagmp (func)
*
* Creates a synchronized ground item.
*
* @version  0.3.0
* @name     create
* @side     server
* @category Item
* @param    ({...}) options                      Creation options.
* @param    (string) options.instance            Gothic item instance name.
* @param    (number) options.amount              Optional amount. Defaults to 1.
* @param    (boolean) options.physicsEnabled     Optional physics flag. Defaults to false.
* @param    ({x, y, z}) options.position         Optional world position. Defaults to 0,0,0.
* @param    ({x, y, z}) options.rotation         Optional Euler rotation. Defaults to 0,0,0.
* @param    (string) options.world               Optional world name. Defaults to the server world.
* @param    (number) options.virtualWorld        Optional virtual world id. Defaults to 0.
* @return   (number) Ground item id, or 0 on failure.
*
*/
  manager["create"] = [](const sol::table& arg) -> ItemGroundManager::ItemGroundId {
    if (g_server == nullptr) {
      return 0;
    }

    auto instance = GetValueOr<std::string>(arg, "instance", {});
    if (instance.empty()) {
      SPDLOG_WARN("ItemGroundManager.create called without an instance");
      return 0;
    }

    ItemGroundManager::CreateOptions options;
    options.instance = instance;
    options.amount = std::max<std::int32_t>(1, GetValueOr<std::int32_t>(arg, "amount", 1));
    options.physics_enabled = GetValueOr<bool>(arg, "physicsEnabled", false);
    options.position = ReadVec3(arg, "position", glm::vec3{0.0f});
    options.rotation = ReadVec3(arg, "rotation", glm::vec3{0.0f});
    options.world = GetValueOr<std::string>(arg, "world", {});
    options.virtual_world = GetValueOr<std::int32_t>(arg, "virtualWorld", 0);
    return g_server->CreateItemGround(std::move(options));
  };

/* luagmp (func)
*
* Destroys a synchronized ground item.
*
* @version  0.3.0
* @name     destroy
* @side     server
* @category Item
* @param    (number|ItemGround) itemGround  Ground item id or object.
* @return   (boolean) True if the ground item was destroyed.
*
*/
  manager["destroy"] = [](const sol::object& object) -> bool {
    if (g_server == nullptr) {
      return false;
    }

    const auto id = ReadItemGroundId(object);
    if (id == 0) {
      SPDLOG_WARN("ItemGroundManager.destroy expects an item ground id or ItemGround object");
      return false;
    }
    return g_server->DestroyItemGround(id);
  };

  lua["ItemGroundManager"] = manager;
}

}  // namespace bindings
}  // namespace lua
