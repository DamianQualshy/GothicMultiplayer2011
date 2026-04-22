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

namespace lua::bindings {
namespace {

} // namespace

/* luagmp (func)
*
* This function will set the desired weather type immediately.
*
* @version  0.3.0
* @name     setWeatherType
* @side     server
* @category Weather
* @param    (number) weather_type     Weather type, for more information see [Weather Constants](../../shared-constants/Weather.md)
*
*/
void Function_SetWeatherType(int weather_type) {
  g_server->SetWeatherType(weather_type);
}

/* luagmp (func)
*
* This function will return the current weather type.
*
* @version  0.3.0
* @name     getWeatherType
* @side     server
* @category Weather
* @return   (number)      Current weather type.
*
*/
int Function_GetWeatherType() {
  return g_server->GetWeatherType();
}

/* luagmp (func)
*
* This function will set the sky weather time when it starts raining/snowing.
*
* @version  0.3.0
* @name     setRainStartTime
* @side     server
* @category Weather
* @param    (number) hour   The sky weather raining start hour.
* @param    (number) min    The sky weather raining start min.
*
*/
void Function_SetRainStartTime(int hour, int min) {
  g_server->SetRainStartTime(hour, min);
}

/* luagmp (func)
*
* This function will return the sky weather time when it starts raining/snowing.
*
* @version  0.3.0
* @name     getRainStartTime
* @side     server
* @category Weather
* @return   ({hour, min})  The sky weather raining start time.
*
*/
sol::object Function_GetRainStartTime(sol::this_state ts) {
  sol::state_view lua(ts);
  auto time = g_server->GetRainStartTime();
  sol::table tbl = lua.create_table();
  tbl["hour"] = time.first;
  tbl["min"] = time.second;
  return tbl;
}

/* luagmp (func)
*
* This function will change the wind scale used during raining/snowing.
*
* @version  0.3.0
* @name     setWindScale
* @side     server
* @category Weather
* @param    (number) wind_scale    Wind scale value.
*
*/
void Function_SetWindScale(float wind_scale) {
  g_server->SetWindScale(wind_scale);
}

/* luagmp (func)
*
* This function will return the current wind scale.
*
* @version  0.3.0
* @name     getWindScale
* @side     server
* @category Weather
* @return   (number)    Current wind scale.
*
*/
float Function_GetWindScale() {
  return g_server->GetWindScale();
}

/* luagmp (func)
*
* This function will enable/disable weather completely.
*
* @version  0.3.0
* @name     setDontRain
* @side     server
* @category Weather
* @param    (boolean) toggle   True to disable weather, false to enable it.
*
*/
bool Function_SetDontRain(bool toggle) {
  return g_server->SetDontRain(toggle);
}


void BindSky(sol::state& lua) {
  lua["setWeatherType"] = Function_SetWeatherType;
  lua["getWeatherType"] = Function_GetWeatherType;
  lua["setRainStartTime"] = Function_SetRainStartTime;
  lua["getRainStartTime"] = Function_GetRainStartTime;
  lua["setWindScale"] = Function_SetWindScale;
  lua["getWindScale"] = Function_GetWindScale;
  lua["setDontRain"] = Function_SetDontRain;
}

} // namespace lua::bindings