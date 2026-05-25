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
#include "sky_utils.h"

namespace gmp::gothic {

/* luagmp (func)
*
* This function will set the desired weather type immediately.
*
* @version  0.3.0
* @name     setWeatherType
* @side     client
* @category Weather
* @param    (number) weather_type     Weather type (WEATHER_SNOW/WEATHER_RAIN or 0 to disable precipitation).
*
*/
void Function_SetWeatherType(int weather_type) {
  ApplyWeatherType(weather_type);
}

/* luagmp (func)
*
* This function will return the current weather type.
*
* @version  0.3.0
* @name     getWeatherType
* @side     client
* @category Weather
* @return   (number)   Current weather type.
*
*/
int Function_GetWeatherType() {
  return GetWeatherType();
}

/* luagmp (func)
*
* This function will set the sky weather time when it starts raining/snowing.
*
* @version  0.3.0
* @name     setRainStartTime
* @side     client
* @category Weather
* @param    (number) hour   The sky weather raining start hour.
* @param    (number) min    The sky weather raining start min.
*
*/
void Function_SetRainStartTime(int hour, int min) {
  SetRainStartTime(hour, min);
}

/* luagmp (func)
*
* This function will return the configured sky weather time when it starts raining/snowing.
*
* @version  0.3.0
* @name     getRainStartTime
* @side     client
* @category Weather
* @return   ({hour, min})  The sky weather raining start time.
*
*/
sol::object Function_GetRainStartTime(sol::this_state ts) {
  sol::state_view lua(ts);
  sol::table tbl = lua.create_table();
  auto time = GetRainStartTime();
  if (time.has_value()) {
    tbl["hour"] = time->hour;
    tbl["min"] = time->min;
  } else {
    tbl["hour"] = 0;
    tbl["min"] = 0;
  }
  return sol::make_object(lua, tbl);
}

/* luagmp (func)
*
* This function will change the wind scale used during raining/snowing.
*
* @version  0.3.0
* @name     setWindScale
* @side     client
* @category Weather
* @param    (number) wind_scale    Wind scale value.
*
*/
void Function_SetWindScale(float wind_scale) {
  SetWindScale(wind_scale);
}

/* luagmp (func)
*
* This function will return the current wind scale.
*
* @version  0.3.0
* @name     getWindScale
* @side     client
* @category Weather
* @return   (number)   Current wind scale.
*
*/
float Function_GetWindScale() {
  return GetWindScale();
}

/* luagmp (func)
*
* This function will enable/disable weather completely.
*
* @version  0.3.0
* @name     setDontRain
* @side     client
* @category Weather
* @param    (boolean) toggle   True to disable weather, false to enable it.
* @return   (boolean)          True on success.
*
*/
bool Function_SetDontRain(bool toggle) {
  return SetDontRain(toggle);
}

/* luagmp (func)
*
* This function will set the sky fog color day variation.
*
* @version  0.3.0
* @name     setFogColor
* @side     client
* @category Sky
* @param    (number) id   The id of fog color day variation.
* @param    (number) r    The red color component in RGB model.
* @param    (number) g    The green color component in RGB model.
* @param    (number) b    The blue color component in RGB model.
*
*/
void Function_SetFogColor(int id, int r, int g, int b) {
  SetFogColor(id, r, g, b);
}

/* luagmp (func)
*
* This function will set the sky clouds color.
*
* @version  0.3.0
* @name     setCloudsColor
* @side     client
* @category Sky
* @param    (number) r    The red color component in RGB model.
* @param    (number) g    The green color component in RGB model.
* @param    (number) b    The blue color component in RGB model.
*
*/
void Function_SetCloudsColor(int r, int g, int b) {
  SetCloudsColor(r, g, b);
}

/* luagmp (func)
*
* This function will set the planet size ratio.
*
* @version  0.3.0
* @name     setPlanetSize
* @side     client
* @category Sky
* @param    (number) planetId    The planet id, for more information see [Planet](../../client-constants/Sky.md) constants.
* @param    (number) size        The size ratio.
*
*/
void Function_SetPlanetSize(int planet_id, float size) {
  SetPlanetSize(planet_id, size);
}

/* luagmp (func)
*
* This function will set the planet color.
*
* @version  0.3.0
* @name     setPlanetColor
* @side     client
* @category Sky
* @param    (number) planetId  The planet id, for more information see [Planet](../../client-constants/Sky.md) constants.
* @param    (number) r         The red color component in RGBA model.
* @param    (number) g         The green color component in RGBA model.
* @param    (number) b         The blue color component in RGBA model.
* @param    (number) a         The alpha color component in RGBA model.
*
*/
void Function_SetPlanetColor(int planet_id, int r, int g, int b, int a) {
  SetPlanetColor(planet_id, r, g, b, a);
}

/* luagmp (func)
*
* Set the planet texture.
*
* @version  0.3.0
* @name     setPlanetTxt
* @side     client
* @category Sky
* @param    (number) planetId   The planet id, for more information see [Planet](../../client-constants/Sky.md) constants.
* @param    (string) texture Name of the texture.
*
*/
void Function_SetPlanetTxt(int planet_id, const std::string& texture) {
  SetPlanetTexture(planet_id, texture);
}

/* luagmp (func)
*
* This function will set the sky lighting color.
*
* @version  0.3.0
* @name     setLightingColor
* @side     client
* @category Sky
* @param    (number) r    The red color component in RGB model.
* @param    (number) g    The green color component in RGB model.
* @param    (number) b    The blue color component in RGB model.
*
*/
void Function_SetLightingColor(int r, int g, int b) {
  SetLightingColor(r, g, b);
}


void BindSky(sol::state& lua) {
  lua["setWeatherType"] = Function_SetWeatherType;
  lua["getWeatherType"] = Function_GetWeatherType;

  lua["setRainStartTime"] = Function_SetRainStartTime;
  lua["getRainStartTime"] = Function_GetRainStartTime;

  lua["setWindScale"] = Function_SetWindScale;
  lua["getWindScale"] = Function_GetWindScale;

  lua["setDontRain"] = Function_SetDontRain;

  lua["setFogColor"] = Function_SetFogColor;
  //lua["getFogColor"] = Function_GetFogColor;

  lua["setCloudsColor"] = Function_SetCloudsColor;
  //lua["getCloudsColor"] = Function_GetCloudsColor;

  lua["setPlanetSize"] = Function_SetPlanetSize;
  //lua["getPlanetSize"] = Function_GetPlanetSize;

  lua["setPlanetColor"] = Function_SetPlanetColor;
  //lua["getPlanetColor"] = Function_GetPlanetColor;

  lua["setPlanetTxt"] = Function_SetPlanetTxt;
  //lua["getPlanetTxt"] = Function_GetPlanetTxt;

  lua["setLightingColor"] = Function_SetLightingColor;
  //lua["getLightingColor"] = Function_GetLightingColor;
}

} // namespace gmp::gothic
