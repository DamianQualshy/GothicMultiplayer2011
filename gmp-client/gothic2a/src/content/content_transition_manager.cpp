/*
MIT License

Copyright (c) 2026 Gothic Multiplayer Team.

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

#include "content/content_transition_manager.h"

#include <spdlog/spdlog.h>

#include <utility>

#include "ZenGin/zGothicAPI.h"
#include "content/gothic_vfs_overlay.h"
#include "hooking/MemoryPatch.h"

namespace gmp::gothic {

namespace {

constexpr DWORD kLoadWorldNoVtAddress = 0x006C90B0;
constexpr DWORD kInsertBackAddress = 0x007A6130;
using LoadWorldNoVtFn = void(__thiscall*)(oCGame*, int, const zSTRING&);
using InsertBackFn = void(__thiscall*)(zCView*, const zSTRING&);

LoadWorldNoVtFn g_load_world_novt = nullptr;
InsertBackFn g_insert_back = nullptr;
ContentTransitionManager* g_content_transition_manager = nullptr;
bool g_disconnect_loading_screen_pending = false;

void __fastcall HookLoadWorldNoVt(oCGame* game, void*, int slot, const zSTRING& world_name) {
  if (g_content_transition_manager) {
    g_content_transition_manager->PrepareForWorldLoad();
  }
  g_load_world_novt(game, slot, world_name);
}

void __fastcall HookInsertBack(zCView* view, void*, const zSTRING& texture_name) {
  if (std::exchange(g_disconnect_loading_screen_pending, false)) {
    g_insert_back(view, zSTRING(kGmpLoadingScreenTexture));
    return;
  }
  g_insert_back(view, texture_name);
}

const char* StateName(ContentTransitionManager::State state) {
  switch (state) {
    case ContentTransitionManager::State::Base:
      return "Base";
    case ContentTransitionManager::State::Connecting:
      return "Connecting";
    case ContentTransitionManager::State::Downloading:
      return "Downloading";
    case ContentTransitionManager::State::ActivatingAddon:
      return "ActivatingAddon";
    case ContentTransitionManager::State::Server:
      return "Server";
    case ContentTransitionManager::State::DeactivatingAddon:
      return "DeactivatingAddon";
    case ContentTransitionManager::State::ReturningToBase:
      return "ReturningToBase";
  }
  return "Unknown";
}

class ScopedResourceThreadingPause {
public:
  ScopedResourceThreadingPause() {
    if (zresMan) {
      previous_ = zresMan->GetThreadingEnabled();
      zresMan->SetThreadingEnabled(0);
    }
  }

  ~ScopedResourceThreadingPause() {
    if (zresMan) {
      zresMan->SetThreadingEnabled(previous_);
    }
  }

private:
  int previous_{0};
};

}  // namespace

void PrepareDisconnectLoadingScreen() { g_disconnect_loading_screen_pending = true; }

bool ContentTransitionManager::Initialize(std::string& error) {
  if (!GothicVfsOverlay::Instance().InstallHooks(error)) {
    return false;
  }
  if (!g_load_world_novt) {
    const auto trampoline = CreateHook(kLoadWorldNoVtAddress, reinterpret_cast<DWORD>(&HookLoadWorldNoVt));
    if (!trampoline) {
      error = "Failed to install the addon world-load lifecycle hook";
      return false;
    }
    g_load_world_novt = reinterpret_cast<LoadWorldNoVtFn>(*trampoline);
    SPDLOG_INFO("Addon content: installed pre-world-load lifecycle hook");
  }
  if (!g_insert_back) {
    const auto trampoline = CreateHook(kInsertBackAddress, reinterpret_cast<DWORD>(&HookInsertBack));
    if (!trampoline) {
      error = "Failed to install the disconnect loading-screen hook";
      return false;
    }
    g_insert_back = reinterpret_cast<InsertBackFn>(*trampoline);
    SPDLOG_INFO("Addon content: installed disconnect loading-screen hook");
  }
  g_content_transition_manager = this;
  error.clear();
  return true;
}

bool ContentTransitionManager::BeginConnection(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != State::Base) {
    error = std::string("Cannot start a connection while content state is ") + StateName(state_);
    return false;
  }
  state_ = State::Connecting;
  SPDLOG_INFO("Content transition: Base -> Connecting");
  error.clear();
  return true;
}

void ContentTransitionManager::MarkDownloading() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::Connecting) {
    state_ = State::Downloading;
    SPDLOG_INFO("Content transition: Connecting -> Downloading");
  }
}

void ContentTransitionManager::AbortConnection() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::Connecting || state_ == State::Downloading) {
    SPDLOG_INFO("Content transition: {} -> Base", StateName(state_));
    state_ = State::Base;
  }
}

bool ContentTransitionManager::ActivateServerContent(const std::vector<std::filesystem::path>& archives, bool contains_gothic_dat,
                                                     std::string& error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Connecting && state_ != State::Downloading) {
      error = std::string("Cannot activate server content while state is ") + StateName(state_);
      return false;
    }
    state_ = State::ActivatingAddon;
  }
  SPDLOG_INFO("Content transition: ActivatingAddon ({} archive(s), GOTHIC.DAT={})", archives.size(), contains_gothic_dat);

  if (!archives.empty() && !GothicVfsOverlay::Instance().Activate(archives, error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::Base;
    active_has_addon_archives_ = false;
    return false;
  }

  if (contains_gothic_dat && !ReloadGothicDat(error)) {
    GothicVfsOverlay::Instance().Deactivate();
    std::string rollback_error;
    if (!ReloadGothicDat(rollback_error)) {
      error += "; base parser rollback also failed: " + rollback_error;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::Base;
    active_has_addon_archives_ = false;
    active_has_gothic_dat_ = false;
    return false;
  }
  if (!contains_gothic_dat && !archives.empty()) {
    PurgeResourceCaches("activating asset-only addon content");
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_has_addon_archives_ = !archives.empty();
    active_has_gothic_dat_ = contains_gothic_dat;
    state_ = State::Server;
  }
  SPDLOG_INFO("Content transition: ActivatingAddon -> Server");
  error.clear();
  return true;
}

bool ContentTransitionManager::DeactivateServerContent(std::string& error) {
  bool purge_addon_assets = false;
  bool reload_base_dat = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == State::Base) {
      error.clear();
      return true;
    }
    if (state_ == State::Connecting || state_ == State::Downloading) {
      state_ = State::Base;
      active_has_addon_archives_ = false;
      active_has_gothic_dat_ = false;
      error.clear();
      return true;
    }
    if (state_ != State::Server) {
      error = std::string("Cannot deactivate server content while state is ") + StateName(state_);
      return false;
    }
    state_ = State::DeactivatingAddon;
    purge_addon_assets = active_has_addon_archives_;
    reload_base_dat = active_has_gothic_dat_;
  }

  SPDLOG_INFO("Content transition: Server -> DeactivatingAddon");
  GothicVfsOverlay::Instance().Deactivate();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::ReturningToBase;
  }

  bool success = true;
  if (reload_base_dat) {
    success = ReloadGothicDat(error);
  } else if (purge_addon_assets) {
    PurgeResourceCaches("returning from asset-only addon content");
    error.clear();
  } else {
    error.clear();
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_has_addon_archives_ = false;
    active_has_gothic_dat_ = false;
    purge_before_next_world_load_ = purge_addon_assets;
    state_ = State::Base;
  }
  SPDLOG_INFO("Content transition: ReturningToBase -> Base");
  return success;
}

bool ContentTransitionManager::HasActiveAddonArchives() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_ == State::Server && active_has_addon_archives_;
}

void ContentTransitionManager::PrepareForWorldLoad() {
  bool purge = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    purge = purge_before_next_world_load_;
    purge_before_next_world_load_ = false;
  }
  if (purge) {
    PurgeResourceCaches("old addon world has been disposed and the base world is about to load");
  }
}

ContentTransitionManager::State ContentTransitionManager::CurrentState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void ContentTransitionManager::PurgeResourceCaches(const char* reason) {
  ScopedResourceThreadingPause pause;
  if (zresMan) {
    zresMan->PurgeCaches(nullptr);
    SPDLOG_INFO("Purged Gothic resource caches while {}", reason);
  }
}

bool ContentTransitionManager::ReloadGothicDat(std::string& error) {
  if (!ogame) {
    error = "Gothic game instance is unavailable during parser reload";
    return false;
  }

  ScopedResourceThreadingPause pause;
  if (zresMan) {
    zresMan->PurgeCaches(nullptr);
  }
  zSTRING parser_file("GOTHIC.DAT");
  if (!ogame->LoadParserFile(parser_file)) {
    error = "Gothic rejected GOTHIC.DAT while reloading the parser";
    SPDLOG_ERROR("{}", error);
    return false;
  }
  if (zresMan) {
    zresMan->PurgeCaches(nullptr);
  }
  SPDLOG_INFO("Reloaded Gothic parser from GOTHIC.DAT and purged resource caches");
  error.clear();
  return true;
}

}  // namespace gmp::gothic
