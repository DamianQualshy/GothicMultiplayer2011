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

#include "lua_interface.h"
#include "gothic-patches/hud_control.h"

#include "ZenGin/zGothicAPI.h"

namespace gmp::gothic {

/* luagmp (func)
*
* This function will convert pixels to virtuals screen X dimension and return it as a result.
* Virtuals are special type of unit used by the game to position UI elements independent from game resolution.
*
* @version  0.3.0
* @name     anx
* @side     client
* @category Interface
* @note     Use this function only when you want to convert x position or width on the screen.
* @note     Virtual screen coordinates are similar to percentage values, where 0 is 0% and 8192 is 100%.
* @param    (number) pixels  The pixels to convert.
* @return   (number)         The virtuals after conversion.
*
*/
int Function_ANX(int pixels) {
  if (!screen) {
    return 0;
  }
  return screen->anx(pixels);
};

/* luagmp (func)
*
* This function will convert pixels to virtuals on screen Y dimension and return it as a result.
* Virtuals are special type of unit used by the game to position UI elements independent from game resolution.
*
* @version  0.3.0
* @name     any
* @side     client
* @category Interface
* @note     Use this function only when you want to convert y position or width on the screen.
* @note     Virtual screen coordinates are similar to percentage values, where 0 is 0% and 8192 is 100%.
* @param    (number) pixels  The pixels to convert.
* @return   (number)         The virtuals after conversion.
*
*/
int Function_ANY(int pixels) {
  if (!screen) {
    return 0;
  }
  return screen->any(pixels);
};

/* luagmp (func)
*
* This function will convert virtuals to pixels on screen X dimension and return it as a result.
*
* @version  0.3.0
* @name     nax
* @side     client
* @category Interface
* @note     Use this function only when you want to convert x position or width on the screen.
* @param    (number) virtuals  The virtuals to convert.
* @return   (number)           The pixels after conversion.
*
*/
int Function_NAX(int virtuals) {
  if (!screen) {
    return 0;
  }
  return screen->nax(virtuals);
};

/* luagmp (func)
*
* This function will convert virtuals to pixels on screen Y dimension and return it as a result.
*
* @version  0.3.0
* @name     nay
* @side     client
* @category Interface
* @note     Use this function only when you want to convert y position or width on the screen.
* @param    (number) virtuals  The virtuals to convert.
* @return   (number)           The pixels after conversion.
*
*/
int Function_NAY(int virtuals) {
  if (!screen) {
    return 0;
  }
  return screen->nay(virtuals);
};

/* luagmp (func)
*
* This function will return the current game resolution.
*
* @version  0.3.0
* @name     getResolution
* @side     client
* @category Interface
* @return   ({x, y})  Table containing width and height.
*
*/
sol::object Function_GetResolution(sol::this_state ts) {
  sol::state_view lua(ts);
  sol::table resolution = lua.create_table();

  int width = 800;
  int height = 600;
  if (zrenderer) {
    width = zrenderer->vid_xdim;
    height = zrenderer->vid_ydim;
  }

  resolution["x"] = width;
  resolution["y"] = height;
  return resolution;
}

/* luagmp (func)
*
* This function will return the current frame rate estimate.
*
* @version  0.3.0
* @name     getFpsRate
* @side     client
* @category Interface
* @return   (number)  Frames per second, or 0 if unavailable.
*
*/
std::int32_t Function_GetFpsRate() {
  if (!ztimer || ztimer->frameTimeFloatSecs <= 0.0f) {
    return 0;
  }

  return static_cast<std::int32_t>(std::lround(1.0f / ztimer->frameTimeFloatSecs));
}

/* luagmp (func)
*
* This function will enable or disable the Gothic HUD.
*
* @version  0.3.0
* @name     enableHud
* @side     client
* @category Interface
* @param    (number) hud_type  HUD type constant.
* @param    (boolean) enabled  New HUD state.
* @return   (boolean)          True on success.
*
*/
bool Function_EnableHud(std::int32_t hud_type, bool enabled) {
  return SetHudEnabled(hud_type, enabled);
}

/* luagmp (func)
*
* This function will return whether the Gothic HUD is enabled.
*
* @version  0.3.0
* @name     isHudEnabled
* @side     client
* @category Interface
* @param    (number) hud_type  HUD type constant.
* @return   (boolean|nil)      HUD state, or nil for unsupported HUD types.
*
*/
sol::object Function_IsHudEnabled(std::int32_t hud_type, sol::this_state ts) {
  sol::state_view lua(ts);
  auto enabled = GetHudEnabled(hud_type);
  if (!enabled.has_value()) {
    return sol::nil;
  }

  return sol::make_object(lua, *enabled);
}

/* luagmp (func)
*
* This function will check whether the Gothic console is open.
*
* @version  0.3.0
* @name     isConsoleOpen
* @side     client
* @category Interface
* @return   (boolean)  True when the console is visible.
*
*/
bool Function_IsConsoleOpen() {
  return zcon && zcon->IsVisible() != 0;
}

void BindInterface(sol::state& lua) {
  lua["anx"] = Function_ANX;
  lua["any"] = Function_ANY;
  lua["nax"] = Function_NAX;
  lua["nay"] = Function_NAY;
  
  lua["getResolution"] = Function_GetResolution;
  lua["getFpsRate"] = Function_GetFpsRate;
  lua["enableHud"] = Function_EnableHud;
  lua["isHudEnabled"] = Function_IsHudEnabled;
  lua["isConsoleOpen"] = Function_IsConsoleOpen;

  // Constants
  lua["HUD_ALL"] = kHudAll;
  lua["HUD_HEALTH_BAR"] = kHudHealthBar;
  lua["HUD_MANA_BAR"] = kHudManaBar;
  lua["HUD_SWIM_BAR"] = kHudSwimBar;
  lua["HUD_FOCUS_BAR"] = kHudFocusBar;
}

} // namespace gmp::gothic


/* luagmp (const)
*
* Represents the whole Gothic player status HUD.
*
* @category HUD
* @side     client
* @name     HUD_ALL
*
*/

/* luagmp (const)
*
* Represents the health status bar.
*
* @category HUD
* @side     client
* @name     HUD_HEALTH_BAR
*
*/

/* luagmp (const)
*
* Represents the mana status bar.
*
* @category HUD
* @side     client
* @name     HUD_MANA_BAR
*
*/

/* luagmp (const)
*
* Represents the swim and dive breath status bar.
*
* @category HUD
* @side     client
* @name     HUD_SWIM_BAR
*
*/

/* luagmp (const)
*
* Represents the focused NPC health status bar.
*
* @category HUD
* @side     client
* @name     HUD_FOCUS_BAR
*
*/
