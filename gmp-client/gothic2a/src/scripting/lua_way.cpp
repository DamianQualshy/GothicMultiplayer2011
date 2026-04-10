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
* @name     setDrawWaynet
* @side     client
* @category World
* @param    (boolean) enabled    True to render waynet, false to disable.
*
*/
void Function_SetDrawWaynet(bool enabled) {
  if (!ogame) {
    return;
  }

  ogame->SetDrawWaynet(enabled);
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
  }

  while (zCWaypoint* waypoint = route->GetNextWP()) {
    result.emplace_back(waypoint->GetName().ToChar());
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

void BindWay(sol::state& lua) {
  sol::usertype<LuaWay> way_type = lua.new_usertype<LuaWay>(
      "Way", sol::constructors<LuaWay(const std::string&, const std::string&)>());

  way_type["getStart"] = &LuaWay::getStart;
  way_type["getEnd"] = &LuaWay::getEnd;
  way_type["getWaypoints"] = &LuaWay::getWaypoints;
  way_type["getCountWaypoints"] = &LuaWay::getCountWaypoints;

  lua["setDrawWaynet"] = Function_SetDrawWaynet;
}

}  // namespace gmp::gothic