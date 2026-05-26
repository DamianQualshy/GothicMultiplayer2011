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

#pragma once

#include <cstdint>
#include <string>

#include "sol/sol.hpp"

#include "lua_vob.h"

namespace gmp::gothic {

class LuaCamera {
public:
  static bool setMode(const std::string& mode);
  static std::string getMode();

  static void setPosition(float x, float y, float z);
  static void setPositionValue(sol::object value);
  static sol::table getPosition(sol::this_state ts);

  static void setRotation(float x, float y, float z);
  static void setRotationValue(sol::object value);
  static sol::table getRotation(sol::this_state ts);

  static bool setTargetVob(const LuaVob& vob);
  static bool setTargetPlayer(std::int64_t player_id);

  static void setFOV(float fov);
  static float getFOV();

  static bool getModeChangeEnabled();
  static void setModeChangeEnabled(bool enabled);

  static bool getMovementEnabled();
  static void setMovementEnabled(bool enabled);
  static void ApplyMovementLock();

  static sol::object getTargetVob(sol::this_state ts);
  static void setTargetVobValue(sol::object value);
};

void BindCamera(sol::state& lua);

}  // namespace gmp::gothic
