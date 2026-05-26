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

#include <string>

#include "sol/sol.hpp"

namespace gmp::gothic {

class LuaMusicTheme {
public:
  static bool loadTheme(const std::string& file_name);
  static bool playTheme();
  static void stopTheme();

  static std::string getFileName();
  static void setFileName(const std::string& file_name);

  static float getVolume();
  static void setVolume(float volume);

  static bool getLoop();
  static void setLoop(bool loop);

  static bool getDisabled();
  static void setDisabled(bool disabled);

  static sol::object getActiveTheme(sol::this_state ts);
  static void setActiveTheme(sol::object value);

  static float getReverbMix();
  static void setReverbMix(float value);

  static float getReverbTime();
  static void setReverbTime(float value);

  static int getTransType();
  static void setTransType(int value);

  static int getTransSubType();
  static void setTransSubType(int value);

  static std::string getName();
  static void setName(const std::string& name);
};

void BindMusicTheme(sol::state& lua);

}  // namespace gmp::gothic
