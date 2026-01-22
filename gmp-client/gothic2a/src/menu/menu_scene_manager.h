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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ZenGin/zGothicAPI.h"
#include "menu/menu_scene.h"

namespace menu {

/**
 * @brief Manages menu scenes (camera placement, weapon presentation, and scene updates)
 *
 * This manager owns the camera anchor and the active scene's weapon vob. It applies
 * scene-defined camera/weapon settings and forwards per-frame updates to the
 * active scene for FPS-independent animations.
 */
class SceneManager {
public:
  explicit SceneManager(oCGame* game);
  ~SceneManager();

  void RegisterScene(const std::string& name, std::unique_ptr<MenuScene> scene, bool include_in_cycle = true);
  bool ActivateScene(const std::string& name);
  bool ActivateNextScene();
  bool ActivateRandomScene();
  void Update();
  void ResetActiveScene();
  void ShowWeapon();
  void HideWeapon();
  void Cleanup();

private:
  void EnsureCameraAnchor();
  void ApplyCameraSettings(const zVEC3& position, float rotation_pitch, float rotation_yaw);
  void SetCameraAnchorTransform(const zVEC3& position, float rotation_pitch, float rotation_yaw);
  void RemoveCameraAnchor();

  oCGame* game_ = nullptr;
  zCVob* camera_anchor_ = nullptr;
  zCVob* active_weapon_ = nullptr;
  bool weapon_visible_ = false;
  bool camera_anchor_in_world_ = false;
  std::unordered_map<std::string, std::unique_ptr<MenuScene>> scenes_;
  std::vector<std::string> cycle_scene_names_;
  MenuScene* active_scene_ = nullptr;
  std::string active_scene_name_;
  int active_scene_index_ = -1;
};

}  // namespace menu