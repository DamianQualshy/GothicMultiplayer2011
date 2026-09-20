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
extern Net::NetClient* g_netclient;
}

namespace {
using gmp::client::EventObserver;
using gmp::client::GameClient;
using gmp::client::Player;

// Exercise the production packet handlers through their public transport
// interface. No socket, downloader, native engine or private access is needed.
class FakeNetClient final : public Net::NetClient {
public:
  void Pulse() override {}
  bool Connect(const char*, std::uint32_t) override { return true; }
  void Disconnect() override {}
  bool IsConnected() const override { return false; }
  bool SendPacket(unsigned char*, std::uint32_t, Net::PacketReliability, Net::PacketPriority, std::uint32_t) override {
    return true;
  }
  void AddPacketHandler(PacketHandler& handler) override { handlers.push_back(&handler); }
  void RemovePacketHandler(PacketHandler& handler) override {
    handlers.erase(std::remove(handlers.begin(), handlers.end(), &handler), handlers.end());
  }
  std::uint32_t GetPing() const override { return 0; }
  Net::NetworkStats GetNetworkStats() const override { return {}; }
  std::vector<PacketHandler*> handlers;
};

class ImmediateScheduler final : public gmp::TaskScheduler {
public:
  void ScheduleOnMainThread(Task task) override { task(); }
};

struct RecordingObserver final : EventObserver {
  GameClient* client{nullptr};
  Player* spawned_player{nullptr};
  bool disturb_snapshot_health{false};
  std::vector<std::string> order;
  std::vector<NpcControlPacket> controls;
  std::vector<NpcAnimationPacket> animations;
  int state_updates{0};
  int position_updates{0};
  int deaths{0};
  int respawns{0};
  bool cleared_with_live_metadata{false};

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
    EXPECT_TRUE(new_spawn);
    EXPECT_TRUE(player.has_spawned());
    EXPECT_EQ(player.max_health(), 2000);
    EXPECT_EQ(player.health(), 1800);
    EXPECT_EQ(player.life_state(), PLAYER_LIFE_ALIVE);
  }
  void OnNpcControl(const NpcControlPacket& packet) override { controls.push_back(packet); }
  void OnNpcAnimation(const NpcAnimationPacket& packet) override { animations.push_back(packet); }
  void OnPlayerStateUpdate(std::uint64_t, const PlayerState&) override { ++state_updates; }
  void OnPlayerPositionUpdate(std::uint64_t, float, float, float) override { ++position_updates; }
  void OnPlayerDied(std::uint64_t) override { ++deaths; }
  void OnPlayerRespawned(std::uint64_t) override { ++respawns; }
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
  packet.control.control_epoch = 4;
  packet.control.state_sequence = packet.actor.state_sequence;
  packet.control.position = packet.actor.position;
  packet.control.normal = packet.actor.normal;
  packet.animation.npc_id = 21;
  packet.animation.revision = 7;
  packet.animation.animation = "S_FISTRUNL";
  return packet;
}

PlayerStateUpdatePacket MakeMovement(std::uint32_t sequence) {
  PlayerStateUpdatePacket packet{};
  packet.packet_type = Net::PT_ACTUAL_STATISTICS;
  packet.player_id = 21;
  packet.state_sequence = sequence;
  packet.state.position = {900.0f, 200.0f, 500.0f};
  packet.state.nrot = {1.0f, 0.0f, 0.0f};
  packet.state.health_points = 1800;
  packet.state.life_state = PLAYER_LIFE_ALIVE;
  return packet;
}

class ClientNpcPacketTest : public ::testing::Test {
protected:
  FakeNetClient transport;
  ImmediateScheduler scheduler;
  RecordingObserver first_observer;
  RecordingObserver second_observer;
  std::unique_ptr<GameClient> first;
  std::unique_ptr<GameClient> second;
  Net::NetClient* previous_transport{nullptr};

  void SetUp() override {
    previous_transport = gmp::client::g_netclient;
    gmp::client::g_netclient = &transport;
    first = std::make_unique<GameClient>(first_observer, scheduler);
    first_observer.client = first.get();
    second = std::make_unique<GameClient>(second_observer, scheduler);
    second_observer.client = second.get();
  }
  void TearDown() override {
    second.reset();
    first.reset();
    EXPECT_TRUE(transport.handlers.empty());
    gmp::client::g_netclient = previous_transport;
  }
  template <typename Packet>
  void Deliver(std::size_t viewer, const Packet& packet) {
    std::vector<unsigned char> bytes;
    const auto size = bitsery::quickSerialization<bitsery::OutputBufferAdapter<std::vector<unsigned char>>>(bytes, packet);
    ASSERT_LT(viewer, transport.handlers.size());
    ASSERT_TRUE(transport.handlers[viewer]->HandlePacket(bytes.data(), static_cast<std::uint32_t>(size)));
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
  joined.player_name = "Joined player";
  Deliver(0, joined);
  auto* player = first->player_manager().GetPlayer(2);
  ASSERT_NE(player, nullptr);
  EXPECT_TRUE(player->has_joined());
  EXPECT_FALSE(player->has_spawned());

  // Movement on a separate channel may overtake the reliable spawn snapshot.
  // Connection-only metadata must not consume that movement's sequence and
  // subsequently reject the snapshot needed to create the engine actor.
  auto movement = MakeMovement(12);
  movement.player_id = 2;
  Deliver(0, movement);
  PlayerPositionUpdatePacket position{};
  position.packet_type = Net::PT_MAP_ONLY;
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
  EXPECT_EQ(first_observer.controls.back().state_sequence, 12u);
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
  PlayerDeathInfoPacket death{Net::PT_DODIE, 21, 9};
  Deliver(0, death);
  auto* npc = first->player_manager().GetPlayer(21);
  ASSERT_NE(npc, nullptr);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_ALIVE);
  EXPECT_EQ(first_observer.deaths, 0);

  death.state_sequence = 12;
  Deliver(0, death);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_DEAD);
  EXPECT_EQ(first_observer.deaths, 1);
  Deliver(0, MakeMovement(11));
  PlayerRespawnInfoPacket respawn{Net::PT_RESPAWN, 21, 11};
  Deliver(0, respawn);
  EXPECT_EQ(npc->life_state(), PLAYER_LIFE_DEAD);
  EXPECT_EQ(npc->health(), 0);
  EXPECT_EQ(first_observer.respawns, 0);
  EXPECT_EQ(first_observer.state_updates, 0);
}

TEST_F(ClientNpcPacketTest, ClearsEngineObserversBeforeDestroyingNetworkMetadata) {
  Deliver(0, MakeSpawn());
  first->Disconnect();
  EXPECT_TRUE(first_observer.cleared_with_live_metadata);
  EXPECT_EQ(first->player_manager().GetPlayer(21), nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
