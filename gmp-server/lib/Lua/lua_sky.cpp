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

#include "lua_sky.h"
#include "game_server.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace lua::bindings {
namespace {

std::optional<std::pair<int, int>> ReadTimeTable(const sol::object& value) {
  if (!value.is<sol::table>()) {
    return std::nullopt;
  }

  sol::table table = value.as<sol::table>();
  sol::object hour = table["hour"];
  sol::object min = table["min"];
  if (!hour.is<int>() || !min.is<int>()) {
    return std::nullopt;
  }

  return std::pair<int, int>{hour.as<int>(), min.as<int>()};
}

sol::object MakeTimeTable(sol::this_state ts, std::pair<std::int32_t, std::int32_t> time) {
  sol::state_view lua(ts);
  sol::table tbl = lua.create_table();
  tbl["hour"] = time.first;
  tbl["min"] = time.second;
  return sol::make_object(lua, tbl);
}

} // namespace


/* luagmp (class)
*
* Static sky controller for authoritative server weather.
*
* @version  0.3.0
* @name     Sky
* @side     server
* @category World
*
*/
class LuaSky {
public:

/* luagmp (property)
*
* Current server weather type. When automatic weather is enabled, this can be overwritten on the next server game minute. For more information, see [Weather Constants](../../shared-constants/Weather.md).
*
* @name     weatherType
* @return   (number)
*
*/
  void SetWeatherType(int weather_type) const { g_server->SetWeatherType(weather_type); }
  int GetWeatherType() const { return g_server->GetWeatherType(); }

/* luagmp (property)
*
* Rain start time: `{hour = number, min = number}`
*
* @name     rainStartTime
* @return   (table)
*
*/
  void SetRainStartTime(const sol::object& value) const {
    if (auto time = ReadTimeTable(value)) {
      g_server->SetRainStartTime(time->first, time->second);
    }
  }

  sol::object GetRainStartTime(sol::this_state ts) const {
    return MakeTimeTable(ts, g_server->GetRainStartTime());
  }

/* luagmp (property)
*
* Rain stop time: `{hour = number, min = number}`
*
* @note     The stop time must be after `rainStartTime` in Gothic sky time.
* @name     rainStopTime
* @return   (table)
*
*/
  void SetRainStopTime(const sol::object& value) const {
    if (auto time = ReadTimeTable(value)) {
      g_server->SetRainStopTime(time->first, time->second);
    }
  }

  sol::object GetRainStopTime(sol::this_state ts) const {
    return MakeTimeTable(ts, g_server->GetRainStopTime());
  }

/* luagmp (property)
*
* Wind scale used during precipitation.
*
* @name     windScale
* @return   (number)
*
*/
  void SetWindScale(float wind_scale) const { g_server->SetWindScale(wind_scale); }
  float GetWindScale() const { return g_server->GetWindScale(); }

/* luagmp (property)
*
* Disables rain and snow rendering while leaving automatic weather timing active.
*
* @name     dontRain
* @return   (boolean)
*
*/
  void SetDontRain(bool toggle) const { g_server->SetDontRain(toggle); }
  bool GetDontRain() const { return g_server->GetDontRain(); }

/* luagmp (property)
*
* Toggles the server automatic weather state machine. Set this to true before manually controlling weather.
*
* @name     disabled
* @return   (boolean)
*
*/
  void SetDisabled(bool toggle) const { g_server->SetWeatherDisabled(toggle); }
  bool GetDisabled() const { return g_server->GetWeatherDisabled(); }
};

void BindSky(sol::state& lua) {
  auto sky_type = lua.new_usertype<LuaSky>("Sky", sol::no_constructor);

  sky_type["weatherType"] = sol::property(&LuaSky::GetWeatherType, &LuaSky::SetWeatherType);
  sky_type["rainStartTime"] = sol::property(&LuaSky::GetRainStartTime, &LuaSky::SetRainStartTime);
  sky_type["rainStopTime"] = sol::property(&LuaSky::GetRainStopTime, &LuaSky::SetRainStopTime);
  sky_type["windScale"] = sol::property(&LuaSky::GetWindScale, &LuaSky::SetWindScale);
  sky_type["dontRain"] = sol::property(&LuaSky::GetDontRain, &LuaSky::SetDontRain);
  sky_type["disabled"] = sol::property(&LuaSky::GetDisabled, &LuaSky::SetDisabled);

  lua["Sky"] = LuaSky{};
}

} // namespace lua::bindings
