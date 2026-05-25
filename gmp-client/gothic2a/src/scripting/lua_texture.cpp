#include "lua_texture.h"

#include <algorithm>
#include <unordered_set>

#include "lua_helpers.h"
#include "ZenGin/zGothicAPI.h"

using namespace Gothic_II_Addon;

namespace gmp::gothic {

class LuaTextureView : public zCView {
public:
  LuaTextureView(LuaTexture& owner, int x, int y, int width, int height) : zCView(x, y, x + width, y + height, VIEW_ITEM), owner_(owner) {
  }

  void Blit() override {
    owner_.Blit();
  }

private:
  LuaTexture& owner_;
};

std::unordered_set<LuaTexture*> LuaTexture::active_textures_;

/* luagmp (class)
*
* This class represents a 2d Texture on screen.
*
* @version  0.3.0
* @name     Texture
* @side     client
* @category UI
*
*/

/* luagmp (constructor)
*
* Creates a new Texture.
*
* @param    (number) x      X position (virtual units).
* @param    (number) y      Y position (virtual units).
* @param    (number) width  Width (virtual units).
* @param    (number) height Height (virtual units).
* @param    (string) file   Texture file path.
*
*/
LuaTexture::LuaTexture(int x, int y, int width, int height, const std::string& file)
    : view_(nullptr),
      texture_(nullptr),
      uvPos_(0.0f, 0.0f),
      uvSize_(1.0f, 1.0f),
      color_(255, 255, 255, 255),
      alphaFunc_(zRND_ALPHA_FUNC_BLEND),
      fillZ_(false),
      visible_(true),
      fileName_(file),
      attached_to_screen_(false) {
  active_textures_.insert(this);
  view_ = new LuaTextureView(*this, x, y, width, height);

  setFile(file);

  if (screen && view_) {
    screen->InsertItem(view_);
    attached_to_screen_ = true;
  }
}

LuaTexture::~LuaTexture() {
  if (screen && view_ && attached_to_screen_) {
    screen->RemoveItem(view_);
    attached_to_screen_ = false;
  }

  delete view_;
  view_ = nullptr;

  if (texture_) {
    texture_->Release();
  }
  texture_ = nullptr;

  active_textures_.erase(this);
}

/* luagmp (method)
*
* This method will set the Texture position in virtual screen units.
*
* @name     setPosition
* @param    (number) x    X position (virtual units).
* @param    (number) y    Y position (virtual units).
*
*/
void LuaTexture::setPosition(int x, int y) {
  updateViewPos(x, y);
}

void LuaTexture::setPositionValue(sol::object value) {
  int x = 0;
  int y = 0;
  if (lua_helpers::ReadVec2(value, x, y)) {
    setPosition(x, y);
  }
}

/* luagmp (method)
*
* This method will return the Texture position in virtual screen units.
*
* @name     getPosition
* @return   ({x, y})      Table containing x and y (virtual units).
*
*/
sol::table LuaTexture::getPosition(sol::this_state s) {
  sol::state_view lua(s);
  sol::table pos = lua.create_table();
  int x = 0;
  int y = 0;
  if (view_) {
    view_->GetPos(x, y);
  }
  pos["x"] = x;
  pos["y"] = y;
  return pos;
}

/* luagmp (method)
*
* This method will set the Texture position in pixel coordinates.
*
* @name     setPositionPx
* @param    (number) x    X position (pixels).
* @param    (number) y    Y position (pixels).
*
*/
void LuaTexture::setPositionPx(int x, int y) {
  if (!screen) {
    return;
  }
  updateViewPos(screen->anx(x), screen->any(y));
}

void LuaTexture::setPositionPxValue(sol::object value) {
  int x = 0;
  int y = 0;
  if (lua_helpers::ReadVec2(value, x, y)) {
    setPositionPx(x, y);
  }
}

/* luagmp (method)
*
* This method will return the Texture position in pixel coordinates.
*
* @name     getPositionPx
* @return   ({x, y})      Table containing x and y (pixels).
*
*/
sol::table LuaTexture::getPositionPx(sol::this_state s) {
  sol::state_view lua(s);
  sol::table pos = lua.create_table();
  int virtualX = 0;
  int virtualY = 0;
  if (view_ && screen) {
    view_->GetPos(virtualX, virtualY);
    pos["x"] = screen->nax(virtualX);
    pos["y"] = screen->nay(virtualY);
  }
  return pos;
}

/* luagmp (method)
*
* This method will set the Texture size in virtual screen units.
*
* @name     setSize
* @param    (number) width    Width (virtual units).
* @param    (number) height   Height (virtual units).
*
*/
void LuaTexture::setSize(int width, int height) {
  updateViewSize(width, height);
}

void LuaTexture::setSizeValue(sol::object value) {
  int width = 0;
  int height = 0;
  if (lua_helpers::ReadSize(value, width, height)) {
    setSize(width, height);
  }
}

/* luagmp (method)
*
* This method will return the Texture size in virtual screen units.
*
* @name     getSize
* @return   ({width, height}) Table containing width and height (virtual units).
*
*/
sol::table LuaTexture::getSize(sol::this_state s) {
  sol::state_view lua(s);
  sol::table size = lua.create_table();
  int width = 0;
  int height = 0;
  if (view_) {
    view_->GetSize(width, height);
  }
  size["width"] = width;
  size["height"] = height;
  return size;
}

/* luagmp (method)
*
* This method will set the Texture size in pixel coordinates.
*
* @name     setSizePx
* @param    (number) width    Width (pixels).
* @param    (number) height   Height (pixels).
*
*/
void LuaTexture::setSizePx(int width, int height) {
  if (!screen) {
    return;
  }
  updateViewSize(screen->anx(width), screen->any(height));
}

void LuaTexture::setSizePxValue(sol::object value) {
  int width = 0;
  int height = 0;
  if (lua_helpers::ReadSize(value, width, height)) {
    setSizePx(width, height);
  }
}

/* luagmp (method)
*
* This method will return the Texture size in pixel coordinates.
*
* @name     getSizePx
* @return   ({width, height}) Table containing width and height (pixels).
*
*/
sol::table LuaTexture::getSizePx(sol::this_state s) {
  sol::state_view lua(s);
  sol::table size = lua.create_table();
  int width = 0;
  int height = 0;
  if (view_ && screen) {
    view_->GetSize(width, height);
    size["width"] = screen->nax(width);
    size["height"] = screen->nay(height);
  }
  return size;
}

/* luagmp (method)
*
* This method will set the Texture rectangle in virtual screen units.
*
* @name     setRect
* @param    (number) x      X position (virtual units).
* @param    (number) y      Y position (virtual units).
* @param    (number) width  Width (virtual units).
* @param    (number) height Height (virtual units).
*
*/
void LuaTexture::setRect(int x, int y, int width, int height) {
  int virtualWidth = 0;
  int virtualHeight = 0;
  if (!view_) {
    return;
  }

  view_->GetSize(virtualWidth, virtualHeight);
  if (virtualWidth == 0 || virtualHeight == 0) {
    return;
  }

  uvPos_[VX] = static_cast<float>(x) / virtualWidth;
  uvPos_[VY] = static_cast<float>(y) / virtualHeight;
  uvSize_[VX] = uvPos_[VX] + static_cast<float>(width) / virtualWidth;
  uvSize_[VY] = uvPos_[VY] + static_cast<float>(height) / virtualHeight;
}

void LuaTexture::setRectValue(sol::object value) {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (lua_helpers::ReadRect(value, x, y, width, height)) {
    setRect(x, y, width, height);
  }
}

/* luagmp (method)
*
* This method will return the Texture rectangle in virtual screen units.
*
* @name     getRect
* @return   ({x, y, width, height}) Table containing x,y,width,height (virtual units).
*
*/
sol::table LuaTexture::getRect(sol::this_state s) {
  sol::state_view lua(s);
  sol::table rect = lua.create_table();
  int width = 0;
  int height = 0;
  if (view_) {
    view_->GetSize(width, height);
  }
  rect["x"] = static_cast<int>(uvPos_[VX] * width);
  rect["y"] = static_cast<int>(uvPos_[VY] * height);
  rect["width"] = static_cast<int>((uvSize_[VX] - uvPos_[VX]) * width);
  rect["height"] = static_cast<int>((uvSize_[VY] - uvPos_[VY]) * height);
  return rect;
}

/* luagmp (method)
*
* This method will set the Texture rectangle in pixel coordinates.
*
* @name     setRectPx
* @param    (number) x        X position (pixels).
* @param    (number) y        Y position (pixels).
* @param    (number) width    Width (pixels).
* @param    (number) height   Height (pixels).
*
*/
void LuaTexture::setRectPx(int x, int y, int width, int height) {
  if (!screen) {
    return;
  }

  setRect(screen->anx(x), screen->any(y), screen->anx(width), screen->any(height));
}

void LuaTexture::setRectPxValue(sol::object value) {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (lua_helpers::ReadRect(value, x, y, width, height)) {
    setRectPx(x, y, width, height);
  }
}

/* luagmp (method)
*
* This method will return the Texture rectangle in pixel coordinates.
*
* @name     getRectPx
* @return   ({x, y, width, height}) Table containing x,y,width,height (pixels).
*
*/
sol::table LuaTexture::getRectPx(sol::this_state s) {
  sol::state_view lua(s);
  sol::table rect = lua.create_table();
  int pixelWidth = 0;
  int pixelHeight = 0;
  if (view_ && screen) {
    view_->GetSize(pixelWidth, pixelHeight);
    pixelWidth = screen->nax(pixelWidth);
    pixelHeight = screen->nay(pixelHeight);
  }
  rect["x"] = static_cast<int>(uvPos_[VX] * pixelWidth);
  rect["y"] = static_cast<int>(uvPos_[VY] * pixelHeight);
  rect["width"] = static_cast<int>((uvSize_[VX] - uvPos_[VX]) * pixelWidth);
  rect["height"] = static_cast<int>((uvSize_[VY] - uvPos_[VY]) * pixelHeight);
  return rect;
}

/* luagmp (method)
*
* This method will set the Texture color.
*
* @name     setColor
* @param    (number) r    The red color component in RGB model.
* @param    (number) g    The green color component in RGB model.
* @param    (number) b    The blue color component in RGB model.
*
*/
void LuaTexture::setColor(unsigned char r, unsigned char g, unsigned char b) {
  color_.SetRGB(r, g, b);
}

/* luagmp (method)
*
* This method will return the Texture color.
*
* @name     getColor
* @return   ({r, g, b, a}) Table containing color in RGBA model.
*
*/
sol::table LuaTexture::getColor(sol::this_state s) {
  sol::state_view lua(s);
  sol::table colorTable = lua.create_table();
  colorTable["r"] = color_.r;
  colorTable["g"] = color_.g;
  colorTable["b"] = color_.b;
  colorTable["a"] = color_.alpha;
  return colorTable;
}

void LuaTexture::setColorValue(sol::object value) {
  int r = color_.r;
  int g = color_.g;
  int b = color_.b;
  int a = color_.alpha;
  if (lua_helpers::ReadColor(value, r, g, b, a)) {
    setColor(lua_helpers::ClampByte(r), lua_helpers::ClampByte(g), lua_helpers::ClampByte(b));
    setAlpha(lua_helpers::ClampByte(a));
  }
}

/* luagmp (method)
*
* This method will set the Texture alpha.
*
* @name     setAlpha
* @param    (number) alpha Opacity value (0-255).
*
*/
void LuaTexture::setAlpha(unsigned char alpha) {
  color_.alpha = alpha;
}

/* luagmp (method)
*
* This method will return the current Texture alpha (opacity).
*
* @name     getAlpha
* @return   (number)      Opacity value (0-255).
*
*/
unsigned char LuaTexture::getAlpha() const {
  return color_.alpha;
}

/* luagmp (method)
*
* This method will set the Texture file name.
*
* @name     setFile
* @param    (string) file  Texture file name.
*
*/
void LuaTexture::setFile(const std::string& file) {
  if (texture_) {
    texture_->Release();
    texture_ = nullptr;
  }

  fileName_ = file;
  zSTRING fileString(file.c_str());
  texture_ = zCTexture::Load(fileString, 0);
  if (view_) {
    view_->InsertBack(fileString);
  }
}

/* luagmp (method)
*
* This method will return the Texture file name.
*
* @name     getFile
* @return   (string)    Texture file name.
*
*/
std::string LuaTexture::getFile() const {
  return fileName_;
}

/* luagmp (method)
*
* This method will set whether the Texture should be rendered.
*
* @name     setVisible
* @param    (boolean) visible   True to render, false to hide.
*
*/
void LuaTexture::setVisible(bool visible) {
  visible_ = visible;
}

/* luagmp (method)
*
* This method will return whether the Texture is visible.
*
* @name     getVisible
* @return   (boolean)   True if visible.
*
*/
bool LuaTexture::getVisible() const {
  return visible_;
}

/* luagmp (method)
*
* This method will move the Texture to the top of the render order.
*
* @name     top
*
*/
void LuaTexture::top() {
  if (view_) {
    view_->Top();
  }
}

/* luagmp (property)
*
* Represents the Texture position in virtual screen units.
*
* @name     position
* @return   ({x, y}) Table containing x and y (virtual units).
*
*/
/* luagmp (property)
*
* Represents the Texture position in pixel coordinates.
*
* @name     positionPx
* @return   ({x, y}) Table containing x and y (pixels).
*
*/
/* luagmp (property)
*
* Represents the Texture size in virtual screen units.
*
* @name     size
* @return   ({width, height}) Table containing width and height (virtual units).
*
*/
/* luagmp (property)
*
* Represents the Texture size in pixel coordinates.
*
* @name     sizePx
* @return   ({width, height}) Table containing width and height (pixels).
*
*/
/* luagmp (property)
*
* Represents the Texture rectangle in virtual screen units.
*
* @name     rect
* @return   ({x, y, width, height}) Table containing x,y,width,height (virtual units).
*
*/
/* luagmp (property)
*
* Represents the Texture rectangle in pixel coordinates.
*
* @name     rectPx
* @return   ({x, y, width, height}) Table containing x,y,width,height (pixels).
*
*/
/* luagmp (property)
*
* Represents the Texture color.
*
* @name     color
* @return   ({r, g, b, a}) Table containing color in RGBA model.
*
*/
/* luagmp (property)
*
* Represents the Texture alpha .
*
* @name     alpha
* @return   (number) Opacity value (0-255).
*
*/
/* luagmp (property)
*
* Represents whether the texture is rendered.
*
* @name     visible
* @return   (boolean) True if visible.
*
*/
/* luagmp (property)
*
* Represents the texture file path.
*
* @name     file
* @return   (string) Texture file path.
*
*/

void LuaTexture::render() {
  if (view_) {
    view_->Blit();
  }
}

void LuaTexture::CleanupViews() {
  if (!screen) {
    return;
  }

  for (auto* texture : active_textures_) {
    if (texture && texture->view_ && texture->attached_to_screen_) {
      screen->RemoveItem(texture->view_);
      texture->attached_to_screen_ = false;
    }
  }
}

void LuaTexture::Blit() {
  if (!visible_ || !view_ || !texture_ || !zrenderer || !screen) {
    return;
  }

  int virtualWidth = 0;
  int virtualHeight = 0;
  int virtualPosX = 0;
  int virtualPosY = 0;
  view_->GetPos(virtualPosX, virtualPosY);
  view_->GetSize(virtualWidth, virtualHeight);

  zVEC2 posMin(static_cast<float>(screen->nax(virtualPosX)), static_cast<float>(screen->nay(virtualPosY)));
  zVEC2 posMax(posMin[VX] + static_cast<float>(screen->nax(virtualWidth)), posMin[VY] + static_cast<float>(screen->nay(virtualHeight)));

  if (posMin[VX] > zrenderer->vid_xdim - 1 || posMin[VY] > zrenderer->vid_ydim - 1) {
    return;
  }

  if (posMax[VX] < 0 || posMax[VY] < 0) {
    return;
  }

  zREAL onScreenPosMinX = std::max(posMin[VX], 0.0f);
  zREAL onScreenPosMinY = std::max(posMin[VY], 0.0f);
  zREAL onScreenPosMaxX = std::min(posMax[VX], static_cast<zREAL>(zrenderer->vid_xdim - 1));
  zREAL onScreenPosMaxY = std::min(posMax[VY], static_cast<zREAL>(zrenderer->vid_ydim - 1));

  zREAL onScreenSizeWidth = onScreenPosMaxX - onScreenPosMinX;
  zREAL onScreenSizeHeight = onScreenPosMaxY - onScreenPosMinY;

  if (onScreenSizeWidth <= 0 || onScreenSizeHeight <= 0) {
    return;
  }

  zrenderer->SetViewport(onScreenPosMinX, onScreenPosMinY, onScreenSizeWidth, onScreenSizeHeight);

  zBOOL oldzWrite = zrenderer->GetZBufferWriteEnabled();
  zrenderer->SetZBufferWriteEnabled(fillZ_);

  zTRnd_ZBufferCmp oldCmp = zrenderer->GetZBufferCompare();
  zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);

  zTRnd_AlphaBlendFunc oldBlendFunc = zrenderer->GetAlphaBlendFunc();
  zrenderer->SetAlphaBlendFunc(alphaFunc_);

  zBOOL oldBilerpFilter = zrenderer->GetBilerpFilterEnabled();
  zrenderer->SetBilerpFilterEnabled(oldBilerpFilter);

  zREAL farZ;
  if (fillZ_) {
    farZ = (zCCamera::activeCam) ? zCCamera::activeCam->farClipZ - 1 : 65534.0f;
  } else {
    farZ = (zCCamera::activeCam) ? zCCamera::activeCam->nearClipZ + 1 : 1.0f;
  }

  zrenderer->DrawTile(texture_, posMin, posMax, farZ, uvPos_, uvSize_, color_);

  zrenderer->SetBilerpFilterEnabled(oldBilerpFilter);
  zrenderer->SetAlphaBlendFunc(oldBlendFunc);
  zrenderer->SetZBufferWriteEnabled(oldzWrite);
  zrenderer->SetZBufferCompare(oldCmp);
}

void LuaTexture::updateViewSize(int width, int height) {
  if (!view_) {
    return;
  }
  view_->SetSize(width, height);
}

void LuaTexture::updateViewPos(int x, int y) {
  if (!view_) {
    return;
  }
  view_->SetPos(x, y);
}

void BindTexture(sol::state& lua) {
  sol::usertype<LuaTexture> texture_type = lua.new_usertype<LuaTexture>(
      "Texture",
      sol::constructors<LuaTexture(int, int, int, int, const std::string&)>());

  texture_type[sol::meta_function::call] =
      [](int x, int y, int width, int height, const std::string& file) {
        return LuaTexture(x, y, width, height, file);
      };

  texture_type["setPosition"] = &LuaTexture::setPosition;
  texture_type["getPosition"] = &LuaTexture::getPosition;
  texture_type["setPositionPx"] = &LuaTexture::setPositionPx;
  texture_type["getPositionPx"] = &LuaTexture::getPositionPx;

  texture_type["setSize"] = &LuaTexture::setSize;
  texture_type["getSize"] = &LuaTexture::getSize;
  texture_type["setSizePx"] = &LuaTexture::setSizePx;
  texture_type["getSizePx"] = &LuaTexture::getSizePx;

  texture_type["setRect"] = &LuaTexture::setRect;
  texture_type["getRect"] = &LuaTexture::getRect;
  texture_type["setRectPx"] = &LuaTexture::setRectPx;
  texture_type["getRectPx"] = &LuaTexture::getRectPx;

  texture_type["setColor"] = &LuaTexture::setColor;
  texture_type["getColor"] = &LuaTexture::getColor;
  texture_type["setAlpha"] = &LuaTexture::setAlpha;
  texture_type["getAlpha"] = &LuaTexture::getAlpha;

  texture_type["setVisible"] = &LuaTexture::setVisible;
  texture_type["getVisible"] = &LuaTexture::getVisible;
  texture_type["setFile"] = &LuaTexture::setFile;
  texture_type["getFile"] = &LuaTexture::getFile;
  
  texture_type["top"] = &LuaTexture::top;

  texture_type["render"] = &LuaTexture::render;

  // Properties (Lua table access)
  texture_type["position"] = sol::property(&LuaTexture::getPosition, &LuaTexture::setPositionValue);
  texture_type["positionPx"] = sol::property(&LuaTexture::getPositionPx, &LuaTexture::setPositionPxValue);
  texture_type["size"] = sol::property(&LuaTexture::getSize, &LuaTexture::setSizeValue);
  texture_type["sizePx"] = sol::property(&LuaTexture::getSizePx, &LuaTexture::setSizePxValue);
  texture_type["rect"] = sol::property(&LuaTexture::getRect, &LuaTexture::setRectValue);
  texture_type["rectPx"] = sol::property(&LuaTexture::getRectPx, &LuaTexture::setRectPxValue);
  texture_type["color"] = sol::property(&LuaTexture::getColor, &LuaTexture::setColorValue);
  texture_type["alpha"] = sol::property(&LuaTexture::getAlpha, &LuaTexture::setAlpha);
  texture_type["visible"] = sol::property(&LuaTexture::getVisible, &LuaTexture::setVisible);
  texture_type["file"] = sol::property(&LuaTexture::getFile, &LuaTexture::setFile);
}

}  // namespace gmp::gothic
