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

#include "lua_vob.h"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

#include "ZenGin/zGothicAPI.h"

namespace gmp::gothic {
namespace {
using namespace Gothic_II_Addon;

bool CanApplyVisual() {
  return zresMan != nullptr;
}

lua::types::Mat4 ToLuaMat4(const zMAT4& mat) {
  lua::types::Mat4 result;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      result.mat_[col][row] = mat.v[row].n[col];
    }
  }
  return result;
}

zMAT4 ToZenMat4(const lua::types::Mat4& mat) {
  zMAT4 result;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      result.v[row].n[col] = mat.mat_[col][row];
    }
  }
  return result;
}
}  // namespace

LuaVob::VobInstance::~VobInstance() {
  if (owned && vob) {
    if (vob->GetHomeWorld()) {
      vob->RemoveVobFromWorld();
      vob->Release();
    }
    delete vob;
  }
  vob = nullptr;
}

/* luagmp (class)
*
* @version  0.3.0
* Represents a 3D object in the world.
*
* @name     Vob
* @side     client
* @category Game
*
*/
/* luagmp (constructor)
*
* Creates a new Vob using the provided visual model.
*
* @param    (string) model Visual model name (e.g. "SPHERE.3DS").
*
*/
LuaVob::LuaVob(const std::string& model) : instance_(std::make_shared<VobInstance>()) {
  instance_->vob = new oCVob();
  instance_->owned = true;
  instance_->pending_visual = model;

  ApplyPendingVisual();
}

LuaVob::LuaVob(std::shared_ptr<VobInstance> instance) : instance_(std::move(instance)) {}

LuaVob LuaVob::FromExisting(zCVob* vob) {
  auto instance = std::make_shared<VobInstance>();
  instance->vob = vob;
  instance->owned = false;
  return LuaVob(std::move(instance));
}

zCVob* LuaVob::vob() const {
  return instance_ ? instance_->vob : nullptr;
}

/* luagmp (property)
*
* Represents the internal engine object name.
*
* @name     objectName
* @return   (string)
*
*/
void LuaVob::setObjectName(const std::string& name) {
  if (auto* handle = vob()) {
    handle->SetObjectName(zSTRING(name.c_str()));
  }
}

std::string LuaVob::getObjectName() const {
  if (auto* handle = vob()) {
    return std::string(handle->GetObjectName().ToChar());
  }
  return {};
}

/* luagmp (property)
*
* Represents the reference to the vob matrix.
*
* @name     matrix
* @return   (Mat4)
*
*/
void LuaVob::setMatrix(const lua::types::Mat4& matrix) {
  if (auto* handle = vob()) {
    handle->SetTrafoObjToWorld(ToZenMat4(matrix));
  }
}

lua::types::Mat4 LuaVob::getMatrix() const {
  if (auto* handle = vob()) {
    return ToLuaMat4(handle->GetNewTrafoObjToWorld());
  }
  return {};
}

/* luagmp (property)
*
* Represents the reference to the parent vob.
* Note: the vob hierarchy will be lost after changing the world.
*
* @name     parent
* @return   (Vob|nil)
*
*/
void LuaVob::setParent(sol::optional<LuaVob> parent) {
  auto* handle = vob();
  if (!handle) {
    return;
  }

  zCWorld* world = handle->GetHomeWorld();
  if (!world && ogame) {
    world = ogame->GetGameWorld();
  }

  if (!world) {
    return;
  }

  if (parent) {
    auto* parent_handle = parent->vob();
    if (parent_handle) {
      world->MoveVobSubtreeTo_novt(handle, parent_handle);
    }
    return;
  }

  world->MoveVobSubtreeToWorldSpace(handle);
}

sol::object LuaVob::getParent(sol::this_state ts) const {
  sol::state_view lua(ts);
  auto* handle = vob();
  if (!handle || !handle->globalVobTreeNode) {
    return sol::make_object(lua, sol::lua_nil);
  }

  auto* parent_node = handle->globalVobTreeNode->parent;
  if (!parent_node || !parent_node->data) {
    return sol::make_object(lua, sol::lua_nil);
  }

  return sol::make_object(lua, fmt::format("{:X}", reinterpret_cast<std::uintptr_t>(parent_node->data)));
}

/* luagmp (property)
*
* Represents the state of dynamic collision of vob. Enabling this option will prevent other dynamic objects from passing through it.
*
* @name     cdDynamic
* @return   (boolean)
*
*/
void LuaVob::setCdDynamic(bool enabled) {
  if (auto* handle = vob()) {
    handle->SetCollDetDyn(enabled ? 1 : 0);
  }
}

bool LuaVob::getCdDynamic() const {
  if (auto* handle = vob()) {
    return handle->collDetectionDynamic != 0;
  }
  return false;
}

/* luagmp (property)
*
* Represents the state of static collision of vob. Enabling this option will prevent static objects (i.e. world mesh) from passing through it.
*
* @name     cdStatic
* @return   (boolean)
*
*/
void LuaVob::setCdStatic(bool enabled) {
  if (auto* handle = vob()) {
    handle->SetCollDetStat(enabled ? 1 : 0);
  }
}

bool LuaVob::getCdStatic() const {
  if (auto* handle = vob()) {
    return handle->collDetectionStatic != 0;
  }
  return false;
}

/* luagmp (property)
*
* Represents the max distance at which the vob will still be rendered.
*
* @name     farClipZScale
* @return   (number)
*
*/
void LuaVob::setFarClipZScale(float scale) {
  if (auto* handle = vob()) {
    handle->m_fVobFarClipZScale = scale;
  }
}

float LuaVob::getFarClipZScale() const {
  if (auto* handle = vob()) {
    return handle->m_fVobFarClipZScale;
  }
  return 0.0f;
}

/* luagmp (property)
*
* Represents the model file name used as vob visual, e.g. SPHERE.3DS.
*
* @name     visual
* @return   (string)
*
*/
void LuaVob::setVisual(const std::string& visual) {
  if (instance_) {
    instance_->pending_visual = visual;
    ApplyPendingVisual();
  }
}

std::string LuaVob::getVisual() const {
  if (auto* handle = vob()) {
    if (auto* visual = handle->GetVisual()) {
      return std::string(visual->GetObjectName().ToChar());
    }
  }
  return {};
}

/* luagmp (property)
*
* Represents the transparency of the vob visual.
*
* @name     visualAlpha
* @return   (number) Alpha in range [0.0, 1.0].
*
*/
void LuaVob::setVisualAlpha(float alpha) {
  if (auto* handle = vob()) {
    float clamped = std::clamp(alpha, 0.0f, 1.0f);
    handle->visualAlpha = clamped;
    handle->visualAlphaEnabled = 1;
  }
}

float LuaVob::getVisualAlpha() const {
  if (auto* handle = vob()) {
    return handle->visualAlpha;
  }
  return 0.0f;
}

/* luagmp (method)
*
* Set the position of the vob in the world.
*
* @name     setPosition
* @param    (number) x Position on X axis.
* @param    (number) y Position on Y axis.
* @param    (number) z Position on Z axis.
*
*/
void LuaVob::setPosition(float x, float y, float z) {
  if (auto* handle = vob()) {
    handle->SetPositionWorld(zVEC3(x, y, z));
  }
}

/* luagmp (method)
*
* Get the position of the vob in the world.
*
* @name     getPosition
* @return   ({x, y, z})
*
*/
sol::table LuaVob::getPosition(sol::this_state s) const {
  sol::state_view lua(s);
  sol::table result = lua.create_table();

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (auto* handle = vob()) {
    handle->GetPositionWorld(x, y, z);
  }

  result["x"] = x;
  result["y"] = y;
  result["z"] = z;
  return result;
}

/* luagmp (method)
*
* Set the euler rotation of the vob in the world.
*
* @name     setRotation
* @param    (number) x Rotation on X axis.
* @param    (number) y Rotation on Y axis.
* @param    (number) z Rotation on Z axis.
*
*/
void LuaVob::setRotation(float x, float y, float z) {
  if (auto* handle = vob()) {
    zMAT4 matrix = handle->GetNewTrafoObjToWorld();
    zVEC3 translation = matrix.GetTranslation();
    matrix.SetByEulerAngles(zVEC3(x, y, z));
    matrix.SetTranslation(translation);
    handle->SetTrafoObjToWorld(matrix);
  }
}

/* luagmp (method)
*
* Get the euler rotation of the vob in the world.
*
* @name     getRotation
* @return   ({x, y, z})
*
*/
sol::table LuaVob::getRotation(sol::this_state s) const {
  sol::state_view lua(s);
  sol::table result = lua.create_table();

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (auto* handle = vob()) {
    zVEC3 euler = handle->GetNewTrafoObjToWorld().GetEulerAngles();
    x = euler.n[VX];
    y = euler.n[VY];
    z = euler.n[VZ];
  }

  result["x"] = x;
  result["y"] = y;
  result["z"] = z;
  return result;
}

/* luagmp (method)
*
* Add the vob to the currently loaded world. If the vob is not added, it won't show up.
*
* @name     addToWorld
* @param    (Vob|nil) parent Optional parent vob to attach to.
*
*/
void LuaVob::addToWorld(sol::optional<LuaVob> parent) {
  auto* handle = vob();
  if (!handle || handle->GetHomeWorld() != nullptr) {
    return;
  }

  if (!ogame) {
    return;
  }

  zCWorld* world = ogame->GetGameWorld();
  if (!world) {
    return;
  }

  if (parent) {
    auto* parent_handle = parent->vob();
    if (parent_handle) {
      ApplyPendingVisual();
      world->AddVobAsChild_novt(handle, parent_handle);
      return;
    }
  }

  ApplyPendingVisual();
  world->AddVob(handle);
}

/* luagmp (method)
*
* Remove the vob from the currently loaded world.
*
* @name     removeFromWorld
*
*/
void LuaVob::removeFromWorld() {
  if (auto* handle = vob()) {
    if (handle->GetHomeWorld()) {
      handle->RemoveVobFromWorld();
      handle->Release();
    }
  }
}

/* luagmp (method)
*
* Try to put the vob on the floor. If the difference between vob position and the floor y position is <= 1000, the method succeeds.
*
* @name     floor
*
*/
void LuaVob::floor() {
  auto* handle = vob();
  if (!handle) {
    return;
  }

  if (!handle->GetHomeWorld()) {
    return;
  }

  auto* ocvob = handle->CastTo<oCVob>();
  if (!ocvob) {
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  handle->GetPositionWorld(x, y, z);

  zVEC3 position(x, y, z);
  if (ocvob->GetFloorPosition(position)) {
    if (std::abs(position.n[VY] - y) <= 1000.0f) {
      handle->SetPositionWorld(position);
    }
    return;
  }

  zVEC3 original(x, y, z);
  ocvob->SetOnFloor(position);
  handle->GetPositionWorld(x, y, z);
  if (std::abs(y - original.n[VY]) > 1000.0f) {
    handle->SetPositionWorld(original);
  }
}

void LuaVob::ApplyPendingVisual() {
  auto* handle = vob();
  if (!handle || !instance_ || instance_->pending_visual.empty() || !CanApplyVisual()) {
    return;
  }

  handle->SetVisual(zSTRING(instance_->pending_visual.c_str()));
  instance_->pending_visual.clear();
}

}  // namespace gmp::gothic
