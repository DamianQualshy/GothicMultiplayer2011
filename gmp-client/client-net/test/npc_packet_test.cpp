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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game_client.hpp"

namespace gmp::client {
// Keep gameplay packet tests independent of socket timing. Connection lifecycle
// and delivery are covered by the transport integration tests.
class GameClientTestPeer {
public:
  static bool HandlePacket(GameClient& client, unsigned char* bytes, std::uint32_t size) {
    return client.HandlePacket(bytes, size);
  }
  static void BeginConnection(GameClient& client) {
    client.connection_state_ = GameClient::ConnectionState::Connecting;
  }
  static std::uint32_t ActionSequence(const GameClient& client) { return client.outgoing_action_sequence_; }
  static void NotifyTransport(GameClient& client, const Net::ConnectionEvent& event) {
    client.OnTransportEvent(event);
    client.DispatchPendingConnectionNotifications();
  }
};
}

namespace {
using gmp::client::EventObserver;
using gmp::client::GameClient;
using gmp::client::Player;

class ImmediateScheduler final : public gmp::TaskScheduler {
public:
  void ScheduleOnMainThread(Task task) override { task(); }
};

struct RecordingObserver final : EventObserver {
  GameClient* client{nullptr};
  Player* spawned_player{nullptr};
  bool disturb_snapshot_health{false};
  bool expected_new_spawn{true};
  std::vector<std::string> order;
  std::vector<NpcControlPacket> controls;
  std::vector<NpcAnimationPacket> animations;
  int state_updates{0};
  int position_updates{0};
  int deaths{0};
  int applied_deaths{0};
  int stand_ups{0};
  int applied_stand_ups{0};
  int respawns{0};
  std::vector<PlayerState> states;
  std::vector<bool> state_corrections;
  std::vector<ActorActionPacket> actions;
  struct CastRecord {
    std::uint64_t caster;
    std::optional<std::uint64_t> target;
    std::uint16_t spell;
    std::uint64_t source_time;
    std::uint64_t generation;
    std::uint64_t target_generation;
    std::uint32_t epoch;
  };
  std::vector<CastRecord> casts;
  std::vector<std::pair<std::uint32_t, std::int32_t>> inventory_results;
  std::vector<std::vector<InventoryEntry>> inventory_snapshots;
  std::vector<bool> equipment_applied;
  bool cleared_with_live_metadata{false};
  int connected{0};
  int disconnected{0};
  int connection_lost{0};
  std::string connection_failure;
  std::string connection_closed;

  void OnConnected() override { ++connected; }
  void OnDisconnected() override { ++disconnected; }
  void OnConnectionLost() override { ++connection_lost; }
  void OnConnectionFailed(const std::string& reason) override { connection_failure = reason; }
  void OnConnectionClosed(const std::string& reason) override { connection_closed = reason; }

  void OnPlayerSpawned(Player& player) override {
    spawned_player = &player;
    order.push_back("spawn");
  }
  void OnPlayerInstanceUpdate(std::uint64_t, const std::string&) override { order.push_back("instance"); }
  void OnPlayerVisualUpdate(std::uint64_t, const std::string&, std::int16_t, const std::string&,
                           std::int16_t, std::int16_t, std::int16_t) override {
    order.push_back("visual");
  }
  void OnPlayerAttributeUpdate(std::uint64_t, PlayerAttributeId attribute, std::int32_t) override {
    if (attribute == ATTR_MAX_HEALTH) {
      order.push_back("max_health");
      // Model/attribute application can temporarily alter the engine adapter's
      // cached health. Snapshot finalization must restore authoritative values.
      if (disturb_snapshot_health && spawned_player) {
        spawned_player->set_health(1);
        spawned_player->set_life_state(PLAYER_LIFE_UNCONSCIOUS);
      }
    }
    if (attribute == ATTR_HEALTH) {
      order.push_back("health");
    }
  }
  void OnPlayerOverlayUpdate(std::uint64_t, const std::string&, bool) override { order.push_back("overlay"); }
  void OnPlayerSpawnSnapshotApplied(Player& player, bool new_spawn) override {
    order.push_back("snapshot_applied");
    EXPECT_EQ(new_spawn, expected_new_spawn);
    EXPECT_TRUE(player.has_spawned());
    EXPECT_EQ(player.max_health(), 2000);
    EXPECT_EQ(player.health(), 1800);
    EXPECT_EQ(player.life_state(), PLAYER_LIFE_ALIVE);
  }
  void OnNpcControl(const NpcControlPacket& packet) override { controls.push_back(packet); }
  void OnNpcAnimation(const NpcAnimationPacket& packet) override { animations.push_back(packet); }
  void OnPlayerStateUpdate(std::uint64_t, const PlayerState& state) override { ++state_updates; states.push_back(state); }
  void OnPlayerStateUpdate(std::uint64_t id, const PlayerState& state, bool authoritative_correction) override {
    state_corrections.push_back(authoritative_correction);
    OnPlayerStateUpdate(id, state);
  }
  void OnPlayerPositionUpdate(std::uint64_t, float, float, float) override { ++position_updates; }
  void OnPlayerDied(std::uint64_t, bool apply) override { ++deaths; if (apply) ++applied_deaths; }
  void OnPlayerStandUp(std::uint64_t, bool apply) override { ++stand_ups; if (apply) ++applied_stand_ups; }
  void OnPlayerRespawned(std::uint64_t, bool) override { ++respawns; }
  void OnActorAction(const ActorActionPacket& action) override { actions.push_back(action); }
  void OnSpellCast(std::uint64_t caster, std::uint16_t spell, std::uint64_t source,
                    std::uint64_t generation, std::uint32_t epoch) override {
    casts.push_back({caster, std::nullopt, spell, source, generation, 0, epoch});
  }
  void OnSpellCastOnTarget(std::uint64_t caster, std::uint64_t target, std::uint16_t spell, std::uint64_t source,
                            std::uint64_t generation, std::uint64_t target_generation, std::uint32_t epoch) override {
    casts.push_back({caster, target, spell, source, generation, target_generation, epoch});
  }
  void OnActorInventorySnapshot(std::uint64_t, const std::vector<InventoryEntry>& inventory) override {
    inventory_snapshots.push_back(inventory);
    order.push_back("inventory");
  }
  void OnActorItemTransactionResult(std::uint32_t actor, std::uint32_t, std::int32_t,
                                    std::int32_t total, bool, std::optional<std::uint32_t>) override {
    inventory_results.emplace_back(actor, total);
  }
  void OnItemEquipped(std::uint64_t, std::int32_t, std::int16_t, std::uint32_t, bool apply) override {
    equipment_applied.push_back(apply);
  }
  void OnPlayerLeft(std::uint64_t id, const std::string&) override {
    EXPECT_NE(client->player_manager().GetPlayer(id), nullptr);
    spawned_player = nullptr;
  }
  void OnPlayersClearing() override {
    if (spawned_player) {
      cleared_with_live_metadata = client->player_manager().GetPlayer(spawned_player->id()) == spawned_player;
      spawned_player = nullptr;
    }
  }
};

NpcSpawnPacket MakeSpawn() {
  NpcSpawnPacket packet;
  packet.actor.packet_type = Net::PT_PLAYER_SPAWN;
  packet.actor.player_id = 21;
  packet.actor.actor_generation = 121;
  packet.actor.state_sequence = 10;
  packet.actor.instance = "TROLL_BLACK";
  packet.actor.player_name = "Troll";
  packet.actor.body_model = "TROLL_BODY";
  packet.actor.max_health = 2000;
  packet.actor.health = 1800;
  packet.actor.life_state = PLAYER_LIFE_ALIVE;
  packet.actor.position = {100.0f, 200.0f, 300.0f};
  packet.actor.normal = {0.0f, 0.0f, 1.0f};
  packet.actor.overlays = {"TEST.MDS"};
  packet.control.npc_id = 21;
  packet.control.actor_generation = packet.actor.actor_generation;
  packet.control.control_epoch = 4;
  packet.control.state_sequence = packet.actor.state_sequence;
  packet.control.position = packet.actor.position;
  packet.control.normal = packet.actor.normal;
  packet.animation.npc_id = 21;
  packet.animation.actor_generation = packet.actor.actor_generation;
  packet.animation.revision = 7;
  packet.animation.animation = "S_FISTRUNL";
  return packet;
}

PlayerStateUpdatePacket MakeMovement(std::uint32_t sequence) {
  PlayerStateUpdatePacket packet{};
  packet.packet_type = Net::PT_ACTUAL_STATISTICS;
  packet.player_id = 21;
  packet.actor_generation = 121;
  packet.state_sequence = sequence;
  packet.state.position = {900.0f, 200.0f, 500.0f};
  packet.state.nrot = {1.0f, 0.0f, 0.0f};
  packet.state.health_points = 1800;
  packet.state.life_state = PLAYER_LIFE_ALIVE;
  return packet;
}

class ClientNpcPacketTest : public ::testing::Test {
protected:
  ImmediateScheduler scheduler;
  RecordingObserver first_observer;
  RecordingObserver second_observer;
  std::unique_ptr<GameClient> first;
  std::unique_ptr<GameClient> second;

  void SetUp() override {
    first = std::make_unique<GameClient>(first_observer, scheduler);
    first_observer.client = first.get();
    second = std::make_unique<GameClient>(second_observer, scheduler);
    second_observer.client = second.get();
  }
  void TearDown() override {
    second.reset();
    first.reset();
  }
  template <typename Packet>
  void Deliver(std::size_t viewer, const Packet& packet) {
    std::vector<unsigned char> bytes;
    const auto size = bitsery::quickSerialization<bitsery::OutputBufferAdapter<std::vector<unsigned char>>>(bytes, packet);
    ASSERT_LT(viewer, 2u);
    GameClient& client = viewer == 0 ? *first : *second;
    ASSERT_TRUE(gmp::client::GameClientTestPeer::HandlePacket(client, bytes.data(), static_cast<std::uint32_t>(size)));
  }
};

TEST_F(ClientNpcPacketTest, SpawnFinalizesLifeStateAfterVisualAndAttributeCallbacks) {
  first_observer.disturb_snapshot_health = true;
  Deliver(0, MakeSpawn());
  const auto& order = first_observer.order;
  ASSERT_FALSE(order.empty());
  EXPECT_EQ(order.front(), "spawn");
  EXPECT_EQ(order.back(), "snapshot_applied");
  for (const auto* name : {"instance", "visual", "max_health", "health", "overlay"}) {
    const auto found = std::find(order.begin(), order.end(), name);
    EXPECT_NE(found, order.end());
    EXPECT_LT(found, order.end() - 1);
  }
  ASSERT_NE(first->player_manager().GetPlayer(21), nullptr);
  EXPECT_TRUE(first->player_manager().GetPlayer(21)->is_npc());
  ASSERT_EQ(first_observer.controls.size(), 1u);
  ASSERT_EQ(first_observer.animations.size(), 1u);
}

TEST_F(ClientNpcPacketTest, OrdinaryPlayerSpawnUsesTheSameAuthoritativeSnapshotFinalization) {
  auto packet = MakeSpawn().actor;
  packet.player_id = 2;
  packet.instance = "PC_HERO";
  first_observer.disturb_snapshot_health = true;
  Deliver(0, packet);
  const auto* player = first->player_manager().GetPlayer(2);
  ASSERT_NE(player, nullptr);
  EXPECT_FALSE(player->is_npc());
  EXPECT_EQ(player->health(), 1800);
  EXPECT_EQ(player->life_state(), PLAYER_LIFE_ALIVE);
  ASSERT_FALSE(first_observer.order.empty());
  EXPECT_EQ(first_observer.order.back(), "snapshot_applied");
}

TEST_F(ClientNpcPacketTest, ReorderedMovementCannotPreventFirstSpawnOfJoinedPlayer) {
  JoinGamePacket joined;
  joined.packet_type = Net::PT_JOIN_GAME;
  joined.player_id = 2;
  joined.actor_generation = 121;
  joined.player_name = "Joined player";
  Deliver(0, joined);
  auto* player = first->player_manager().GetPlayer(2);
  ASSERT_NE(player, nullptr);
  EXPECT_TRUE(player->has_joined());
  EXPECT_FALSE(player->has_spawned());

  // Unreliable movement may overtake the reliable spawn snapshot.
  // Connection-only metadata must not consume that movement's sequence and
  // subsequently reject the snapshot needed to create the engine actor.
  auto movement = MakeMovement(12);
  movement.player_id = 2;
  Deliver(0, movement);
  PlayerPositionUpdatePacket position{};
  position.packet_type = Net::PT_MAP_ONLY;
  position.actor_generation = 121;
  position.player_id = 2;
  position.state_sequence = 13;
  position.position = movement.state.position;
  Deliver(0, position);
  EXPECT_FALSE(player->has_spawned());
  EXPECT_EQ(first_observer.state_updates, 0);
  EXPECT_EQ(first_observer.position_updates, 0);
  EXPECT_TRUE(first_observer.order.empty());

  auto spawn = MakeSpawn().actor;
  spawn.player_id = 2;
  spawn.instance = "PC_HERO";
  Deliver(0, spawn);
  EXPECT_TRUE(player->has_spawned());
  EXPECT_EQ(player->state_sequence(), spawn.state_sequence);
  EXPECT_EQ(player->position(), spawn.position);
  EXPECT_EQ(player->health(), spawn.health);
  EXPECT_EQ(player->life_state(), PLAYER_LIFE_ALIVE);
  EXPECT_EQ(std::count(first_observer.order.begin(), first_observer.order.end(), "spawn"), 1);
  EXPECT_EQ(std::count(first_observer.order.begin(), first_observer.order.end(), "snapshot_applied"), 1);
}

TEST_F(ClientNpcPacketTest, MovementDoesNotDispatchActionsAndOlderControlCannotRewindPosition) {
  const auto spawn = MakeSpawn();
  Deliver(0, spawn);
  const auto movement = MakeMovement(12);
  Deliver(0, movement);
  const auto* npc = first->player_manager().GetPlayer(21);
  ASSERT_NE(npc, nullptr);
  EXPECT_EQ(npc->npc_control_epoch(), spawn.control.control_epoch);
  EXPECT_EQ(first_observer.controls.size(), 1u);
  EXPECT_EQ(first_observer.animations.size(), 1u);
  EXPECT_EQ(first_observer.state_updates, 1);

  auto next_control = spawn.control;
  ++next_control.control_epoch;
  next_control.state_sequence = 11;
  Deliver(0, next_control);
  EXPECT_EQ(npc->npc_control_epoch(), next_control.control_epoch);
  EXPECT_EQ(npc->state_sequence(), 12u);
  EXPECT_EQ(npc->position(), movement.state.position);
  EXPECT_EQ(npc->rotation(), movement.state.nrot);
  ASSERT_EQ(first_observer.controls.size(), 2u);
  EXPECT_EQ(first_observer.controls.back().position, movement.state.position);
  EXPECT_EQ(first_observer.controls.back().state_sequence, 11u);
}

TEST_F(ClientNpcPacketTest, AnimationRevisionIsIndependentPerViewerAndRestoredByRespawnSnapshot) {
  auto spawn = MakeSpawn();
  Deliver(0, spawn);
  Deliver(1, spawn);
  auto selected = spawn.animation;
  selected.revision = 8;
  selected.animation = "S_FISTRUN";
  Deliver(0, selected);
  Deliver(1, selected);
  Deliver(0, spawn.animation);
  Deliver(1, selected);
  EXPECT_EQ(first_observer.animations.size(), 2u);
  EXPECT_EQ(second_observer.animations.size(), 2u);
  EXPECT_EQ(first_observer.animations.back().animation, "S_FISTRUN");
  EXPECT_EQ(second_observer.animations.back().animation, "S_FISTRUN");

  DisconnectionInfoPacket leave{Net::PT_LEFT_GAME, 21};
  Deliver(0, leave);
  EXPECT_EQ(first->player_manager().GetPlayer(21), nullptr);
  ASSERT_NE(second->player_manager().GetPlayer(21), nullptr);
  spawn.animation = selected;
  Deliver(0, spawn);
  ASSERT_EQ(first_observer.animations.size(), 3u);
  EXPECT_EQ(first_observer.animations.back().revision, selected.revision);
  EXPECT_EQ(first_observer.animations.back().animation, selected.animation);
  EXPECT_EQ(second_observer.animations.size(), 2u);
}

TEST_F(ClientNpcPacketTest, StaleLifeReportsCannotKillFreshSpawnOrResurrectNewerDeath) {
  Deliver(0, MakeSpawn());
  PlayerDeathInfoPacket death;
  death.packet_type = Net::PT_DODIE;
  death.player_id = 21;
  death.actor_generation = 121;
  death.state_sequence = 9;
  Deliver(0, death);
  auto* npc = first->player_manager().GetPlayer(21);
  ASSERT_NE(npc, nullptr);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_ALIVE);
  EXPECT_EQ(first_observer.deaths, 1);
  EXPECT_EQ(first_observer.applied_deaths, 0);

  death.state_sequence = 12;
  Deliver(0, death);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_DEAD);
  EXPECT_EQ(first_observer.deaths, 2);
  EXPECT_EQ(first_observer.applied_deaths, 1);
  Deliver(0, MakeMovement(11));
  PlayerRespawnInfoPacket respawn;
  respawn.packet_type = Net::PT_RESPAWN;
  respawn.player_id = 21;
  respawn.actor_generation = 121;
  respawn.state_sequence = 11;
  Deliver(0, respawn);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_DEAD);
  EXPECT_EQ(npc->health(), 0);
  EXPECT_EQ(first_observer.respawns, 0);
  EXPECT_EQ(first_observer.state_updates, 1);
}

TEST_F(ClientNpcPacketTest, MovementCannotConsumeIndependentMaximumOrCurrentHealthCommands) {
  Deliver(0, MakeSpawn());
  auto movement = MakeMovement(20);
  movement.state.correction_mask = kSyncPosition | kSyncOrientation;
  Deliver(0, movement);
  PlayerAttributeUpdatePacket maximum;
  maximum.packet_type = Net::PT_PLAYER_ATTRIBUTE_UPDATE;
  maximum.player_id = 21;
  maximum.actor_generation = 121;
  maximum.state_sequence = 13;
  maximum.attribute_id = ATTR_MAX_HEALTH;
  maximum.value = 100000;
  Deliver(0, maximum);
  auto health = maximum;
  health.state_sequence = 14;
  health.command_revision = 7;
  health.attribute_id = ATTR_HEALTH;
  health.value = 90000;
  Deliver(0, health);
  const auto* npc = first->player_manager().GetPlayer(21);
  ASSERT_NE(npc, nullptr);
  EXPECT_EQ(npc->max_health(), 100000);
  EXPECT_EQ(npc->health(), 90000);
  EXPECT_EQ(npc->position(), movement.state.position);
  EXPECT_EQ(npc->command_revision(), 7u);
  Deliver(0, MakeMovement(12));
  EXPECT_EQ(npc->health(), 90000);
  EXPECT_EQ(npc->position(), movement.state.position);
}

TEST_F(ClientNpcPacketTest, SpawnCachesCompleteCombatAndPoseAndReplacesCollections) {
  auto packet = MakeSpawn();
  packet.actor.source_time_ms = 123456;
  packet.actor.weapon_mode = 6;
  packet.actor.active_spell_nr = 8;
  packet.actor.active_spell_instance = 123;
  packet.actor.melee_weapon_instance = 321;
  packet.actor.ranged_weapon_instance = 322;
  packet.actor.body_state = 4;
  packet.actor.walk_mode = 2;
  packet.actor.look_yaw = 0.25f;
  packet.actor.look_pitch = 0.75f;
  packet.actor.focus_target_id = 7;
  packet.actor.invulnerable = true;
  packet.actor.weapon_skills = {{1, 80}};
  Deliver(0, packet);
  const auto* actor = first->player_manager().GetPlayer(21);
  ASSERT_NE(actor, nullptr);
  const auto state = actor->state();
  EXPECT_EQ(state.source_time_ms, packet.actor.source_time_ms);
  EXPECT_EQ(state.weapon_mode, packet.actor.weapon_mode);
  EXPECT_EQ(state.active_spell_instance, packet.actor.active_spell_instance);
  EXPECT_EQ(state.melee_weapon_instance, packet.actor.melee_weapon_instance);
  EXPECT_EQ(state.ranged_weapon_instance, packet.actor.ranged_weapon_instance);
  EXPECT_EQ(state.body_state, packet.actor.body_state);
  EXPECT_EQ(state.walk_mode, packet.actor.walk_mode);
  EXPECT_EQ(state.look_yaw, packet.actor.look_yaw);
  EXPECT_EQ(state.focus_target_id, packet.actor.focus_target_id);
  EXPECT_TRUE(state.invulnerable);
  ++packet.actor.state_sequence;
  packet.actor.overlays = {"REPLACEMENT.MDS"};
  packet.actor.weapon_skills.clear();
  first_observer.expected_new_spawn = false;
  Deliver(0, packet);
  ASSERT_EQ(actor->overlays().size(), 1u);
  EXPECT_EQ(actor->overlays().front(), "REPLACEMENT.MDS");
  EXPECT_TRUE(actor->weapon_skills().empty());
}

TEST_F(ClientNpcPacketTest, ActorGenerationRejectsMovementAndLifeFromReusedIdentifier) {
  auto spawn = MakeSpawn();
  Deliver(0, spawn);
  ++spawn.actor.actor_generation;
  spawn.control.actor_generation = spawn.animation.actor_generation = spawn.actor.actor_generation;
  spawn.actor.state_sequence = spawn.control.state_sequence = 1;
  Deliver(0, spawn);
  auto movement = MakeMovement(1000);
  Deliver(0, movement);
  PlayerDeathInfoPacket death;
  death.packet_type = Net::PT_DODIE;
  death.player_id = 21;
  death.actor_generation = 121;
  death.state_sequence = 1001;
  Deliver(0, death);
  auto* npc = first->player_manager().GetPlayer(21);
  ASSERT_NE(npc, nullptr);
  EXPECT_EQ(npc->actor_generation(), 122u);
  EXPECT_EQ(npc->position(), spawn.actor.position);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_ALIVE);
  EXPECT_EQ(first_observer.deaths, 0);
  movement.actor_generation = 122;
  movement.state_sequence = 2;
  Deliver(0, movement);
  EXPECT_EQ(npc->position(), movement.state.position);
}

TEST_F(ClientNpcPacketTest, LaterSnapshotDoesNotSuppressOrReplayEarlierDeathNotification) {
  Deliver(0, MakeSpawn());
  auto state = MakeMovement(12);
  state.state.life_state = PLAYER_LIFE_DEAD;
  state.state.health_points = 0;
  Deliver(0, state);
  PlayerDeathInfoPacket death;
  death.packet_type = Net::PT_DODIE;
  death.player_id = 21;
  death.actor_generation = 121;
  death.state_sequence = 11;
  Deliver(0, death);
  Deliver(0, death);
  EXPECT_EQ(first_observer.deaths, 1);
  EXPECT_EQ(first_observer.applied_deaths, 0);
  EXPECT_EQ(first->player_manager().GetPlayer(21)->position(), state.state.position);
}

TEST_F(ClientNpcPacketTest, RespawnRestoresManaAndHealthWithoutNarrowing) {
  Deliver(0, MakeSpawn());
  PlayerRespawnInfoPacket respawn;
  respawn.packet_type = Net::PT_RESPAWN;
  respawn.player_id = 21;
  respawn.actor_generation = 121;
  respawn.state_sequence = 11;
  respawn.health = 90000;
  respawn.mana = 80000;
  Deliver(0, respawn);
  const auto* actor = first->player_manager().GetPlayer(21);
  ASSERT_NE(actor, nullptr);
  EXPECT_EQ(actor->health(), respawn.health);
  EXPECT_EQ(actor->mana(), respawn.mana);
  EXPECT_EQ(first_observer.respawns, 1);
}

TEST_F(ClientNpcPacketTest, NativeRecoveryPredictionKeepsConfirmedLifeUntilServerAcknowledgement) {
  auto* local = first->player_manager().CreateLocalPlayer(1);
  auto spawn = MakeSpawn().actor;
  spawn.player_id = 1;
  Deliver(0, spawn);
  PlayerUnconsciousPacket down{};
  down.packet_type = Net::PT_PLAYER_UNCONSCIOUS;
  down.player_id = 1;
  down.actor_generation = spawn.actor_generation;
  down.state_sequence = 11;
  down.command_revision = 5;
  Deliver(0, down);
  auto native = local->state();
  native.life_state = PLAYER_LIFE_ALIVE;
  native.health_points = 1;
  native.body_state = 0;
  native.position.x = 123;
  first->UpdatePlayerStats(native);
  first->UpdatePlayerStats(native);
  EXPECT_EQ(local->life_state(), PLAYER_LIFE_UNCONSCIOUS);
  EXPECT_EQ(local->position().x, 123);
  PlayerStandUpPacket recovered{};
  recovered.packet_type = Net::PT_PLAYER_STANDUP;
  recovered.player_id = 1;
  recovered.actor_generation = spawn.actor_generation;
  recovered.state_sequence = 12;
  recovered.command_revision = 6;
  Deliver(0, recovered);
  first->UpdatePlayerStats(native);
  EXPECT_EQ(local->life_state(), PLAYER_LIFE_ALIVE);
  EXPECT_EQ(first_observer.applied_stand_ups, 1);
  first->UpdatePlayerStats(PlayerState{});
  EXPECT_EQ(local->life_state(), PLAYER_LIFE_ALIVE);
}

TEST_F(ClientNpcPacketTest, StandUpAfterNewerAliveSnapshotStillNotifiesOnceWithoutReplayingOldLife) {
  Deliver(0, MakeSpawn());
  PlayerUnconsciousPacket down{};
  down.packet_type = Net::PT_PLAYER_UNCONSCIOUS;
  down.player_id = 21;
  down.actor_generation = 121;
  down.state_sequence = 11;
  down.command_revision = 5;
  Deliver(0, down);
  auto pose = MakeMovement(13);
  pose.command_revision = 6;
  pose.state.life_state = PLAYER_LIFE_ALIVE;
  pose.state.health_points = 1;
  Deliver(0, pose);
  PlayerStandUpPacket recovered{};
  recovered.packet_type = Net::PT_PLAYER_STANDUP;
  recovered.player_id = 21;
  recovered.actor_generation = 121;
  recovered.state_sequence = 12;
  recovered.command_revision = 6;
  Deliver(0, recovered);
  Deliver(0, recovered);
  EXPECT_EQ(first->player_manager().GetPlayer(21)->life_state(), PLAYER_LIFE_ALIVE);
  EXPECT_EQ(first_observer.stand_ups, 1);
  EXPECT_EQ(first_observer.applied_stand_ups, 0);
}

TEST_F(ClientNpcPacketTest, RecoveryRejectsLatePreKnockoutAnimationsAndAcceptsCurrentLocomotion) {
  Deliver(0, MakeSpawn());
  PlayerUnconsciousPacket down{};
  down.packet_type = Net::PT_PLAYER_UNCONSCIOUS;
  down.player_id = 21;
  down.actor_generation = 121;
  down.state_sequence = 11;
  down.command_revision = 5;
  Deliver(0, down);
  PlayerStandUpPacket recovered{};
  recovered.packet_type = Net::PT_PLAYER_STANDUP;
  recovered.player_id = 21;
  recovered.actor_generation = 121;
  recovered.state_sequence = 12;
  recovered.command_revision = 6;
  Deliver(0, recovered);
  ActorActionPacket action;
  action.actor_id = 21;
  action.actor_generation = 121;
  action.control_epoch = 4;
  action.action_id = 1;
  action.command_revision = 4;
  action.name = "T_1HATTACKMOVE";
  Deliver(0, action);
  EXPECT_TRUE(first_observer.actions.empty());
  action.command_revision = 6;
  action.name = "T_RUNSTRAFEL";
  Deliver(0, action);
  ASSERT_EQ(first_observer.actions.size(), 1u);
  EXPECT_EQ(first_observer.actions.front().name, "T_RUNSTRAFEL");
}

TEST_F(ClientNpcPacketTest, AcceptedStandUpEstablishesBarrierEvenIfAnObserverAlreadyChangedLife) {
  Deliver(0, MakeSpawn());
  auto* actor = first->player_manager().GetPlayer(21);
  actor->set_life_state(PLAYER_LIFE_ALIVE);
  PlayerStandUpPacket recovered{};
  recovered.packet_type = Net::PT_PLAYER_STANDUP;
  recovered.player_id = 21;
  recovered.actor_generation = 121;
  recovered.state_sequence = 11;
  recovered.command_revision = 6;
  Deliver(0, recovered);
  ActorActionPacket action;
  action.actor_id = 21;
  action.actor_generation = 121;
  action.control_epoch = 4;
  action.action_id = 1;
  action.command_revision = 5;
  action.name = "T_1HATTACK";
  Deliver(0, action);
  EXPECT_TRUE(first_observer.actions.empty());
  action.command_revision = 6;
  Deliver(0, action);
  ASSERT_EQ(first_observer.actions.size(), 1u);
}

TEST_F(ClientNpcPacketTest, ContinuousSamplesAndExplicitComponentCorrectionsKeepDistinctPresentationSemantics) {
  Deliver(0, MakeSpawn());
  for (std::uint32_t sequence = 11; sequence < 21; ++sequence) {
    auto sample = MakeMovement(sequence);
    sample.state.source_time_ms = 1000 + (sequence - 11) * 80;
    sample.state.animation_name = "T_RUNSTRAFEL";
    sample.state.correction_mask = kSyncAll;
    Deliver(0, sample);
  }
  ASSERT_EQ(first_observer.state_corrections.size(), 10u);
  EXPECT_TRUE(std::none_of(first_observer.state_corrections.begin(), first_observer.state_corrections.end(), [](bool value) { return value; }));
  auto command = MakeMovement(21);
  command.command_revision = 4;
  command.state.correction_mask = kSyncCombat;
  command.state.weapon_mode = 0;
  Deliver(0, command);
  ASSERT_EQ(first_observer.state_corrections.size(), 11u);
  EXPECT_TRUE(first_observer.state_corrections.back());
  EXPECT_EQ(first_observer.states.back().correction_mask, kSyncCombat);
  EXPECT_EQ(first->player_manager().GetPlayer(21)->command_revision(), 4u);
}

TEST_F(ClientNpcPacketTest, ObsoleteEquipmentCommandStillRequestsVerificationWithoutReplaying) {
  Deliver(0, MakeSpawn());
  auto state = MakeMovement(12);
  state.state.equipped_armor_instance = 202;
  Deliver(0, state);
  EquipItemPacket equip;
  equip.packet_type = Net::PT_EQUIPITEM;
  equip.player_id = 21;
  equip.actor_generation = 121;
  equip.state_sequence = 11;
  equip.item_instance = 101;
  equip.slot_id = Net::EQUIP_SLOT_ARMOR;
  Deliver(0, equip);
  ASSERT_EQ(first_observer.equipment_applied.size(), 1u);
  EXPECT_FALSE(first_observer.equipment_applied.back());
  EXPECT_EQ(first->player_manager().GetPlayer(21)->equipped_armor(), 202);
}

TEST_F(ClientNpcPacketTest, RepeatedAnimationActionsUseEventIdentityInsteadOfSampledName) {
  Deliver(0, MakeSpawn());
  ActorActionPacket action;
  action.actor_id = 21;
  action.actor_generation = 121;
  action.control_epoch = 4;
  action.action_id = 1;
  action.name = "T_1HATTACK";
  action.kind = ActorActionKind::AnimationStart;
  Deliver(0, action);
  Deliver(0, action);
  action.action_id = 2;
  Deliver(0, action);
  ASSERT_EQ(first_observer.actions.size(), 2u);
  EXPECT_EQ(first_observer.actions.front().name, first_observer.actions.back().name);
}

TEST_F(ClientNpcPacketTest, HostHandoverRejectsOldActionsWithoutBlockingTheNewHost) {
  Deliver(0, MakeSpawn());
  ActorActionPacket action;
  action.actor_id = 21;
  action.actor_generation = 121;
  action.control_epoch = 4;
  action.action_id = 100;
  action.kind = ActorActionKind::Shot;
  Deliver(0, action);
  auto control = MakeSpawn().control;
  control.control_epoch = 5;
  Deliver(0, control);
  action.action_id = 101;
  Deliver(0, action);
  action.control_epoch = 5;
  action.action_id = 1;
  Deliver(0, action);
  ASSERT_EQ(first_observer.actions.size(), 2u);
  EXPECT_EQ(first_observer.actions.back().action_id, 1u);
}

TEST_F(ClientNpcPacketTest, CastSourceTimelineAndAssignmentReachObserverOnce) {
  Deliver(0, MakeSpawn());
  CastSpellPacket cast{};
  cast.packet_type = Net::PT_CASTSPELL;
  cast.caster_id = 21;
  cast.actor_generation = 121;
  cast.control_epoch = 4;
  cast.action_id = 1;
  cast.spell_id = 1;
  cast.source_time_ms = (std::uint64_t{1} << 33) + 123;
  Deliver(0, cast);
  Deliver(0, cast);
  ASSERT_EQ(first_observer.casts.size(), 1u);
  EXPECT_EQ(first_observer.casts.front().caster, 21u);
  EXPECT_EQ(first_observer.casts.front().source_time, cast.source_time_ms);
  EXPECT_EQ(first_observer.casts.front().generation, 121u);
  EXPECT_EQ(first_observer.casts.front().epoch, 4u);
  cast.action_id = 2;
  cast.control_epoch = 3;
  Deliver(0, cast);
  EXPECT_EQ(first_observer.casts.size(), 1u);
}

TEST_F(ClientNpcPacketTest, TargetedCastKeepsSourceTimeAndBothIncarnations) {
  Deliver(0, MakeSpawn());
  auto target = MakeSpawn();
  target.actor.player_id = 22;
  target.actor.actor_generation = 122;
  target.control.npc_id = 22;
  target.control.actor_generation = 122;
  target.animation.npc_id = 22;
  target.animation.actor_generation = 122;
  Deliver(0, target);
  CastSpellPacket cast{};
  cast.packet_type = Net::PT_CASTSPELLONTARGET;
  cast.caster_id = 21;
  cast.actor_generation = 121;
  cast.target_id = 22;
  cast.target_generation = 121;  // Same ID, wrong incarnation.
  cast.control_epoch = 4;
  cast.action_id = 1;
  cast.spell_id = 1;
  cast.source_time_ms = (std::uint64_t{1} << 34) + 456;
  Deliver(0, cast);
  EXPECT_TRUE(first_observer.casts.empty());
  cast.target_generation = 122;
  Deliver(0, cast);
  ASSERT_EQ(first_observer.casts.size(), 1u);
  EXPECT_EQ(first_observer.casts.front().target, 22u);
  EXPECT_EQ(first_observer.casts.front().source_time, cast.source_time_ms);
  EXPECT_EQ(first_observer.casts.front().generation, 121u);
  EXPECT_EQ(first_observer.casts.front().target_generation, 122u);
}

TEST_F(ClientNpcPacketTest, SynchronousHitsFollowApprovalAndFailedCastsDiscardHits) {
  using Peer = gmp::client::GameClientTestPeer;
  auto* local = first->player_manager().CreateLocalPlayer(1);
  local->set_actor_generation(101);
  local->set_has_spawned(true);
  Deliver(0, MakeSpawn());
  first->BeginHitBatch();
  first->SendPlayerHit(21, 10, 32, false);
  EXPECT_EQ(Peer::ActionSequence(*first), 0u);
  first->SendCastSpell(21, 1, 123);
  EXPECT_EQ(Peer::ActionSequence(*first), 1u);
  first->EndHitBatch(true);
  EXPECT_EQ(Peer::ActionSequence(*first), 2u);
  first->BeginHitBatch();
  first->SendPlayerHit(21, 10, 32, false);
  first->EndHitBatch(false);
  EXPECT_EQ(Peer::ActionSequence(*first), 2u);
  first->BeginHitBatch();
  first->SendPlayerHit(21, 10, 32, false);
  local->set_actor_generation(102);
  first->EndHitBatch(true);
  EXPECT_EQ(Peer::ActionSequence(*first), 2u);
}

TEST_F(ClientNpcPacketTest, InventoryCorrectionsAreScopedToActorGenerationAndItem) {
  auto* local = first->player_manager().CreateLocalPlayer(1);
  local->set_actor_generation(101);
  Deliver(0, MakeSpawn());
  ItemTransactionResultPacket result;
  result.actor_id = 21;
  result.actor_generation = 121;
  result.item_instance = 123;
  result.state_sequence = 100;
  result.resulting_amount = 2;
  Deliver(0, result);
  result.state_sequence = 99;
  result.resulting_amount = 3;
  Deliver(0, result);
  result.actor_id = 1;
  result.actor_generation = 101;
  result.state_sequence = 1;
  result.resulting_amount = 4;
  Deliver(0, result);
  first->player_manager().GetPlayer(21)->set_actor_generation(122);
  result.actor_id = 21;
  result.actor_generation = 121;
  result.state_sequence = 101;
  Deliver(0, result);
  result.actor_generation = 122;
  result.state_sequence = 1;
  result.resulting_amount = 5;
  Deliver(0, result);
  ASSERT_EQ(first_observer.inventory_results.size(), 3u);
  EXPECT_EQ(first_observer.inventory_results[0], std::make_pair(21u, 2));
  EXPECT_EQ(first_observer.inventory_results[1], std::make_pair(1u, 4));
  EXPECT_EQ(first_observer.inventory_results[2], std::make_pair(21u, 5));
  EXPECT_EQ(first->player_manager().GetPlayer(21)->inventory_count(123), 5);
  EXPECT_EQ(local->inventory_count(123), 4);
}

TEST_F(ClientNpcPacketTest, LocalInventoryStartsFromTheServerSnapshotAndOnlyServerPacketsChangeCounts) {
  auto* local = first->player_manager().CreateLocalPlayer(1);
  auto spawn = MakeSpawn().actor;
  spawn.player_id = 1;
  spawn.inventory = {{99, 3}};
  Deliver(0, spawn);
  EXPECT_EQ(local->inventory_count(99), 3);
  EXPECT_EQ(local->inventory_count(150), 0);
  ASSERT_EQ(first_observer.inventory_snapshots.size(), 1u);
  first->SendPlayerWorldEnter("NEWWORLD.ZEN", {{99, 100000}, {150, 10}});
  EXPECT_EQ(local->inventory_count(99), 3);
  EXPECT_EQ(local->inventory_count(150), 0);

  GiveItemPacket grant;
  grant.packet_type = Net::PT_GIVEITEM;
  grant.player_id = 1;
  grant.actor_generation = spawn.actor_generation;
  grant.state_sequence = 11;
  grant.item_instance = 99;
  grant.item_amount = 2;
  Deliver(0, grant);
  Deliver(0, grant);
  EXPECT_EQ(local->inventory_count(99), 5);
  grant.item_instance = 150;
  grant.state_sequence = 9;
  Deliver(0, grant);
  EXPECT_EQ(local->inventory_count(150), 0);

  RemoveItemPacket remove;
  remove.packet_type = Net::PT_REMOVEITEM;
  remove.player_id = 1;
  remove.actor_generation = spawn.actor_generation;
  remove.state_sequence = 12;
  remove.item_instance = 99;
  remove.item_amount = 1;
  Deliver(0, remove);
  Deliver(0, remove);
  EXPECT_EQ(local->inventory_count(99), 4);
  first_observer.expected_new_spawn = false;
  Deliver(0, spawn);
  EXPECT_EQ(local->inventory_count(99), 4);
  spawn.state_sequence = 13;
  spawn.inventory.clear();
  Deliver(0, spawn);
  EXPECT_EQ(local->inventory_count(99), 0);
}

TEST_F(ClientNpcPacketTest, DelayedTransactionAcknowledgementKeepsTheNewerServerInventory) {
  auto* local = first->player_manager().CreateLocalPlayer(1);
  local->set_actor_generation(101);
  ItemTransactionResultPacket result;
  result.actor_id = 1;
  result.actor_generation = 101;
  result.item_instance = 99;
  result.state_sequence = 20;
  result.resulting_amount = 5;
  Deliver(0, result);
  result.request_id = 1;
  result.state_sequence = 19;
  result.resulting_amount = 100;
  Deliver(0, result);
  EXPECT_EQ(local->inventory_count(99), 5);
  ASSERT_EQ(first_observer.inventory_results.size(), 2u);
  EXPECT_EQ(first_observer.inventory_results.back(), std::make_pair(1u, 5));
}

TEST_F(ClientNpcPacketTest, WorldLoadingRetainsServerInventoryAndDoesNotApplyQueuedGrantsTwice) {
  auto* local = first->player_manager().CreateLocalPlayer(1);
  auto spawn = MakeSpawn().actor;
  spawn.player_id = 1;
  spawn.inventory = {{99, 3}};
  Deliver(0, spawn);
  PlayerWorldUpdatePacket world;
  world.packet_type = Net::PT_PLAYER_WORLD_UPDATE;
  world.player_id = 1;
  world.actor_generation = spawn.actor_generation + 1;
  world.world_name = "NEWWORLD.ZEN";
  Deliver(0, world);
  EXPECT_EQ(local->inventory_count(99), 3);
  GiveItemPacket grant;
  grant.packet_type = Net::PT_GIVEITEM;
  grant.player_id = 1;
  grant.actor_generation = world.actor_generation;
  grant.item_instance = 99;
  grant.item_amount = 2;
  grant.state_sequence = 1;
  Deliver(0, grant);
  EXPECT_EQ(local->inventory_count(99), 5);
  first->SendPlayerWorldEnter(world.world_name);
  EXPECT_EQ(local->inventory_count(99), 5);
  grant.actor_generation = spawn.actor_generation;
  grant.state_sequence = 100;
  Deliver(0, grant);
  EXPECT_EQ(local->inventory_count(99), 5);
}

TEST_F(ClientNpcPacketTest, NpcSnapshotRestoresFullInventoryBeforeFinalEquipmentApplication) {
  auto spawn = MakeSpawn();
  spawn.actor.inventory = {{100, 100000}, {101, 2}};
  Deliver(0, spawn);
  ASSERT_EQ(first_observer.inventory_snapshots.size(), 1u);
  ASSERT_EQ(first_observer.inventory_snapshots.front().size(), 2u);
  EXPECT_EQ(first_observer.inventory_snapshots.front()[0].amount, 100000);
  const auto& order = first_observer.order;
  EXPECT_LT(std::find(order.begin(), order.end(), "inventory"), std::find(order.begin(), order.end(), "snapshot_applied"));
  first_observer.expected_new_spawn = false;
  spawn.actor.inventory.clear();
  spawn.actor.state_sequence = spawn.control.state_sequence = 11;
  Deliver(0, spawn);
  ASSERT_EQ(first_observer.inventory_snapshots.size(), 2u);
  EXPECT_TRUE(first_observer.inventory_snapshots.back().empty());
}

TEST_F(ClientNpcPacketTest, InvalidNpcInventorySnapshotCannotCreateTheNativeActor) {
  auto spawn = MakeSpawn();
  spawn.actor.inventory = {{100, 1}, {100, 2}};
  Deliver(0, spawn);
  EXPECT_EQ(first->player_manager().GetPlayer(21), nullptr);
  EXPECT_TRUE(first_observer.inventory_snapshots.empty());
}

TEST_F(ClientNpcPacketTest, WorldLoadRetainsNewGenerationTeleportUntilActorIsReady) {
  first->player_manager().CreateLocalPlayer(21);
  Deliver(0, MakeSpawn().actor);
  PlayerWorldUpdatePacket world;
  world.packet_type = Net::PT_PLAYER_WORLD_UPDATE;
  world.player_id = 21;
  world.actor_generation = 122;
  world.command_revision = 9;
  world.world_name = "NEWWORLD.ZEN";
  Deliver(0, world);
  PlayerPositionUpdatePacket position;
  position.packet_type = Net::PT_MAP_ONLY;
  position.player_id = 21;
  position.actor_generation = 122;
  position.command_revision = 10;
  position.state_sequence = 1;
  position.position = {22.0f, 33.0f, 44.0f};
  Deliver(0, position);
  EXPECT_EQ(first->player_manager().GetLocalPlayer().position(), position.position);
  EXPECT_EQ(first_observer.position_updates, 0);
  EXPECT_EQ(first_observer.state_updates, 0);
  // The packet fixture intentionally has no socket. Native correction delivery
  // precedes the reliable ready send and does not depend on its transport result.
  first->SendPlayerWorldEnter(world.world_name);
  ASSERT_EQ(first_observer.states.size(), 1u);
  EXPECT_EQ(first_observer.states.front().position, position.position);
  EXPECT_EQ(first_observer.states.front().correction_mask, kSyncPosition);
}

TEST_F(ClientNpcPacketTest, PersistentMobReplayDoesNotRequireItsOriginatingActor) {
  first->player_manager().CreateLocalPlayer(21);
  Deliver(0, MakeSpawn().actor);
  first->SendPlayerWorldEnter("NEWWORLD.ZEN");
  ActorActionPacket action;
  action.kind = ActorActionKind::MobState;
  action.target_generation = 121;
  action.name = "WORLD_DOOR_01";
  action.target_state = 1;
  Deliver(0, action);
  ASSERT_EQ(first_observer.actions.size(), 1u);
  EXPECT_EQ(first_observer.actions.front().actor_id, 0u);
}

TEST_F(ClientNpcPacketTest, MobReplayWaitsForNativeWorldAndRejectsPreviousViewerGeneration) {
  first->player_manager().CreateLocalPlayer(21);
  Deliver(0, MakeSpawn().actor);
  ActorActionPacket action;
  action.kind = ActorActionKind::MobState;
  action.target_generation = 121;
  action.name = "WORLD_DOOR_01";
  action.target_state = 1;
  Deliver(0, action);
  EXPECT_TRUE(first_observer.actions.empty());

  PlayerWorldUpdatePacket world;
  world.packet_type = Net::PT_PLAYER_WORLD_UPDATE;
  world.player_id = 21;
  world.actor_generation = 122;
  world.world_name = "OTHERWORLD.ZEN";
  Deliver(0, world);
  Deliver(0, action);  // Queued and late old-world states must both disappear.
  action.target_generation = 122;
  action.target_state = 0;
  Deliver(0, action);
  action.target_state = 2;
  Deliver(0, action);  // Absolute states coalesce during loading.
  auto spawn = MakeSpawn().actor;
  spawn.actor_generation = 122;
  Deliver(0, spawn);
  EXPECT_TRUE(first_observer.actions.empty());  // Metadata is not native world readiness.

  first->SendPlayerWorldEnter(world.world_name);
  ASSERT_EQ(first_observer.actions.size(), 1u);
  EXPECT_EQ(first_observer.actions.front().target_generation, 122u);
  EXPECT_EQ(first_observer.actions.front().target_state, 2);
  action.target_generation = 121;
  Deliver(0, action);
  EXPECT_EQ(first_observer.actions.size(), 1u);
  action.target_generation = 122;
  action.target_state = 3;
  Deliver(0, action);
  ASSERT_EQ(first_observer.actions.size(), 2u);
  EXPECT_EQ(first_observer.actions.back().target_state, 3);
}

TEST_F(ClientNpcPacketTest, DisconnectDiscardsDeferredMobReplayBeforeAnotherSession) {
  first->player_manager().CreateLocalPlayer(21);
  Deliver(0, MakeSpawn().actor);
  ActorActionPacket action;
  action.kind = ActorActionKind::MobState;
  action.target_generation = 121;
  action.name = "WORLD_DOOR_01";
  action.target_state = 1;
  Deliver(0, action);
  first->Disconnect();
  first->player_manager().CreateLocalPlayer(21);
  Deliver(0, MakeSpawn().actor);
  first->SendPlayerWorldEnter("NEWWORLD.ZEN");
  EXPECT_TRUE(first_observer.actions.empty());
}

TEST_F(ClientNpcPacketTest, DuplicateJoinCannotOverwriteSpawnedActor) {
  JoinGamePacket join;
  join.packet_type = Net::PT_JOIN_GAME;
  join.player_id = 2;
  join.actor_generation = 121;
  join.player_name = "Original";
  Deliver(0, join);
  auto spawn = MakeSpawn().actor;
  spawn.player_id = 2;
  Deliver(0, spawn);
  join.player_name = "Duplicate";
  join.position = {999.0f, 999.0f, 999.0f};
  Deliver(0, join);
  const auto* actor = first->player_manager().GetPlayer(2);
  ASSERT_NE(actor, nullptr);
  EXPECT_EQ(actor->name(), spawn.player_name);
  EXPECT_EQ(actor->position(), spawn.position);
}

TEST_F(ClientNpcPacketTest, IncompatibleActorProtocolIsRejectedBeforeLocalMetadataCreation) {
  InitialInfoPacket packet;
  packet.packet_type = Net::PT_INITIAL_INFO;
  packet.protocol_version = kActorProtocolVersion - 1;
  packet.actor_generation = 121;
  packet.player_id = 2;
  Deliver(0, packet);
  EXPECT_FALSE(first->player_manager().HasLocalPlayer());
  EXPECT_EQ(first->GetConnectionState(), GameClient::ConnectionState::Failed);
  EXPECT_EQ(first_observer.connection_failure, "Incompatible actor synchronization protocol");
}

TEST_F(ClientNpcPacketTest, ClearsEngineObserversBeforeDestroyingNetworkMetadata) {
  Deliver(0, MakeSpawn());
  first->Disconnect();
  EXPECT_TRUE(first_observer.cleared_with_live_metadata);
  EXPECT_EQ(first->player_manager().GetPlayer(21), nullptr);
}

TEST_F(ClientNpcPacketTest, TransportRejectionPreservesTheApplicationCloseReason) {
  using Peer = gmp::client::GameClientTestPeer;
  Peer::BeginConnection(*first);
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Closed, 1, Net::CloseReason::ServerFull, "capacity"});
  EXPECT_EQ(first->GetConnectionState(), GameClient::ConnectionState::Failed);
  EXPECT_EQ(first->GetConnectionCloseReason(), Net::CloseReason::ServerFull);
  EXPECT_EQ(first_observer.connection_failure, "Server is full");
  EXPECT_EQ(first_observer.connected, 0);
  EXPECT_EQ(first_observer.connection_lost, 0);
}

TEST_F(ClientNpcPacketTest, GracefulRemoteCloseClearsActorsAndPreservesKickReason) {
  using Peer = gmp::client::GameClientTestPeer;
  Peer::BeginConnection(*first);
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Connected, 1});
  Deliver(0, MakeSpawn());
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Closed, 1, Net::CloseReason::Kicked, "kick"});
  EXPECT_EQ(first_observer.connected, 1);
  EXPECT_TRUE(first_observer.cleared_with_live_metadata);
  EXPECT_EQ(first->player_manager().GetPlayer(21), nullptr);
  EXPECT_EQ(first_observer.connection_closed, "Kicked from the server");
  EXPECT_EQ(first_observer.connection_lost, 0);
  EXPECT_EQ(first->GetConnectionCloseReason(), Net::CloseReason::Kicked);
  first->Disconnect();
  EXPECT_EQ(first->GetConnectionCloseReason(), Net::CloseReason::Kicked);
}

TEST_F(ClientNpcPacketTest, TransportFailureDispatchesConnectionLossAfterAcceptance) {
  using Peer = gmp::client::GameClientTestPeer;
  Peer::BeginConnection(*first);
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Connected, 1});
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Failure, 1, Net::CloseReason::Failure, "timed out"});
  EXPECT_EQ(first->GetConnectionState(), GameClient::ConnectionState::Disconnected);
  EXPECT_EQ(first->GetConnectionError(), "timed out");
  EXPECT_EQ(first_observer.connection_lost, 1);
  EXPECT_TRUE(first_observer.connection_closed.empty());
  EXPECT_TRUE(first_observer.connection_failure.empty());
}

TEST_F(ClientNpcPacketTest, LocalDisconnectNotifiesOnceAndAllowsAnotherConnection) {
  using Peer = gmp::client::GameClientTestPeer;
  Peer::BeginConnection(*first);
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Connected, 1});
  first->Disconnect();
  first->Disconnect();
  EXPECT_EQ(first_observer.disconnected, 1);
  EXPECT_EQ(first->GetConnectionCloseReason(), Net::CloseReason::Normal);
  Peer::BeginConnection(*first);
  Peer::NotifyTransport(*first, {Net::ConnectionEvent::Kind::Connected, 2});
  EXPECT_EQ(first_observer.connected, 2);
  EXPECT_EQ(first->GetConnectionCloseReason(), Net::CloseReason::None);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
