/*
MIT License

Copyright (c) 2023 Gothic Multiplayer Team.

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
#include <optional>
#include <vector>

#include "CSyncFuncs.h"
#include "HooksManager.h"
#include "ZenGin/zGothicAPI.h"
#include "event_observer.hpp"
#include "game_client.hpp"
#include "gothic2a_player.hpp"
#include "gothic_task_scheduler.h"
#include "client_resources/client_resource_runtime.h"

struct MD5Sum {
  BYTE data[16];
};

union STime {
  std::uint32_t time;
  struct {
    unsigned short day;
    unsigned char hour, min;
  };
};

class NetGame : public CSyncFuncs, public gmp::client::EventObserver {
public:
  void HandleNetwork();
  void ClearMultiplayerMessages();
  bool IsConnected();
  bool Connect(std::string_view full_address);
  void JoinGame();
  void SendDropItem(short instance, short amount, const std::string& instance_name, const glm::vec3& position,
                    const glm::vec3& rotation, bool physics_enabled);
  void SendTakeItem(short instance, short amount, const std::string& instance_name, std::optional<std::uint32_t> item_ground_id);
  void SendPlayerWorldEnter(const std::string& world_name);
  void SendCastSpell(oCNpc* Target, short SpellId);
  void SendMessage(const char* msg);
  void SendPlayerHit(std::uint32_t victim_id, std::int32_t damage, std::uint32_t damage_type, bool dont_kill);
  void SendPlayerUnconscious(std::optional<std::uint32_t> attacker_id);
  void SendPlayerStandUp();
  void SendPlayerDeath(std::optional<std::uint32_t> killer_id);
  void UpdatePlayerStats(short anim);
  void SyncGameTime();
  void Disconnect();
  void RestoreHealth();
  void SetDayLengthMs(float day_length_ms);
  float GetDayLengthMs() const;

  // Task scheduler hook - called from render hook
  static void __stdcall ProcessTaskScheduler();

  void Shutdown();

  static NetGame& Instance() {
    static NetGame instance;
    return instance;
  }

  std::vector<Gothic2APlayer*> players;
  int HeroLastHp;
  zSTRING map;
  bool IsInGame{false};
  short mp_restore{0};
  int ForceHideMap{0};
  bool IsReadyToJoin{false};
  std::unique_ptr<gmp::GothicTaskScheduler> task_scheduler;
  std::unique_ptr<gmp::client::GameClient> game_client;
  std::unique_ptr<ClientResourceRuntime> resource_runtime;

  // EventObserver interface implementation
  void OnConnectionStarted() override;
  void OnConnected() override;
  void OnConnectionFailed(const std::string& error) override;
  void OnDisconnected() override;
  void OnConnectionLost() override;
  bool RequestResourceDownloadConsent(std::size_t resource_count, std::uint64_t total_bytes) override;
  void OnResourceDownloadProgress(const std::string& resource_name, std::uint64_t downloaded_bytes, std::uint64_t total_bytes) override;
  void OnResourceDownloadFailed(const std::string& reason) override;
  void OnResourcesReady() override;
  void OnMapChange(const std::string& map_name) override;
  void OnGameInfoReceived(std::uint32_t raw_game_time, float day_length_ms, std::uint8_t flags) override;
  void OnSkySettingsReceived(std::uint8_t flags, std::int32_t weather_type,
                             std::int16_t rain_start_hour, std::int16_t rain_start_min,
                             std::int16_t rain_stop_hour, std::int16_t rain_stop_min,
                             float wind_scale, bool dont_rain, float rain_weight, bool render_lightning) override;
  void OnLocalPlayerJoined(gmp::client::Player& player) override;
  void OnLocalPlayerSpawned(gmp::client::Player& player) override;
  void OnPlayerJoined(gmp::client::Player& player) override;
  void OnPlayerSpawned(gmp::client::Player& player) override;
  void OnPlayerLeft(std::uint64_t player_id, const std::string& player_name) override;
  void OnPlayerStateUpdate(std::uint64_t player_id, const PlayerState& state) override;
  void OnPlayerPositionUpdate(std::uint64_t player_id, float x, float y, float z) override;
  void OnPlayerNameUpdate(std::uint64_t player_id, const std::string& name) override;
  void OnPlayerInstanceUpdate(std::uint64_t player_id, const std::string& instance) override;
  void OnPlayerColorUpdate(std::uint64_t player_id, std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
  void OnPlayerSkillWeaponUpdate(std::uint64_t player_id, std::int32_t skill_id, std::int32_t percentage) override;
  void OnPlayerTalentUpdate(std::uint64_t player_id, std::int32_t talent_id, std::int32_t talent_value) override;
  void OnPlayerVisualUpdate(std::uint64_t player_id, const std::string& body_model, std::int16_t body_texture, const std::string& head_model,
                            std::int16_t head_texture) override;
  void OnPlayerFatnessUpdate(std::uint64_t player_id, float fatness) override;
  void OnPlayerScaleUpdate(std::uint64_t player_id, const glm::vec3& scale) override;
  void OnPlayerOverlayUpdate(std::uint64_t player_id, const std::string& overlay, bool apply) override;
  void OnPlayerAnimationPlay(std::uint64_t player_id, std::int16_t animation) override;
  void OnPlayerAnimationStop(std::uint64_t player_id, std::int16_t animation) override;
  void OnPlayerFaceAnimationPlay(std::uint64_t player_id, const std::string& animation) override;
  void OnPlayerFaceAnimationStop(std::uint64_t player_id, const std::string& animation) override;
  void OnPlayerGesticulation(std::uint64_t player_id) override;
  void OnPlayerAttributeUpdate(std::uint64_t player_id, PlayerAttributeId attribute_id, std::int32_t value) override;
  void OnPlayerWorldUpdate(std::uint64_t player_id, const std::string& world_name, const std::string& start_point) override;
  void OnPlayerDied(std::uint64_t player_id) override;
  void OnPlayerRespawned(std::uint64_t player_id) override;
  void OnPlayerUnconscious(std::uint64_t player_id, std::optional<std::uint64_t> attacker_id) override;
  void OnPlayerStandUp(std::uint64_t player_id) override;
  void OnPlayerPingUpdate(std::uint64_t player_id, std::int32_t ping) override;
  void OnItemDropped(std::uint64_t player_id, std::uint16_t item_instance, std::uint16_t amount) override;
  void OnItemTaken(std::uint64_t player_id, std::uint16_t item_instance) override;
  void OnItemGroundCreate(std::uint32_t item_ground_id, const std::string& item_instance, std::int32_t amount,
                          bool physics_enabled, const glm::vec3& position, const glm::vec3& rotation) override;
  void OnItemGroundDestroy(std::uint32_t item_ground_id) override;
  void OnItemsGroundDestroy() override;
  void OnItemGiven(std::uint64_t player_id, const std::string& item_instance, std::int32_t amount) override;
  void OnItemEquipped(std::uint64_t player_id, const std::string& item_instance, std::int16_t slot_id) override;
  void OnItemUnequipped(std::uint64_t player_id, const std::string& item_instance) override;
  void OnItemRemoved(std::uint64_t player_id, const std::string& item_instance, std::int32_t amount) override;
  void OnSpellCast(std::uint64_t caster_id, std::uint16_t spell_id) override;
  void OnSpellCastOnTarget(std::uint64_t caster_id, std::uint64_t target_id, std::uint16_t spell_id) override;
  void OnPlayerMessage(std::optional<std::uint64_t> sender_id, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                       const std::string& message) override;
  void OnLuaEvent(const std::string& event_name, std::uint32_t source_element, const std::string& payload) override;

private:
  NetGame();
  time_t last_mp_regen;

  struct GameTimeSnapshot {
    int day;
    int hour;
    int min;
  };

  struct PendingLocalSpawnPosition {
    std::uint64_t player_id;
    zVEC3 position;
    int remaining_frames;
  };

  Gothic2APlayer* GetPlayerById(std::uint64_t player_id);
  void SpawnRemotePlayer(gmp::client::Player& new_player);
  void ApplyPlayerLifeState(std::uint64_t player_id, std::uint8_t life_state, std::optional<std::uint64_t> actor_id, bool trigger_event);
  void ApplyPendingLocalSpawnPosition();
  void UpdateClientEventState();

  std::optional<GameTimeSnapshot> last_game_time_;
  std::optional<PendingLocalSpawnPosition> pending_local_spawn_position_;
  std::string last_world_name_;
  float day_length_ms_{0.0f};

public:
  std::optional<std::uint64_t> GetPlayerIdByNpc(oCNpc* npc);
};
