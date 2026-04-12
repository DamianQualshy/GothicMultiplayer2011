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

#include "lua_way.h"

#include <limits>

#include "ZenGin/zGothicAPI.h"

using namespace Gothic_II_Addon;

namespace gmp::gothic {
namespace {

zCRoute* FindRouteByName(const std::string& start_wp, const std::string& end_wp) {
  if (!ogame) {
    return nullptr;
  }

  zCWorld* world = ogame->GetGameWorld();
  if (!world || !world->wayNet) {
    return nullptr;
  }

  zSTRING start(start_wp.c_str());
  zSTRING end(end_wp.c_str());
  return world->wayNet->FindRoute(start, end, nullptr);
}

/* luagmp (func)
*
* Enable or disable waynet rendering.
*
* @version  0.3.0
* @name     toggleDrawWaynet
* @side     client
* @category Game
* @param    (boolean) enabled    True to render waynet, false to disable.
*
*/
void Function_ToggleDrawWaynet(bool enabled) {
  if (!ogame) {
    return;
  }

  ogame->SetDrawWaynet(enabled);
}

zCWayNet* GetWayNet() {
  if (!ogame) {
    return nullptr;
  }

  zCWorld* world = ogame->GetGameWorld();
  if (!world) {
    return nullptr;
  }

  return world->wayNet;
}

sol::table MakeVec3Table(sol::state_view lua, const zVEC3& position) {
  sol::table tbl = lua.create_table();
  tbl["x"] = position[VX];
  tbl["y"] = position[VY];
  tbl["z"] = position[VZ];
  return tbl;
}

sol::table MakeWaypointTable(sol::state_view lua, zCWaypoint* waypoint) {
  sol::table tbl = lua.create_table();
  tbl["name"] = waypoint->GetName().ToChar();
  const zVEC3 position = waypoint->GetPositionWorld();
  tbl["x"] = position[VX];
  tbl["y"] = position[VY];
  tbl["z"] = position[VZ];
  return tbl;
}

}  // namespace

/* luagmp (class)
*
* @version  0.3.0
* Represents a way path generated between two waypoint names.
*
* @name     Way
* @side     client
* @category World
*
*/
/* luagmp (constructor)
*
* Creates a path between two waypoint names.
*
* @param    (string) startWp   Name of the start waypoint.
* @param    (string) endWp     Name of the end waypoint.
*
*/
LuaWay::LuaWay(const std::string& start_wp, const std::string& end_wp)
    : start_wp_(start_wp), end_wp_(end_wp) {}

/* luagmp (method)
*
* Returns the start waypoint name.
*
* @name     getStart
* @return   (string)
*
*/
const std::string& LuaWay::getStart() const {
  return start_wp_;
}

/* luagmp (method)
*
* Returns the end waypoint name.
*
* @name     getEnd
* @return   (string)
*
*/
const std::string& LuaWay::getEnd() const {
  return end_wp_;
}

/* luagmp (method)
*
* Get all waypoints from the computed route.
*
* @name     getWaypoints
* @return   ([wpName...]) Array with waypoint names.
*
*/
std::vector<std::string> LuaWay::getWaypoints() const {
  std::vector<std::string> result;

  zCRoute* route = FindRouteByName(start_wp_, end_wp_);
  if (!route) {
    return result;
  }

  zSTRING start(start_wp_.c_str());
  zCWaypoint* start_waypoint = ogame->GetGameWorld()->wayNet->GetWaypoint(start);
  if (start_waypoint) {
    route->SetStart(start_waypoint);
    result.emplace_back(start_wp_);
  }

  while (zCWaypoint* waypoint = route->GetNextWP()) {
    const std::string waypoint_name = waypoint->GetName().ToChar();
    if (!result.empty() && result.back() == waypoint_name) {
      continue;
    }
    result.emplace_back(waypoint_name);
  }

  delete route;
  return result;
}

/* luagmp (method)
*
* Get number of waypoints in the computed route.
*
* @name     getCountWaypoints
* @return   (number)
*
*/
int LuaWay::getCountWaypoints() const {
  zCRoute* route = FindRouteByName(start_wp_, end_wp_);
  if (!route) {
    return 0;
  }

  const int count = route->GetNumberOfWaypoints();
  delete route;
  return count;
}

/* luagmp (func)
*
* Retrieve world position of a waypoint by name.
*
* @version  0.3.0
* @name     getWaypoint
* @side     client
* @category World
* @param    (string) name       Waypoint name.
* @return   ({x, y, z}|nil)     Waypoint position or nil.
*
*/
sol::object Function_GetWaypoint(const std::string& waypoint_name, sol::this_state ts) {
  sol::state_view lua(ts);
  zCWayNet* way_net = GetWayNet();
  if (!way_net) {
    return sol::nil;
  }

  zSTRING name(waypoint_name.c_str());
  zCWaypoint* waypoint = way_net->GetWaypoint(name);
  if (!waypoint) {
    return sol::nil;
  }

  return sol::make_object(lua, MakeVec3Table(lua, waypoint->GetPositionWorld()));
}

/* luagmp (func)
*
* Retrieve nearest waypoint for a given position.
*
* @version  0.3.0
* @name     getNearestWaypoint
* @side     client
* @category World
* @param    (number) x                Position X.
* @param    (number) y                Position Y.
* @param    (number) z                Position Z.
* @param    (number|nil) distance     Optional maximum search distance.
* @return   ({name, x, y, z}|nil)     Waypoint information or nil.
*
*/
sol::object Function_GetNearestWaypoint(float x, float y, float z, sol::optional<float> distance, sol::this_state ts) {
  sol::state_view lua(ts);
  zCWayNet* way_net = GetWayNet();
  if (!way_net) {
    return sol::nil;
  }

  const bool limit_distance = distance.has_value() && distance.value() > 0.0f;
  const float max_distance_sq = distance.value_or(-1.0f) * distance.value_or(-1.0f);
  zCWaypoint* nearest = nullptr;
  float nearest_sq = std::numeric_limits<float>::max();

  for (zCListSort<zCWaypoint>* node = way_net->wplist.next; node; node = node->next) {
    zCWaypoint* waypoint = node->data;
    if (!waypoint) {
      continue;
    }

    const zVEC3 position = waypoint->GetPositionWorld();
    const float dx = position[VX] - x;
    const float dy = position[VY] - y;
    const float dz = position[VZ] - z;
    const float distance_sq = (dx * dx) + (dy * dy) + (dz * dz);
    if (limit_distance && distance_sq > max_distance_sq) {
      continue;
    }

    if (distance_sq < nearest_sq) {
      nearest_sq = distance_sq;
      nearest = waypoint;
    }
  }

  if (!nearest) {
    return sol::nil;
  }

  return sol::make_object(lua, MakeWaypointTable(lua, nearest));
}

/* luagmp (func)
*
* Retrieve second nearest waypoint for a given position.
*
* @version  0.3.0
* @name     getNextNearestWaypoint
* @side     client
* @category World
* @param    (number) x                Position X.
* @param    (number) y                Position Y.
* @param    (number) z                Position Z.
* @return   ({name, x, y, z}|nil)     Waypoint information or nil.
*
*/
sol::object Function_GetNextNearestWaypoint(float x, float y, float z, sol::this_state ts) {
  sol::state_view lua(ts);
  zCWayNet* way_net = GetWayNet();
  if (!way_net) {
    return sol::nil;
  }

  zCWaypoint* nearest = nullptr;
  zCWaypoint* second_nearest = nullptr;
  float nearest_sq = std::numeric_limits<float>::max();
  float second_nearest_sq = std::numeric_limits<float>::max();

  for (zCListSort<zCWaypoint>* node = way_net->wplist.next; node; node = node->next) {
    zCWaypoint* waypoint = node->data;
    if (!waypoint) {
      continue;
    }

    const zVEC3 position = waypoint->GetPositionWorld();
    const float dx = position[VX] - x;
    const float dy = position[VY] - y;
    const float dz = position[VZ] - z;
    const float distance_sq = (dx * dx) + (dy * dy) + (dz * dz);
    if (distance_sq < nearest_sq) {
      second_nearest = nearest;
      second_nearest_sq = nearest_sq;
      nearest = waypoint;
      nearest_sq = distance_sq;
      continue;
    }

    if (distance_sq < second_nearest_sq) {
      second_nearest = waypoint;
      second_nearest_sq = distance_sq;
    }
  }

  if (!second_nearest) {
    return sol::nil;
  }

  return sol::make_object(lua, MakeWaypointTable(lua, second_nearest));
}

/* luagmp (func)
*
* Retrieve list of all waypoints from current world.
*
* @version  0.3.0
* @name     getWaypoints
* @side     client
* @category World
* @return   ({...})                 Array of waypoint tables {name, x, y, z}.
*
*/
sol::table Function_GetWaypoints(sol::this_state ts) {
  sol::state_view lua(ts);
  sol::table waypoints = lua.create_table();
  zCWayNet* way_net = GetWayNet();
  if (!way_net) {
    return waypoints;
  }

  int index = 1;
  for (zCListSort<zCWaypoint>* node = way_net->wplist.next; node; node = node->next) {
    zCWaypoint* waypoint = node->data;
    if (!waypoint) {
      continue;
    }

    waypoints[index++] = MakeWaypointTable(lua, waypoint);
  }

  return waypoints;
}

void BindWay(sol::state& lua) {
  sol::usertype<LuaWay> way_type = lua.new_usertype<LuaWay>(
      "Way", sol::constructors<LuaWay(const std::string&, const std::string&)>());

  way_type["getStart"] = &LuaWay::getStart;
  way_type["getEnd"] = &LuaWay::getEnd;
  way_type["getWaypoints"] = &LuaWay::getWaypoints;
  way_type["getCountWaypoints"] = &LuaWay::getCountWaypoints;

  lua["toggleDrawWaynet"] = Function_ToggleDrawWaynet;
  
  lua["getWaypoint"] = Function_GetWaypoint;
  lua["getNearestWaypoint"] = Function_GetNearestWaypoint;
  lua["getNextNearestWaypoint"] = Function_GetNextNearestWaypoint;
  lua["getWaypoints"] = Function_GetWaypoints;
}

}  // namespace gmp::gothic