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
#include "net_game.h"
#include "sky_utils.h"

#include <optional>
#include <string>

namespace gmp::gothic {
namespace {

struct Color3 {
  int r;
  int g;
  int b;
};

struct Color4 {
  int r;
  int g;
  int b;
  int a;
};

std::optional<RainStartTime> ReadTimeTable(const sol::object& value) {
  if (!value.is<sol::table>()) {
    return std::nullopt;
  }

  sol::table table = value.as<sol::table>();
  sol::object hour = table["hour"];
  sol::object min = table["min"];
  if (!hour.is<int>() || !min.is<int>()) {
    return std::nullopt;
  }

  return RainStartTime{hour.as<int>(), min.as<int>()};
}

std::optional<Color3> ReadColor3Table(const sol::table& table) {
  sol::object r = table["r"];
  sol::object g = table["g"];
  sol::object b = table["b"];
  if (!r.is<int>() || !g.is<int>() || !b.is<int>()) {
    return std::nullopt;
  }

  return Color3{r.as<int>(), g.as<int>(), b.as<int>()};
}

std::optional<Color4> ReadColor4Table(const sol::table& table) {
  auto color = ReadColor3Table(table);
  sol::object a = table["a"];
  if (!color.has_value() || !a.is<int>()) {
    return std::nullopt;
  }

  return Color4{color->r, color->g, color->b, a.as<int>()};
}

sol::object MakeTimeTable(sol::this_state ts, std::optional<RainStartTime> time) {
  sol::state_view lua(ts);
  sol::table tbl = lua.create_table();
  tbl["hour"] = time.has_value() ? time->hour : 0;
  tbl["min"] = time.has_value() ? time->min : 0;
  return sol::make_object(lua, tbl);
}

}  // namespace

/* luagmp (class)
*
* Static sky controller for local and synchronized sky settings.
*
* @version  0.3.0
* @name     Sky
* @side     client
* @category World
*
*/
class LuaSky {
public:

/* luagmp (property)
*
* Current local weather type. Use `refresh()` to pull the authoritative value from the server. For more information, see [Weather Constants](../../shared-constants/Weather.md).
*
* @name     weatherType
* @return   (number)
*
*/
  void SetWeatherType(int weather_type) const { ApplyWeatherType(weather_type); }
  int GetWeatherType() const { return gmp::gothic::GetWeatherType(); }

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
      gmp::gothic::SetRainStartTime(time->hour, time->min);
    }
  }

  sol::object GetRainStartTime(sol::this_state ts) const {
    return MakeTimeTable(ts, gmp::gothic::GetRainStartTime());
  }

/* luagmp (property)
*
* Rain stop time: `{hour = number, min = number}`
*
* @name     rainStopTime
* @return   (table)
*
*/
  void SetRainStopTime(const sol::object& value) const {
    if (auto time = ReadTimeTable(value)) {
      gmp::gothic::SetRainStopTime(time->hour, time->min);
    }
  }

  sol::object GetRainStopTime(sol::this_state ts) const {
    return MakeTimeTable(ts, gmp::gothic::GetRainStopTime());
  }

/* luagmp (property)
*
* Wind scale used during precipitation.
*
* @name     windScale
* @return   (number)
*
*/
  void SetWindScale(float wind_scale) const { gmp::gothic::SetWindScale(wind_scale); }
  float GetWindScale() const { return gmp::gothic::GetWindScale(); }

/* luagmp (property)
*
* Toggles local rain and snow rendering.
*
* @name     dontRain
* @return   (boolean)
*
*/
  void SetDontRain(bool toggle) const { gmp::gothic::SetDontRain(toggle); }
  bool GetDontRain() const { return gmp::gothic::GetDontRain(); }

/* luagmp (property)
*
* Write-only fog color table: `{id = number, r = number, g = number, b = number}`
*
* @name     fogColor
* @return   (table)
*
*/
  void SetFogColor(const sol::object& value) const {
    auto table = ToTable(value);
    if (!table.has_value()) {
      return;
    }

    sol::object id = (*table)["id"];
    if (auto color = ReadColor3Table(*table); color.has_value() && id.is<int>()) {
      gmp::gothic::SetFogColor(id.as<int>(), color->r, color->g, color->b);
    }
  }

/* luagmp (property)
*
* Write-only cloud color table: `{r = number, g = number, b = number}`
*
* @name     cloudsColor
* @return   (table)
*
*/
  void SetCloudsColor(const sol::object& value) const {
    auto table = ToTable(value);
    if (!table.has_value()) {
      return;
    }

    if (auto color = ReadColor3Table(*table)) {
      gmp::gothic::SetCloudsColor(color->r, color->g, color->b);
    }
  }

/* luagmp (property)
*
* Write-only planet size table: `{planetId = number, size = number}`. For planet identifiers, see [Sky Constants](../../client-constants/Sky.md).
*
* @name     planetSize
* @return   (table)
*
*/
  void SetPlanetSize(const sol::object& value) const {
    auto table = ToTable(value);
    if (!table.has_value()) {
      return;
    }

    sol::object planet_id = (*table)["planetId"];
    sol::object size = (*table)["size"];
    if (planet_id.is<int>() && size.is<float>()) {
      gmp::gothic::SetPlanetSize(planet_id.as<int>(), size.as<float>());
    }
  }

/* luagmp (property)
*
* Write-only planet color table: `{planetId = number, r = number, g = number, b = number, a = number}`. For planet identifiers, see [Sky Constants](../../client-constants/Sky.md).
*
* @name     planetColor
* @return   (table)
*
*/
  void SetPlanetColor(const sol::object& value) const {
    auto table = ToTable(value);
    if (!table.has_value()) {
      return;
    }

    sol::object planet_id = (*table)["planetId"];
    if (auto color = ReadColor4Table(*table); color.has_value() && planet_id.is<int>()) {
      gmp::gothic::SetPlanetColor(planet_id.as<int>(), color->r, color->g, color->b, color->a);
    }
  }

/* luagmp (property)
*
* Write-only planet texture table: `{planetId = number, texture = string}`. For planet identifiers, see [Sky Constants](../../client-constants/Sky.md).
*
* @name     planetTxt
* @return   (table)
*
*/
  void SetPlanetTxt(const sol::object& value) const {
    auto table = ToTable(value);
    if (!table.has_value()) {
      return;
    }

    sol::object planet_id = (*table)["planetId"];
    sol::object texture = (*table)["texture"];
    if (planet_id.is<int>() && texture.is<std::string>()) {
      gmp::gothic::SetPlanetTexture(planet_id.as<int>(), texture.as<std::string>());
    }
  }

/* luagmp (property)
*
* Write-only lighting color table: `{r = number, g = number, b = number}`
*
* @name     lightingColor
* @return   (table)
*
*/
  void SetLightingColor(const sol::object& value) const {
    auto table = ToTable(value);
    if (!table.has_value()) {
      return;
    }

    if (auto color = ReadColor3Table(*table)) {
      gmp::gothic::SetLightingColor(color->r, color->g, color->b);
    }
  }

/* luagmp (method)
*
* Requests authoritative time and sky settings from the server and applies them when the reply arrives.
*
* @version  0.3.0
* @name     refresh
* @side     client
* @category Sky
* @return   (boolean) True if the refresh request was sent.
*
*/
  static bool Refresh() {
    auto& net_game = NetGame::Instance();
    if (!net_game.IsConnected()) {
      return false;
    }

    net_game.SyncGameTime();
    return true;
  }

  sol::object GetWriteOnly(sol::this_state ts) const {
    sol::state_view lua(ts);
    return sol::object(lua, sol::in_place, sol::nil);
  }

private:
  static std::optional<sol::table> ToTable(const sol::object& value) {
    if (!value.is<sol::table>()) {
      return std::nullopt;
    }

    return value.as<sol::table>();
  }
};

void BindSky(sol::state& lua) {
  auto sky_type = lua.new_usertype<LuaSky>("Sky", sol::no_constructor);

  sky_type["weatherType"] = sol::property(&LuaSky::GetWeatherType, &LuaSky::SetWeatherType);
  sky_type["rainStartTime"] = sol::property(&LuaSky::GetRainStartTime, &LuaSky::SetRainStartTime);
  sky_type["rainStopTime"] = sol::property(&LuaSky::GetRainStopTime, &LuaSky::SetRainStopTime);
  sky_type["windScale"] = sol::property(&LuaSky::GetWindScale, &LuaSky::SetWindScale);
  sky_type["dontRain"] = sol::property(&LuaSky::GetDontRain, &LuaSky::SetDontRain);
  sky_type["fogColor"] = sol::property(&LuaSky::GetWriteOnly, &LuaSky::SetFogColor);
  sky_type["cloudsColor"] = sol::property(&LuaSky::GetWriteOnly, &LuaSky::SetCloudsColor);
  sky_type["planetSize"] = sol::property(&LuaSky::GetWriteOnly, &LuaSky::SetPlanetSize);
  sky_type["planetColor"] = sol::property(&LuaSky::GetWriteOnly, &LuaSky::SetPlanetColor);
  sky_type["planetTxt"] = sol::property(&LuaSky::GetWriteOnly, &LuaSky::SetPlanetTxt);
  sky_type["lightingColor"] = sol::property(&LuaSky::GetWriteOnly, &LuaSky::SetLightingColor);

  sky_type["refresh"] = &LuaSky::Refresh;

  lua["Sky"] = LuaSky{};
}

} // namespace gmp::gothic
