/*
MIT License

Copyright (c) 2022 Gothic Multiplayer Team (pampi, skejt23, mecio)

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

#include "game_server.h"

#include <bitsery/ext/value_range.h>
#include <bitsery/traits/vector.h>
#include <httplib.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <version.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <dylib.hpp>
#include <limits>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>

#include "gothic_clock.h"
#include "Lua/event_bind.h"
#include "master_server_endpoint.h"
#include "net_enums.h"
#include "packets.h"
#include "platform_depend.h"
#include "server_events.h"
#include "server_constants.h"
#include "shared/event.h"
#include "shared/lua_runtime/lua_value_codec.h"
#include "shared/math.h"
#include "sol/sol.hpp"
#include "znet_server.h"

Net::NetServer* g_net_server = nullptr;

std::atomic<bool> g_is_server_running = true;
void (*g_destroy_net_server_func)(Net::NetServer*) = nullptr;

using namespace Net;

namespace {

constexpr std::size_t kMaxWorldNameLength = 32;
constexpr std::size_t kMaxPlayerNameLength = 64;
constexpr std::uint32_t kItemGroundChannel = 14;
constexpr const char* kBanListFileName = "bans.json";
constexpr const char* kItemRegistryPath = "instances/items.json";
constexpr const char* kAnimationRegistryPath = "instances/anims.json";
constexpr std::string_view kFrame = "-========================================-";
constexpr auto kPlayerPingUpdateInterval = std::chrono::milliseconds(2500);
constexpr std::uint8_t kFullSkySettingsFlags = SKY_SETTING_WEATHER | SKY_SETTING_RAIN_START | SKY_SETTING_RAIN_STOP |
                                                SKY_SETTING_WIND_SCALE | SKY_SETTING_DONT_RAIN | SKY_SETTING_RAIN_WEIGHT |
                                                SKY_SETTING_LIGHTNING;

SkySettingsPacket MakeSkySettingsPacket(std::uint8_t flags, std::int32_t weather_type, std::int32_t rain_start_hour,
                                        std::int32_t rain_start_min, std::int32_t rain_stop_hour, std::int32_t rain_stop_min,
                                        float wind_scale, bool dont_rain, float rain_weight, bool render_lightning) {
  SkySettingsPacket packet{};
  packet.packet_type = PT_SKY_SETTINGS;
  packet.flags = flags;
  packet.weather_type = weather_type;
  packet.rain_start_hour = static_cast<std::int16_t>(rain_start_hour);
  packet.rain_start_min = static_cast<std::int16_t>(rain_start_min);
  packet.rain_stop_hour = static_cast<std::int16_t>(rain_stop_hour);
  packet.rain_stop_min = static_cast<std::int16_t>(rain_stop_min);
  packet.wind_scale = wind_scale;
  packet.dont_rain = dont_rain ? 1 : 0;
  packet.rain_weight = std::clamp(rain_weight, 0.0f, 1.0f);
  packet.render_lightning = render_lightning ? 1 : 0;
  return packet;
}

std::string FormatConnectionDetails(ConnectionHandle id) {
  const auto id_string = std::to_string(id);
  if (!g_net_server) {
    return "id " + id_string;
  }

  std::string address = g_net_server->GetPlayerIp(id);
  if (address.empty() || address == "UNASSIGNED_SYSTEM_ADDRESS") {
    return "id " + id_string;
  }

  address.append(", id ").append(id_string);
  return address;
}

std::string FormatPlayerLabel(const PlayerManager::Player& player) {
  const auto connection_details = FormatConnectionDetails(player.connection);
  if (player.name.empty()) {
    return connection_details;
  }
  std::string label = player.name;
  label.append(" (").append(connection_details).append(")");
  return label;
}

#ifdef MASTER_SERVER_ENDPOINT
constexpr std::string_view kMasterServerEndpoint = MASTER_SERVER_ENDPOINT;
#else
constexpr std::string_view kMasterServerEndpoint{};
#endif

std::string FormatCurrentDateTime() {
  auto now = std::time(nullptr);
  std::tm local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &now);
#else
  localtime_r(&now, &local_tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%H:%M:%S %d/%m/%Y");
  return oss.str();
}

std::uint8_t GetPlayerLifeState(const PlayerManager::Player& player) {
  if (player.tod != 0) {
    return PLAYER_LIFE_DEAD;
  }
  if ((player.flags & PlayerManager::PL_UNCONCIOUS) != 0) {
    return PLAYER_LIFE_UNCONSCIOUS;
  }
  return PLAYER_LIFE_ALIVE;
}

void ClearTransientCombatState(PlayerManager::Player& player) {
  player.state.left_hand_item_instance = 0;
  player.state.right_hand_item_instance = 0;
  player.state.animation = -1;
  player.state.animation_name.clear();
  player.state.weapon_mode = 0;
  player.state.active_spell_nr = 0;
  player.state.active_spell_instance = 0;
}

void PopulatePlayerSpawnSnapshot(PlayerSpawnPacket& packet, const PlayerManager::Player& player) {
  packet.instance = player.instance;
  packet.name_color_r = player.name_color_r;
  packet.name_color_g = player.name_color_g;
  packet.name_color_b = player.name_color_b;

  packet.strength = player.strength;
  packet.dexterity = player.dexterity;
  packet.level = player.level;
  packet.exp = player.exp;
  packet.next_level_exp = player.next_level_exp;
  packet.learn_points = player.learn_points;
  packet.health = player.health;
  packet.max_health = player.max_health;
  packet.mana = player.mana;
  packet.max_mana = player.max_mana;
  packet.life_state = GetPlayerLifeState(player);

  packet.fatness = player.fatness;
  packet.scale = player.scale;

  packet.weapon_skills.clear();
  packet.weapon_skills.reserve(player.weapon_skills.size());
  for (const auto& [skill_id, percentage] : player.weapon_skills) {
    PlayerSpawnPacket::SkillEntry entry;
    entry.skill_id = skill_id;
    entry.percentage = percentage;
    packet.weapon_skills.push_back(std::move(entry));
  }

  packet.talents.clear();
  packet.talents.reserve(player.talents.size());
  for (const auto& [talent_id, value] : player.talents) {
    PlayerSpawnPacket::TalentEntry entry;
    entry.talent_id = talent_id;
    entry.value = value;
    packet.talents.push_back(std::move(entry));
  }

  packet.overlays = player.overlays;
}

std::uint32_t AdvancePlayerStateSequence(PlayerManager::Player& player) {
  ++player.state_sequence;
  if (player.state_sequence == 0) {
    ++player.state_sequence;
  }
  return player.state_sequence;
}

PlayerStateUpdatePacket MakePlayerStateUpdatePacket(const PlayerManager::Player& player) {
  PlayerStateUpdatePacket packet{};
  packet.packet_type = PT_ACTUAL_STATISTICS;
  packet.player_id = player.player_id;
  packet.state_sequence = player.state_sequence;
  packet.state = player.state;
  packet.state.health_points = player.health;
  packet.state.mana_points = player.mana;
  packet.state.life_state = GetPlayerLifeState(player);
  return packet;
}

PlayerSpawnPacket MakePlayerSpawnPacket(const PlayerManager::Player& player) {
  PlayerSpawnPacket packet{};
  packet.packet_type = PT_PLAYER_SPAWN;
  packet.player_id = player.player_id;
  packet.state_sequence = player.state_sequence;
  packet.player_name = player.name;
  packet.position = player.state.position;
  packet.normal = player.state.nrot;
  packet.left_hand_item_instance = player.state.left_hand_item_instance;
  packet.right_hand_item_instance = player.state.right_hand_item_instance;
  packet.equipped_armor_instance = player.state.equipped_armor_instance;
  packet.equipped_helmet_instance = player.state.equipped_helmet_instance;
  packet.equipped_shield_instance = player.state.equipped_shield_instance;
  packet.equipped_amulet_instance = player.state.equipped_amulet_instance;
  packet.equipped_belt_instance = player.state.equipped_belt_instance;
  packet.equipped_ring_left_instance = player.state.equipped_ring_left_instance;
  packet.equipped_ring_right_instance = player.state.equipped_ring_right_instance;
  packet.animation = player.state.animation;
  packet.animation_name = player.state.animation_name;
  packet.body_model = player.body_model;
  packet.body_texture = player.body_texture;
  packet.head_model = player.head_model;
  packet.head_texture = player.head_texture;
  packet.walk_style = player.walkstyle;
  PopulatePlayerSpawnSnapshot(packet, player);
  return packet;
}

bool IsSameVisibilityScope(const PlayerManager::Player& a, const PlayerManager::Player& b) {
  return a.world == b.world && a.virtual_world == b.virtual_world;
}

bool IsInsideStreamRange(const PlayerManager::Player& viewer, const PlayerManager::Player& subject, float radius, float height) {
  if (!IsSameVisibilityScope(viewer, subject)) {
    return false;
  }

  const auto delta = subject.state.position - viewer.state.position;
  if (height > 0.0f && std::abs(delta.y) > height) {
    return false;
  }

  const auto radius_squared = radius * radius;
  return delta.x * delta.x + delta.z * delta.z <= radius_squared;
}

std::string SanitizeWorldName(std::string world) {
  if (world.size() > kMaxWorldNameLength) {
    SPDLOG_WARN("World name '{}' is longer than {} characters and will be truncated", world, kMaxWorldNameLength);
    world.resize(kMaxWorldNameLength);
  }

  return world;
}

std::unique_ptr<httplib::Client> CreateMasterServerClient(const master_server::EndpointInfo& info) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  if (info.use_https) {
    auto client = std::make_unique<httplib::SSLClient>(info.host, info.port);
    client->enable_server_certificate_verification(true);
    return client;
  }
#else
  if (info.use_https) {
    SPDLOG_ERROR("Master server endpoint '{}' requires HTTPS support, but the build lacks OpenSSL support.", info.host);
    return nullptr;
  }
#endif

  return std::make_unique<httplib::Client>(info.host, info.port);
}

std::string SanitizeServerText(std::string text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch < 0x20 && ch != 0x07) {
      text.resize(i);
      break;
    }
  }
  return text;
}

std::string SanitizePlayerName(std::string name) {
  name = SanitizeServerText(std::move(name));
  if (name.size() > kMaxPlayerNameLength) {
    SPDLOG_WARN("Player name '{}' is longer than {} characters and will be truncated", name, kMaxPlayerNameLength);
    name.resize(kMaxPlayerNameLength);
  }
  return name;
}

std::string SanitizePlayerAnimationName(std::string name) {
  name = SanitizeServerText(std::move(name));
  if (name.size() > kMaxPlayerAnimationNameLength) {
    name.resize(kMaxPlayerAnimationNameLength);
  }
  return name;
}

std::uint8_t ClampColorComponent(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

MessagePacket CreateMessagePacket(std::optional<std::uint32_t> sender_id, std::optional<std::uint32_t> recipient_id, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::string text, std::uint8_t packet_type = PT_MSG) {
  MessagePacket packet{};
  packet.packet_type = packet_type;
  packet.message = SanitizeServerText(std::move(text));
  packet.r = ClampColorComponent(static_cast<int>(r));
  packet.g = ClampColorComponent(static_cast<int>(g));
  packet.b = ClampColorComponent(static_cast<int>(b));
  packet.sender = sender_id;
  packet.recipient = recipient_id;
  return packet;
}

void LogServerBanner() {
  SPDLOG_INFO(kFrame);
  SPDLOG_INFO("-= Gothic Multiplayer Dedicated Server");
  SPDLOG_INFO(kFrame);

  constexpr std::string_view git_tag_long = GIT_TAG_LONG;
  if (!git_tag_long.empty()) {
    SPDLOG_INFO("-= Version: {}", git_tag_long);
  } else {
    SPDLOG_INFO("-= Version: Development build");
  }

  constexpr std::string_view git_commit = GIT_COMMIT_LONG;
  if (!git_commit.empty()) {
    SPDLOG_INFO("-= Commit: {}", git_commit);
  }

  SPDLOG_INFO("-= Build date: {} {}", __DATE__, __TIME__);
  SPDLOG_INFO("-= GMP Team 2011-2025");
}

template <typename Packet, typename TContainer = std::vector<std::uint8_t>>
void SerializeAndSend(const Packet& packet, Net::PacketPriority priority, Net::PacketReliability reliable, Net::ConnectionHandle id,
                      std::uint32_t channel = 0) {
  TContainer buffer;
  auto written_size = bitsery::quickSerialization<bitsery::OutputBufferAdapter<TContainer>>(buffer, packet);
  g_net_server->Send(buffer.data(), written_size, priority, reliable, channel, id);
}

template <typename Packet>
void BroadcastToRelevant(PlayerManager& player_manager, const PlayerManager::Player& subject, const Packet& packet, Net::PacketPriority priority,
                         Net::PacketReliability reliable, std::uint32_t channel = 0) {
  if (subject.is_ingame) {
    SerializeAndSend(packet, priority, reliable, subject.connection, channel);
  }

  for (const auto& viewer_id : subject.streamed_by_players) {
    auto viewer_opt = player_manager.GetPlayer(viewer_id);
    if (!viewer_opt.has_value()) {
      continue;
    }
    const auto& viewer = viewer_opt->get();
    if (!viewer.is_ingame) {
      continue;
    }
    if (viewer.world != subject.world || viewer.virtual_world != subject.virtual_world) {
      continue;
    }
    SerializeAndSend(packet, priority, reliable, viewer.connection, channel);
  }
}

void BroadcastPlayerStateToViewers(PlayerManager& player_manager, const PlayerManager::Player& subject) {
  if (!subject.is_ingame) {
    return;
  }

  const auto packet = MakePlayerStateUpdatePacket(subject);
  for (const auto viewer_id : subject.streamed_by_players) {
    auto viewer_opt = player_manager.GetPlayer(viewer_id);
    if (!viewer_opt.has_value()) {
      continue;
    }

    const auto& viewer = viewer_opt->get();
    if (!viewer.is_ingame || !IsSameVisibilityScope(viewer, subject)) {
      continue;
    }

    SerializeAndSend(packet, IMMEDIATE_PRIORITY, UNRELIABLE, viewer.connection);
  }
}

void StreamOutSubjectFromViewer(PlayerManager::Player& subject, PlayerManager::Player& viewer) {
  if (subject.streamed_by_players.erase(viewer.player_id) == 0) {
    return;
  }

  viewer.spawned_players.erase(subject.player_id);

  DisconnectionInfoPacket packet{};
  packet.packet_type = PT_LEFT_GAME;
  packet.disconnected_id = subject.player_id;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, viewer.connection);
}

void StreamOutAllKnownPlayers(PlayerManager& player_manager, PlayerManager::Player& player) {
  const std::vector<PlayerManager::PlayerId> viewers(player.streamed_by_players.begin(), player.streamed_by_players.end());
  for (const auto viewer_id : viewers) {
    auto viewer_opt = player_manager.GetPlayer(viewer_id);
    if (!viewer_opt.has_value()) {
      player.streamed_by_players.erase(viewer_id);
      continue;
    }

    StreamOutSubjectFromViewer(player, viewer_opt->get());
  }

  const std::vector<PlayerManager::PlayerId> spawned_players(player.spawned_players.begin(), player.spawned_players.end());
  for (const auto spawned_id : spawned_players) {
    auto spawned_opt = player_manager.GetPlayer(spawned_id);
    if (!spawned_opt.has_value()) {
      player.spawned_players.erase(spawned_id);
      continue;
    }

    StreamOutSubjectFromViewer(spawned_opt->get(), player);
  }

  player.spawned_players.clear();
  player.streamed_by_players.clear();
}

bool CanSeeItemGround(const PlayerManager::Player& player, const ItemGroundManager::ItemGround& item_ground) {
  return player.is_ingame && player.world == item_ground.world && player.virtual_world == item_ground.virtual_world;
}

void AddInventoryItem(PlayerManager::Player& player, const std::string& instance, std::int32_t amount) {
  if (instance.empty() || amount <= 0) {
    return;
  }

  auto& stored_amount = player.inventory[instance];
  if (stored_amount < 0) {
    stored_amount = 0;
  }
  const std::int64_t next_amount = static_cast<std::int64_t>(stored_amount) + amount;
  stored_amount = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(next_amount, 0, std::numeric_limits<std::int32_t>::max()));
}

void RemoveInventoryItem(PlayerManager::Player& player, const std::string& instance, std::int32_t amount) {
  if (instance.empty() || amount <= 0) {
    return;
  }

  auto it = player.inventory.find(instance);
  if (it == player.inventory.end()) {
    return;
  }

  const std::int32_t next_amount = it->second - amount;
  if (next_amount <= 0) {
    player.inventory.erase(it);
  } else {
    it->second = next_amount;
  }
}

void SendPlayerAttributeUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, PlayerAttributeId attribute_id,
                               std::int32_t value) {
  PlayerAttributeUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_ATTRIBUTE_UPDATE;
  packet.player_id = subject.player_id;
  packet.attribute_id = attribute_id;
  packet.value = value;

  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerAttributeSnapshot(PlayerManager& player_manager, const PlayerManager::Player& subject, Net::ConnectionHandle connection) {
  PlayerAttributeSnapshotPacket packet{};
  packet.packet_type = PT_PLAYER_ATTRIBUTE_SNAPSHOT;
  packet.player_id = subject.player_id;
  packet.strength = subject.strength;
  packet.dexterity = subject.dexterity;
  packet.level = subject.level;
  packet.exp = subject.exp;
  packet.next_level_exp = subject.next_level_exp;
  packet.learn_points = subject.learn_points;
  packet.health = subject.health;
  packet.max_health = subject.max_health;
  packet.mana = subject.mana;
  packet.max_mana = subject.max_mana;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void SendPlayerInstanceUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, Net::ConnectionHandle connection) {
  PlayerInstanceUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_INSTANCE_UPDATE;
  packet.player_id = subject.player_id;
  packet.instance = subject.instance;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerInstanceUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject) {
  PlayerInstanceUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_INSTANCE_UPDATE;
  packet.player_id = subject.player_id;
  packet.instance = subject.instance;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerColorUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, Net::ConnectionHandle connection) {
  PlayerColorUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_COLOR_UPDATE;
  packet.player_id = subject.player_id;
  packet.r = subject.name_color_r;
  packet.g = subject.name_color_g;
  packet.b = subject.name_color_b;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerColorUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject) {
  PlayerColorUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_COLOR_UPDATE;
  packet.player_id = subject.player_id;
  packet.r = subject.name_color_r;
  packet.g = subject.name_color_g;
  packet.b = subject.name_color_b;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerSkillWeaponUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, std::int32_t skill_id,
                                 std::int32_t percentage, Net::ConnectionHandle connection) {
  PlayerSkillWeaponUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_SKILL_WEAPON_UPDATE;
  packet.player_id = subject.player_id;
  packet.skill_id = skill_id;
  packet.percentage = percentage;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerSkillWeaponUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, std::int32_t skill_id,
                                      std::int32_t percentage) {
  PlayerSkillWeaponUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_SKILL_WEAPON_UPDATE;
  packet.player_id = subject.player_id;
  packet.skill_id = skill_id;
  packet.percentage = percentage;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerTalentUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, std::int32_t talent_id,
                            std::int32_t talent_value, Net::ConnectionHandle connection) {
  PlayerTalentUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_TALENT_UPDATE;
  packet.player_id = subject.player_id;
  packet.talent_id = talent_id;
  packet.talent_value = talent_value;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerTalentUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, std::int32_t talent_id,
                                 std::int32_t talent_value) {
  PlayerTalentUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_TALENT_UPDATE;
  packet.player_id = subject.player_id;
  packet.talent_id = talent_id;
  packet.talent_value = talent_value;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerFatnessUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, Net::ConnectionHandle connection) {
  PlayerFatnessUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_FATNESS_UPDATE;
  packet.player_id = subject.player_id;
  packet.fatness = subject.fatness;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerFatnessUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject) {
  PlayerFatnessUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_FATNESS_UPDATE;
  packet.player_id = subject.player_id;
  packet.fatness = subject.fatness;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerScaleUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, Net::ConnectionHandle connection) {
  PlayerScaleUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_SCALE_UPDATE;
  packet.player_id = subject.player_id;
  packet.scale = subject.scale;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerScaleUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject) {
  PlayerScaleUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_SCALE_UPDATE;
  packet.player_id = subject.player_id;
  packet.scale = subject.scale;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void SendPlayerOverlayUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, const std::string& overlay,
                             bool apply, Net::ConnectionHandle connection) {
  PlayerOverlayUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_OVERLAY_UPDATE;
  packet.player_id = subject.player_id;
  packet.overlay = overlay;
  packet.apply = apply ? 1 : 0;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, connection);
}

void BroadcastPlayerOverlayUpdate(PlayerManager& player_manager, const PlayerManager::Player& subject, const std::string& overlay,
                                  bool apply) {
  PlayerOverlayUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_OVERLAY_UPDATE;
  packet.player_id = subject.player_id;
  packet.overlay = overlay;
  packet.apply = apply ? 1 : 0;
  BroadcastToRelevant(player_manager, subject, packet, IMMEDIATE_PRIORITY, RELIABLE);
}

std::vector<std::string> BuildPreferredAnimationMdsList(const PlayerManager::Player& player) {
  std::vector<std::string> preferred_mds;
  preferred_mds.reserve(player.overlays.size());
  for (auto it = player.overlays.rbegin(); it != player.overlays.rend(); ++it) {
    preferred_mds.push_back(*it);
  }
  return preferred_mds;
}

void LoadNetworkLibrary() {
  try {
    static dylib lib("znet_server");
    auto create_net_server_func = lib.get_function<Net::NetServer*()>("CreateNetServer");
    g_destroy_net_server_func = lib.get_function<void(Net::NetServer*)>("DestroyNetServer");
    g_net_server = create_net_server_func();
  } catch (std::exception& ex) {
    SPDLOG_ERROR("LoadNetworkLibrary error: {}", ex.what());
    std::abort();
  }
}

void InitializeLogger(const Config& config) {
  auto logger = spdlog::default_logger();
  logger->sinks().clear();

  if (config.Get<bool>("log_to_stdout")) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%^%l%$] %v");
    logger->sinks().push_back(std::move(console_sink));
  }

  auto log_file = config.Get<std::string>("log_file");
  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::move(log_file), false);
  file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
  logger->sinks().push_back(std::move(file_sink));

  auto log_level = config.Get<std::string>("log_level");
  spdlog::set_level(spdlog::level::from_str(log_level));
  spdlog::flush_on(spdlog::level::debug);
}
}  // namespace

GameServer::GameServer() {
  InitializeLogger(config_);
  LogServerBanner();
  config_.LogConfigValues();
  server_world_ = SanitizeWorldName(config_.Get<std::string>("map"));
  config_.Set<std::string>("map", server_world_);
  g_server = this;

  // Register server-side events.
  EventManager::Instance().RegisterEvent(kEventOnTickName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerConnectName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerDisconnectName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerMessageName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerCommandName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerUnconsciousName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerStandUpName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerDeathName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerDropItemName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerTakeItemName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerChangeHealthName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerChangeManaName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerWorldChangeName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerWorldEnterName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerWeaponModeChangeName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipAmuletName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipArmorName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipBeltName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipHandItemName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipHelmetName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipMeleeWeaponName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipRangedWeaponName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipRingName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipShieldName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerEquipSpellSlotName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerCastSpellName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerSpawnName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerRespawnName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerSpawnForName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerUnspawnForName);
  EventManager::Instance().RegisterEvent(kEventOnPlayerHitName);
}

GameServer::~GameServer() {
  g_is_server_running = false;
  if (main_thread.joinable()) {
    main_thread_running.store(false, std::memory_order_release);
    main_thread.join();
  }

  resource_server_.reset();

  EventManager::Instance().Reset();
  resource_manager_.reset();
  lua_script_.reset();

  if (public_list_http_thread_future_.valid()) {
    public_list_http_thread_future_.wait();
  }

  if (g_net_server != nullptr) {
    g_net_server->RemovePacketHandler(*this);
    g_destroy_net_server_func(g_net_server);
  }

  g_server = nullptr;
}

bool GameServer::Init() {
  LoadNetworkLibrary();
  g_net_server->AddPacketHandler(*this);
#ifndef WIN32
  if (config_.Get<bool>("daemon")) {
    System::MakeMeDaemon(false);
  }
#endif
  auto slots = config_.Get<std::int32_t>("slots");
  allow_modification = config_.Get<bool>("allow_modification");

  auto port = config_.Get<std::int32_t>("port");

  if (!item_registry_.Load(kItemRegistryPath)) {
    return false;
  }

  if (!animation_registry_.Load(kAnimationRegistryPath)) {
    return false;
  }

  if (!g_net_server->Start(port, slots)) {
    SPDLOG_CRITICAL("Failed to start server on port {}", port);
    return false;
  }

  const auto bound_port = static_cast<std::uint16_t>(g_net_server->GetPort());

  ban_manager_ = std::make_unique<BanManager>(*g_net_server);
  ban_manager_->Load();
  g_is_server_running = true;

  auto seconds_per_game_minute = config_.Get<std::int32_t>("seconds_per_game_minute");
  clock_ = std::make_unique<GothicClock>(GothicClock::Time{}, seconds_per_game_minute);
  weather_.Initialize(clock_->GetTime());
  if (IsPublic() && !kMasterServerEndpoint.empty()) {
    public_list_http_thread_future_ = std::async(std::launch::async, &GameServer::AddToPublicListHTTP, this);
    SPDLOG_INFO("Master Server heartbeat started.");
  } else if (IsPublic()) {
    SPDLOG_WARN("Server marked as public, but no Master Server endpoint is configured. Skipping registration.");
  } else if (!IsPublic()) {
    SPDLOG_WARN("Server marked as private, skipping connection to Master Server..");
  }
  this->last_stand_timer = 0;

  SPDLOG_INFO(kFrame);

  // Initialize Lua VM
  lua_script_ = std::make_unique<LuaScript>();

  // Initialize resource manager
  resource_manager_ = std::make_unique<ResourceManager>();

  // Set up resource-aware timer binding
  resource_manager_->BindResourceAwareTimer(*lua_script_);

  // Set up exports proxy
  resource_manager_->CreateExportsProxy(lua_script_->GetLuaState());

  // Discover and load all resources from resources/
  auto discovered_resources = resource_manager_->DiscoverResources();
  resource_manager_->LogResourceInfo();

  try {
    client_resource_descriptors_ = ClientResourcePackager::Build(resource_manager_->GetDiscoveredResourceInfo());
  } catch (const std::exception& ex) {
    SPDLOG_ERROR("Failed to pack client resources: {}", ex.what());
    return false;
  }

  resource_server_ = std::make_unique<ResourceServer>(bound_port, std::filesystem::absolute("public"));
  if (!resource_server_->Start()) {
    return false;
  }

  for (const auto& resource_name : discovered_resources) {
    resource_manager_->LoadResource(resource_name, *lua_script_);
  }

  last_update_time_ = std::chrono::steady_clock::now();
  last_ping_update_time_ = last_update_time_;

  main_thread_running.store(true, std::memory_order_release);
  main_thread = std::thread([this]() {
    while (main_thread_running.load(std::memory_order_acquire)) {
      Run();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });
  SPDLOG_INFO("");
  SPDLOG_INFO(kFrame);
  SPDLOG_INFO("Gothic Multiplayer Server initialized successfully!");
  SPDLOG_INFO(kFrame);
  return true;
}

void GameServer::Run() {
  g_net_server->Pulse();
  const auto advanced_times = clock_->RunClock();
  UpdateAuthoritativeWorldState(advanced_times);

  if (lua_script_) {
    lua_script_->ProcessTimers();
  }

  auto now = std::chrono::steady_clock::now();
  if (now - last_ping_update_time_ > kPlayerPingUpdateInterval) {
    last_ping_update_time_ = now;
    BroadcastPlayerPings();
  }

  ProcessRespawns();

  EventManager::Instance().TriggerEvent(kEventOnTickName, OnTickEvent{});

  // Send updates to all players.
  if (now - last_update_time_ > std::chrono::milliseconds(config_.Get<std::int32_t>("tick_rate_ms"))) {
    last_update_time_ = now;
    const auto stream_radius = static_cast<float>(config_.Get<std::int32_t>("stream_radius"));
    const auto stream_height = static_cast<float>(config_.Get<std::int32_t>("stream_height"));

    // Pre-filter active players
    std::vector<std::pair<PlayerId, const Player*>> active_players;
    active_players.reserve(player_manager_.GetPlayerCount());
    player_manager_.ForEachPlayer([&](const Player& player) {
      if (player.is_ingame) {
        active_players.emplace_back(player.player_id, &player);
      }
    });

    using PlayersKey = std::pair<PlayerId, PlayerId>;
    struct PlayersKeyHash {
      std::size_t operator()(const PlayersKey& key) const {
        std::hash<uint64_t> hasher;
        return hasher(key.first) ^ (hasher(key.second) << 1);
      }
    };

    struct PlayersKeyEqual {
      bool operator()(const PlayersKey& lhs, const PlayersKey& rhs) const {
        return lhs.first == rhs.first && lhs.second == rhs.second;
      }
    };

    // Pre-allocate map with estimated size
    std::unordered_map<PlayersKey, float, PlayersKeyHash, PlayersKeyEqual> distances;
    distances.reserve((active_players.size() * (active_players.size() - 1)) / 2);
    // Iteration over player pairs
    for (size_t i = 0; i < active_players.size(); ++i) {
      for (size_t j = i + 1; j < active_players.size(); ++j) {
        PlayersKey key{std::min(active_players[i].first, active_players[j].first), std::max(active_players[i].first, active_players[j].first)};

        if (!IsSameVisibilityScope(*active_players[i].second, *active_players[j].second)) {
          distances[key] = std::numeric_limits<float>::infinity();
          continue;
        }

        const auto delta = active_players[i].second->state.position - active_players[j].second->state.position;
        if (stream_height > 0.0f && std::abs(delta.y) > stream_height) {
          distances[key] = std::numeric_limits<float>::infinity();
          continue;
        }

        distances[key] = std::sqrt(delta.x * delta.x + delta.z * delta.z);
      }
    }

    for (const auto& [players, distance] : distances) {
      auto player_a_opt = player_manager_.GetPlayer(players.first);
      auto player_b_opt = player_manager_.GetPlayer(players.second);

      if (!player_a_opt.has_value() || !player_b_opt.has_value()) {
        continue;
      }

      auto& player_a = player_a_opt->get();
      auto& player_b = player_b_opt->get();

      if (distance < stream_radius) {
        const auto stream_subject_to_viewer = [](PlayerManager::Player& subject, PlayerManager::Player& viewer) {
          if (subject.streamed_by_players.insert(viewer.player_id).second) {
            viewer.spawned_players.insert(subject.player_id);
            const auto spawn_packet = MakePlayerSpawnPacket(subject);
            SerializeAndSend(spawn_packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, viewer.connection);
          }
        };

        stream_subject_to_viewer(player_a, player_b);
        stream_subject_to_viewer(player_b, player_a);

        const auto player_a_update_packet = MakePlayerStateUpdatePacket(player_a);
        const auto player_b_update_packet = MakePlayerStateUpdatePacket(player_b);

        SerializeAndSend(player_a_update_packet, IMMEDIATE_PRIORITY, UNRELIABLE, player_b.connection);
        SerializeAndSend(player_b_update_packet, IMMEDIATE_PRIORITY, UNRELIABLE, player_a.connection);
      } else {
        StreamOutSubjectFromViewer(player_a, player_b);
        StreamOutSubjectFromViewer(player_b, player_a);
      }
    }
  }
}

void GameServer::ProcessRespawns() {
  auto respawn_time_seconds = config_.Get<std::int32_t>("respawn_time_seconds");
  if (respawn_time_seconds < 0) {
    return;
  }

  const auto now = std::time(nullptr);

  player_manager_.ForEachPlayer([&](Player& player) {
    if (!player.is_ingame || player.tod == 0) {
      return;
    }

    if (player.respawn_time_ms.has_value()) {
      const auto respawn_ms = player.respawn_time_ms.value();
      if (respawn_ms == 0) {
        return;
      }
      const auto respawn_seconds = static_cast<std::int64_t>(respawn_ms + 999) / 1000;
      if (player.tod + respawn_seconds > now) {
        return;
      }
    } else if (respawn_time_seconds != 0) {
      if (player.tod + respawn_time_seconds > now) {
        return;
      }
    }

    RespawnPlayerInternal(player);
  });
}

bool GameServer::RespawnPlayerInternal(Player& player) {
  player.flags = 0;
  player.tod = 0;
  const auto old_health = player.health;
  const auto old_mana = player.mana;
  player.health = player.max_health;
  player.mana = player.max_mana;
  player.state.health_points = player.health;
  player.state.mana_points = player.mana;
  player.state.life_state = GetPlayerLifeState(player);
  ClearTransientCombatState(player);
  AdvancePlayerStateSequence(player);
  if (old_health != player.health) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName,
                                          OnPlayerChangeHealthEvent{player.player_id, old_health, player.health});
  }
  if (old_mana != player.mana) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeManaName, OnPlayerChangeManaEvent{player.player_id, old_mana, player.mana});
  }

  SendRespawnInfo(player.player_id);
  EventManager::Instance().TriggerEvent(kEventOnPlayerRespawnName, OnPlayerRespawnEvent{player.player_id, player.state.position});
  return true;
}

bool GameServer::HandlePacket(Net::ConnectionHandle connectionHandle, unsigned char* data, std::uint32_t size) {
  Packet p(data, size, connectionHandle);

  unsigned char packetIdentifier = GetPacketIdentifier(p);

  switch (packetIdentifier) {
    case ID_DISCONNECTION_NOTIFICATION: {
      auto player_opt = player_manager_.GetPlayerByConnection(p.id);
      const auto player_label = player_opt.has_value() ? FormatPlayerLabel(player_opt->get()) : FormatConnectionDetails(p.id);
      if (player_opt.has_value()) {
        SendDisconnectionInfo(player_opt->get().player_id);
      }
      HandlePlayerDisconnect(p.id, gmp::server::DISCONNECTED);
      SPDLOG_INFO("{} disconnected. Still connected users: {}.", player_label, player_manager_.GetPlayerCount());
      break;
    }
    case ID_NEW_INCOMING_CONNECTION: {
      // Add player to the manager
      auto max_slots = GetMaxSlots();
      PlayerId new_player_id = player_manager_.AddPlayer(p.id, "", max_slots);

      if (auto new_player = player_manager_.GetPlayer(new_player_id)) {
        new_player->get().world = server_world_;
        new_player->get().virtual_world = 0;
      }

      // Send packet with initial information.
      InitialInfoPacket packet;
      packet.packet_type = PT_INITIAL_INFO;
      packet.map_name = server_world_;
      packet.player_id = new_player_id;
      packet.server_name = GetHostname();
      packet.max_slots = static_cast<std::uint16_t>(max_slots);
      packet.resource_token = resource_server_->IssueToken(p.id);
      packet.resource_base_path = "/public";
      packet.client_resources.reserve(client_resource_descriptors_.size());
      for (const auto& descriptor : client_resource_descriptors_) {
        ClientResourceInfoEntry entry;
        entry.name = descriptor.name;
        entry.version = descriptor.version;
        entry.manifest_path = descriptor.manifest_path;
        entry.manifest_sha256 = descriptor.manifest_sha256;
        entry.archive_path = descriptor.archive_path;
        entry.archive_sha256 = descriptor.archive_sha256;
        entry.archive_size = descriptor.archive_size;
        packet.client_resources.push_back(std::move(entry));
      }
      SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE, p.id, 9);
    }
      SPDLOG_INFO("Incoming connection from {}. Now connected users: {}.", FormatConnectionDetails(p.id), player_manager_.GetPlayerCount());
      break;
    case ID_INCOMPATIBLE_PROTOCOL_VERSION:
      SPDLOG_WARN("ID_INCOMPATIBLE_PROTOCOL_VERSION");
      break;
    case ID_CONNECTION_LOST: {
      auto player_opt = player_manager_.GetPlayerByConnection(p.id);
      const auto player_label = player_opt.has_value() ? FormatPlayerLabel(player_opt->get()) : FormatConnectionDetails(p.id);
      if (player_opt.has_value()) {
        SendDisconnectionInfo(player_opt->get().player_id);
      }
      HandlePlayerDisconnect(p.id, gmp::server::LOST_CONNECTION);
      SPDLOG_WARN("{} lost connection. Still connected users: {}.", player_label, player_manager_.GetPlayerCount());
      break;
    }
    case PT_REQUEST_FILE_LENGTH:
    case PT_REQUEST_FILE_PART:
      break;
    case PT_JOIN_GAME:
      SomeoneJoinGame(p);
      break;
    case PT_ACTUAL_STATISTICS:  // dostarcza nam informacji o sobie
      HandlePlayerUpdate(p);
      break;
    case PT_MSG:
      HandleNormalMsg(p);
      break;
    case PT_CASTSPELL:
      HandleCastSpell(p, false);
      break;
    case PT_CASTSPELLONTARGET:
      HandleCastSpell(p, true);
      break;
    case PT_PLAYER_HIT: {
      PlayerHitReportPacket packet;
      using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
      auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
      if (!state.second) {
        SPDLOG_WARN("Failed to deserialize PlayerHitReportPacket");
        break;
      }
      auto attacker_opt = player_manager_.GetPlayerByConnection(p.id);
      auto victim_opt = player_manager_.GetPlayer(packet.victim_id);
      if (!attacker_opt.has_value() || !victim_opt.has_value()) {
        break;
      }
      auto& attacker = attacker_opt->get();
      auto& victim = victim_opt->get();
      if (!attacker.is_ingame || !victim.is_ingame || victim.tod != 0) {
        break;
      }
      ApplyPlayerDamage(victim, attacker.player_id, packet.damage, packet.damage_type, packet.dont_kill);
    } break;
    case PT_PLAYER_UNCONSCIOUS: {
      PlayerUnconsciousPacket packet;
      using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
      auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
      if (!state.second) {
        SPDLOG_WARN("Failed to deserialize PlayerUnconsciousPacket");
        break;
      }
      auto player_opt = player_manager_.GetPlayerByConnection(p.id);
      if (!player_opt.has_value()) {
        break;
      }
      auto& player = player_opt->get();
      if (!player.is_ingame || player.tod != 0) {
        break;
      }
      std::optional<PlayerId> attacker;
      if (packet.attacker_id.has_value()) {
        auto attacker_opt = player_manager_.GetPlayer(packet.attacker_id.value());
        if (attacker_opt.has_value() && attacker_opt->get().is_ingame) {
          attacker = packet.attacker_id.value();
        }
      }
      MakePlayerUnconscious(player, attacker);
    } break;
    case PT_PLAYER_STANDUP: {
      PlayerStandUpPacket packet;
      using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
      auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
      if (!state.second) {
        SPDLOG_WARN("Failed to deserialize PlayerStandUpPacket");
        break;
      }
      auto player_opt = player_manager_.GetPlayerByConnection(p.id);
      if (!player_opt.has_value()) {
        break;
      }
      auto& player = player_opt->get();
      if (player.is_ingame && player.tod == 0 && (player.flags & PlayerManager::PL_UNCONCIOUS) != 0) {
        const auto old_health = player.health;
        player.flags &= ~PlayerManager::PL_UNCONCIOUS;
        if (player.health <= 0) {
          player.health = 1;
        }
        player.state.health_points = player.health;
        player.state.life_state = GetPlayerLifeState(player);
        AdvancePlayerStateSequence(player);
        if (old_health != player.health) {
          EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName,
                                                OnPlayerChangeHealthEvent{player.player_id, old_health, player.health});
          SendPlayerAttributeUpdate(player_manager_, player, ATTR_HEALTH, player.health);
        }
        EventManager::Instance().TriggerEvent(kEventOnPlayerStandUpName, OnPlayerStandUpEvent{player.player_id});
        SendStandUpInfo(player.player_id);
      }
    } break;
    case PT_PLAYER_DIED: {
      PlayerDeathReportPacket packet;
      using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
      auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
      if (!state.second) {
        SPDLOG_WARN("Failed to deserialize PlayerDeathReportPacket");
        break;
      }
      auto player_opt = player_manager_.GetPlayerByConnection(p.id);
      if (!player_opt.has_value()) {
        break;
      }
      auto& player = player_opt->get();
      if (!player.is_ingame || player.tod != 0) {
        break;
      }

      std::optional<PlayerId> killer_id;
      if (packet.killer_id.has_value()) {
        auto killer_opt = player_manager_.GetPlayer(packet.killer_id.value());
        if (killer_opt.has_value() && killer_opt->get().is_ingame) {
          killer_id = packet.killer_id.value();
        }
      }
      if (killer_id.has_value()) {
        if ((player.flags & PlayerManager::PL_UNCONCIOUS) == 0) {
          MakePlayerUnconscious(player, killer_id);
          break;
        }
      }
      HandlePlayerDeath(player, killer_id);
    } break;
    case PT_DROPITEM:
      HandleDropItem(p);
      break;
    case PT_TAKEITEM:
      HandleTakeItem(p);
      break;
    case PT_PLAYER_WORLD_ENTER:
      HandlePlayerWorldEnter(p);
      break;
    case PT_GAME_INFO:  // na razie tylko czas
      HandleGameInfo(p);
      break;
    case PT_LUA_EVENT:
      HandleLuaEvent(p);
      break;
    case PT_VOICE:
      HandleVoice(p);
      break;
    default:
      SPDLOG_WARN("(S)He or it try to do something strange. It's packet ID: {}", packetIdentifier);
      break;
  }
  return true;
}

bool GameServer::Receive() {
  g_net_server->Pulse();
  return true;
}

unsigned char GameServer::GetPacketIdentifier(const Packet& p) {
  if ((unsigned char)p.data[0] == ID_TIMESTAMP) {
    return (unsigned char)p.data[1 + sizeof(std::uint32_t)];
  } else
    return (unsigned char)p.data[0];
}

void GameServer::DeleteFromPlayerList(PlayerId player_id) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (player_opt.has_value()) {
    UnstreamGroundItemsFromPlayer(player_opt->get(), false);
  }
  player_manager_.RemovePlayer(player_id);
}

void GameServer::HandlePlayerDisconnect(Net::ConnectionHandle connection, std::int32_t reason) {
  resource_server_->RevokeToken(connection);

  auto player_opt = player_manager_.GetPlayerByConnection(connection);
  if (player_opt.has_value()) {
    auto& player = player_opt.value().get();
    if (player.is_ingame) {
      EventManager::Instance().TriggerEvent(kEventOnPlayerDisconnectName, OnPlayerDisconnectEvent{player.player_id, reason});
    }
    DeleteFromPlayerList(player.player_id);
  }
}

bool GameServer::ApplyPlayerDamage(Player& victim, std::optional<PlayerId> attacker_id, std::int32_t damage, std::uint32_t damage_type,
                                   bool dont_kill) {
  if (!victim.is_ingame || victim.tod != 0 || damage <= 0) {
    return false;
  }

  const auto event_damage = static_cast<std::int16_t>(std::min<std::int32_t>(damage, std::numeric_limits<std::int16_t>::max()));
  std::optional<std::uint64_t> event_attacker;
  if (attacker_id.has_value()) {
    event_attacker = attacker_id.value();
  }
  auto result = EventManager::Instance().DispatchEvent(
      kEventOnPlayerHitName, std::any(OnPlayerHitEvent{event_attacker, victim.player_id, event_damage, damage_type}));
  if (result.cancelled) {
    return false;
  }

  if (result.value.has_value()) {
    damage = result.value.value();
  }
  if (damage <= 0) {
    return false;
  }

  const auto old_health = victim.health;
  auto new_health = static_cast<std::int32_t>(old_health) - damage;
  const bool player_damage = attacker_id.has_value();
  const bool victim_was_unconscious = (victim.flags & PlayerManager::PL_UNCONCIOUS) != 0;

  if (new_health <= 0) {
    if (player_damage && !victim_was_unconscious) {
      MakePlayerUnconscious(victim, attacker_id);
      return false;
    }
    if (!player_damage && dont_kill && old_health > 0) {
      new_health = 1;
    } else {
      HandlePlayerDeath(victim, attacker_id);
      return true;
    }
  }
  new_health = std::clamp<std::int32_t>(new_health, 0, victim.max_health);

  victim.health = static_cast<std::int16_t>(new_health);
  victim.state.health_points = victim.health;
  victim.state.life_state = GetPlayerLifeState(victim);
  AdvancePlayerStateSequence(victim);
  if (old_health != victim.health) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName,
                                          OnPlayerChangeHealthEvent{victim.player_id, old_health, victim.health});
    SendPlayerAttributeUpdate(player_manager_, victim, ATTR_HEALTH, victim.health);
  }

  return false;
}

bool GameServer::MakePlayerUnconscious(Player& victim, std::optional<PlayerId> attacker_id) {
  if (!victim.is_ingame || victim.tod != 0) {
    return false;
  }

  const bool was_unconscious = (victim.flags & PlayerManager::PL_UNCONCIOUS) != 0;
  const auto old_health = victim.health;
  victim.flags |= PlayerManager::PL_UNCONCIOUS;
  victim.health = 1;
  victim.state.health_points = 1;
  victim.state.life_state = GetPlayerLifeState(victim);
  ClearTransientCombatState(victim);
  AdvancePlayerStateSequence(victim);

  if (old_health != victim.health) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName,
                                          OnPlayerChangeHealthEvent{victim.player_id, old_health, victim.health});
    SendPlayerAttributeUpdate(player_manager_, victim, ATTR_HEALTH, victim.health);
  }

  if (was_unconscious) {
    return false;
  }

  std::optional<std::uint64_t> event_attacker;
  if (attacker_id.has_value()) {
    event_attacker = attacker_id.value();
  }
  EventManager::Instance().TriggerEvent(kEventOnPlayerUnconsciousName, OnPlayerUnconsciousEvent{event_attacker, victim.player_id});
  SendUnconsciousInfo(victim.player_id, attacker_id);
  return true;
}

void GameServer::HandlePlayerDeath(Player& victim, std::optional<PlayerId> killer_id) {
  if (victim.tod != 0) {
    return;
  }

  const auto old_health = victim.health;
  victim.health = 0;
  victim.state.health_points = 0;
  victim.flags &= ~PlayerManager::PL_UNCONCIOUS;
  victim.tod = time(NULL);
  victim.state.life_state = GetPlayerLifeState(victim);
  ClearTransientCombatState(victim);
  AdvancePlayerStateSequence(victim);
  if (old_health != victim.health) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName, OnPlayerChangeHealthEvent{victim.player_id, old_health, victim.health});
  }

  EventManager::Instance().TriggerEvent(kEventOnPlayerDeathName, OnPlayerDeathEvent{victim.player_id, killer_id});

  SendDeathInfo(victim.player_id);
}

void GameServer::SomeoneJoinGame(Packet p) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt) {
    SPDLOG_WARN("Someone tried to join game, but he is not on the player list, connection {}!", p.id);
    return;
  }
  auto& player = player_opt.value().get();

  if (!allow_modification) {
    if (!player.passed_crc_test) {
      resource_server_->RevokeToken(p.id);
      player_manager_.RemovePlayerByConnection(p.id);
      g_net_server->AddToBanList(p.id, 3600000);  // i dorzucamy banana na 1h
      return;
    }
  }

  JoinGamePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize JoinGamePacket");
    return;
  }
  SPDLOG_TRACE("{} from {}", packet, p.id);

  player.state.position = packet.position;
  player.state.nrot = packet.normal;
  player.state.left_hand_item_instance = packet.left_hand_item_instance;
  player.state.right_hand_item_instance = packet.right_hand_item_instance;
  player.state.equipped_armor_instance = packet.equipped_armor_instance;
  player.state.equipped_helmet_instance = packet.equipped_helmet_instance;
  player.state.equipped_shield_instance = packet.equipped_shield_instance;
  player.state.equipped_amulet_instance = packet.equipped_amulet_instance;
  player.state.equipped_belt_instance = packet.equipped_belt_instance;
  player.state.equipped_ring_left_instance = packet.equipped_ring_left_instance;
  player.state.equipped_ring_right_instance = packet.equipped_ring_right_instance;
  ResolvePlayerStateItemIndexes(player.player_id, player.state);
  player.state.animation = packet.animation;
  player.state.animation_name = SanitizePlayerAnimationName(std::move(packet.animation_name));
  player.body_model = packet.body_model;
  player.body_texture = packet.body_texture;
  player.head_model = packet.head_model;
  player.head_texture = packet.head_texture;
  player.walkstyle = packet.walk_style;
  player.name = SanitizePlayerName(packet.player_name);

  // Inform the joining player about already spawned players before any spawn happens
  SendExistingPlayersPacket(player);

  BroadcastPlayerJoined(player);

  SPDLOG_INFO("{} joined the server. Now connected users: {}.", FormatPlayerLabel(player), player_manager_.GetPlayerCount());

  // join
  EventManager::Instance().TriggerEvent(kEventOnPlayerConnectName, player.player_id);
}

void GameServer::HandlePlayerUpdate(Packet p) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt.has_value()) {
    return;
  }
  auto& updated_player = player_opt.value().get();
  if (!updated_player.is_ingame) {
    return;
  }

  PlayerStateUpdatePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize PlayerStateUpdatePacket");
    return;
  }

  const auto old_state = updated_player.state;
  const auto old_health = updated_player.health;
  const auto old_mana = updated_player.mana;
  const auto item_index_or_nil = [](std::int16_t item_index) -> std::optional<std::int32_t> {
    return item_index == 0 ? std::nullopt : std::optional<std::int32_t>{item_index};
  };

  updated_player.state.position = packet.state.position;
  updated_player.state.nrot = packet.state.nrot;
  updated_player.state.left_hand_item_instance = packet.state.left_hand_item_instance;
  updated_player.state.right_hand_item_instance = packet.state.right_hand_item_instance;
  updated_player.state.equipped_armor_instance = packet.state.equipped_armor_instance;
  updated_player.state.equipped_helmet_instance = packet.state.equipped_helmet_instance;
  updated_player.state.equipped_shield_instance = packet.state.equipped_shield_instance;
  updated_player.state.equipped_amulet_instance = packet.state.equipped_amulet_instance;
  updated_player.state.equipped_belt_instance = packet.state.equipped_belt_instance;
  updated_player.state.equipped_ring_left_instance = packet.state.equipped_ring_left_instance;
  updated_player.state.equipped_ring_right_instance = packet.state.equipped_ring_right_instance;
  updated_player.state.animation = packet.state.animation;
  updated_player.state.animation_name = SanitizePlayerAnimationName(std::move(packet.state.animation_name));
  updated_player.state.weapon_mode = packet.state.weapon_mode;
  updated_player.state.active_spell_nr = packet.state.active_spell_nr;
  updated_player.state.active_spell_instance = packet.state.active_spell_instance;
  updated_player.state.head_direction = packet.state.head_direction;
  updated_player.state.melee_weapon_instance = packet.state.melee_weapon_instance;
  updated_player.state.ranged_weapon_instance = packet.state.ranged_weapon_instance;
  ResolvePlayerStateItemIndexes(updated_player.player_id, updated_player.state);
  if (updated_player.tod != 0 || (updated_player.flags & PlayerManager::PL_UNCONCIOUS) != 0) {
    ClearTransientCombatState(updated_player);
  }

  if (updated_player.tod == 0) {
    const auto requested_health = std::clamp<std::int32_t>(packet.state.health_points, 0, updated_player.max_health);
    const auto requested_mana = std::clamp<std::int32_t>(packet.state.mana_points, 0, updated_player.max_mana);
    bool died = false;
    const auto effective_health = requested_health > old_health ? static_cast<std::int32_t>(old_health) : requested_health;

    if (effective_health < old_health && (updated_player.flags & PlayerManager::PL_UNCONCIOUS) == 0) {
      died = ApplyPlayerDamage(updated_player, std::nullopt, static_cast<std::int32_t>(old_health) - effective_health, 0, false);
    }

    if (!died && requested_mana != old_mana) {
      updated_player.mana = static_cast<std::int16_t>(requested_mana);
      if (old_mana != updated_player.mana) {
        EventManager::Instance().TriggerEvent(kEventOnPlayerChangeManaName,
                                              OnPlayerChangeManaEvent{updated_player.player_id, old_mana, updated_player.mana});
      }
    }
  }

  updated_player.state.health_points = updated_player.health;
  updated_player.state.mana_points = updated_player.mana;
  updated_player.state.life_state = GetPlayerLifeState(updated_player);
  AdvancePlayerStateSequence(updated_player);

  if (old_state.weapon_mode != updated_player.state.weapon_mode) {
    EventManager::Instance().TriggerEvent(
        kEventOnPlayerWeaponModeChangeName,
        OnPlayerWeaponModeChangeEvent{updated_player.player_id, old_state.weapon_mode, updated_player.state.weapon_mode});
  }

  if (old_state.left_hand_item_instance != updated_player.state.left_hand_item_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.left_hand_item_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipHandItemName,
                                          OnPlayerEquipHandItemEvent{updated_player.player_id, 0, item_index});
  }

  if (old_state.right_hand_item_instance != updated_player.state.right_hand_item_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.right_hand_item_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipHandItemName,
                                          OnPlayerEquipHandItemEvent{updated_player.player_id, 1, item_index});
  }

  if (old_state.equipped_armor_instance != updated_player.state.equipped_armor_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_armor_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipArmorName, OnPlayerEquipArmorEvent{updated_player.player_id, item_index});
  }

  if (old_state.equipped_amulet_instance != updated_player.state.equipped_amulet_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_amulet_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipAmuletName, OnPlayerEquipAmuletEvent{updated_player.player_id, item_index});
  }

  if (old_state.equipped_belt_instance != updated_player.state.equipped_belt_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_belt_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipBeltName, OnPlayerEquipBeltEvent{updated_player.player_id, item_index});
  }

  if (old_state.equipped_ring_left_instance != updated_player.state.equipped_ring_left_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_ring_left_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipRingName, OnPlayerEquipRingEvent{updated_player.player_id, 0, item_index});
  }

  if (old_state.equipped_ring_right_instance != updated_player.state.equipped_ring_right_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_ring_right_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipRingName, OnPlayerEquipRingEvent{updated_player.player_id, 1, item_index});
  }

  if (old_state.equipped_helmet_instance != updated_player.state.equipped_helmet_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_helmet_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipHelmetName, OnPlayerEquipHelmetEvent{updated_player.player_id, item_index});
  }

  if (old_state.equipped_shield_instance != updated_player.state.equipped_shield_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.equipped_shield_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipShieldName, OnPlayerEquipShieldEvent{updated_player.player_id, item_index});
  }

  if (old_state.melee_weapon_instance != updated_player.state.melee_weapon_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.melee_weapon_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipMeleeWeaponName,
                                          OnPlayerEquipMeleeWeaponEvent{updated_player.player_id, item_index});
  }

  if (old_state.ranged_weapon_instance != updated_player.state.ranged_weapon_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.ranged_weapon_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipRangedWeaponName,
                                          OnPlayerEquipRangedWeaponEvent{updated_player.player_id, item_index});
  }

  if (old_state.active_spell_nr != updated_player.state.active_spell_nr ||
      old_state.active_spell_instance != updated_player.state.active_spell_instance) {
    const auto item_index = item_index_or_nil(updated_player.state.active_spell_instance);
    EventManager::Instance().TriggerEvent(kEventOnPlayerEquipSpellSlotName,
                                          OnPlayerEquipSpellSlotEvent{updated_player.player_id,
                                                                      updated_player.state.active_spell_nr,
                                                                      item_index});
  }

  BroadcastPlayerStateToViewers(player_manager_, updated_player);
}

void GameServer::HandleLuaEvent(Packet p) {
  LuaEventPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!state.second) {
    SPDLOG_ERROR("Failed to deserialize LuaEventPacket, error code: {}", static_cast<int>(state.first));
    return;
  }

  if (!lua_script_) {
    SPDLOG_ERROR("Lua event '{}' received without active Lua script", packet.event_name);
    return;
  }

  std::string payload(packet.payload.begin(), packet.payload.end());
  std::vector<sol::object> args;
  std::string error;
  if (!gmp::lua::DecodeLuaArgs(lua_script_->GetLuaState(), payload, args, error)) {
    SPDLOG_ERROR("Failed to decode Lua event '{}' payload: {}", packet.event_name, error);
    return;
  }

  if (!lua::bindings::TriggerRemoteEvent(lua_script_->GetLuaState(), packet.event_name, packet.source_element, args)) {
    SPDLOG_WARN("Lua event '{}' rejected or cancelled", packet.event_name);
  }
}

void GameServer::HandleVoice(Packet p) {
  // TODO: no need to resend player id right now, it won't be needed until we add 3d chat
  if (p.length == 0) {
    return;
  }

  std::string data;
  data.resize(p.length);
  memcpy(data.data(), p.data, p.length);
  player_manager_.ForEachIngamePlayer([&](const Player& existing_player) {
    if (existing_player.connection != p.id) {
      g_net_server->Send((unsigned char*)data.data(), p.length, IMMEDIATE_PRIORITY, UNRELIABLE, 5, existing_player.connection);
    }
  });
}

void GameServer::HandleNormalMsg(Packet p) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt.has_value() || !player_opt.value().get().is_ingame)
    return;

  auto& player = player_opt.value().get();

  MessagePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize MessagePacket");
    return;
  }

  packet.message = SanitizeServerText(packet.message);
  packet.r = 255;
  packet.g = 255;
  packet.b = 255;

  if (!packet.message.empty() && packet.message.front() == '/') {
    auto command_line = packet.message.substr(1);
    auto command_start = command_line.find_first_not_of(' ');
    if (command_start != std::string::npos) {
      command_line = command_line.substr(command_start);
      auto space_pos = command_line.find(' ');
      auto command = command_line.substr(0, space_pos);
      if (!command.empty()) {
        auto params_start = command_line.find_first_not_of(' ', space_pos);
        std::string params = params_start == std::string::npos ? std::string{} : command_line.substr(params_start);
        SPDLOG_INFO("{} issued command: /{} {}", player.name, command, params);
        EventManager::Instance().TriggerEvent(kEventOnPlayerCommandName, OnPlayerCommandEvent{player.player_id, command, params});
      }
    }
    return;
  }

  EventManager::Instance().TriggerEvent(kEventOnPlayerMessageName, OnPlayerMessageEvent{player.player_id, packet.message});

  packet.sender = player.player_id;
  player_manager_.ForEachIngamePlayer(
      [&](const Player& existing_player) { SerializeAndSend(packet, LOW_PRIORITY, RELIABLE_ORDERED, existing_player.connection); });

  SPDLOG_INFO("{}", packet);
}

void GameServer::HandleCastSpell(Packet p, bool target) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt.has_value() || !player_opt.value().get().is_ingame)
    return;

  auto& player = player_opt.value().get();

  CastSpellPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize CastSpellPacket");
    return;
  }
  packet.caster_id = player.player_id;

  if (target) {
    if (!packet.target_id.has_value()) {
      SPDLOG_ERROR("No target in cast spell packet!");
      return;
    }

    auto target_opt = player_manager_.GetPlayer(*packet.target_id);
    if (!target_opt.has_value() || !target_opt.value().get().is_ingame) {
      return;
    }
  }

  EventManager::Instance().TriggerEvent(kEventOnPlayerCastSpellName, OnPlayerCastSpellEvent{player.player_id, packet.spell_id, packet.target_id});

  player_manager_.ForEachIngamePlayer([&](const Player& existing_player) {
    if (existing_player.player_id != player.player_id) {
      SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE, existing_player.connection);
    }
  });
}

void GameServer::HandleDropItem(Packet p) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt.has_value() || !player_opt.value().get().is_ingame)
    return;

  auto& player = player_opt.value().get();

  DropItemPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize DropItemPacket");
    return;
  }

  auto resolved_instance = ResolveItemInstance(packet.item_instance_name);
  if (!resolved_instance.has_value()) {
    SPDLOG_WARN("Player {} tried to drop unknown item instance '{}'", player.player_id,
                SanitizeServerText(packet.item_instance_name));
    return;
  }

  const auto instance = *resolved_instance;
  const ItemRegistry::Item* item_definition = item_registry_.Find(instance);
  if (item_definition != nullptr && packet.item_instance > 0 && packet.item_instance != item_definition->index) {
    SPDLOG_WARN("Player {} tried to drop item '{}' with mismatched index {} (expected {})", player.player_id, instance,
                packet.item_instance, item_definition->index);
    return;
  }

  const auto amount = std::max<std::int32_t>(1, packet.item_amount);
  ItemGroundManager::CreateOptions options;
  options.instance = instance;
  options.amount = amount;
  options.physics_enabled = packet.physics_enabled;
  options.position = packet.position;
  options.rotation = packet.rotation;
  options.world = player.world;
  options.virtual_world = player.virtual_world;

  const auto item_ground_id = CreateItemGround(std::move(options));
  auto result = EventManager::Instance().DispatchEvent(kEventOnPlayerDropItemName,
                                                       OnPlayerDropItemEvent{player.player_id, item_ground_id});
  if (result.cancelled) {
    DestroyItemGround(item_ground_id);
    SendInventoryAddCorrection(player, instance, amount);
    return;
  }

  RemoveInventoryItem(player, instance, amount);
}

void GameServer::HandleTakeItem(Packet p) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt.has_value() || !player_opt.value().get().is_ingame)
    return;

  auto& player = player_opt.value().get();

  TakeItemPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize TakeItemPacket");
    return;
  }

  if (!packet.item_ground_id.has_value()) {
    SPDLOG_WARN("Player {} took item {} without an item ground id", player.player_id, packet.item_instance_name);
    return;
  }

  auto* item_ground = item_ground_manager_.Get(*packet.item_ground_id);
  if (item_ground == nullptr) {
    SPDLOG_WARN("Player {} tried to take missing item ground {}", player.player_id, *packet.item_ground_id);
    return;
  }

  if (!CanSeeItemGround(player, *item_ground)) {
    SPDLOG_WARN("Player {} tried to take item ground {} outside their world or virtual world", player.player_id,
                *packet.item_ground_id);
    return;
  }

  const auto instance = item_ground->instance;
  const auto amount = item_ground->amount;
  auto result = EventManager::Instance().DispatchEvent(kEventOnPlayerTakeItemName,
                                                       OnPlayerTakeItemEvent{player.player_id, item_ground->id});
  if (result.cancelled) {
    SendItemGroundCreate(*item_ground, player.connection);
    SendInventoryRemoveCorrection(player, instance, amount);
    return;
  }

  AddInventoryItem(player, instance, amount);
  SendInventoryAddCorrection(player, instance, amount);
  DestroyItemGround(item_ground->id);
}

void GameServer::HandlePlayerWorldEnter(Packet p) {
  auto player_opt = player_manager_.GetPlayerByConnection(p.id);
  if (!player_opt.has_value() || !player_opt.value().get().is_ingame) {
    return;
  }

  auto& player = player_opt.value().get();

  PlayerWorldEnterPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);
  if (!state.second) {
    SPDLOG_WARN("Failed to deserialize PlayerWorldEnterPacket");
    return;
  }

  const auto entered_world = SanitizeWorldName(packet.world_name);
  if (!entered_world.empty() && entered_world != player.world) {
    SPDLOG_WARN("Player {} reported entering world '{}', but server state is '{}'", player.player_id, entered_world,
                player.world);
  }

  EventManager::Instance().TriggerEvent(kEventOnPlayerWorldEnterName, OnPlayerWorldEnterEvent{player.player_id, player.world});
  StreamRelevantGroundItemsToPlayer(player, true);
}

void GameServer::SendItemGroundCreate(const ItemGroundManager::ItemGround& item_ground, Net::ConnectionHandle connection) {
  ItemGroundCreatePacket packet{};
  packet.packet_type = PT_ITEM_GROUND_CREATE;
  packet.item_ground_id = item_ground.id;
  packet.item_instance = item_ground.instance;
  packet.amount = item_ground.amount;
  packet.physics_enabled = item_ground.physics_enabled;
  packet.position = item_ground.position;
  packet.rotation = item_ground.rotation;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED, connection, kItemGroundChannel);
}

void GameServer::SendItemGroundDestroy(std::uint32_t item_ground_id, Net::ConnectionHandle connection) {
  ItemGroundDestroyPacket packet{};
  packet.packet_type = PT_ITEM_GROUND_DESTROY;
  packet.item_ground_id = item_ground_id;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED, connection, kItemGroundChannel);
}

void GameServer::SendItemGroundClear(Net::ConnectionHandle connection) {
  ItemGroundClearPacket packet{};
  packet.packet_type = PT_ITEM_GROUND_CLEAR;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED, connection, kItemGroundChannel);
}

void GameServer::StreamItemGroundToPlayer(ItemGroundManager::ItemGround& item_ground, Player& player, bool force) {
  if (!CanSeeItemGround(player, item_ground)) {
    return;
  }

  const bool newly_streamed = item_ground.streamed_to.insert(player.player_id).second;
  if (newly_streamed || force) {
    SendItemGroundCreate(item_ground, player.connection);
  }
}

void GameServer::StreamRelevantGroundItemsToPlayer(Player& player, bool force) {
  for (auto& [_, item_ground] : item_ground_manager_.Items()) {
    StreamItemGroundToPlayer(item_ground, player, force);
  }
}

void GameServer::UnstreamGroundItemsFromPlayer(Player& player, bool notify_client) {
  bool had_streamed_items = false;
  for (auto& [_, item_ground] : item_ground_manager_.Items()) {
    had_streamed_items = item_ground.streamed_to.erase(player.player_id) > 0 || had_streamed_items;
  }

  if (notify_client && had_streamed_items && player.is_ingame) {
    SendItemGroundClear(player.connection);
  }
}

void GameServer::RefreshItemGroundStreaming(ItemGroundManager::ItemGround& item_ground) {
  std::vector<PlayerId> stale_viewers;
  stale_viewers.reserve(item_ground.streamed_to.size());

  for (const auto player_id : item_ground.streamed_to) {
    auto player_opt = player_manager_.GetPlayer(player_id);
    if (!player_opt.has_value() || !CanSeeItemGround(player_opt->get(), item_ground)) {
      stale_viewers.push_back(player_id);
    } else {
      SendItemGroundCreate(item_ground, player_opt->get().connection);
    }
  }

  for (const auto player_id : stale_viewers) {
    item_ground.streamed_to.erase(player_id);
    auto player_opt = player_manager_.GetPlayer(player_id);
    if (player_opt.has_value() && player_opt->get().is_ingame) {
      SendItemGroundDestroy(item_ground.id, player_opt->get().connection);
    }
  }

  player_manager_.ForEachIngamePlayer([&](Player& player) { StreamItemGroundToPlayer(item_ground, player); });
}

void GameServer::SendInventoryAddCorrection(Player& player, const std::string& instance, std::int32_t amount) {
  if (instance.empty() || amount <= 0) {
    return;
  }

  GiveItemPacket packet{};
  packet.packet_type = PT_GIVEITEM;
  packet.player_id = player.player_id;
  packet.item_instance = instance;
  packet.item_amount = amount;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED, player.connection);
}

void GameServer::SendInventoryRemoveCorrection(Player& player, const std::string& instance, std::int32_t amount) {
  if (instance.empty() || amount <= 0) {
    return;
  }

  RemoveItemPacket packet{};
  packet.packet_type = PT_REMOVEITEM;
  packet.player_id = player.player_id;
  packet.item_instance = instance;
  packet.item_amount = amount;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED, player.connection);
}

std::optional<std::string> GameServer::ResolveItemInstance(std::string instance) const {
  instance = SanitizeServerText(std::move(instance));
  if (instance.size() > 255) {
    instance.resize(255);
  }
  if (instance.empty()) {
    return std::nullopt;
  }
  return item_registry_.CanonicalizeInstance(instance);
}

std::int16_t GameServer::ResolveItemIndex(PlayerId player_id, std::int16_t index, const char* field_name) const {
  if (index <= 0) {
    return 0;
  }

  if (item_registry_.ContainsIndex(index)) {
    return index;
  }

  SPDLOG_WARN("Player {} reported unknown item index {} in {}", player_id, index, field_name);
  return 0;
}

void GameServer::ResolvePlayerStateItemIndexes(PlayerId player_id, PlayerState& state) const {
  state.left_hand_item_instance = ResolveItemIndex(player_id, state.left_hand_item_instance, "left_hand_item_instance");
  state.right_hand_item_instance = ResolveItemIndex(player_id, state.right_hand_item_instance, "right_hand_item_instance");
  state.equipped_armor_instance = ResolveItemIndex(player_id, state.equipped_armor_instance, "equipped_armor_instance");
  state.equipped_helmet_instance = ResolveItemIndex(player_id, state.equipped_helmet_instance, "equipped_helmet_instance");
  state.equipped_shield_instance = ResolveItemIndex(player_id, state.equipped_shield_instance, "equipped_shield_instance");
  state.equipped_amulet_instance = ResolveItemIndex(player_id, state.equipped_amulet_instance, "equipped_amulet_instance");
  state.equipped_belt_instance = ResolveItemIndex(player_id, state.equipped_belt_instance, "equipped_belt_instance");
  state.equipped_ring_left_instance = ResolveItemIndex(player_id, state.equipped_ring_left_instance, "equipped_ring_left_instance");
  state.equipped_ring_right_instance = ResolveItemIndex(player_id, state.equipped_ring_right_instance, "equipped_ring_right_instance");
  state.active_spell_instance = ResolveItemIndex(player_id, state.active_spell_instance, "active_spell_instance");
  state.melee_weapon_instance = ResolveItemIndex(player_id, state.melee_weapon_instance, "melee_weapon_instance");
  state.ranged_weapon_instance = ResolveItemIndex(player_id, state.ranged_weapon_instance, "ranged_weapon_instance");
}

std::uint32_t GameServer::CreateItemGround(ItemGroundManager::CreateOptions options) {
  auto resolved_instance = ResolveItemInstance(options.instance);
  if (!resolved_instance.has_value()) {
    SPDLOG_WARN("Cannot create ground item for unknown item instance '{}'", SanitizeServerText(options.instance));
    return 0;
  }
  options.instance = *resolved_instance;
  options.world = SanitizeWorldName(options.world.empty() ? server_world_ : options.world);
  options.amount = std::max<std::int32_t>(1, options.amount);
  options.virtual_world = std::clamp<std::int32_t>(options.virtual_world, 0, 65535);

  auto& item_ground = item_ground_manager_.Create(std::move(options));
  RefreshItemGroundStreaming(item_ground);
  return item_ground.id;
}

bool GameServer::DestroyItemGround(std::uint32_t item_ground_id) {
  auto* item_ground = item_ground_manager_.Get(item_ground_id);
  if (item_ground == nullptr) {
    return false;
  }

  const auto streamed_to = item_ground->streamed_to;
  for (const auto player_id : streamed_to) {
    auto player_opt = player_manager_.GetPlayer(player_id);
    if (player_opt.has_value() && player_opt->get().is_ingame) {
      SendItemGroundDestroy(item_ground_id, player_opt->get().connection);
    }
  }

  return item_ground_manager_.Destroy(item_ground_id);
}

bool GameServer::SetItemGroundPosition(std::uint32_t item_ground_id, const glm::vec3& position) {
  auto* item_ground = item_ground_manager_.Get(item_ground_id);
  if (item_ground == nullptr) {
    return false;
  }

  item_ground->position = position;
  RefreshItemGroundStreaming(*item_ground);
  return true;
}

bool GameServer::SetItemGroundRotation(std::uint32_t item_ground_id, const glm::vec3& rotation) {
  auto* item_ground = item_ground_manager_.Get(item_ground_id);
  if (item_ground == nullptr) {
    return false;
  }

  item_ground->rotation = rotation;
  RefreshItemGroundStreaming(*item_ground);
  return true;
}

bool GameServer::SetItemGroundVirtualWorld(std::uint32_t item_ground_id, std::int32_t virtual_world) {
  auto* item_ground = item_ground_manager_.Get(item_ground_id);
  if (item_ground == nullptr) {
    return false;
  }

  item_ground->virtual_world = std::clamp<std::int32_t>(virtual_world, 0, 65535);
  RefreshItemGroundStreaming(*item_ground);
  return true;
}

bool GameServer::SetItemGroundPhysicsEnabled(std::uint32_t item_ground_id, bool enabled) {
  auto* item_ground = item_ground_manager_.Get(item_ground_id);
  if (item_ground == nullptr) {
    return false;
  }

  item_ground->physics_enabled = enabled;
  RefreshItemGroundStreaming(*item_ground);
  return true;
}

nlohmann::json GameServer::BuildMasterServerPayload() const {
  const auto port = g_net_server ? g_net_server->GetPort() : static_cast<std::uint32_t>(config_.Get<std::int32_t>("port"));
  const auto ip_address = g_net_server ? g_net_server->GetAddress() : std::string{};

  const auto name = SanitizeServerText(config_.Get<std::string>("name"));
  const auto player_count = static_cast<std::uint32_t>(player_manager_.GetPlayerCount());
  const auto max_slots = static_cast<std::uint32_t>(config_.Get<std::int32_t>("slots"));
  const auto map = SanitizeServerText(config_.Get<std::string>("map"));

  return nlohmann::json{{"server_seed", config_.Get<std::string>("server_identity_seed")},
                        {"ip_address", ip_address},
                        {"port", port},
                        {"name", name},
                        {"current_players", player_count},
                        {"max_slots", max_slots},
                        {"map", map}};
}

void GameServer::AddToPublicListHTTP() {
  using namespace std::chrono_literals;

  auto endpoint_info_opt = master_server::ParseEndpoint(kMasterServerEndpoint);
  if (!endpoint_info_opt) {
    SPDLOG_WARN("Master server endpoint '{}' has an invalid format. Skipping public list updates.", kMasterServerEndpoint);
    return;
  }

  const auto endpoint_info = *endpoint_info_opt;

  auto client = CreateMasterServerClient(endpoint_info);
  if (!client) {
    SPDLOG_ERROR("Unable to create HTTP client for master server endpoint '{}'.", kMasterServerEndpoint);
    return;
  }

  client->set_connection_timeout(5, 0);
  client->set_read_timeout(5, 0);
  client->set_write_timeout(5, 0);

  auto last_update = std::chrono::system_clock::now() - 15s;

  while (g_is_server_running) {
    auto now = std::chrono::system_clock::now();
    if (now - last_update >= 15s) {
      last_update = now;
      auto payload = BuildMasterServerPayload();

      auto response = client->Post(endpoint_info.path.c_str(), payload.dump(), "application/json");
      if (!response) {
        SPDLOG_WARN("Failed to update master server at {}:{}{}: {}", endpoint_info.host, endpoint_info.port, endpoint_info.path,
                    httplib::to_string(response.error()));
      } else if (response->status >= 400) {
        SPDLOG_WARN("Master server responded with status {} when updating {}:{}{}", response->status, endpoint_info.host, endpoint_info.port,
                    endpoint_info.path);
      } else {
        SPDLOG_DEBUG("Master server heartbeat succeeded with status {}", response->status);
      }
    }
    std::this_thread::sleep_for(100ms);
  }
}

void GameServer::HandleGameInfo(Packet p) {
  SendGameInfo(p.id);
  SendSkySettings(p.id);
}

// void GameServer::HandleGameInfo(Packet p){
void GameServer::SendGameInfo(Net::ConnectionHandle who) {
  SendGameInfo(who, clock_->GetTime());
}

void GameServer::SendGameInfo(Net::ConnectionHandle who, GothicClock::Time time) {
  GameInfoPacket packet;
  packet.packet_type = PT_GAME_INFO;
  GothicClock::TimeUnion game_time = time;
  packet.raw_game_time = game_time.raw;
  packet.day_length_ms = static_cast<float>(clock_->GetDayLengthMs());

  if (config_.Get<bool>("hide_map")) {
    packet.flags |= HIDE_MAP;
  }

  SerializeAndSend(packet, MEDIUM_PRIORITY, RELIABLE, who, 9);
}

void GameServer::SendSkySettings(Net::ConnectionHandle connection) {
  const auto& state = weather_.GetState();
  auto packet = MakeSkySettingsPacket(kFullSkySettingsFlags, state.weather_type, state.rain_start_hour, state.rain_start_min,
                                      state.rain_stop_hour, state.rain_stop_min, state.wind_scale, state.dont_rain, state.rain_weight,
                                      state.render_lightning);

  SerializeAndSend(packet, MEDIUM_PRIORITY, RELIABLE, connection);
}

void GameServer::BroadcastGameInfo() {
  BroadcastGameInfo(clock_->GetTime());
}

void GameServer::BroadcastGameInfo(GothicClock::Time time) {
  player_manager_.ForEachIngamePlayer([&](const Player& player) { SendGameInfo(player.connection, time); });
}

void GameServer::BroadcastSkySettings() {
  const auto& state = weather_.GetState();
  auto packet = MakeSkySettingsPacket(kFullSkySettingsFlags, state.weather_type, state.rain_start_hour, state.rain_start_min,
                                      state.rain_stop_hour, state.rain_stop_min, state.wind_scale, state.dont_rain, state.rain_weight,
                                      state.render_lightning);

  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, MEDIUM_PRIORITY, RELIABLE, player.connection); });
}

void GameServer::BroadcastPlayerPings() {
  PlayerPingUpdatePacket packet;
  packet.packet_type = PT_PLAYER_PING_UPDATE;
  packet.pings.reserve(player_manager_.GetPlayerCount());

  player_manager_.ForEachIngamePlayer([&](const Player& player) {
    packet.pings.push_back(PlayerPingEntry{player.player_id, GetPlayerPing(player.player_id)});
  });

  if (packet.pings.empty()) {
    return;
  }

  player_manager_.ForEachIngamePlayer([&](const Player& player) {
    SerializeAndSend(packet, LOW_PRIORITY, UNRELIABLE, player.connection);
  });
}

void GameServer::UpdateAuthoritativeWorldState(const std::vector<GothicClock::Time>& advanced_times) {
  if (advanced_times.empty()) {
    return;
  }

  for (const auto& current_time : advanced_times) {
    const auto current_minute = GothicWeather::ToTotalGameMinutes(current_time);
    if (current_minute == last_weather_update_minute_) {
      continue;
    }

    last_weather_update_minute_ = current_minute;
    BroadcastGameInfo(current_time);

    if (weather_.Update(current_time)) {
      BroadcastSkySettings();
    }
  }
}

void GameServer::HandleMapNameReq(Packet p) {
}

void GameServer::SendDisconnectionInfo(PlayerId disconnected_player_id) {
  DisconnectionInfoPacket packet;
  packet.disconnected_id = disconnected_player_id;
  packet.packet_type = PT_LEFT_GAME;

  player_manager_.ForEachIngamePlayer([&](const Player& player) {
    if (player.player_id != disconnected_player_id) {
      SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE, player.connection);
    }
  });
}

bool GameServer::IsPublic() {
  return (config_.Get<bool>("public")) ? true : false;
}

void GameServer::SendMessageToAll(std::uint8_t r, std::uint8_t g, std::uint8_t b, const std::string& text) {
  if (text.empty()) {
    return;
  }

  auto packet = CreateMessagePacket(std::nullopt, std::nullopt, r, g, b, text);
  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, LOW_PRIORITY, RELIABLE_ORDERED, player.connection); });
}

void GameServer::SendMessageToPlayer(PlayerId player_id, std::uint8_t r, std::uint8_t g, std::uint8_t b, const std::string& text) {
  if (text.empty()) {
    return;
  }

  auto target_player = player_manager_.GetPlayer(player_id);
  if (!target_player.has_value() || !target_player->get().is_ingame) {
    SPDLOG_WARN("Cannot send message to player {} because they are not connected", player_id);
    return;
  }

  auto packet = CreateMessagePacket(std::nullopt, player_id, r, g, b, text);
  SerializeAndSend(packet, LOW_PRIORITY, RELIABLE_ORDERED, target_player->get().connection);
}

void GameServer::SendPlayerMessageToAll(PlayerId sender_id, std::uint8_t r, std::uint8_t g, std::uint8_t b, const std::string& text) {
  if (text.empty()) {
    return;
  }

  auto sender = player_manager_.GetPlayer(sender_id);
  if (!sender.has_value() || !sender->get().is_ingame) {
    SPDLOG_WARN("Cannot broadcast message from invalid sender {}", sender_id);
    return;
  }

  auto packet = CreateMessagePacket(sender_id, std::nullopt, r, g, b, text);
  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, LOW_PRIORITY, RELIABLE_ORDERED, player.connection); });
}

void GameServer::SendPlayerMessageToPlayer(PlayerId sender_id, PlayerId receiver_id, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                           const std::string& text) {
  if (text.empty()) {
    return;
  }

  auto sender = player_manager_.GetPlayer(sender_id);
  if (!sender.has_value() || !sender->get().is_ingame) {
    SPDLOG_WARN("Cannot send player message from invalid sender {}", sender_id);
    return;
  }

  auto receiver = player_manager_.GetPlayer(receiver_id);
  if (!receiver.has_value() || !receiver->get().is_ingame) {
    SPDLOG_WARN("Cannot send player message to invalid receiver {}", receiver_id);
    return;
  }

  auto packet = CreateMessagePacket(sender_id, receiver_id, r, g, b, text);
  SerializeAndSend(packet, LOW_PRIORITY, RELIABLE_ORDERED, receiver->get().connection);
}

bool GameServer::TriggerClientEvent(const std::vector<PlayerId>& targets, const std::string& event_name, PlayerId source_element,
                                    const std::string& payload) {
  if (!g_net_server) {
    SPDLOG_WARN("Cannot trigger client event '{}' before network server is initialized", event_name);
    return false;
  }

  if (payload.size() > kMaxLuaEventPayloadSize) {
    SPDLOG_WARN("Lua event '{}' payload too large ({} bytes)", event_name, payload.size());
    return false;
  }

  LuaEventPacket packet;
  packet.packet_type = PT_LUA_EVENT;
  packet.event_name = event_name;
  packet.source_element = source_element;
  packet.payload.assign(payload.begin(), payload.end());

  bool sent = false;
  for (auto player_id : targets) {
    auto connection = player_manager_.GetConnectionHandle(player_id);
    if (!connection.has_value()) {
      SPDLOG_WARN("triggerClientEvent: player {} is not connected", player_id);
      continue;
    }
    SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED, connection.value());
    sent = true;
  }

  return sent;
}

void GameServer::SendDeathInfo(PlayerId dead_player_id) {
  PlayerDeathInfoPacket packet;
  packet.packet_type = PT_DODIE;
  packet.player_id = dead_player_id;

  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE, player.connection, 13); });
}

void GameServer::SendRespawnInfo(PlayerId respawned_player_id) {
  PlayerRespawnInfoPacket packet;
  packet.packet_type = PT_RESPAWN;
  packet.player_id = respawned_player_id;

  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE, player.connection, 13); });
}

void GameServer::SendUnconsciousInfo(PlayerId player_id, std::optional<PlayerId> attacker_id) {
  PlayerUnconsciousPacket packet;
  packet.packet_type = PT_PLAYER_UNCONSCIOUS;
  packet.player_id = player_id;
  packet.attacker_id = attacker_id;

  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, player.connection); });
}

void GameServer::SendStandUpInfo(PlayerId player_id) {
  PlayerStandUpPacket packet;
  packet.packet_type = PT_PLAYER_STANDUP;
  packet.player_id = player_id;

  player_manager_.ForEachIngamePlayer([&](const Player& player) { SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, player.connection); });
}

void GameServer::BroadcastPlayerJoined(const Player& joining_player) {
  JoinGamePacket packet;
  packet.packet_type = PT_JOIN_GAME;
  packet.position = joining_player.state.position;
  packet.normal = joining_player.state.nrot;
  packet.left_hand_item_instance = joining_player.state.left_hand_item_instance;
  packet.right_hand_item_instance = joining_player.state.right_hand_item_instance;
  packet.equipped_armor_instance = joining_player.state.equipped_armor_instance;
  packet.equipped_helmet_instance = joining_player.state.equipped_helmet_instance;
  packet.equipped_shield_instance = joining_player.state.equipped_shield_instance;
  packet.equipped_amulet_instance = joining_player.state.equipped_amulet_instance;
  packet.equipped_belt_instance = joining_player.state.equipped_belt_instance;
  packet.equipped_ring_left_instance = joining_player.state.equipped_ring_left_instance;
  packet.equipped_ring_right_instance = joining_player.state.equipped_ring_right_instance;
  packet.animation = joining_player.state.animation;
  packet.animation_name = joining_player.state.animation_name;
  packet.body_model = joining_player.body_model;
  packet.body_texture = joining_player.body_texture;
  packet.head_model = joining_player.head_model;
  packet.head_texture = joining_player.head_texture;
  packet.walk_style = joining_player.walkstyle;
  packet.player_name = joining_player.name;
  packet.player_id = joining_player.player_id;

  player_manager_.ForEachPlayer([&](const Player& existing_player) {
    if (existing_player.player_id == joining_player.player_id) {
      return;
    }
    if (!existing_player.is_ingame) {
      return;
    }
    if (!IsSameVisibilityScope(existing_player, joining_player)) {
      return;
    }
    SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE, existing_player.connection);
  });
}

void GameServer::SendExistingPlayersPacket(Player& target_player) {
  const auto stream_radius = static_cast<float>(config_.Get<std::int32_t>("stream_radius"));
  const auto stream_height = static_cast<float>(config_.Get<std::int32_t>("stream_height"));
  std::vector<ExistingPlayerInfo> existing_players;
  player_manager_.ForEachPlayer([&](Player& existing_player) {
    if (existing_player.player_id == target_player.player_id) {
      return;
    }

    // Skip players that have not finished the join handshake yet
    if (existing_player.name.empty()) {
      return;
    }
    if (!existing_player.is_ingame) {
      return;
    }

    if (existing_player.world != target_player.world || existing_player.virtual_world != target_player.virtual_world) {
      return;
    }
    if (!IsInsideStreamRange(target_player, existing_player, stream_radius, stream_height)) {
      return;
    }

    target_player.spawned_players.insert(existing_player.player_id);
    existing_player.streamed_by_players.insert(target_player.player_id);

    ExistingPlayerInfo player_packet;
    player_packet.player_id = existing_player.player_id;
    player_packet.state_sequence = existing_player.state_sequence;
    player_packet.position = existing_player.state.position;
    player_packet.left_hand_item_instance = existing_player.state.left_hand_item_instance;
    player_packet.right_hand_item_instance = existing_player.state.right_hand_item_instance;
    player_packet.equipped_armor_instance = existing_player.state.equipped_armor_instance;
    player_packet.equipped_helmet_instance = existing_player.state.equipped_helmet_instance;
    player_packet.equipped_shield_instance = existing_player.state.equipped_shield_instance;
    player_packet.equipped_amulet_instance = existing_player.state.equipped_amulet_instance;
    player_packet.equipped_belt_instance = existing_player.state.equipped_belt_instance;
    player_packet.equipped_ring_left_instance = existing_player.state.equipped_ring_left_instance;
    player_packet.equipped_ring_right_instance = existing_player.state.equipped_ring_right_instance;
    player_packet.animation = existing_player.state.animation;
    player_packet.animation_name = existing_player.state.animation_name;
    player_packet.body_model = existing_player.body_model;
    player_packet.body_texture = existing_player.body_texture;
    player_packet.head_model = existing_player.head_model;
    player_packet.head_texture = existing_player.head_texture;
    player_packet.walk_style = existing_player.walkstyle;
    player_packet.player_name = existing_player.name;
    player_packet.instance = existing_player.instance;
    player_packet.name_color_r = existing_player.name_color_r;
    player_packet.name_color_g = existing_player.name_color_g;
    player_packet.name_color_b = existing_player.name_color_b;

    player_packet.strength = existing_player.strength;
    player_packet.dexterity = existing_player.dexterity;
    player_packet.level = existing_player.level;
    player_packet.exp = existing_player.exp;
    player_packet.next_level_exp = existing_player.next_level_exp;
    player_packet.learn_points = existing_player.learn_points;
    player_packet.health = existing_player.health;
    player_packet.max_health = existing_player.max_health;
    player_packet.mana = existing_player.mana;
    player_packet.max_mana = existing_player.max_mana;
    player_packet.life_state = GetPlayerLifeState(existing_player);

    player_packet.fatness = existing_player.fatness;
    player_packet.scale = existing_player.scale;

    player_packet.weapon_skills.reserve(existing_player.weapon_skills.size());
    for (const auto& [skill_id, percentage] : existing_player.weapon_skills) {
      ExistingPlayerInfo::SkillEntry entry;
      entry.skill_id = skill_id;
      entry.percentage = percentage;
      player_packet.weapon_skills.push_back(std::move(entry));
    }

    player_packet.talents.reserve(existing_player.talents.size());
    for (const auto& [talent_id, value] : existing_player.talents) {
      ExistingPlayerInfo::TalentEntry entry;
      entry.talent_id = talent_id;
      entry.value = value;
      player_packet.talents.push_back(std::move(entry));
    }

    player_packet.overlays = existing_player.overlays;

    existing_players.push_back(std::move(player_packet));
  });

  if (existing_players.empty()) {
    return;
  }

  ExistingPlayersPacket existing_players_packet;
  existing_players_packet.packet_type = PT_EXISTING_PLAYERS;
  existing_players_packet.existing_players = std::move(existing_players);
  SerializeAndSend(existing_players_packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, target_player.connection);
}

bool GameServer::SpawnPlayer(PlayerId player_id, std::optional<glm::vec3> position_override) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("spawnPlayer called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  if (player.is_ingame) {
    SPDLOG_WARN("spawnPlayer called for already spawned player {}", player_id);
    return false;
  }

  if (position_override.has_value()) {
    player.state.position = *position_override;
  }

  const bool was_dead = player.tod != 0;

  player.flags = 0;
  player.tod = 0;
  player.health = player.max_health;
  player.mana = player.max_mana;
  player.state.health_points = player.health;
  player.state.mana_points = player.mana;
  player.state.life_state = GetPlayerLifeState(player);
  ClearTransientCombatState(player);
  AdvancePlayerStateSequence(player);

  player.is_ingame = 1;

  const auto packet = MakePlayerSpawnPacket(player);

  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, player.connection);
  StreamRelevantGroundItemsToPlayer(player, true);

  const auto stream_radius = static_cast<float>(config_.Get<std::int32_t>("stream_radius"));
  const auto stream_height = static_cast<float>(config_.Get<std::int32_t>("stream_height"));
  player_manager_.ForEachIngamePlayer([&](Player& existing_player) {
    if (existing_player.player_id == player.player_id) {
      return;
    }
    if (!IsInsideStreamRange(existing_player, player, stream_radius, stream_height)) {
      return;
    }

    existing_player.spawned_players.insert(player.player_id);
    player.streamed_by_players.insert(existing_player.player_id);

    SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, existing_player.connection);
  });

  if (was_dead) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerRespawnName, OnPlayerRespawnEvent{player.player_id, player.state.position});
  }

  EventManager::Instance().TriggerEvent(kEventOnPlayerSpawnName, OnPlayerSpawnEvent{player.player_id, player.state.position});
  return true;
}

bool GameServer::UnspawnPlayer(PlayerId player_id) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("unspawnPlayer called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  if (!player.is_ingame) {
    SPDLOG_WARN("unspawnPlayer called for already unspawned player {}", player_id);
    return false;
  }

  UnstreamGroundItemsFromPlayer(player, true);
  StreamOutAllKnownPlayers(player_manager_, player);

  DisconnectionInfoPacket subject_left_packet;
  subject_left_packet.packet_type = PT_LEFT_GAME;
  subject_left_packet.disconnected_id = player.player_id;

  SerializeAndSend(subject_left_packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, player.connection);

  player.is_ingame = 0;
  return true;
}

bool GameServer::SetPlayerName(PlayerId player_id, const std::string& name) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerName called for unknown player id {}", player_id);
    return false;
  }

  auto sanitized_name = SanitizePlayerName(name);
  if (sanitized_name.empty()) {
    SPDLOG_WARN("setPlayerName called with empty name for player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.name = sanitized_name;

  PlayerNameUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_NAME_UPDATE;
  packet.player_id = player.player_id;
  packet.name = player.name;

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::SetPlayerPosition(PlayerId player_id, const glm::vec3& position) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerPosition called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.state.position = position;
  const auto state_sequence = AdvancePlayerStateSequence(player);

  PlayerPositionUpdatePacket packet{};
  packet.packet_type = PT_MAP_ONLY;
  packet.player_id = player.player_id;
  packet.state_sequence = state_sequence;
  packet.position = position;

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);

  return true;
}

bool GameServer::SetPlayerAngle(PlayerId player_id, float angle) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerAngle called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const float angle_radians = angle;
  player.state.nrot = glm::vec3{std::sin(angle_radians), 0.0f, std::cos(angle_radians)};
  AdvancePlayerStateSequence(player);

  const auto packet = MakePlayerStateUpdatePacket(player);
  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::SetPlayerWorld(PlayerId player_id, const std::string& world, std::optional<std::string> start_point) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerWorld called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto sanitized_world = SanitizeWorldName(world);
  const bool world_changed = sanitized_world != player.world;
  std::string start_point_name = SanitizeServerText(start_point.value_or(""));
  if (start_point_name.size() > 64) {
    start_point_name.resize(64);
  }

  if (world_changed) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerWorldChangeName,
                                          OnPlayerChangeWorldEvent{player.player_id, sanitized_world, start_point_name});
  }

  if (player.is_ingame && world_changed) {
    UnstreamGroundItemsFromPlayer(player, true);
    StreamOutAllKnownPlayers(player_manager_, player);
  }

  player.world = sanitized_world;

  PlayerWorldUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_WORLD_UPDATE;
  packet.player_id = player.player_id;
  packet.world_name = player.world;
  packet.start_point = start_point_name;

  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, player.connection);

  if (!player.is_ingame || !world_changed) {
    return true;
  }

  SendExistingPlayersPacket(player);
  StreamRelevantGroundItemsToPlayer(player);

  const auto spawn_packet = MakePlayerSpawnPacket(player);
  const auto stream_radius = static_cast<float>(config_.Get<std::int32_t>("stream_radius"));
  const auto stream_height = static_cast<float>(config_.Get<std::int32_t>("stream_height"));

  player_manager_.ForEachIngamePlayer([&](Player& existing_player) {
    if (existing_player.player_id == player.player_id) {
      return;
    }
    if (!IsInsideStreamRange(existing_player, player, stream_radius, stream_height)) {
      return;
    }

    existing_player.spawned_players.insert(player.player_id);
    player.streamed_by_players.insert(existing_player.player_id);

    SerializeAndSend(spawn_packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, existing_player.connection);
  });

  return true;
}

bool GameServer::SetPlayerVirtualWorld(PlayerId player_id, std::int32_t virtual_world) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerVirtualWorld called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto clamped_virtual_world = std::clamp<std::int32_t>(virtual_world, 0, 65535);
  const bool virtual_world_changed = clamped_virtual_world != player.virtual_world;

  if (player.is_ingame && virtual_world_changed) {
    UnstreamGroundItemsFromPlayer(player, true);
    StreamOutAllKnownPlayers(player_manager_, player);
  }

  player.virtual_world = clamped_virtual_world;

  if (!player.is_ingame || !virtual_world_changed) {
    return true;
  }

  SendExistingPlayersPacket(player);
  StreamRelevantGroundItemsToPlayer(player);

  const auto spawn_packet = MakePlayerSpawnPacket(player);
  const auto stream_radius = static_cast<float>(config_.Get<std::int32_t>("stream_radius"));
  const auto stream_height = static_cast<float>(config_.Get<std::int32_t>("stream_height"));

  player_manager_.ForEachIngamePlayer([&](Player& existing_player) {
    if (existing_player.player_id == player.player_id) {
      return;
    }
    if (!IsInsideStreamRange(existing_player, player, stream_radius, stream_height)) {
      return;
    }

    existing_player.spawned_players.insert(player.player_id);
    player.streamed_by_players.insert(existing_player.player_id);

    SerializeAndSend(spawn_packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED, existing_player.connection);
  });

  return true;
}

bool GameServer::SetPlayerStrength(PlayerId player_id, std::int32_t strength) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerStrength called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.strength = std::max<std::int32_t>(0, strength);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_STRENGTH, player.strength);
  return true;
}

bool GameServer::SetPlayerDexterity(PlayerId player_id, std::int32_t dexterity) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerDexterity called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.dexterity = std::max<std::int32_t>(0, dexterity);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_DEXTERITY, player.dexterity);
  return true;
}

bool GameServer::SetPlayerLevel(PlayerId player_id, std::int32_t level) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerLevel called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.level = std::max<std::int32_t>(0, level);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_LEVEL, player.level);
  return true;
}

bool GameServer::SetPlayerExp(PlayerId player_id, std::int32_t exp) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerExp called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.exp = std::max<std::int32_t>(0, exp);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_EXP, player.exp);
  return true;
}

bool GameServer::SetPlayerNextLevelExp(PlayerId player_id, std::int32_t next_level_exp) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerNextLevelExp called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.next_level_exp = std::max<std::int32_t>(0, next_level_exp);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_NEXT_LEVEL_EXP, player.next_level_exp);
  return true;
}

bool GameServer::SetPlayerLearnPoints(PlayerId player_id, std::int32_t learn_points) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerLearnPoints called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.learn_points = std::max<std::int32_t>(0, learn_points);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_LEARN_POINTS, player.learn_points);
  return true;
}

bool GameServer::SetPlayerMaxHealth(PlayerId player_id, std::int32_t max_health) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerMaxHealth called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto old_health = player.health;
  player.max_health = static_cast<std::int16_t>(std::max<std::int32_t>(0, max_health));
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_MAX_HEALTH, player.max_health);
  if (player.tod == 0 && player.max_health <= 0) {
    HandlePlayerDeath(player, std::nullopt);
    return true;
  }
  if (player.health > player.max_health) {
    player.health = player.max_health;
  }
  if (old_health != player.health) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName,
                                          OnPlayerChangeHealthEvent{player.player_id, old_health, player.health});
  }
  player.state.health_points = player.health;
  player.state.life_state = GetPlayerLifeState(player);
  AdvancePlayerStateSequence(player);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_HEALTH, player.health);
  return true;
}

bool GameServer::SetPlayerHealth(PlayerId player_id, std::int32_t health) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerHealth called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto clamped = std::clamp<std::int32_t>(health, 0, player.max_health);
  const auto old_health = player.health;
  if (player.tod != 0 && clamped > 0) {
    SPDLOG_WARN("setPlayerHealth called with positive health for dead player {}; use respawnPlayer instead", player_id);
    return false;
  }
  if (player.tod == 0 && old_health > 0 && clamped <= 0) {
    HandlePlayerDeath(player, std::nullopt);
    return true;
  }

  const bool should_stand_up = player.tod == 0 && (player.flags & PlayerManager::PL_UNCONCIOUS) != 0 && clamped > 1;
  if (should_stand_up) {
    player.flags &= ~PlayerManager::PL_UNCONCIOUS;
  }

  player.health = static_cast<std::int16_t>(clamped);
  if (old_health != player.health) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeHealthName,
                                          OnPlayerChangeHealthEvent{player.player_id, old_health, player.health});
  }
  player.state.health_points = player.health;
  player.state.life_state = GetPlayerLifeState(player);
  AdvancePlayerStateSequence(player);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_HEALTH, player.health);
  if (should_stand_up) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerStandUpName, OnPlayerStandUpEvent{player.player_id});
    SendStandUpInfo(player.player_id);
  }
  return true;
}

bool GameServer::SetPlayerMaxMana(PlayerId player_id, std::int32_t max_mana) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerMaxMana called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto old_mana = player.mana;
  player.max_mana = static_cast<std::int16_t>(std::max<std::int32_t>(0, max_mana));
  if (player.mana > player.max_mana) {
    player.mana = player.max_mana;
  }
  if (old_mana != player.mana) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeManaName, OnPlayerChangeManaEvent{player.player_id, old_mana, player.mana});
  }
  player.state.mana_points = player.mana;
  AdvancePlayerStateSequence(player);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_MAX_MANA, player.max_mana);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_MANA, player.mana);
  return true;
}

bool GameServer::SetPlayerMana(PlayerId player_id, std::int32_t mana) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerMana called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto clamped = std::clamp<std::int32_t>(mana, 0, player.max_mana);
  const auto old_mana = player.mana;
  player.mana = static_cast<std::int16_t>(clamped);
  if (old_mana != player.mana) {
    EventManager::Instance().TriggerEvent(kEventOnPlayerChangeManaName, OnPlayerChangeManaEvent{player.player_id, old_mana, player.mana});
  }
  player.state.mana_points = player.mana;
  AdvancePlayerStateSequence(player);
  SendPlayerAttributeUpdate(player_manager_, player, ATTR_MANA, player.mana);
  return true;
}

bool GameServer::SetPlayerVisual(PlayerId player_id, const std::string& body_model, std::int16_t body_texture, const std::string& head_model,
                                 std::int16_t head_texture) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerVisual called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.body_model = SanitizeServerText(body_model);
  player.head_model = SanitizeServerText(head_model);
  if (player.body_model.size() > 64) {
    player.body_model.resize(64);
  }
  if (player.head_model.size() > 64) {
    player.head_model.resize(64);
  }
  player.body_texture = static_cast<std::int16_t>(std::clamp<int>(body_texture, 0, 255));
  player.head_texture = static_cast<std::int16_t>(std::clamp<int>(head_texture, 0, 255));

  PlayerVisualUpdatePacket packet{};
  packet.packet_type = PT_PLAYER_VISUAL_UPDATE;
  packet.player_id = player.player_id;
  packet.body_model = player.body_model;
  packet.body_texture = player.body_texture;
  packet.head_model = player.head_model;
  packet.head_texture = player.head_texture;

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::SetPlayerInstance(PlayerId player_id, const std::string& instance) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerInstance called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.instance = SanitizeServerText(instance);
  if (player.instance.size() > 255) {
    player.instance.resize(255);
  }

  BroadcastPlayerInstanceUpdate(player_manager_, player);
  return true;
}

bool GameServer::SetPlayerColor(PlayerId player_id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerColor called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.name_color_r = r;
  player.name_color_g = g;
  player.name_color_b = b;

  BroadcastPlayerColorUpdate(player_manager_, player);
  return true;
}

bool GameServer::SetPlayerSkillWeapon(PlayerId player_id, std::int32_t skill_id, std::int32_t percentage) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerSkillWeapon called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto clamped = std::clamp<std::int32_t>(percentage, 0, 100);
  player.weapon_skills[skill_id] = clamped;
  BroadcastPlayerSkillWeaponUpdate(player_manager_, player, skill_id, clamped);
  return true;
}

bool GameServer::SetPlayerTalent(PlayerId player_id, std::int32_t talent_id, std::int32_t talent_value) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerTalent called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto clamped = std::max<std::int32_t>(0, talent_value);
  player.talents[talent_id] = clamped;
  BroadcastPlayerTalentUpdate(player_manager_, player, talent_id, clamped);
  return true;
}

bool GameServer::SetPlayerFatness(PlayerId player_id, float fatness) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerFatness called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.fatness = fatness;
  BroadcastPlayerFatnessUpdate(player_manager_, player);
  return true;
}

bool GameServer::SetPlayerScale(PlayerId player_id, const glm::vec3& scale) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerScale called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  player.scale = scale;
  BroadcastPlayerScaleUpdate(player_manager_, player);
  return true;
}

bool GameServer::SetPlayerWeaponMode(PlayerId player_id, std::int32_t weapon_mode) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerWeaponMode called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  const auto old_mode = player.state.weapon_mode;
  const auto clamped_mode = static_cast<std::uint8_t>(std::clamp<std::int32_t>(weapon_mode, 0, 255));
  player.state.weapon_mode = clamped_mode;

  if (old_mode != player.state.weapon_mode) {
    EventManager::Instance().TriggerEvent(
        kEventOnPlayerWeaponModeChangeName,
        OnPlayerWeaponModeChangeEvent{player.player_id, old_mode, player.state.weapon_mode});
  }

  AdvancePlayerStateSequence(player);

  const auto packet = MakePlayerStateUpdatePacket(player);
  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::ApplyPlayerOverlay(PlayerId player_id, const std::string& overlay) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("applyPlayerOverlay called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  auto overlay_name = SanitizeServerText(overlay);
  if (overlay_name.size() > 255) {
    overlay_name.resize(255);
  }
  if (overlay_name.empty()) {
    return false;
  }

  if (std::find(player.overlays.begin(), player.overlays.end(), overlay_name) == player.overlays.end()) {
    player.overlays.push_back(overlay_name);
    BroadcastPlayerOverlayUpdate(player_manager_, player, overlay_name, true);
  }

  return true;
}

bool GameServer::RemovePlayerOverlay(PlayerId player_id, const std::string& overlay) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("removePlayerOverlay called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  auto overlay_name = SanitizeServerText(overlay);
  if (overlay_name.size() > 255) {
    overlay_name.resize(255);
  }
  if (overlay_name.empty()) {
    return false;
  }

  auto it = std::find(player.overlays.begin(), player.overlays.end(), overlay_name);
  if (it == player.overlays.end()) {
    return false;
  }

  player.overlays.erase(it);
  BroadcastPlayerOverlayUpdate(player_manager_, player, overlay_name, false);
  return true;
}

bool GameServer::PlayAnimation(PlayerId player_id, const std::string& animation) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("playAni called for unknown player id {}", player_id);
    return false;
  }

  auto animation_name = SanitizeServerText(animation);
  if (animation_name.size() > 255) {
    animation_name.resize(255);
  }
  if (animation_name.empty()) {
    return false;
  }

  auto& player = player_opt->get();
  const std::optional<std::int16_t> animation_id = animation_registry_.ResolveId(animation_name, BuildPreferredAnimationMdsList(player));
  if (!animation_id.has_value()) {
    SPDLOG_WARN("playAni called with unknown, inactive, or ambiguous animation '{}'.", animation_name);
    return false;
  }

  PlayerAnimationPacket packet{};
  packet.packet_type = PT_PLAYER_ANI_PLAY;
  packet.player_id = player.player_id;
  packet.animation = *animation_id;

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::StopAnimation(PlayerId player_id, const std::string& animation) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("stopAni called for unknown player id {}", player_id);
    return false;
  }

  auto animation_name = SanitizeServerText(animation);
  if (animation_name.size() > 255) {
    animation_name.resize(255);
  }

  auto& player = player_opt->get();
  std::int16_t animation_id = -1;
  if (!animation_name.empty()) {
    const std::optional<std::int16_t> resolved_animation_id =
        animation_registry_.ResolveId(animation_name, BuildPreferredAnimationMdsList(player));
    if (!resolved_animation_id.has_value()) {
      SPDLOG_WARN("stopAni called with unknown, inactive, or ambiguous animation '{}'.", animation_name);
      return false;
    }
    animation_id = *resolved_animation_id;
  }

  PlayerAnimationStopPacket packet{};
  packet.packet_type = PT_PLAYER_ANI_STOP;
  packet.player_id = player.player_id;
  packet.animation = animation_id;

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::PlayFaceAnimation(PlayerId player_id, const std::string& animation) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("playFaceAni called for unknown player id {}", player_id);
    return false;
  }

  auto animation_name = SanitizeServerText(animation);
  if (animation_name.size() > 255) {
    animation_name.resize(255);
  }
  if (animation_name.empty()) {
    return false;
  }

  auto& player = player_opt->get();
  PlayerFaceAnimationPacket packet{};
  packet.packet_type = PT_PLAYER_FACE_ANI_PLAY;
  packet.player_id = player.player_id;
  packet.animation = std::move(animation_name);

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::StopFaceAnimation(PlayerId player_id, const std::string& animation) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("stopFaceAni called for unknown player id {}", player_id);
    return false;
  }

  auto animation_name = SanitizeServerText(animation);
  if (animation_name.size() > 255) {
    animation_name.resize(255);
  }

  auto& player = player_opt->get();
  PlayerFaceAnimationStopPacket packet{};
  packet.packet_type = PT_PLAYER_FACE_ANI_STOP;
  packet.player_id = player.player_id;
  packet.animation = std::move(animation_name);

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::PlayGesticulation(PlayerId player_id) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("playGesticulation called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  PlayerGesticulationPacket packet{};
  packet.packet_type = PT_PLAYER_GESTICULATION;
  packet.player_id = player.player_id;

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::GiveItem(PlayerId player_id, const std::string& instance, std::int32_t amount) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("giveItem called for unknown player id {}", player_id);
    return false;
  }

  if (amount <= 0) {
    return false;
  }

  auto item_instance = ResolveItemInstance(instance);
  if (!item_instance.has_value()) {
    SPDLOG_WARN("giveItem called with unknown item instance '{}'", SanitizeServerText(instance));
    return false;
  }

  auto& player = player_opt->get();
  AddInventoryItem(player, *item_instance, amount);

  GiveItemPacket packet{};
  packet.packet_type = PT_GIVEITEM;
  packet.player_id = player.player_id;
  packet.item_instance = std::move(*item_instance);
  packet.item_amount = std::max<std::int32_t>(0, amount);

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::EquipItem(PlayerId player_id, const std::string& instance, std::int32_t slot_id) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("equipItem called for unknown player id {}", player_id);
    return false;
  }

  auto item_instance = ResolveItemInstance(instance);
  if (!item_instance.has_value()) {
    SPDLOG_WARN("equipItem called with unknown item instance '{}'", SanitizeServerText(instance));
    return false;
  }

  auto& player = player_opt->get();
  EquipItemPacket packet{};
  packet.packet_type = PT_EQUIPITEM;
  packet.player_id = player.player_id;
  packet.item_instance = std::move(*item_instance);
  packet.slot_id = static_cast<std::int16_t>(std::clamp<std::int32_t>(
      slot_id, std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max()));

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

bool GameServer::UnequipItem(PlayerId player_id, const std::string& instance) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("unequipItem called for unknown player id {}", player_id);
    return false;
  }

  auto item_instance = ResolveItemInstance(instance);
  if (!item_instance.has_value()) {
    SPDLOG_WARN("unequipItem called with unknown item instance '{}'", SanitizeServerText(instance));
    return false;
  }

  auto& player = player_opt->get();
  UnequipItemPacket packet{};
  packet.packet_type = PT_UNEQUIPITEM;
  packet.player_id = player.player_id;
  packet.item_instance = std::move(*item_instance);

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

std::int32_t GameServer::HasItem(PlayerId player_id, const std::string& instance) const {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("hasItem called for unknown player id {}", player_id);
    return 0;
  }

  auto item_instance = ResolveItemInstance(instance);
  if (!item_instance.has_value()) {
    return 0;
  }

  const auto& player = player_opt->get();
  const auto it = player.inventory.find(*item_instance);
  if (it == player.inventory.end()) {
    return 0;
  }

  return std::max<std::int32_t>(0, it->second);
}

bool GameServer::RemoveItem(PlayerId player_id, const std::string& instance, std::int32_t amount) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("removeItem called for unknown player id {}", player_id);
    return false;
  }

  if (amount <= 0) {
    return false;
  }

  auto item_instance = ResolveItemInstance(instance);
  if (!item_instance.has_value()) {
    SPDLOG_WARN("removeItem called with unknown item instance '{}'", SanitizeServerText(instance));
    return false;
  }

  auto& player = player_opt->get();
  RemoveInventoryItem(player, *item_instance, amount);

  RemoveItemPacket packet{};
  packet.packet_type = PT_REMOVEITEM;
  packet.player_id = player.player_id;
  packet.item_instance = std::move(*item_instance);
  packet.item_amount = std::max<std::int32_t>(0, amount);

  BroadcastToRelevant(player_manager_, player, packet, IMMEDIATE_PRIORITY, RELIABLE);
  return true;
}

std::optional<glm::vec3> GameServer::GetPlayerPosition(PlayerId player_id) const {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    return std::nullopt;
  }

  const auto& player = player_opt->get();
  return player.state.position;
}

std::string GameServer::GetHostname() const {
  return config_.Get<std::string>("name");
}

std::uint32_t GameServer::GetMaxSlots() const {
  return static_cast<std::uint32_t>(config_.Get<std::int32_t>("slots"));
}

bool GameServer::SetServerWorld(const std::string& world) {
  auto sanitized_world = SanitizeWorldName(world);
  server_world_ = sanitized_world;
  config_.Set<std::string>("map", sanitized_world);
  return true;
}

std::string GameServer::GetServerWorld() const {
  return server_world_;
}

std::vector<GameServer::PlayerId> GameServer::FindNearbyPlayers(const glm::vec3& position, float radius, const std::string& world,
                                                                std::int32_t virtual_world) const {
  std::vector<PlayerId> nearby_players;
  if (radius < 0.0f) {
    return nearby_players;
  }

  const auto sanitized_world = SanitizeWorldName(world);
  const float radius_squared = radius * radius;
  nearby_players.reserve(player_manager_.GetPlayerCount());

  player_manager_.ForEachIngamePlayer([&](const Player& player) {
    if (!sanitized_world.empty() && player.world != sanitized_world) {
      return;
    }

    if (player.virtual_world != virtual_world) {
      return;
    }

    const auto delta = player.state.position - position;
    if (glm::dot(delta, delta) <= radius_squared) {
      nearby_players.push_back(player.player_id);
    }
  });

  return nearby_players;
}

std::vector<GameServer::PlayerId> GameServer::GetSpawnedPlayersForPlayer(PlayerId player_id) const {
  std::vector<PlayerId> spawned_players;
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value() || !player_opt->get().is_ingame) {
    return spawned_players;
  }

  const auto& player = player_opt->get();
  spawned_players.reserve(player.spawned_players.size());
  spawned_players.insert(spawned_players.end(), player.spawned_players.begin(), player.spawned_players.end());

  return spawned_players;
}

std::vector<GameServer::PlayerId> GameServer::GetStreamedPlayersByPlayer(PlayerId player_id) const {
  std::vector<PlayerId> streaming_players;
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value() || !player_opt->get().is_ingame) {
    return streaming_players;
  }

  const auto& player = player_opt->get();
  streaming_players.reserve(player.streamed_by_players.size());
  streaming_players.insert(streaming_players.end(), player.streamed_by_players.begin(), player.streamed_by_players.end());

  return streaming_players;
}

bool GameServer::SetStreamerRadius(std::int32_t radius) {
  if (radius < 0) {
    SPDLOG_WARN("setStreamerRadius called with invalid radius {}", radius);
    return false;
  }

  config_.Set<std::int32_t>("stream_radius", radius);
  return true;
}

std::int32_t GameServer::GetStreamerRadius() const {
  return config_.Get<std::int32_t>("stream_radius");
}

bool GameServer::SetStreamerHeight(std::int32_t height) {
  if (height < 0) {
    SPDLOG_WARN("setStreamerHeight called with invalid height {}", height);
    return false;
  }

  config_.Set<std::int32_t>("stream_height", height);
  return true;
}

std::int32_t GameServer::GetStreamerHeight() const {
  return config_.Get<std::int32_t>("stream_height");
}

bool GameServer::SetTime(std::int32_t hour, std::int32_t min, std::int32_t day) {
  if (!clock_) {
    return false;
  }

  if (hour < 0 || hour > 23 || min < 0 || min > 59 || day < 0) {
    SPDLOG_WARN("setTime called with invalid parameters: day={}, hour={}, min={}", day, hour, min);
    return false;
  }

  auto current_time = clock_->GetTime();
  GothicClock::Time new_time{static_cast<std::uint16_t>(day == 0 ? current_time.day_ : day), static_cast<std::uint8_t>(hour),
                             static_cast<std::uint8_t>(min)};
  clock_->UpdateTime(new_time);

  EventManager::Instance().TriggerEvent(kEventOnClockUpdateName, OnClockUpdateEvent{new_time.day_, new_time.hour_, new_time.min_});

  BroadcastGameInfo();
  weather_.ResetLastSkyTime(new_time);
  last_weather_update_minute_ = -1;
  if (weather_.Update(new_time)) {
    BroadcastSkySettings();
  }
  return true;
}

GothicClock::Time GameServer::GetTime() const {
  if (!clock_) {
    return GothicClock::Time{};
  }

  return clock_->GetTime();
}

bool GameServer::SetDayLength(float day_length_ms) {
  if (!clock_) {
    return false;
  }

  if (!clock_->SetDayLengthMs(day_length_ms)) {
    return false;
  }

  BroadcastGameInfo();
  return true;
}

float GameServer::GetDayLength() const {
  if (!clock_) {
    return 0.0f;
  }

  return static_cast<float>(clock_->GetDayLengthMs());
}

bool GameServer::SetWeatherType(std::int32_t weather_type) {
  if (!weather_.SetWeatherType(weather_type)) {
    SPDLOG_WARN("Sky.weatherType assigned invalid weather type {}", weather_type);
    return false;
  }

  BroadcastSkySettings();
  return true;
}

std::int32_t GameServer::GetWeatherType() const {
  return weather_.GetWeatherType();
}

bool GameServer::SetRainStartTime(std::int32_t hour, std::int32_t min) {
  if (!weather_.SetRainStartTime(hour, min)) {
    SPDLOG_WARN("Sky.rainStartTime assigned invalid parameters: hour={}, min={}", hour, min);
    return false;
  }

  BroadcastSkySettings();
  return true;
}

std::pair<std::int32_t, std::int32_t> GameServer::GetRainStartTime() const {
  return weather_.GetRainStartTime();
}

bool GameServer::SetRainStopTime(std::int32_t hour, std::int32_t min) {
  if (!weather_.SetRainStopTime(hour, min)) {
    SPDLOG_WARN("Sky.rainStopTime assigned invalid parameters: hour={}, min={}", hour, min);
    return false;
  }

  BroadcastSkySettings();
  return true;
}

std::pair<std::int32_t, std::int32_t> GameServer::GetRainStopTime() const {
  return weather_.GetRainStopTime();
}

bool GameServer::SetWindScale(float wind_scale) {
  if (!weather_.SetWindScale(wind_scale)) {
    SPDLOG_WARN("Sky.windScale assigned non-finite value {}", wind_scale);
    return false;
  }

  BroadcastSkySettings();
  return true;
}

float GameServer::GetWindScale() const {
  return weather_.GetWindScale();
}

bool GameServer::SetDontRain(bool toggle) {
  weather_.SetDontRain(toggle);
  if (clock_) {
    weather_.Update(clock_->GetTime());
  }

  BroadcastSkySettings();
  return true;
}

bool GameServer::GetDontRain() const {
  return weather_.GetDontRain();
}

bool GameServer::SetWeatherDisabled(bool toggle) {
  weather_.SetDisabled(toggle);
  BroadcastSkySettings();
  return true;
}

bool GameServer::GetWeatherDisabled() const {
  return weather_.GetDisabled();
}

bool GameServer::KickPlayer(PlayerId player_id, const std::string& reason) {
  if (!g_net_server) {
    SPDLOG_WARN("kickPlayer called before the server is initialized");
    return false;
  }

  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("kickPlayer called for unknown player id {}", player_id);
    return false;
  }

  const auto& player = player_opt->get();
  if (!reason.empty()) {
    SPDLOG_INFO("Kicking player {} ({}) for reason '{}'", player.player_id, player.name, reason);
  } else {
    SPDLOG_INFO("Kicking player {} ({})", player.player_id, player.name);
  }

  g_net_server->CloseConnection(player.connection, true);
  return true;
}

bool GameServer::BanPlayer(PlayerId player_id, const std::string& reason) {
  if (!g_net_server) {
    SPDLOG_WARN("banPlayer called before the server is initialized");
    return false;
  }

  if (!ban_manager_) {
    SPDLOG_WARN("banPlayer called before ban manager is initialized");
    return false;
  }

  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("banPlayer called for unknown player id {}", player_id);
    return false;
  }

  const auto& player = player_opt->get();
  const char* ip = g_net_server->GetPlayerIp(player.connection);
  if (!ip || std::string_view(ip).empty()) {
    SPDLOG_WARN("banPlayer could not resolve IP for player id {}", player_id);
    return false;
  }

  auto& ban_list = ban_manager_->GetBanList();
  auto ban_it = std::find_if(ban_list.begin(), ban_list.end(), [&](const BanEntry& entry) { return entry.ip == ip; });

  if (ban_it == ban_list.end()) {
    BanEntry entry{};
    entry.nickname = player.name;
    entry.ip = ip;
    entry.date = FormatCurrentDateTime();
    entry.reason = reason;
    ban_list.emplace_back(std::move(entry));
  } else {
    ban_it->nickname = player.name;
    ban_it->date = FormatCurrentDateTime();
    ban_it->reason = reason;
  }

  g_net_server->AddToBanList(ip, 0);
  ban_manager_->Save();
  g_net_server->CloseConnection(player.connection, true);
  return true;
}

bool GameServer::IsPlayerConnected(PlayerId player_id) const {
  return player_manager_.HasPlayer(player_id);
}

bool GameServer::IsPlayerDead(PlayerId player_id) const {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    return false;
  }

  return player_opt->get().tod != 0;
}

bool GameServer::IsPlayerSpawned(PlayerId player_id) const {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    return false;
  }

  return player_opt->get().is_ingame != 0;
}

bool GameServer::IsPlayerUnconscious(PlayerId player_id) const {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    return false;
  }

  return (player_opt->get().flags & PlayerManager::PL_UNCONCIOUS) != 0;
}

bool GameServer::RespawnPlayer(PlayerId player_id) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("respawnPlayer called for unknown player id {}", player_id);
    return false;
  }

  auto& player = player_opt->get();
  if (!player.is_ingame || player.tod == 0) {
    return false;
  }

  return RespawnPlayerInternal(player);
}

bool GameServer::SetPlayerRespawnTime(PlayerId player_id, std::int32_t respawn_time_ms) {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    SPDLOG_WARN("setPlayerRespawnTime called for unknown player id {}", player_id);
    return false;
  }

  player_opt->get().respawn_time_ms = respawn_time_ms;
  return true;
}

std::optional<std::int32_t> GameServer::GetPlayerRespawnTime(PlayerId player_id) const {
  auto player_opt = player_manager_.GetPlayer(player_id);
  if (!player_opt.has_value()) {
    return std::nullopt;
  }

  if (player_opt->get().respawn_time_ms.has_value()) {
    return player_opt->get().respawn_time_ms.value();
  }

  auto default_respawn_seconds = config_.Get<std::int32_t>("respawn_time_seconds");
  if (default_respawn_seconds < 0) {
    return 0;
  }

  return default_respawn_seconds * 1000;
}

std::uint32_t GameServer::GetPort() const {
  if (g_net_server) {
    return g_net_server->GetPort();
  }
  return 0;
}

std::string GameServer::GetPlayerIp(PlayerId player_id) const {
  if (!g_net_server) {
    return {};
  }

  auto connection_opt = player_manager_.GetConnectionHandle(player_id);
  if (!connection_opt.has_value()) {
    return {};
  }

  const char* ip = g_net_server->GetPlayerIp(connection_opt.value());
  if (!ip || std::string_view(ip).empty() || std::string_view(ip) == "UNASSIGNED_SYSTEM_ADDRESS") {
    return {};
  }

  return ip;
}

std::int32_t GameServer::GetPlayerPing(PlayerId player_id) const {
  if (!g_net_server) {
    return -1;
  }

  auto connection_opt = player_manager_.GetConnectionHandle(player_id);
  if (!connection_opt.has_value()) {
    return -1;
  }

  return g_net_server->GetPing(connection_opt.value());
}

std::string GameServer::GetPlayerMacAddress(PlayerId player_id) const {
  (void)player_id;
  return {};
}

std::string GameServer::GetPlayerUUID(PlayerId player_id) const {
  (void)player_id;
  return {};
}
