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
#include <unordered_set>

#include "ZenGin/zGothicAPI.h"
#include "sol/sol.hpp"

#include "lua_vob.h"

namespace gmp::gothic {

class LuaSound3d {
public:
  explicit LuaSound3d(const std::string& filename);
  ~LuaSound3d();

  void play();
  void stop();
  bool isPlaying() const;
  void update();

  static void UpdateActiveSounds();

  void setTargetVob(const LuaVob& vob);
  bool setTargetPlayer(std::uint64_t player_id);

  std::string getFile() const;
  void setFile(const std::string& filename);

  float getPlayingTime() const;

  float getVolume() const;
  void setVolume(float volume);

  bool getLooping() const;
  void setLooping(bool looping);

  float getBalance() const;
  void setBalance(float balance);

  float getObstruction() const;
  void setObstruction(float obstruction);

  float getRadius() const;
  void setRadius(float radius);

  float getConeAngle() const;
  void setConeAngle(float angle);

  float getReverbLevel() const;
  void setReverbLevel(float level);

  bool getAmbient() const;
  void setAmbient(bool ambient);

  float getPitchOffset() const;
  void setPitchOffset(float pitch_offset);

  sol::object getTargetVob(sol::this_state ts) const;
  void setTargetVobValue(sol::object value);

private:
  void ReloadSound();
  void StopIfNeeded();
  void ApplyParams();
  void ReplayIfNeeded(bool was_playing);

  std::string file_;
  float volume_;
  bool looping_;
  float balance_;
  int handle_;
  Gothic_II_Addon::zCVob* target_vob_;
  Gothic_II_Addon::zCSoundFX* sound_fx_;
  Gothic_II_Addon::zCSoundSystem::zTSound3DParams params_;

  static std::unordered_set<LuaSound3d*> active_sounds_;
};

void BindSound3d(sol::state& lua);

}  // namespace gmp::gothic
