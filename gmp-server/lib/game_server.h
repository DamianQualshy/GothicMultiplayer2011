

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

#pragma once

#include <string.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace httplib {
class Server;
struct Request;
struct Response;
}  // namespace httplib

#include "Script.h"
#include "animation_registry.h"
#include "ban_manager.h"
#include "client_resource_packager.h"
#include "common_structs.h"
#include "config.h"
#include "player_manager.h"
#include "item_registry.h"
#include "item_ground_manager.h"
#include "resource_manager.h"
#include "resource_server.h"
#include "znet_server.h"
#include "gothic_clock.h"
#include "gothic_weather.h"

#define DEFAULT_ADMIN_PORT 0x404

class CLog;
class GothicClock;

enum CONFIG_FLAGS { HIDE_MAP = 0x04 };

struct Packet {
  // Not owning.
  unsigned char* data = nullptr;
  std::uint32_t length = 0;
  Net::ConnectionHandle id;
};

class GameServer : public Net::PacketHandler {
public:
  using PlayerId = PlayerManager::PlayerId;
  using Player = PlayerManager::Player;

  using BanEntry = BanManager::BanEntry;

  GameServer();
  ~GameServer() override;

  void AddToPublicListHTTP();
  nlohmann::json BuildMasterServerPayload() const;
  bool Receive();
  bool HandlePacket(Net::ConnectionHandle connectionHandle, unsigned char* data, std::uint32_t size);
  void Run();
  bool Init();
  bool IsPublic(void);
  void SendMessageToAll(std::uint8_t r, std::uint8_t g, std::uint8_t b, const std::string& text);
  void SendMessageToPlayer(PlayerId player_id, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                           const std::string& text);
  void SendPlayerMessageToAll(PlayerId sender_id, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                              const std::string& text);
  void SendPlayerMessageToPlayer(PlayerId sender_id, PlayerId receiver_id, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b, const std::string& text);
  bool SpawnPlayer(PlayerId player_id, std::optional<glm::vec3> position_override = std::nullopt);
  bool UnspawnPlayer(PlayerId player_id);
  bool SetPlayerName(PlayerId player_id, const std::string& name);
  bool SetPlayerInstance(PlayerId player_id, const std::string& instance);
  bool SetPlayerColor(PlayerId player_id, std::uint8_t r, std::uint8_t g, std::uint8_t b);
  bool SetPlayerStrength(PlayerId player_id, std::int32_t strength);
  bool SetPlayerDexterity(PlayerId player_id, std::int32_t dexterity);
  bool SetPlayerLevel(PlayerId player_id, std::int32_t level);
  bool SetPlayerExp(PlayerId player_id, std::int32_t exp);
  bool SetPlayerNextLevelExp(PlayerId player_id, std::int32_t next_level_exp);
  bool SetPlayerLearnPoints(PlayerId player_id, std::int32_t learn_points);
  bool SetPlayerMaxHealth(PlayerId player_id, std::int32_t max_health);
  bool SetPlayerHealth(PlayerId player_id, std::int32_t health);
  bool SetPlayerMaxMana(PlayerId player_id, std::int32_t max_mana);
  bool SetPlayerMana(PlayerId player_id, std::int32_t mana);
  bool SetPlayerSkillWeapon(PlayerId player_id, std::int32_t skill_id, std::int32_t percentage);
  bool SetPlayerTalent(PlayerId player_id, std::int32_t talent_id, std::int32_t talent_value);
  bool SetPlayerVisual(PlayerId player_id, const std::string& body_model, std::int16_t body_texture, const std::string& head_model,
                       std::int16_t head_texture, std::int16_t teeth_texture, std::int16_t skin_color);
  bool SetPlayerFatness(PlayerId player_id, float fatness);
  bool SetPlayerScale(PlayerId player_id, const glm::vec3& scale);
  bool SetPlayerWeaponMode(PlayerId player_id, std::int32_t weapon_mode);
  bool ApplyPlayerOverlay(PlayerId player_id, const std::string& overlay);
  bool RemovePlayerOverlay(PlayerId player_id, const std::string& overlay);
  bool PlayAnimation(PlayerId player_id, const std::string& animation);
  bool StopAnimation(PlayerId player_id, const std::string& animation);
  bool PlayFaceAnimation(PlayerId player_id, const std::string& animation);
  bool StopFaceAnimation(PlayerId player_id, const std::string& animation);
  bool PlayGesticulation(PlayerId player_id);
  bool SetPlayerPosition(PlayerId player_id, const glm::vec3& position);
  bool SetPlayerAngle(PlayerId player_id, float angle);
  bool GiveItem(PlayerId player_id, const std::string& instance, std::int32_t amount);
  bool EquipItem(PlayerId player_id, const std::string& instance, std::int32_t slot_id = -1);
  bool UnequipItem(PlayerId player_id, const std::string& instance);
  std::int32_t HasItem(PlayerId player_id, const std::string& instance) const;
  bool RemoveItem(PlayerId player_id, const std::string& instance, std::int32_t amount);
  bool SetPlayerWorld(PlayerId player_id, const std::string& world, std::optional<std::string> start_point = std::nullopt);
  bool SetPlayerVirtualWorld(PlayerId player_id, std::int32_t virtual_world);
  std::optional<glm::vec3> GetPlayerPosition(PlayerId player_id) const;
  std::string GetHostname() const;
  std::uint32_t GetMaxSlots() const;
  bool SetServerWorld(const std::string& world);
  std::string GetServerWorld() const;
  std::vector<PlayerId> FindNearbyPlayers(const glm::vec3& position, float radius, const std::string& world,
                                          std::int32_t virtual_world) const;
  std::vector<PlayerId> GetSpawnedPlayersForPlayer(PlayerId player_id) const;
  std::vector<PlayerId> GetStreamedPlayersByPlayer(PlayerId player_id) const;
  bool SetStreamerRadius(std::int32_t radius);
  std::int32_t GetStreamerRadius() const;
  bool SetStreamerHeight(std::int32_t height);
  std::int32_t GetStreamerHeight() const;
  bool SetTime(std::int32_t hour, std::int32_t min, std::int32_t day = 0);
  GothicClock::Time GetTime() const;
  bool SetDayLength(float day_length_ms);
  float GetDayLength() const;
  bool SetWeatherType(std::int32_t weather_type);
  std::int32_t GetWeatherType() const;
  bool SetRainStartTime(std::int32_t hour, std::int32_t min);
  std::pair<std::int32_t, std::int32_t> GetRainStartTime() const;
  bool SetRainStopTime(std::int32_t hour, std::int32_t min);
  std::pair<std::int32_t, std::int32_t> GetRainStopTime() const;
  bool SetWindScale(float wind_scale);
  float GetWindScale() const;
  bool SetDontRain(bool toggle);
  bool GetDontRain() const;
  bool SetWeatherDisabled(bool toggle);
  bool GetWeatherDisabled() const;
  bool TriggerClientEvent(const std::vector<PlayerId>& targets, const std::string& event_name, PlayerId source_element,
                          const std::string& payload);
  bool KickPlayer(PlayerId player_id, const std::string& reason);
  bool BanPlayer(PlayerId player_id, const std::string& reason);
  bool IsPlayerConnected(PlayerId player_id) const;
  bool IsPlayerAdmin(PlayerId player_id) const;
  bool IsPlayerDead(PlayerId player_id) const;
  bool IsPlayerSpawned(PlayerId player_id) const;
  bool IsPlayerUnconscious(PlayerId player_id) const;
  bool RespawnPlayer(PlayerId player_id);
  bool SetPlayerRespawnTime(PlayerId player_id, std::int32_t respawn_time_ms);
  std::optional<std::int32_t> GetPlayerRespawnTime(PlayerId player_id) const;
  std::string GetPlayerIp(PlayerId player_id) const;
  std::int32_t GetPlayerPing(PlayerId player_id) const;
  std::string GetPlayerMacAddress(PlayerId player_id) const;
  std::string GetPlayerUUID(PlayerId player_id) const;
  std::uint32_t CreateItemGround(ItemGroundManager::CreateOptions options);
  bool DestroyItemGround(std::uint32_t item_ground_id);
  bool SetItemGroundPosition(std::uint32_t item_ground_id, const glm::vec3& position);
  bool SetItemGroundRotation(std::uint32_t item_ground_id, const glm::vec3& rotation);
  bool SetItemGroundVirtualWorld(std::uint32_t item_ground_id, std::int32_t virtual_world);
  bool SetItemGroundPhysicsEnabled(std::uint32_t item_ground_id, bool enabled);
  ItemGroundManager& GetItemGroundManager() {
    return item_ground_manager_;
  }
  const ItemGroundManager& GetItemGroundManager() const {
    return item_ground_manager_;
  }

  const ItemRegistry& GetItemRegistry() const {
    return item_registry_;
  }

  PlayerManager& GetPlayerManager() {
    return player_manager_;
  }
  const PlayerManager& GetPlayerManager() const {
    return player_manager_;
  }

  std::uint32_t GetPort() const;

private:
  void DeleteFromPlayerList(PlayerId player_id);
  void HandleCastSpell(Packet p, bool target);
  void HandleDropItem(Packet p);
  void HandleTakeItem(Packet p);
  void HandlePlayerWorldEnter(Packet p);
  void HandleLuaEvent(Packet p);
  void HandleVoice(Packet p);
  void SomeoneJoinGame(Packet p);
  void HandlePlayerUpdate(Packet p);
  void HandlePlayerDisconnect(Net::ConnectionHandle connection, std::int32_t reason);
  void HandleRconCommand(Player& player, const std::string& params);
  void HandleDiagnosticsCommand(Player& player);
  void HandleAdminLogin(Player& player, const std::string& password);
  bool ApplyPlayerDamage(Player& victim, std::optional<PlayerId> attacker_id, std::int32_t damage, std::uint32_t damage_type, bool dont_kill);
  bool MakePlayerUnconscious(Player& victim, std::optional<PlayerId> attacker_id);
  void HandlePlayerDeath(Player& victim, std::optional<PlayerId> killer_id);
  void HandleNormalMsg(Packet p);
  void HandleGameInfo(Packet p);
  void HandleMapNameReq(Packet p);
  void SendDisconnectionInfo(PlayerId player_id);
  void SendDeathInfo(PlayerId player_id);
  void SendRespawnInfo(PlayerId player_id);
  void SendUnconsciousInfo(PlayerId player_id, std::optional<PlayerId> attacker_id);
  void SendStandUpInfo(PlayerId player_id);
  void BroadcastPlayerJoined(const Player& joining_player);
  void SendGameInfo(Net::ConnectionHandle connection);
  void SendGameInfo(Net::ConnectionHandle connection, GothicClock::Time time);
  void SendSkySettings(Net::ConnectionHandle connection);
  void BroadcastGameInfo();
  void BroadcastGameInfo(GothicClock::Time time);
  void BroadcastSkySettings();
  void BroadcastPlayerPings();
  void SendAdminAuthStatus(const Player& player);
  void UpdateAuthoritativeWorldState(const std::vector<GothicClock::Time>& advanced_times);
  void SendExistingPlayersPacket(Player& target_player);
  bool RespawnPlayerInternal(Player& player);
  void SendItemGroundCreate(const ItemGroundManager::ItemGround& item_ground, Net::ConnectionHandle connection);
  void SendItemGroundDestroy(std::uint32_t item_ground_id, Net::ConnectionHandle connection);
  void SendItemGroundClear(Net::ConnectionHandle connection);
  void StreamItemGroundToPlayer(ItemGroundManager::ItemGround& item_ground, Player& player, bool force = false);
  void StreamRelevantGroundItemsToPlayer(Player& player, bool force = false);
  void UnstreamGroundItemsFromPlayer(Player& player, bool notify_client);
  void RefreshItemGroundStreaming(ItemGroundManager::ItemGround& item_ground);
  void SendInventoryAddCorrection(Player& player, const std::string& instance, std::int32_t amount);
  void SendInventoryRemoveCorrection(Player& player, const std::string& instance, std::int32_t amount);
  std::optional<std::string> ResolveItemInstance(std::string instance) const;
  std::int16_t ResolveItemIndex(PlayerId player_id, std::int16_t index, const char* field_name) const;
  void ResolvePlayerStateItemIndexes(PlayerId player_id, PlayerState& state) const;

  std::unique_ptr<BanManager> ban_manager_;
  std::unique_ptr<LuaScript> lua_script_;
  std::unique_ptr<ResourceManager> resource_manager_;
  time_t last_stand_timer;
  time_t regen_time;

  void ProcessRespawns();

  unsigned char GetPacketIdentifier(const Packet& p);
  int serverPort;
  unsigned short maxConnections;
  PlayerManager player_manager_;
  AnimationRegistry animation_registry_;
  ItemRegistry item_registry_;
  ItemGroundManager item_ground_manager_;
  bool allow_modification = false;
  Config config_;
  std::unique_ptr<GothicClock> clock_;
  GothicWeather weather_;
  std::string server_world_;
  std::int64_t last_weather_update_minute_{-1};
  std::chrono::time_point<std::chrono::steady_clock> last_ping_update_time_{};
  std::future<void> public_list_http_thread_future_;
  std::chrono::time_point<std::chrono::steady_clock> last_update_time_{};
  std::thread main_thread;
  std::atomic<bool> main_thread_running = false;
  std::vector<ClientResourceDescriptor> client_resource_descriptors_;

  std::unique_ptr<ResourceServer> resource_server_;
};

inline GameServer* g_server = nullptr;
