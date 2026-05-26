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

#include "lua_draw3d.h"

#include <algorithm>
#include <cmath>

#include "lua_helpers.h"

using namespace Gothic_II_Addon;

namespace gmp::gothic {
namespace {
constexpr float kDefaultDraw3dDistance = 1000.0f;

float DistanceSquared(const zVEC3& left, const zVEC3& right) {
  const float dx = left[VX] - right[VX];
  const float dy = left[VY] - right[VY];
  const float dz = left[VZ] - right[VZ];
  return dx * dx + dy * dy + dz * dz;
}
}  // namespace

class LuaDraw3dView : public zCView {
public:
  explicit LuaDraw3dView(LuaDraw3d& owner) : zCView(0, 0, 8192, 8192), owner_(owner) {
  }

  void Blit() override {
    owner_.Blit();
  }

private:
  LuaDraw3d& owner_;
};

std::unordered_set<LuaDraw3d*> LuaDraw3d::active_draws_;

/* luagmp (class)
*
* 3D text drawing helper for rendering world-space text on screen.
*
* @version  0.3.0
* @name     Draw3d
* @side     client
* @category UI
*
*/

/* luagmp (constructor)
*
* Creates a new Draw3d object with default settings.
*
*/
LuaDraw3d::LuaDraw3d()
    : view_(nullptr),
      font_name_("FONT_DEFAULT.TGA"),
      position_(0.0f, 0.0f, 0.0f),
      color_(255, 255, 255, 255),
      visible_(true),
      attached_to_screen_(false),
      distance_(kDefaultDraw3dDistance) {
  Initialize();
}

/* luagmp (constructor)
*
* Creates a new Draw3d object at the provided world position.
*
* @param    (number) x    Initial X world position.
* @param    (number) y    Initial Y world position.
* @param    (number) z    Initial Z world position.
*
*/
LuaDraw3d::LuaDraw3d(float x, float y, float z)
    : view_(nullptr),
      font_name_("FONT_DEFAULT.TGA"),
      position_(x, y, z),
      color_(255, 255, 255, 255),
      visible_(true),
      attached_to_screen_(false),
      distance_(kDefaultDraw3dDistance) {
  Initialize();
}

/* luagmp (constructor)
*
* Creates a new Draw3d object at the provided world position with initial text.
*
* @param    (number) x       Initial X world position.
* @param    (number) y       Initial Y world position.
* @param    (number) z       Initial Z world position.
* @param    (string) text    Initial text line.
*
*/
LuaDraw3d::LuaDraw3d(float x, float y, float z, const std::string& text)
    : view_(nullptr),
      lines_{text},
      font_name_("FONT_DEFAULT.TGA"),
      position_(x, y, z),
      color_(255, 255, 255, 255),
      visible_(true),
      attached_to_screen_(false),
      distance_(kDefaultDraw3dDistance) {
  Initialize();
}

LuaDraw3d::~LuaDraw3d() {
  if (screen && view_ && attached_to_screen_) {
    screen->RemoveItem(view_);
    attached_to_screen_ = false;
  }

  delete view_;
  view_ = nullptr;
  active_draws_.erase(this);
}

void LuaDraw3d::Initialize() {
  active_draws_.insert(this);
  view_ = new LuaDraw3dView(*this);
  if (view_) {
    view_->SetFont(font_name_.c_str());
    view_->SetFontColor(color_);
  }
}

void LuaDraw3d::CleanupViews() {
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

void LuaDraw3d::RenderActiveDraws() {
  auto draws = active_draws_;
  for (auto* draw : draws) {
    if (draw) {
      draw->Blit();
    }
  }
}

/* luagmp (method)
*
* Sets the world position used to project this text to screen space.
*
* @name     setPosition
* @param    (number) x    X world position.
* @param    (number) y    Y world position.
* @param    (number) z    Z world position.
*
*/
void LuaDraw3d::setPosition(float x, float y, float z) {
  position_ = zVEC3(x, y, z);
}

void LuaDraw3d::setPositionValue(sol::object value) {
  float x = position_[VX];
  float y = position_[VY];
  float z = position_[VZ];
  if (lua_helpers::ReadVec3(value, x, y, z)) {
    setPosition(x, y, z);
  }
}

/* luagmp (method)
*
* Returns the current world position.
*
* @name     getPosition
* @return   ({x, y, z})   Table containing x,y,z world position.
*
*/
sol::table LuaDraw3d::getPosition(sol::this_state s) const {
  return lua_helpers::MakeVec3Table(sol::state_view(s), position_);
}

/* luagmp (method)
*
* Appends a text line to the Draw3d object.
*
* @name     insertText
* @param    (string) text    Text line to append.
*
*/
void LuaDraw3d::insertText(const std::string& text) {
  lines_.push_back(text);
}

/* luagmp (method)
*
* Removes a text line by zero-based index.
*
* @name     removeText
* @param    (number) index    Zero-based text line index.
*
*/
void LuaDraw3d::removeText(int index) {
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return;
  }

  lines_.erase(lines_.begin() + index);
}

/* luagmp (method)
*
* Updates a text line by zero-based index.
*
* @name     updateText
* @param    (number) index    Zero-based text line index.
* @param    (string) text     Replacement text.
*
*/
void LuaDraw3d::updateText(int index, const std::string& text) {
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return;
  }

  lines_[index] = text;
}

/* luagmp (method)
*
* Removes all text lines.
*
* @name     clearText
*
*/
void LuaDraw3d::clearText() {
  lines_.clear();
}

/* luagmp (method)
*
* Returns all text lines.
*
* @name     getText
* @return   (table)    Array-like table of text lines.
*
*/
sol::table LuaDraw3d::getText(sol::this_state s) const {
  sol::state_view lua(s);
  sol::table result = lua.create_table();
  int index = 1;
  for (const auto& line : lines_) {
    result[index++] = line;
  }
  return result;
}

/* luagmp (method)
*
* Replaces all text lines with one text line.
*
* @name     setText
* @param    (string) text    Text to display.
*
*/
void LuaDraw3d::setText(const std::string& text) {
  lines_.clear();
  lines_.push_back(text);
}

/* luagmp (method)
*
* Sets the font used for rendering.
*
* @name     setFont
* @param    (string) font    Font file name.
*
*/
void LuaDraw3d::setFont(const std::string& font_name) {
  font_name_ = font_name;
  if (view_) {
    view_->SetFont(font_name_.c_str());
  }
}

/* luagmp (method)
*
* Returns the current font file name.
*
* @name     getFont
* @return   (string)    Font file name.
*
*/
std::string LuaDraw3d::getFont() const {
  return font_name_;
}

/* luagmp (method)
*
* Sets the text color.
*
* @name     setColor
* @param    (number) r    Red color component.
* @param    (number) g    Green color component.
* @param    (number) b    Blue color component.
*
*/
void LuaDraw3d::setColor(int r, int g, int b) {
  color_.SetRGB(r, g, b);
  if (view_) {
    view_->SetFontColor(color_);
  }
}

void LuaDraw3d::setColorValue(sol::object value) {
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
* Returns the text color.
*
* @name     getColor
* @return   ({r, g, b, a})    Table containing color in RGBA model.
*
*/
sol::table LuaDraw3d::getColor(sol::this_state s) const {
  sol::state_view lua(s);
  sol::table color = lua.create_table();
  color["r"] = color_.r;
  color["g"] = color_.g;
  color["b"] = color_.b;
  color["a"] = color_.alpha;
  return color;
}

/* luagmp (method)
*
* Sets the text alpha.
*
* @name     setAlpha
* @param    (number) alpha    Opacity value (0-255).
*
*/
void LuaDraw3d::setAlpha(int alpha) {
  color_.alpha = lua_helpers::ClampByte(alpha);
  if (view_) {
    view_->SetFontColor(color_);
  }
}

/* luagmp (method)
*
* Returns the current alpha.
*
* @name     getAlpha
* @return   (number)    Opacity value (0-255).
*
*/
int LuaDraw3d::getAlpha() const {
  return color_.alpha;
}

/* luagmp (method)
*
* Sets whether the Draw3d object should render.
*
* @name     setVisible
* @param    (boolean) visible    True to render, false to hide.
*
*/
void LuaDraw3d::setVisible(bool visible) {
  visible_ = visible;
}

/* luagmp (method)
*
* Returns whether this Draw3d object is visible.
*
* @name     getVisible
* @return   (boolean)    True if visible.
*
*/
bool LuaDraw3d::getVisible() const {
  return visible_;
}

/* luagmp (method)
*
* Sets the max render distance from the local player.
*
* @name     setDistance
* @param    (number) distance    Max distance in world units. Zero disables distance culling.
*
*/
void LuaDraw3d::setDistance(float distance) {
  distance_ = std::max(0.0f, distance);
}

/* luagmp (method)
*
* Returns the max render distance.
*
* @name     getDistance
* @return   (number)    Max distance in world units.
*
*/
float LuaDraw3d::getDistance() const {
  return distance_;
}

/* luagmp (method)
*
* Moves the Draw3d view to the top of the screen view stack.
*
* @name     top
*
*/
void LuaDraw3d::top() {
  if (view_ && attached_to_screen_) {
    view_->Top();
  }
}

/* luagmp (method)
*
* Forces immediate rendering of this Draw3d object.
*
* @name     render
*
*/
void LuaDraw3d::render() {
  Blit();
}

/* luagmp (property)
*
* Alias for position.
*
* @name     worldPosition
* @return   ({x, y, z})   Table containing x,y,z world position.
*
*/
/* luagmp (property)
*
* Represents the displayed text lines.
*
* @name     text
* @return   (table)       Array-like table of text lines.
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
* Represents the Draw3d color.
*
* @name     color
* @return   ({r, g, b, a}) Table containing color in RGBA model.
*
*/
/* luagmp (property)
*
* Represents the Draw3d alpha.
*
* @name     alpha
* @return   (number)      Opacity value (0-255).
*
*/
/* luagmp (property)
*
* Represents whether the Draw3d object is rendered.
*
* @name     visible
* @return   (boolean)     True if visible.
*
*/
/* luagmp (property)
*
* Represents the max render distance from the local player.
*
* @name     distance
* @return   (number)      Max distance in world units.
*
*/

void LuaDraw3d::Blit() {
  if (!visible_ || lines_.empty() || !screen || !zCCamera::activeCam) {
    return;
  }

  if (distance_ > 0.0f && player) {
    const float max_distance_sq = distance_ * distance_;
    if (DistanceSquared(position_, player->GetPositionWorld()) > max_distance_sq) {
      return;
    }
  }

  zCCamera* camera = zCCamera::activeCam;
  camera->Activate();
  zVEC3 projected = camera->camMatrix * position_;
  if (projected[VZ] <= 0.0f) {
    return;
  }

  float center_x = 0.0f;
  float center_y = 0.0f;
  camera->Project(&projected, center_x, center_y);

  zCView* screen_view = screen;
  zCFont* previous_font = screen_view->GetFont();
  const zCOLOR previous_font_color = screen_view->fontColor;

  screen_view->SetFont(font_name_.c_str());
  screen_view->SetFontColor(color_);

  const int center_x_virtual = screen_view->anx(static_cast<int>(center_x));
  const int center_y_virtual = screen_view->any(static_cast<int>(center_y));
  const int line_height = screen_view->FontY();

  for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
    zSTRING line(lines_[i].c_str());
    const int line_width = screen_view->FontSize(line);
    screen_view->Print(center_x_virtual - line_width / 2, center_y_virtual + i * line_height, line);
  }

  screen_view->SetFont(previous_font);
  screen_view->SetFontColor(previous_font_color);
}

void BindDraw3d(sol::state& lua) {
  sol::usertype<LuaDraw3d> draw_type = lua.new_usertype<LuaDraw3d>(
      "Draw3d",
      sol::constructors<LuaDraw3d(), LuaDraw3d(float, float, float), LuaDraw3d(float, float, float, const std::string&)>());

  draw_type[sol::meta_function::call] = sol::overload(
      []() { return LuaDraw3d(); },
      [](float x, float y, float z) { return LuaDraw3d(x, y, z); },
      [](float x, float y, float z, const std::string& text) { return LuaDraw3d(x, y, z, text); });

  draw_type["setPosition"] = &LuaDraw3d::setPosition;
  draw_type["getPosition"] = &LuaDraw3d::getPosition;
  draw_type["setWorldPosition"] = &LuaDraw3d::setPosition;
  draw_type["getWorldPosition"] = &LuaDraw3d::getPosition;

  draw_type["insertText"] = &LuaDraw3d::insertText;
  draw_type["removeText"] = &LuaDraw3d::removeText;
  draw_type["updateText"] = &LuaDraw3d::updateText;
  draw_type["clearText"] = &LuaDraw3d::clearText;
  draw_type["getText"] = &LuaDraw3d::getText;
  draw_type["setText"] = &LuaDraw3d::setText;

  draw_type["setFont"] = &LuaDraw3d::setFont;
  draw_type["getFont"] = &LuaDraw3d::getFont;
  draw_type["setColor"] = &LuaDraw3d::setColor;
  draw_type["getColor"] = &LuaDraw3d::getColor;
  draw_type["setAlpha"] = &LuaDraw3d::setAlpha;
  draw_type["getAlpha"] = &LuaDraw3d::getAlpha;
  draw_type["setVisible"] = &LuaDraw3d::setVisible;
  draw_type["getVisible"] = &LuaDraw3d::getVisible;
  draw_type["setDistance"] = &LuaDraw3d::setDistance;
  draw_type["getDistance"] = &LuaDraw3d::getDistance;

  draw_type["top"] = &LuaDraw3d::top;
  draw_type["render"] = &LuaDraw3d::render;

  draw_type["worldPosition"] = sol::property(&LuaDraw3d::getPosition, &LuaDraw3d::setPositionValue);
  draw_type["text"] = sol::property(&LuaDraw3d::getText, &LuaDraw3d::setText);
  draw_type["font"] = sol::property(&LuaDraw3d::getFont, &LuaDraw3d::setFont);
  draw_type["color"] = sol::property(&LuaDraw3d::getColor, &LuaDraw3d::setColorValue);
  draw_type["alpha"] = sol::property(&LuaDraw3d::getAlpha, &LuaDraw3d::setAlpha);
  draw_type["visible"] = sol::property(&LuaDraw3d::getVisible, &LuaDraw3d::setVisible);
  draw_type["distance"] = sol::property(&LuaDraw3d::getDistance, &LuaDraw3d::setDistance);
}

}  // namespace gmp::gothic
