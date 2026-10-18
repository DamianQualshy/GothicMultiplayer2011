#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

#include "character.h"
#include "item_ground_manager.h"
#include "item_registry.h"
#include "inventory_sync.h"
#include "packets.h"
#include "game_server.h"
#include "Script.h"
#include "Lua/lua_item.h"
#include "server_events.h"
#include "shared/event.h"

TEST(ActorInventory, InsufficientDropLeavesOwnershipUnchanged) {
  Character actor;
  ASSERT_TRUE(actor.AddInventory("ITRW_ARROW", 3));
  EXPECT_FALSE(actor.RemoveInventory("ITRW_ARROW", 4));
  EXPECT_EQ(actor.InventoryCount("ITRW_ARROW"), 3);
  EXPECT_FALSE(actor.RemoveInventory("UNKNOWN", 1));
  EXPECT_FALSE(actor.RemoveInventory("ITRW_ARROW", -1));
  EXPECT_TRUE(actor.RemoveInventory("ITRW_ARROW", 3));
  EXPECT_FALSE(actor.inventory.contains("ITRW_ARROW"));
}

TEST(ActorInventory, StackOverflowCannotCommitPartialTake) {
  Character actor;
  ASSERT_TRUE(actor.AddInventory("ITMI_GOLD", std::numeric_limits<std::int32_t>::max() - 1));
  EXPECT_FALSE(actor.AddInventory("ITMI_GOLD", 2));
  EXPECT_EQ(actor.InventoryCount("ITMI_GOLD"), std::numeric_limits<std::int32_t>::max() - 1);
  EXPECT_TRUE(actor.AddInventory("ITMI_GOLD", 1));
}

TEST(GroundTransactions, CallbackDestructionDoesNotTransferReservationToReplacement) {
  ItemGroundManager ground;
  ItemGroundManager::CreateOptions options;
  options.instance = "ITMI_GOLD";
  const auto first_id = ground.Create(options).id;
  ASSERT_TRUE(ground.Reserve(first_id, 1));
  EXPECT_FALSE(ground.Reserve(first_id, 2));
  ground.ReleaseReservation(first_id, 2);
  EXPECT_FALSE(ground.Reserve(first_id, 2));
  ASSERT_TRUE(ground.Destroy(first_id));
  const auto replacement_id = ground.Create(options).id;
  EXPECT_NE(first_id, replacement_id);
  EXPECT_EQ(ground.Get(first_id), nullptr);
  EXPECT_TRUE(ground.Reserve(replacement_id, 2));
  ground.Clear();
  EXPECT_GT(ground.Create(options).id, replacement_id);
}

class ItemCatalog : public ::testing::Test {
protected:
  void SetUp() override {
    const auto directory = std::filesystem::current_path() / "build" / "actor-inventory-tests";
    std::filesystem::create_directories(directory);
    path = directory /
           ("gmp-item-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
  }
  void TearDown() override {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  void Write(const std::string& source) {
    std::ofstream output(path, std::ios::binary);
    output << source;
  }
  std::filesystem::path path;
  ItemRegistry registry;
};

TEST_F(ItemCatalog, RejectsMalformedCatalogWithoutRetainingPartialDefinitions) {
  Write(R"({"items":[{"instance":"ITPO_TEST","index":1,"mainflag":"bad","flags":0}]})");
  EXPECT_FALSE(registry.Load(path));
  EXPECT_EQ(registry.Size(), 0u);
  EXPECT_EQ(registry.FindByIndex(1), nullptr);
}

TEST_F(ItemCatalog, ItemPropertiesComeFromTheCatalog) {
  Write(R"({"items":[{"instance":"ITPO_TEST","index":99,"mainflag":1,"flags":0,"scemename":"POTION"},
                     {"instance":"ITRU_TEST","index":150,"mainflag":512,"flags":0,"spell":19,
                      "damage":{"total":25,"types":32},"range":3000}]})");
  ASSERT_TRUE(registry.Load(path));
  ASSERT_NE(registry.Find("itpo_test"), nullptr);
  EXPECT_EQ(registry.Find("itpo_test")->scemename, "POTION");
  ASSERT_NE(registry.FindByIndex(150), nullptr);
  EXPECT_EQ(registry.FindByIndex(150)->damage->total, 25);
  EXPECT_EQ(registry.FindByIndex(150)->spell, 19);
}

TEST_F(ItemCatalog, NativeInventoryDeclarationOnlyValidatesRegisteredPositiveUniqueEntries) {
  Write(R"({"items":[{"instance":"ITPO_TEST","index":99,"mainflag":1,"flags":0}]})");
  ASSERT_TRUE(registry.Load(path));
  EXPECT_TRUE(ValidateNativeInventoryDeclaration({{99, 100000}}, registry));
  EXPECT_TRUE(ValidateNativeInventoryDeclaration({}, registry));
  EXPECT_FALSE(ValidateNativeInventoryDeclaration({{99, 10}, {99, 11}}, registry));
  EXPECT_FALSE(ValidateNativeInventoryDeclaration({{99, 10}, {100, 1}}, registry));
  EXPECT_FALSE(ValidateNativeInventoryDeclaration({{99, 0}}, registry));
  EXPECT_FALSE(ValidateNativeInventoryDeclaration({{99, -1}}, registry));
  EXPECT_FALSE(ValidateNativeInventoryDeclaration(std::vector<InventoryEntry>(kMaxInventoryEntries + 1, {99, 1}), registry));
}

class ServerActorValidation : public ItemCatalog {
protected:
  void SetUp() override {
    ItemCatalog::SetUp();
    Write(R"({"items":[{"instance":"ITPO_TEST","index":99,"mainflag":1,"flags":0},
                      {"instance":"ITRU_TEST","index":150,"mainflag":512,"flags":0,"spell":19,
                       "damage":{"total":25,"types":32},"range":3000},
                      {"instance":"ITMW_TEST","index":175,"mainflag":2,"flags":16384,
                       "damage":{"total":5,"types":4},"range":50},
                      {"instance":"ITAR_TEST","index":176,"mainflag":16,"flags":0,
                       "protections":[{"type":2,"value":1000}]}]})");
    // Initialize trusted fixture data without starting network listeners or resources.
    ASSERT_TRUE(const_cast<ItemRegistry&>(server.GetItemRegistry()).Load(path));
    first = server.GetPlayerManager().AddPlayer(70001, "HostOne", 2);
    second = server.GetPlayerManager().AddPlayer(70002, "HostTwo", 2);
    ASSERT_NE(first, 0u);
    ASSERT_NE(second, 0u);
    npc_id = server.GetNpcManager().Create("Mage", "PC_HERO", "NEWWORLD.ZEN", std::max(2u, server.GetMaxSlots()));
    ASSERT_NE(npc_id, 0u);
    for (const auto id : {first, second, npc_id}) {
      auto& actor = server.GetCharacter(id)->get();
      actor.is_ingame = 1;
      actor.world = "NEWWORLD.ZEN";
      actor.health = actor.max_health = actor.mana = actor.max_mana = 100;
      actor.strength = 10;
      actor.state.health_points = actor.state.mana_points = 100;
      actor.state.life_state = PLAYER_LIFE_ALIVE;
      actor.state.weapon_mode = 1;
    }
    for (const auto id : {first, second}) {
      auto& player = server.GetPlayerManager().GetPlayer(id)->get();
      player.npc_world_ready = true;
      player.spawned_players.insert(npc_id);
      player.spawned_players.insert(first == id ? second : first);
    }
    auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
    npc.streamed_by_players = {first, second};
    ASSERT_TRUE(server.SetNpcHostPlayer(npc_id, first));
    ASSERT_TRUE(npc.AddInventory("ITPO_TEST", 3));
    ASSERT_TRUE(npc.AddInventory("ITRU_TEST", 1));
  }
  template <typename T>
  void Send(Net::ConnectionHandle connection, T packet) {
    std::vector<std::uint8_t> bytes;
    using Adapter = bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>;
    bytes.resize(bitsery::quickSerialization<Adapter>(bytes, packet));
    EXPECT_TRUE(server.HandlePacket(connection, bytes.data(), static_cast<std::uint32_t>(bytes.size())));
  }
  GameServer server;
  std::uint32_t first{0}, second{0}, npc_id{0};
  PlayerHitReportPacket Hit(Character& attacker, Character& victim, std::uint32_t identity,
                            std::uint32_t damage_type = 2, bool nonlethal = false) {
    PlayerHitReportPacket hit{};
    hit.packet_type = Net::PT_PLAYER_HIT;
    hit.attacker_id = attacker.player_id;
    hit.actor_generation = attacker.actor_generation;
    hit.victim_id = victim.player_id;
    hit.victim_generation = victim.actor_generation;
    hit.attack_id = identity;
    hit.command_revision = attacker.command_revision;
    hit.damage = 10;
    hit.damage_type = damage_type;
    hit.dont_kill = nonlethal;
    return hit;
  }
  void ApproveProjectile(Character& attacker, std::uint32_t identity) {
    attacker.approved_projectiles.push_back({identity, 10, 32, 3000,
        std::chrono::steady_clock::now() + std::chrono::seconds(5), {}, 1, attacker.state.position});
  }
  PlayerHitReportPacket FinishingMove(Character& attacker, Character& victim, std::uint32_t identity = 1) {
    EXPECT_TRUE(attacker.AddInventory("ITMW_TEST", 1));
    attacker.state.melee_weapon_instance = 175;
    attacker.state.weapon_mode = 3;
    victim.health = victim.state.health_points = 1;
    victim.flags |= PlayerManager::PL_UNCONCIOUS;
    victim.state.life_state = PLAYER_LIFE_UNCONSCIOUS;
    auto hit = Hit(attacker, victim, identity, 4, true);
    hit.finishing_move = true;
    return hit;
  }
};

TEST_F(ServerActorValidation, WorldEntryNeverImportsNativeInventoryAndPreservesServerGrants) {
  auto& player = server.GetPlayerManager().GetPlayer(first)->get();
  player.npc_world_ready = false;
  ASSERT_TRUE(server.GiveItem(first, "ITPO_TEST", 3));
  PlayerWorldEnterPacket ready;
  ready.packet_type = Net::PT_PLAYER_WORLD_ENTER;
  ready.actor_generation = player.actor_generation;
  ready.world_name = player.world;
  ready.inventory = {{99, 100000}, {150, 50}};
  Send(70001, ready);
  EXPECT_TRUE(player.npc_world_ready);
  EXPECT_TRUE(player.inventory_initialized);
  EXPECT_EQ(player.InventoryCount("ITPO_TEST"), 3);
  EXPECT_EQ(player.InventoryCount("ITRU_TEST"), 0);
  player.npc_world_ready = false;
  ready.inventory = {{99, 1}};
  Send(70001, ready);
  EXPECT_EQ(player.InventoryCount("ITPO_TEST"), 3);
  EXPECT_EQ(player.InventoryCount("ITRU_TEST"), 0);
}

TEST_F(ServerActorValidation, UnownedItemUseCannotAssignNativeEffects) {
  auto& player = server.GetPlayerManager().GetPlayer(first)->get();
  player.health = player.state.health_points = 10;
  player.mana = player.state.mana_points = 20;
  int requests = 0;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerUseItemName, [&](std::any) { ++requests; }));
  ActorActionPacket use;
  use.actor_id = first;
  use.actor_generation = player.actor_generation;
  use.command_revision = player.command_revision;
  use.action_id = 1;
  use.kind = ActorActionKind::ItemUse;
  use.item_instance = 99;
  use.quantity = 1;
  use.health_delta = use.mana_delta = 50;
  Send(70001, use);
  EXPECT_EQ(player.health, 10);
  EXPECT_EQ(player.mana, 20);
  EXPECT_EQ(player.InventoryCount("ITPO_TEST"), 0);
  EXPECT_EQ(requests, 1);
}

TEST_F(ServerActorValidation, PoseSnapshotsCannotAssignHealthOrManaIncreases) {
  auto& player = server.GetPlayerManager().GetPlayer(first)->get();
  player.health = player.state.health_points = 10;
  player.mana = player.state.mana_points = 20;
  PlayerStateUpdatePacket update;
  update.packet_type = Net::PT_ACTUAL_STATISTICS;
  update.actor_generation = player.actor_generation;
  update.command_revision = player.command_revision;
  update.state_sequence = 1;
  update.state = player.state;
  update.state.nrot = {0, 0, 1};
  update.state.position.x = 123;
  update.state.source_time_ms = ActorClockMilliseconds();
  update.state.health_points = update.state.mana_points = 100;
  Send(70001, update);
  EXPECT_EQ(player.health, 10);
  EXPECT_EQ(player.mana, 20);
  EXPECT_EQ(player.state.health_points, 10);
  EXPECT_EQ(player.state.mana_points, 20);
  EXPECT_EQ(player.state.position.x, 123);
}

TEST_F(ServerActorValidation, PvpMeleeKnocksOutThenAllowsExecution) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  victim.health = victim.state.health_points = 5;
  Send(70001, Hit(attacker, victim, 1));
  EXPECT_EQ(victim.health, 1);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
  attacker.last_attack_at -= std::chrono::seconds(1);
  Send(70001, Hit(attacker, victim, 2));
  EXPECT_EQ(victim.health, 0);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_DEAD);
}

TEST_F(ServerActorValidation, NativeFinishingMoveKillsWithoutNormalDamageOrArmorCalculation) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  auto hit = FinishingMove(attacker, victim);
  ASSERT_TRUE(victim.AddInventory("ITAR_TEST", 1));
  victim.state.equipped_armor_instance = 176;
  hit.damage = 0;
  hit.damage_type = 0;
  int hits = 0;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerHitName, [&](std::any event) {
    const auto request = std::any_cast<OnPlayerHitEvent>(event);
    ++hits;
    EXPECT_EQ(request.damage, 1);
  }));
  Send(70001, hit);
  EXPECT_EQ(hits, 1);
  EXPECT_EQ(victim.health, 0);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_DEAD);
  EXPECT_NE(victim.tod, 0);
  EXPECT_EQ(victim.flags & PlayerManager::PL_UNCONCIOUS, 0);
}

TEST_F(ServerActorValidation, FinishingMoveRequiresOwnedMeleeWeaponAndCanonicalUnconsciousVictim) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  auto hit = FinishingMove(attacker, victim);
  const auto reject = [&] {
    Send(70001, hit);
    EXPECT_EQ(victim.health, 1);
    EXPECT_EQ(victim.tod, 0);
    EXPECT_FALSE(attacker.last_hit_action_id.has_value());
  };
  victim.state.life_state = PLAYER_LIFE_ALIVE;
  reject();
  victim.state.life_state = PLAYER_LIFE_UNCONSCIOUS;
  victim.flags &= ~PlayerManager::PL_UNCONCIOUS;
  reject();
  victim.flags |= PlayerManager::PL_UNCONCIOUS;
  attacker.state.weapon_mode = 1;
  reject(); // Fists cannot perform Gothic's finishing move.
  attacker.state.weapon_mode = 5;
  reject();
  attacker.state.weapon_mode = 3;
  attacker.state.melee_weapon_instance = 99;
  reject();
  attacker.state.melee_weapon_instance = 175;
  ASSERT_TRUE(attacker.RemoveInventory("ITMW_TEST", 1));
  reject();
  ASSERT_TRUE(attacker.AddInventory("ITMW_TEST", 1));
  victim.state.position.x = 501.0f;
  reject();
  victim.state.position.x = 0.0f;
  victim.invulnerable = true;
  reject();
  victim.invulnerable = false;
  victim.virtual_world = 1;
  reject();
  victim.virtual_world = 0;
  ++hit.victim_generation;
  reject();
  hit.victim_generation = victim.actor_generation;
  ++hit.actor_generation;
  reject();
  hit.actor_generation = attacker.actor_generation;
  ++hit.command_revision;
  reject();
}

TEST_F(ServerActorValidation, FinishingMoveHonorsHitCancellationAndCannotBeReplayed) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  const auto hit = FinishingMove(attacker, victim);
  int requests = 0;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerHitName, [&](std::any) {
    ++requests;
    EventManager::Instance().CancelCurrentEvent();
  }));
  Send(70001, hit);
  Send(70001, hit);
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(victim.health, 1);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
}

TEST_F(ServerActorValidation, FinishingMoveCannotExecuteMultipleVictimsOrBypassMeleeCooldown) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  const auto first_hit = FinishingMove(attacker, victim);
  auto second_hit = FinishingMove(attacker, npc);
  Send(70001, first_hit);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_DEAD);
  Send(70001, second_hit);
  EXPECT_EQ(npc.health, 1);
  second_hit.attack_id = 2;
  Send(70001, second_hit);
  EXPECT_EQ(npc.health, 1);
  attacker.last_attack_at -= std::chrono::seconds(1);
  Send(70001, second_hit);
  EXPECT_EQ(npc.state.life_state, PLAYER_LIFE_DEAD);
}

TEST_F(ServerActorValidation, FinishingMoveRechecksRecoveryDuringHitCallback) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  const auto hit = FinishingMove(attacker, victim);
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerHitName, [&](std::any) {
    EXPECT_TRUE(server.SetPlayerHealth(second, 20));
  }));
  Send(70001, hit);
  EXPECT_EQ(victim.health, 20);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_ALIVE);
}

TEST_F(ServerActorValidation, FinishingMoveRechecksWeaponAndRangeDuringHitCallback) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  auto hit = FinishingMove(attacker, victim);
  int requests = 0;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerHitName, [&](std::any) {
    if (++requests == 1) attacker.state.weapon_mode = 1;
    else attacker.state.position.x = 501.0f;
  }));
  Send(70001, hit);
  EXPECT_EQ(victim.health, 1);
  attacker.state.weapon_mode = 3;
  attacker.last_attack_at -= std::chrono::seconds(1);
  hit.attack_id = 2;
  Send(70001, hit);
  EXPECT_EQ(requests, 2);
  EXPECT_EQ(victim.health, 1);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
}

TEST_F(ServerActorValidation, HostedNpcFinishingMoveRequiresCurrentHostAndRechecksResourceCallback) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  auto hit = FinishingMove(npc, victim);
  hit.control_epoch = npc.control_epoch;
  Send(70002, hit); // Another client cannot report this NPC's execution.
  EXPECT_EQ(victim.health, 1);
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerHitName, [&](std::any) {
    EXPECT_TRUE(server.SetNpcHostPlayer(npc_id, second));
  }));
  Send(70001, hit);
  EXPECT_EQ(victim.health, 1);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
}

TEST_F(ServerActorValidation, StandUpRetriesCurrentRevisionAfterMinimumRecoveryDelay) {
  auto& player = server.GetPlayerManager().GetPlayer(first)->get();
  player.health = player.state.health_points = 1;
  player.flags |= PlayerManager::PL_UNCONCIOUS;
  player.state.life_state = PLAYER_LIFE_UNCONSCIOUS;
  player.state.body_state = 22 | 256;
  player.state.animation = 43;
  player.state.animation_name = "S_WOUNDED";
  player.command_revision = 10;
  PlayerStandUpPacket recovery{};
  recovery.packet_type = Net::PT_PLAYER_STANDUP;
  recovery.actor_generation = player.actor_generation;
  recovery.command_revision = 9;
  player.last_damage_at = std::chrono::steady_clock::now() - std::chrono::seconds(2);
  Send(70001, recovery);
  EXPECT_EQ(player.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
  recovery.command_revision = player.command_revision;
  player.last_damage_at = std::chrono::steady_clock::now();
  Send(70001, recovery);
  EXPECT_EQ(player.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
  player.last_damage_at -= std::chrono::seconds(2);
  Send(70001, recovery);
  EXPECT_EQ(player.state.life_state, PLAYER_LIFE_ALIVE);
  EXPECT_EQ(player.health, 1);
  EXPECT_EQ(player.flags & PlayerManager::PL_UNCONCIOUS, 0);
  EXPECT_EQ(player.state.body_state, 256u);
  EXPECT_EQ(player.state.animation, -1);
  EXPECT_TRUE(player.state.animation_name.empty());
  EXPECT_FALSE(player.CombatRevisionIsCurrent(recovery.command_revision));
}

TEST_F(ServerActorValidation, StandUpCannotResurrectDeadPlayerOrUsePreviousGeneration) {
  auto& player = server.GetPlayerManager().GetPlayer(first)->get();
  player.health = player.state.health_points = 1;
  player.flags |= PlayerManager::PL_UNCONCIOUS;
  player.state.life_state = PLAYER_LIFE_UNCONSCIOUS;
  player.last_damage_at = std::chrono::steady_clock::now() - std::chrono::seconds(2);
  PlayerStandUpPacket recovery{};
  recovery.packet_type = Net::PT_PLAYER_STANDUP;
  recovery.actor_generation = player.actor_generation + 1;
  recovery.command_revision = player.command_revision;
  Send(70001, recovery);
  EXPECT_EQ(player.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
  EXPECT_TRUE(server.SetPlayerHealth(first, 0));
  recovery.actor_generation = player.actor_generation;
  recovery.command_revision = player.command_revision;
  Send(70001, recovery);
  EXPECT_EQ(player.health, 0);
  EXPECT_EQ(player.state.life_state, PLAYER_LIFE_DEAD);
  EXPECT_NE(player.tod, 0);
}

TEST_F(ServerActorValidation, ScriptedRecoveryClearsUnconsciousPoseAndRejectsOldActions) {
  auto& player = server.GetPlayerManager().GetPlayer(first)->get();
  player.health = player.state.health_points = 1;
  player.flags |= PlayerManager::PL_UNCONCIOUS;
  player.state.life_state = PLAYER_LIFE_UNCONSCIOUS;
  player.state.body_state = 22;
  player.state.animation = 43;
  player.state.animation_name = "S_WOUNDEDB";
  const auto old_revision = player.command_revision;
  EXPECT_TRUE(server.SetPlayerHealth(first, 20));
  EXPECT_EQ(player.state.life_state, PLAYER_LIFE_ALIVE);
  EXPECT_EQ(player.state.body_state, 0u);
  EXPECT_EQ(player.state.animation, -1);
  EXPECT_TRUE(player.state.animation_name.empty());
  EXPECT_FALSE(player.CombatRevisionIsCurrent(old_revision));
}

TEST_F(ServerActorValidation, ConsecutivePaidProjectilesSurviveWeaponSwitchAndCanKill) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  victim.health = victim.state.health_points = npc.health = npc.state.health_points = 5;
  ApproveProjectile(attacker, 100);
  ApproveProjectile(attacker, 101);
  attacker.state.weapon_mode = 0;
  Send(70001, Hit(attacker, victim, 1, 32));
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_DEAD);
  Send(70001, Hit(attacker, npc, 2, 32));
  EXPECT_EQ(npc.state.life_state, PLAYER_LIFE_DEAD);
  EXPECT_TRUE(attacker.approved_projectiles.front().victims.contains(second));
  EXPECT_TRUE(attacker.approved_projectiles.back().victims.contains(npc_id));
}

TEST_F(ServerActorValidation, ExplicitNonlethalProjectileProtectsUnconsciousPlayers) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  victim.health = victim.state.health_points = 5;
  ApproveProjectile(attacker, 100);
  ApproveProjectile(attacker, 101);
  Send(70001, Hit(attacker, victim, 1, 32, true));
  EXPECT_EQ(victim.health, 1);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
  Send(70001, Hit(attacker, victim, 2, 32, true));
  EXPECT_EQ(victim.health, 1);
  EXPECT_EQ(victim.state.life_state, PLAYER_LIFE_UNCONSCIOUS);
}

TEST_F(ServerActorValidation, OldCombatReportsCannotCrossARespawnCommandBarrier) {
  auto& attacker = server.GetPlayerManager().GetPlayer(first)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  attacker.command_revision = attacker.combat_barrier_revision = 10;
  auto old_hit = Hit(attacker, victim, 1);
  old_hit.command_revision = 9;
  Send(70001, old_hit);
  EXPECT_EQ(victim.health, 100);
  Send(70001, Hit(attacker, victim, 1));
  EXPECT_EQ(victim.health, 90);
  auto future_hit = Hit(attacker, victim, 2);
  future_hit.command_revision = 11;
  Send(70001, future_hit);
  EXPECT_EQ(victim.health, 90);
}

TEST_F(ServerActorValidation, LifeResetClearsOutstandingProjectileApprovals) {
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  ApproveProjectile(victim, 100);
  ASSERT_FALSE(victim.approved_projectiles.empty());
  ASSERT_TRUE(server.SetPlayerHealth(second, 0));
  EXPECT_TRUE(victim.approved_projectiles.empty());
  EXPECT_EQ(victim.combat_barrier_revision, victim.command_revision);
}

TEST_F(ServerActorValidation, NpcUseRechecksHostAfterResourceCallback) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  npc.health = npc.state.health_points = 10;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerUseItemName, [&](std::any) {
    EXPECT_TRUE(server.SetNpcHostPlayer(npc_id, second));
  }));
  ActorActionPacket use;
  use.actor_id = npc_id;
  use.actor_generation = npc.actor_generation;
  use.control_epoch = npc.control_epoch;
  use.action_id = 1;
  use.kind = ActorActionKind::ItemUse;
  use.item_instance = 99;
  use.quantity = 1;
  use.health_delta = 50;
  Send(70001, use);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 3);
  EXPECT_EQ(npc.health, 10);
  use.control_epoch = npc.control_epoch;
  use.action_id = 2;
  Send(70001, use);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 3);
  Send(70002, use);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 2);
  EXPECT_EQ(npc.health, 60);
}

TEST_F(ServerActorValidation, NpcCastRechecksHostAfterResourceCallback) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerCastSpellName, [&](std::any) {
    EXPECT_TRUE(server.SetNpcHostPlayer(npc_id, second));
  }));
  CastSpellPacket cast{};
  cast.packet_type = Net::PT_CASTSPELL;
  cast.caster_id = npc_id;
  cast.actor_generation = npc.actor_generation;
  cast.control_epoch = npc.control_epoch;
  cast.action_id = 1;
  cast.item_instance = 150;
  cast.spell_id = 19;
  cast.mana_delta = -5;
  Send(70001, cast);
  EXPECT_EQ(npc.mana, 100);
  EXPECT_TRUE(npc.approved_projectiles.empty());
  cast.control_epoch = npc.control_epoch;
  cast.action_id = 2;
  Send(70002, cast);
  EXPECT_EQ(npc.mana, 95);
  EXPECT_FALSE(npc.approved_projectiles.empty());
}

TEST_F(ServerActorValidation, RejectedScrollTargetsSendFreshInventoryAndManaCorrections) {
  Write(R"({"items":[{"instance":"ITSC_TEST","index":151,"mainflag":512,"flags":2097152,"spell":19,
                     "damage":{"total":25,"types":32},"range":3000}]})");
  ASSERT_TRUE(const_cast<ItemRegistry&>(server.GetItemRegistry()).Load(path));
  auto& caster = server.GetNpcManager().GetNpc(npc_id)->get();
  ASSERT_TRUE(caster.AddInventory("ITSC_TEST", 1));
  CastSpellPacket cast{};
  cast.packet_type = Net::PT_CASTSPELLONTARGET;
  cast.caster_id = npc_id;
  cast.actor_generation = caster.actor_generation;
  cast.control_epoch = caster.control_epoch;
  cast.item_instance = 151;
  cast.spell_id = 19;
  const auto reject = [&] {
    const auto revision = caster.command_revision;
    const auto sequence = caster.state_sequence;
    ++cast.action_id;
    Send(70001, cast);
    EXPECT_TRUE(IsNewActorSequence(caster.command_revision, revision));
    EXPECT_TRUE(IsNewActorSequence(caster.state_sequence, sequence));
    EXPECT_EQ(caster.mana, 100);
    EXPECT_EQ(caster.InventoryCount("ITSC_TEST"), 1);
    EXPECT_TRUE(caster.approved_projectiles.empty());
  };
  reject(); // Target packet without a target.
  cast.target_id = 99999;
  reject(); // Missing target.
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  cast.target_id = second;
  cast.target_generation = victim.actor_generation + 1;
  reject(); // Reused or stale target incarnation.
  cast.target_generation = victim.actor_generation;
  victim.virtual_world = 1;
  reject(); // Different visibility scope.
  victim.virtual_world = 0;
  victim.state.position.x = 4000.0f;
  reject(); // Beyond the trusted spell reach.
  victim.state.position.x = 0.0f;
  const auto target = server.GetNpcManager().Create("Target", "PC_HERO", "NEWWORLD.ZEN", std::max(2u, server.GetMaxSlots()));
  ASSERT_NE(target, 0u);
  auto& npc_target = server.GetNpcManager().GetNpc(target)->get();
  npc_target.is_ingame = 1;
  npc_target.health = npc_target.state.health_points = 100;
  npc_target.state.life_state = PLAYER_LIFE_ALIVE;
  server.GetPlayerManager().GetPlayer(first)->get().spawned_players.insert(target);
  cast.target_id = target;
  cast.target_generation = npc_target.actor_generation;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerCastSpellName, [&](std::any) {
    EXPECT_TRUE(server.DestroyNpc(target));
  }));
  reject(); // A resource destroys the target during validation.
}

TEST_F(ServerActorValidation, NpcHitRechecksHostAfterResourceCallback) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerHitName, [&](std::any) {
    EXPECT_TRUE(server.SetNpcHostPlayer(npc_id, second));
  }));
  PlayerHitReportPacket hit{};
  hit.packet_type = Net::PT_PLAYER_HIT;
  hit.attacker_id = npc_id;
  hit.actor_generation = npc.actor_generation;
  hit.control_epoch = npc.control_epoch;
  hit.attack_id = 1;
  hit.victim_id = second;
  hit.victim_generation = victim.actor_generation;
  hit.damage = 10;
  hit.damage_type = 2;
  Send(70001, hit);
  EXPECT_EQ(victim.health, 100);
}


TEST_F(ServerActorValidation, UnprofiledUseDispatchesBeforeOwnershipCheck) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  int requests = 0;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerUseItemName, [&](std::any event) {
    const auto request = std::any_cast<OnPlayerUseItemEvent>(event);
    EXPECT_EQ(request.player_id, npc_id);
    ++requests;
  }));
  ActorActionPacket use;
  use.actor_id = npc_id;
  use.actor_generation = npc.actor_generation;
  use.control_epoch = npc.control_epoch;
  use.action_id = 1;
  use.kind = ActorActionKind::ItemUse;
  use.item_instance = 99;
  use.quantity = 4; // The request is observable even though the actor owns only three.
  use.health_delta = 50;
  npc.health = npc.state.health_points = 10;
  Send(70001, use);
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(npc.health, 10);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 3);
  use.command_revision = npc.command_revision;
  use.action_id = 2;
  use.quantity = 1;
  Send(70001, use);
  Send(70001, use); // Duplicate cannot dispatch or consume twice.
  EXPECT_EQ(requests, 2);
  EXPECT_EQ(npc.health, 60);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 2);
}

TEST_F(ServerActorValidation, ReusableItemUseDoesNotConsumeOrRequireAProfile) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  ActorActionPacket use;
  use.actor_id = npc_id;
  use.actor_generation = npc.actor_generation;
  use.control_epoch = npc.control_epoch;
  use.action_id = 1;
  use.kind = ActorActionKind::ItemUse;
  use.item_instance = 99;
  use.quantity = 0;
  use.mana_delta = 1000000;
  npc.mana = npc.state.mana_points = 20;
  Send(70001, use);
  EXPECT_EQ(npc.mana, npc.max_mana);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 3);
}

TEST_F(ServerActorValidation, ItemManagerCallbackIsExplicitAndConsumesOnlyOnce) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  npc.health = npc.state.health_points = 10;
  LuaScript script;
  auto& lua = script.GetLuaState();
  lua["test_actor_id"] = npc_id;
  ASSERT_TRUE(lua.safe_script(R"(
    callback_calls = 0
    assert(ItemManager.setOnUse('ITPO_TEST', function(id, quantity, target)
      assert(id == test_actor_id and quantity == 1 and target == nil)
      callback_calls = callback_calls + 1
      assert(removeItem(id, 'ITPO_TEST', quantity))
      assert(setPlayerHealth(id, getPlayerHealth(id) + 7))
    end))
    local item = Item.getByInstance('ITPO_TEST')
    assert(type(item.onUse) == 'function')
    assert(not pcall(function() item.onUse = function() end end))
    assert(not pcall(function() item.instance = 'OTHER' end))
    assert(not ItemManager.setOnUse('UNKNOWN', function() end))
    assert(not ItemManager.setOnEquip('ITPO_TEST', 12))
  )", sol::script_pass_on_error).valid());
  ActorActionPacket use;
  use.actor_id = npc_id;
  use.actor_generation = npc.actor_generation;
  use.control_epoch = npc.control_epoch;
  use.action_id = 1;
  use.kind = ActorActionKind::ItemUse;
  use.item_instance = 99;
  use.quantity = 1;
  use.health_delta = 50;
  Send(70001, use);
  EXPECT_EQ(npc.health, 60);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 2);
  EXPECT_EQ(lua["callback_calls"].get<int>(), 0);
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerUseItemName, [&](std::any event) {
    EventManager::Instance().CancelCurrentEvent();
    const auto request = std::any_cast<OnPlayerUseItemEvent>(event);
    sol::protected_function callback = lua::bindings::LuaItem("ITPO_TEST").getOnUse(sol::this_state{lua.lua_state()});
    EXPECT_TRUE(callback(request.player_id, request.quantity, sol::nil).valid());
  }));
  use.action_id = 2;
  use.command_revision = npc.command_revision;
  Send(70001, use);
  EXPECT_EQ(npc.health, 67);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 1);
  EXPECT_EQ(lua["callback_calls"].get<int>(), 1);
}

TEST_F(ServerActorValidation, CancelledUseDoesNotCallRegisteredCallbackOrConsume) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  LuaScript script;
  auto& lua = script.GetLuaState();
  ASSERT_TRUE(lua.safe_script(R"(
    callback_calls = 0
    assert(ItemManager.setOnUse('ITPO_TEST', function() callback_calls = callback_calls + 1 end))
  )", sol::script_pass_on_error).valid());
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerUseItemName, [&](std::any) {
    EventManager::Instance().CancelCurrentEvent();
  }));
  ActorActionPacket use;
  use.actor_id = npc_id;
  use.actor_generation = npc.actor_generation;
  use.control_epoch = npc.control_epoch;
  use.action_id = 1;
  use.kind = ActorActionKind::ItemUse;
  use.item_instance = 99;
  use.quantity = 1;
  use.health_delta = 50;
  Send(70001, use);
  EXPECT_EQ(lua["callback_calls"].get<int>(), 0);
  EXPECT_EQ(npc.InventoryCount("ITPO_TEST"), 3);
}

TEST_F(ServerActorValidation, ItemCallbacksAreRemovedBeforeTheirLuaStateCloses) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  {
    LuaScript script;
    ASSERT_TRUE(script.GetLuaState().safe_script("assert(ItemManager.setOnUse('ITPO_TEST', function() return false end))",
                                                 sol::script_pass_on_error).valid());
    EXPECT_TRUE(lua::bindings::InvokeItemCallback("ITPO_TEST", lua::bindings::ItemCallback::Use, npc_id, 1).handled);
  }
  EXPECT_FALSE(lua::bindings::InvokeItemCallback("ITPO_TEST", lua::bindings::ItemCallback::Use, npc_id, 1).handled);
}

TEST_F(ServerActorValidation, ExplicitCancelledCastCallbackControlsMana) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  auto& victim = server.GetPlayerManager().GetPlayer(second)->get();
  LuaScript script;
  auto& lua = script.GetLuaState();
  lua["test_target_id"] = second;
  ASSERT_TRUE(lua.safe_script(R"(
    assert(ItemManager.setOnUse('ITRU_TEST', function(id, quantity, target)
      assert(quantity == 0 and target == test_target_id)
      setPlayerMana(id, getPlayerMana(id) - 12)
    end))
  )", sol::script_pass_on_error).valid());
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerCastSpellName, [&](std::any event) {
    EventManager::Instance().CancelCurrentEvent();
    const auto request = std::any_cast<OnPlayerCastSpellEvent>(event);
    sol::protected_function callback = lua::bindings::LuaItem("ITRU_TEST").getOnUse(sol::this_state{lua.lua_state()});
    EXPECT_TRUE(callback(request.caster_id, 0, request.target_id.value()).valid());
  }));
  CastSpellPacket cast{};
  cast.packet_type = Net::PT_CASTSPELLONTARGET;
  cast.caster_id = npc_id;
  cast.actor_generation = npc.actor_generation;
  cast.control_epoch = npc.control_epoch;
  cast.action_id = 1;
  cast.item_instance = 150;
  cast.spell_id = 19;
  cast.mana_delta = -5;
  cast.target_id = second;
  cast.target_generation = victim.actor_generation;
  Send(70001, cast);
  EXPECT_EQ(npc.mana, 88);
  EXPECT_EQ(npc.InventoryCount("ITRU_TEST"), 1);
  EXPECT_TRUE(npc.approved_projectiles.empty());
}

TEST_F(ServerActorValidation, NativeCastDeltaPreservesNewerManaCommandsAndConsecutiveCasts) {
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  CastSpellPacket cast{};
  cast.packet_type = Net::PT_CASTSPELL;
  cast.caster_id = npc_id;
  cast.actor_generation = npc.actor_generation;
  cast.control_epoch = npc.control_epoch;
  cast.command_revision = npc.command_revision;
  cast.item_instance = 150;
  cast.spell_id = 19;
  cast.mana_delta = -5;
  ASSERT_TRUE(server.SetPlayerMana(npc_id, 80));
  cast.action_id = 1;
  Send(70001, cast);
  EXPECT_EQ(npc.mana, 75);
  cast.action_id = 2; // Same captured command revision, next genuine native cast.
  Send(70001, cast);
  EXPECT_EQ(npc.mana, 70);
  EXPECT_EQ(npc.approved_projectiles.size(), 2u);
}

TEST_F(ServerActorValidation, CancelledNativeEquipmentRequestKeepsPreviousEquipment) {
  Write(R"({"items":[{"instance":"ITRI_TEST","index":152,"mainflag":1,"flags":2048}]})");
  ASSERT_TRUE(const_cast<ItemRegistry&>(server.GetItemRegistry()).Load(path));
  auto& actor = server.GetPlayerManager().GetPlayer(first)->get();
  ASSERT_TRUE(actor.AddInventory("ITRI_TEST", 1));
  int requests = 0;
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerEquipRingName, [&](std::any event) {
    const auto request = std::any_cast<OnPlayerEquipRingEvent>(event);
    EXPECT_EQ(request.item_index, 152);
    EXPECT_FALSE(request.previous_item_index);
    ++requests;
    EventManager::Instance().CancelCurrentEvent();
  }));
  PlayerStateUpdatePacket packet{};
  packet.packet_type = Net::PT_ACTUAL_STATISTICS;
  packet.player_id = first;
  packet.actor_generation = actor.actor_generation;
  packet.command_revision = actor.command_revision;
  packet.state_sequence = 1;
  packet.state = actor.state;
  packet.state.nrot = {0, 0, 1};
  packet.state.source_time_ms = 1000;
  packet.state.equipped_ring_left_instance = 152;
  Send(70001, packet);
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(actor.state.equipped_ring_left_instance, 0);
  EXPECT_EQ(actor.InventoryCount("ITRI_TEST"), 1);
}

TEST_F(ServerActorValidation, ExplicitEquipmentCallbacksCanAssignCancelledRequestsWithoutRecursion) {
  Write(R"({"items":[{"instance":"ITRI_TEST","index":152,"mainflag":1,"flags":2048}]})");
  ASSERT_TRUE(const_cast<ItemRegistry&>(server.GetItemRegistry()).Load(path));
  auto& npc = server.GetNpcManager().GetNpc(npc_id)->get();
  ASSERT_TRUE(npc.AddInventory("ITRI_TEST", 2));
  LuaScript script;
  auto& lua = script.GetLuaState();
  ASSERT_TRUE(lua.safe_script(R"(
    transitions = {}
    assert(ItemManager.setOnEquip('ITRI_TEST', function(id, slot)
      table.insert(transitions, 'equip:' .. slot)
      assert(equipItem(id, 'ITRI_TEST', slot))
    end))
    assert(ItemManager.setOnUnequip('ITRI_TEST', function(id, slot)
      table.insert(transitions, 'unequip:' .. slot)
      assert(unequipItem(id, 'ITRI_TEST', slot))
    end))
  )", sol::script_pass_on_error).valid());
  ASSERT_TRUE(server.EquipItem(npc_id, "ITRI_TEST", Net::EQUIP_SLOT_RING_LEFT));
  EXPECT_EQ(lua["transitions"].get<sol::table>().size(), 0u);
  ASSERT_TRUE(EventManager::Instance().SubscribeToEvent(kEventOnPlayerEquipRingName, [&](std::any event) {
    const auto request = std::any_cast<OnPlayerEquipRingEvent>(event);
    EventManager::Instance().CancelCurrentEvent();
    lua::bindings::LuaItem item("ITRI_TEST");
    sol::protected_function callback = request.item_index ? item.getOnEquip(sol::this_state{lua.lua_state()}) : item.getOnUnequip(sol::this_state{lua.lua_state()});
    EXPECT_TRUE(callback(request.player_id, request.ring_slot == 0 ? Net::EQUIP_SLOT_RING_LEFT : Net::EQUIP_SLOT_RING_RIGHT).valid());
  }));
  EXPECT_FALSE(server.EquipItem(npc_id, "ITRI_TEST", Net::EQUIP_SLOT_RING_RIGHT));
  EXPECT_EQ(npc.state.equipped_ring_right_instance, 152);
  EXPECT_FALSE(server.UnequipItem(npc_id, "ITRI_TEST", Net::EQUIP_SLOT_RING_LEFT));
  EXPECT_EQ(npc.state.equipped_ring_left_instance, 0);
  EXPECT_EQ(npc.state.equipped_ring_right_instance, 152);
  auto transitions = lua["transitions"].get<sol::table>();
  ASSERT_EQ(transitions.size(), 2u);
  EXPECT_EQ(transitions[1].get<std::string>(), "equip:-10");
  EXPECT_EQ(transitions[2].get<std::string>(), "unequip:-9");
}

TEST(ServerActorLuaBindings, FunctionsAndConstantsUseProductionBindings) {
  GameServer server;
  LuaScript script;
  auto& lua = script.GetLuaState();
  const auto id = server.GetPlayerManager().AddPlayer(70001, "LuaActor", 1);
  lua["test_actor_id"] = id;
  const auto definitions = lua.safe_script(R"(
    for _, name in ipairs({'setPlayerPosition', 'setPlayerInvulnerable', 'isPlayerInvulnerable',
                           'isPlayerInvulnerable'}) do
      assert(type(_G[name]) == 'function', name)
    end
    assert(setPlayerPosition(test_actor_id, 1, 2, 3))
    assert(setPlayerPosition(test_actor_id, 4, 5, 6, true))
    assert(setPlayerPosition(test_actor_id, 7, 8, 9, false))
    assert(EQUIP_SLOT_GENERIC == -1 and EQUIP_SLOT_MELEE_WEAPON == -3)
    assert(EQUIP_SLOT_RING_LEFT == -9 and EQUIP_SLOT_RING_RIGHT == -10)
    assert(setPlayerInvulnerable(test_actor_id, true))
    assert(isPlayerInvulnerable(test_actor_id))
    assert(setPlayerInvulnerable(test_actor_id, false))
    assert(not isPlayerInvulnerable(test_actor_id))
    assert(setPlayerRespawnTime(test_actor_id, 1))
    assert(getPlayerRespawnTime(test_actor_id) == 1)
    assert(not setPlayerRespawnTime(test_actor_id, -1))
    assert(setItemUseProfile == nil and setItemSpellProfile == nil)
  )", sol::script_pass_on_error);
  ASSERT_TRUE(definitions.valid());
  ASSERT_TRUE(server.GetPlayerPosition(id));
  EXPECT_EQ(*server.GetPlayerPosition(id), glm::vec3(7, 8, 9));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  const auto project = std::filesystem::current_path();
  const auto scratch = project / "build" / "actor-unit-tests";
  std::filesystem::create_directories(scratch);
  std::filesystem::current_path(scratch);
  const auto result = RUN_ALL_TESTS();
  std::filesystem::current_path(project);
  return result;
}
