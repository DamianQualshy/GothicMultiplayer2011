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
#include <unordered_set>
#include <vector>

#include "ZenGin/zGothicAPI.h"
#include "sol/sol.hpp"

namespace gmp::gothic {

class LuaDraw3dView;

class LuaDraw3d {
public:
  LuaDraw3d();
  LuaDraw3d(float x, float y, float z);
  LuaDraw3d(float x, float y, float z, const std::string& text);
  ~LuaDraw3d();

  static void CleanupViews();
  static void RenderActiveDraws();

  void setPosition(float x, float y, float z);
  void setPositionValue(sol::object value);
  sol::table getPosition(sol::this_state s) const;

  void insertText(const std::string& text);
  void removeText(int index);
  void updateText(int index, const std::string& text);
  void clearText();
  sol::table getText(sol::this_state s) const;
  void setText(const std::string& text);

  void setFont(const std::string& font_name);
  std::string getFont() const;

  void setColor(int r, int g, int b);
  void setColorValue(sol::object value);
  sol::table getColor(sol::this_state s) const;

  void setAlpha(int alpha);
  int getAlpha() const;

  void setVisible(bool visible);
  bool getVisible() const;

  void setDistance(float distance);
  float getDistance() const;

  void top();
  void render();

private:
  friend class LuaDraw3dView;

  void Initialize();
  void Blit();

  LuaDraw3dView* view_;
  std::vector<std::string> lines_;
  std::string font_name_;
  Gothic_II_Addon::zVEC3 position_;
  Gothic_II_Addon::zCOLOR color_;
  bool visible_;
  bool attached_to_screen_;
  float distance_;

  static std::unordered_set<LuaDraw3d*> active_draws_;
};

void BindDraw3d(sol::state& lua);

}  // namespace gmp::gothic
