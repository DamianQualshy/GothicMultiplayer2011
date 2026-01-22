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

#include "menu_scene_manager.h"

#include <cmath>
#include <random>

namespace {
float NormalizeRotation(float rotation) {
  float normalized = std::fmod(rotation, 360.0f);
  if (normalized > 180.0f) {
    normalized -= 360.0f;
  } else if (normalized < -180.0f) {
    normalized += 360.0f;
  }
  return normalized;
}
}  // namespace

namespace menu {

SceneManager::SceneManager(oCGame* game) : game_(game) {
}

SceneManager::~SceneManager() {
  Cleanup();
}

void SceneManager::RegisterScene(const std::string& name, std::unique_ptr<MenuScene> scene, bool include_in_cycle) {
  scenes_[name] = std::move(scene);
  if (include_in_cycle) {
    cycle_scene_names_.push_back(name);
  }
}

bool SceneManager::ActivateScene(const std::string& name) {
  auto it = scenes_.find(name);
  if (it == scenes_.end()) {
    return false;
  }

  const bool was_weapon_visible = weapon_visible_;
  if (active_scene_) {
    if (weapon_visible_) {
      HideWeapon();
    }
    active_scene_->OnExit();
  }

  active_scene_ = it->second.get();
  active_scene_name_ = name;
  active_scene_index_ = -1;
  for (size_t i = 0; i < cycle_scene_names_.size(); ++i) {
    if (cycle_scene_names_[i] == name) {
      active_scene_index_ = static_cast<int>(i);
      break;
    }
  }

  if (active_scene_) {
    const auto settings = active_scene_->GetSettings();
    ApplyCameraSettings(settings.camera_position, settings.camera_pitch, settings.camera_yaw);
    active_scene_->Reset();
    active_scene_->OnEnter();
    if (was_weapon_visible) {
      ShowWeapon();
    }
  }

  return true;
}

bool SceneManager::ActivateNextScene() {
  if (cycle_scene_names_.empty()) {
    return false;
  }

  int next_index = 0;
  if (active_scene_index_ >= 0) {
    next_index = (active_scene_index_ + 1) % static_cast<int>(cycle_scene_names_.size());
  }

  return ActivateScene(cycle_scene_names_[next_index]);
}

bool SceneManager::ActivateRandomScene() {
  if (cycle_scene_names_.empty()) {
    return false;
  }

  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<size_t> distribution(0, cycle_scene_names_.size() - 1);
  return ActivateScene(cycle_scene_names_[distribution(rng)]);
}

void SceneManager::Update() {
  if (active_scene_) {
    active_scene_->Update();
  }
}

void SceneManager::ResetActiveScene() {
  if (active_scene_) {
    active_scene_->Reset();
  }
}

void SceneManager::ShowWeapon() {
  if (weapon_visible_ || !active_scene_ || !game_) {
    return;
  }

  EnsureCameraAnchor();

  const auto settings = active_scene_->GetSettings();
  if (!settings.show_weapon || !settings.weapon_visual_name || !settings.weapon_baseline) {
    return;
  }

  zCVisual* weapon_visual = zCVisual::LoadVisual(zSTRING(settings.weapon_visual_name));
  if (!weapon_visual) {
    return;
  }

  active_weapon_ = new zCVob();
  if (!active_weapon_) {
    return;
  }

  active_weapon_->SetVisual(weapon_visual);
  const MenuWeaponBaseline& baseline = *settings.weapon_baseline;
  SetCameraAnchorTransform(baseline.camera_position, baseline.camera_pitch, baseline.camera_yaw);

  active_weapon_->SetPositionWorld(baseline.weapon_position);

  if (game_->GetWorld() && camera_anchor_) {
    game_->GetWorld()->AddVobAsChild_novt(active_weapon_, camera_anchor_);
  }

  ApplyCameraSettings(settings.camera_position, settings.camera_pitch, settings.camera_yaw);
  const float weapon_yaw = NormalizeRotation(NormalizeRotation(settings.camera_yaw) - NormalizeRotation(baseline.camera_yaw));
  active_weapon_->ResetRotationsWorld();
  active_weapon_->RotateWorldY(weapon_yaw);

  active_scene_->SetWeapon(active_weapon_);
  weapon_visible_ = true;
}

void SceneManager::HideWeapon() {
  if (!weapon_visible_) {
    return;
  }

  if (active_scene_) {
    active_scene_->SetWeapon(nullptr);
  }

  if (active_weapon_) {
    active_weapon_->RemoveVobFromWorld();
    active_weapon_->Release();
    active_weapon_ = nullptr;
  }

  weapon_visible_ = false;
}

void SceneManager::Cleanup() {
  if (active_scene_) {
    active_scene_->OnExit();
  }

  HideWeapon();
  active_scene_ = nullptr;
  active_scene_name_.clear();
  scenes_.clear();
  cycle_scene_names_.clear();
  active_scene_index_ = -1;
  RemoveCameraAnchor();
}

void SceneManager::EnsureCameraAnchor() {
  if (camera_anchor_) {
    return;
  }

  camera_anchor_ = new zCVob();
}

void SceneManager::ApplyCameraSettings(const zVEC3& position, float rotation_pitch, float rotation_yaw) {
  if (!game_) {
    return;
  }

  EnsureCameraAnchor();

  if (!camera_anchor_) {
    return;
  }

  SetCameraAnchorTransform(position, rotation_pitch, rotation_yaw);
  game_->CamInit(camera_anchor_, zCCamera::activeCam);
}

void SceneManager::SetCameraAnchorTransform(const zVEC3& position, float rotation_pitch, float rotation_yaw) {
  if (!camera_anchor_) {
    return;
  }

  camera_anchor_->SetPositionWorld(position);
  camera_anchor_->ResetRotationsWorld();
  camera_anchor_->RotateWorldX(NormalizeRotation(rotation_pitch));
  camera_anchor_->RotateWorldY(NormalizeRotation(rotation_yaw));

  if (game_ && game_->GetWorld() && !camera_anchor_in_world_) {
    game_->GetWorld()->AddVob(camera_anchor_);
    camera_anchor_in_world_ = true;
  }
}

void SceneManager::RemoveCameraAnchor() {
  if (camera_anchor_) {
    camera_anchor_->RemoveVobFromWorld();
    camera_anchor_->Release();
    camera_anchor_ = nullptr;
    camera_anchor_in_world_ = false;
  }
}

}  // namespace menu