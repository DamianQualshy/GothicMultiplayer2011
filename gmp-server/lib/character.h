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

#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common_structs.h"

// State shared by connected players and server-owned NPCs. A character has
// viewers, but neither a network connection nor a set of entities it streams.
struct Character {
  using CharacterId = std::uint32_t;

  // Retain the existing name for compatibility with player packets and APIs.
  CharacterId player_id{0};
  std::string name;
  std::string instance;
  std::uint8_t name_color_r{255};
  std::uint8_t name_color_g{255};
  std::uint8_t name_color_b{255};
  std::string world;
  std::int32_t virtual_world{0};
  std::unordered_set<CharacterId> streamed_by_players;

  std::string body_model;
  std::int16_t body_texture{0};
  std::string head_model;
  std::int16_t head_texture{0};
  std::int16_t teeth_texture{0};
  std::int16_t skin_color{0};
  float fatness{1.0f};
  glm::vec3 scale{1.0f, 1.0f, 1.0f};
  std::vector<std::string> overlays;

  std::uint8_t flags{0};
  std::uint8_t walkstyle{0};
  std::uint8_t fight_pos{0};
  std::uint8_t spellhand{0};
  std::uint8_t headstate{0};
  std::uint8_t is_ingame{0};

  std::int16_t health{0};
  std::int16_t max_health{100};
  std::int16_t mana{0};
  std::int16_t max_mana{100};
  std::int32_t level{0};
  std::int32_t exp{0};
  std::int32_t next_level_exp{0};
  std::int32_t learn_points{0};
  std::int32_t strength{0};
  std::int32_t dexterity{0};
  std::unordered_map<std::string, std::int32_t> inventory;
  std::unordered_map<int, int> weapon_skills;
  std::unordered_map<int, int> talents;

  std::time_t tod{0};
  std::optional<std::int32_t> respawn_time_ms;
  PlayerState state{};
  std::uint32_t state_sequence{0};
  std::optional<std::int32_t> pending_equipped_armor_instance;
  std::optional<std::int32_t> pending_equipped_helmet_instance;
  std::optional<std::int32_t> pending_equipped_shield_instance;
  std::optional<std::int32_t> pending_melee_weapon_instance;
  std::optional<std::int32_t> pending_ranged_weapon_instance;
};
