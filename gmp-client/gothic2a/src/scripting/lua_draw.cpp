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

#include "lua_draw.h"

#include <unordered_set>

#include "lua_helpers.h"
#include "ZenGin/zGothicAPI.h"

using namespace Gothic_II_Addon;

namespace gmp::gothic {

class LuaDrawView : public zCView {
public:
  explicit LuaDrawView(LuaDraw& owner) : zCView(0, 0, 8192, 8192), owner_(owner) {
  }

  void Blit() override {
    owner_.Blit();
  }

private:
  LuaDraw& owner_;
};

std::unordered_set<LuaDraw*> LuaDraw::active_draws_;

/* luagmp (class)
*
* 2D text drawing helper for rendering overlay text on screen.
*
* @version  0.3.0
* @name     Draw
* @side     client
* @category UI
*
*/

/* luagmp (constructor)
*
* Creates a new Draw object with default settings.
*
*/
LuaDraw::LuaDraw()
    : view_(nullptr),
      text_(""),
      fontName_("FONT_DEFAULT.TGA"),
      posX_(0),
      posY_(0),
      color_(255, 255, 255, 255),
      visible_(true),
      attached_to_screen_(false) {
  Initialize();
}

/* luagmp (constructor)
*
* Creates a new Draw object with an initial position and text.
*
* @param    (number) x    Initial X position (virtual units).
* @param    (number) y    Initial Y position (virtual units).
* @param    (string) text Initial text content.
*
*/
LuaDraw::LuaDraw(int x, int y, const std::string& text)
    : view_(nullptr),
      text_(text),
      fontName_("FONT_DEFAULT.TGA"),
      posX_(x),
      posY_(y),
      color_(255, 255, 255, 255),
      visible_(true),
      attached_to_screen_(false) {
  Initialize();
}

LuaDraw::~LuaDraw() {
  if (screen && view_ && attached_to_screen_) {
    screen->RemoveItem(view_);
    attached_to_screen_ = false;
  }

  delete view_;
  view_ = nullptr;

  active_draws_.erase(this);
}

/* luagmp (method)
*
* This function will set the draw position in virtual screen units.
*
* @name     setPosition
* @param    (number) x    X position.
* @param    (number) y    Y position.
*
*/
void LuaDraw::setPosition(int x, int y) {
  posX_ = x;
  posY_ = y;
  if (view_) {
    view_->SetPos(x, y);
  }
}

void LuaDraw::setPositionValue(sol::object value) {
  int x = posX_;
  int y = posY_;
  if (lua_helpers::ReadVec2(value, x, y)) {
    setPosition(x, y);
  }
}

/* luagmp (method)
*
* This function will return the draw position in virtual screen units.
*
* @name     getPosition
* @return   ({x, y})    Table containing x and y numbers.
*
*/
sol::table LuaDraw::getPosition(sol::this_state s) {
  sol::state_view lua(s);
  sol::table pos = lua.create_table();
  int x = posX_;
  int y = posY_;
  if (view_) {
    view_->GetPos(x, y);
  }
  pos["x"] = x;
  pos["y"] = y;
  return pos;
}

/* luagmp (method)
*
* This function will set the draw position in pixel coordinates.
*
* @name     setPositionPx
* @param    (number) x    X position.
* @param    (number) y    Y position.
*
*/
void LuaDraw::setPositionPx(int x, int y) {
  if (!screen) {
    return;
  }
  setPosition(screen->anx(x), screen->any(y));
}

void LuaDraw::setPositionPxValue(sol::object value) {
  int x = 0;
  int y = 0;
  if (lua_helpers::ReadVec2(value, x, y)) {
    setPositionPx(x, y);
  }
}

/* luagmp (method)
*
* This function will return the draw position in pixel coordinates.
*
* @name     getPositionPx
* @return   ({x, y})    Table containing x and y numbers.
*
*/
sol::table LuaDraw::getPositionPx(sol::this_state s) {
  sol::state_view lua(s);
  sol::table pos = lua.create_table();
  int x = posX_;
  int y = posY_;
  if (view_ && screen) {
    view_->GetPos(x, y);
    pos["x"] = screen->nax(x);
    pos["y"] = screen->nay(y);
  }
  return pos;
}

/* luagmp (method)
*
* This function will set the text to render.
*
* @name     setText
* @param    (string) text    Text to display.
*
*/
void LuaDraw::setText(const std::string& text) {
  text_ = text;
}

/* luagmp (method)
*
* This function will return the current text.
*
* @name     getText
* @return   (string)    Current text.
*
*/
std::string LuaDraw::getText() const {
  return text_;
}

/* luagmp (method)
*
* This function will set the font used for rendering.
*
* @name     setFont
* @param    (string) font     Font file name.
*
*/
void LuaDraw::setFont(const std::string& fontName) {
  fontName_ = fontName;
  if (view_) {
    view_->SetFont(fontName_.c_str());
  }
}

/* luagmp (method)
*
* This function will return the current font file name.
*
* @name     getFont
* @return   (string)    Font file name.
*
*/
std::string LuaDraw::getFont() const {
  return fontName_;
}

/* luagmp (method)
*
* This function will set the text color.
*
* @name     setColor
* @param    (number) r    The red color component in RGB model.
* @param    (number) g    The green color component in RGB model.
* @param    (number) b    The blue color component in RGB model.
*
*/
void LuaDraw::setColor(int r, int g, int b) {
  color_.SetRGB(r, g, b);
  if (view_) {
    view_->SetFontColor(color_);
  }
}

/* luagmp (method)
*
* This function will return the current text color.
*
* @name     getColor
* @return   ({r, g, b, a})  Table containing color in RGBA model.
*
*/
sol::table LuaDraw::getColor(sol::this_state s) {
  sol::state_view lua(s);
  sol::table color = lua.create_table();
  color["r"] = color_.r;
  color["g"] = color_.g;
  color["b"] = color_.b;
  color["a"] = color_.alpha;
  return color;
}

void LuaDraw::setColorValue(sol::object value) {
  int r = color_.r;
  int g = color_.g;
  int b = color_.b;
  int a = color_.alpha;
  if (lua_helpers::ReadColor(value, r, g, b, a)) {
    setColor(r, g, b);
    setAlpha(a);
  }
}

/* luagmp (method)
*
* This function will set the text alpha.
*
* @name     setAlpha
* @param    (number) alpha    Opacity value (0-255).
*
*/
void LuaDraw::setAlpha(int a) {
  color_.alpha = a;
  if (view_) {
    view_->SetFontColor(color_);
  }
}

/* luagmp (method)
*
* This function will return the current alpha
*
* @name     getAlpha
* @return   (number)    Opacity value (0-255).
*
*/
int LuaDraw::getAlpha() const {
  return color_.alpha;
}

/* luagmp (method)
*
* This function will set whether the Draw object should render.
*
* @name     setVisible
* @param    (boolean) visible     True to render, false to hide.
*
*/
void LuaDraw::setVisible(bool visible) {
  visible_ = visible;
}

/* luagmp (method)
*
* This function will return whether this Draw object is visible.
*
* @name     getVisible
* @return   (boolean)     True if visible.
*
*/
bool LuaDraw::getVisible() const {
  return visible_;
}

/* luagmp (property)
*
* Represents the draw position in virtual screen units.
*
* @name     position
* @return   ({x, y})      Table containing x and y numbers.
*
*/
/* luagmp (property)
*
* Represents the draw position in pixel coordinates.
*
* @name     positionPx
* @return   ({x, y})      Table containing x and y numbers.
*
*/
/* luagmp (property)
*
* Represents the displayed text.
*
* @name     text
* @return   (string)      Current text.
*
*/
/* luagmp (property)
*
* Represents the font identifier used for rendering.
*
* @name     font
* @return   (string)      Font identifier/name.
*
*/
/* luagmp (property)
*
* Represents the draw's color.
*
* @name     color
* @return   ({r, g, b, a}) Table containing color in RGBA model.
*
*/
/* luagmp (property)
*
* Represents the draw's alpha.
*
* @name     alpha
* @return   (number)        Opacity value (0-255).
*
*/
/* luagmp (property)
*
* Represents whether the Draw object is rendered.
*
* @name     visible
* @return   (boolean)       True if visible.
*
*/

void LuaDraw::render() {
  if (view_) {
    view_->Blit();
  }
}

void LuaDraw::Initialize() {
  active_draws_.insert(this);
  view_ = new LuaDrawView(*this);
  if (view_) {
    view_->SetFont(fontName_.c_str());
    view_->SetFontColor(color_);
    view_->SetPos(posX_, posY_);
    if (screen) {
      screen->InsertItem(view_);
      attached_to_screen_ = true;
    }
  }
}

void LuaDraw::CleanupViews() {
  if (!screen) {
    return;
  }

  for (auto* draw : active_draws_) {
    if (draw && draw->view_ && draw->attached_to_screen_) {
      screen->RemoveItem(draw->view_);
      draw->attached_to_screen_ = false;
    }
  }
}

void LuaDraw::Blit() {
  if (!visible_ || text_.empty()) {
    return;
  }

  if (view_) {
    view_->ClrPrintwin();
    view_->Print(posX_, posY_, text_.c_str());
    view_->zCView::Blit();
  }
}

void BindDraw(sol::state& lua) {
  sol::usertype<LuaDraw> draw_type = lua.new_usertype<LuaDraw>(
      "Draw",
      sol::constructors<LuaDraw(), LuaDraw(int, int, const std::string&)>());

  draw_type[sol::meta_function::call] = sol::overload(
      []() { return LuaDraw(); },
      [](int x, int y, const std::string& text) { return LuaDraw(x, y, text); });

  draw_type["setPosition"] = &LuaDraw::setPosition;
  draw_type["getPosition"] = &LuaDraw::getPosition;
  draw_type["setPositionPx"] = &LuaDraw::setPositionPx;
  draw_type["getPositionPx"] = &LuaDraw::getPositionPx;

  draw_type["setText"] = &LuaDraw::setText;
  draw_type["getText"] = &LuaDraw::getText;

  draw_type["setFont"] = &LuaDraw::setFont;
  draw_type["getFont"] = &LuaDraw::getFont;

  draw_type["setColor"] = &LuaDraw::setColor;
  draw_type["getColor"] = &LuaDraw::getColor;

  draw_type["setAlpha"] = &LuaDraw::setAlpha;
  draw_type["getAlpha"] = &LuaDraw::getAlpha;

  draw_type["setVisible"] = &LuaDraw::setVisible;
  draw_type["getVisible"] = &LuaDraw::getVisible;

  draw_type["render"] = &LuaDraw::render;

  // Properties (Lua table access)
  draw_type["position"] = sol::property(&LuaDraw::getPosition, &LuaDraw::setPositionValue);
  draw_type["positionPx"] = sol::property(&LuaDraw::getPositionPx, &LuaDraw::setPositionPxValue);
  draw_type["text"] = sol::property(&LuaDraw::getText, &LuaDraw::setText);
  draw_type["font"] = sol::property(&LuaDraw::getFont, &LuaDraw::setFont);
  draw_type["color"] = sol::property(&LuaDraw::getColor, &LuaDraw::setColorValue);
  draw_type["alpha"] = sol::property(&LuaDraw::getAlpha, &LuaDraw::setAlpha);
  draw_type["visible"] = sol::property(&LuaDraw::getVisible, &LuaDraw::setVisible);
}

}  // namespace gmp::gothic
