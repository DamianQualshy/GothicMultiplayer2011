#include <gtest/gtest.h>

#include "player_manager.h"

TEST(PlayerManagerConnectionTest, KeepsSessionIdSeparateFromTransportHandle) {
  PlayerManager players;
  const auto id = players.AddPlayer(70001, "first", 2);
  EXPECT_EQ(id, 1u);
  ASSERT_TRUE(players.GetPlayerId(70001));
  EXPECT_EQ(*players.GetPlayerId(70001), id);
  ASSERT_TRUE(players.GetConnectionHandle(id));
  EXPECT_EQ(*players.GetConnectionHandle(id), 70001u);
}

TEST(PlayerManagerConnectionTest, FullServerDoesNotCreatePlayerZeroOrOverwriteExistingPlayer) {
  PlayerManager players;
  const auto first = players.AddPlayer(70001, "first", 1);
  ASSERT_NE(first, 0u);
  EXPECT_EQ(players.AddPlayer(70002, "second", 1), 0u);
  EXPECT_EQ(players.GetPlayerCount(), 1u);
  EXPECT_FALSE(players.HasPlayer(0));
  EXPECT_FALSE(players.HasConnection(70002));
  ASSERT_TRUE(players.GetPlayer(first));
  EXPECT_EQ(players.GetPlayer(first)->get().name, "first");
}

TEST(PlayerManagerConnectionTest, RejectsInvalidAndDuplicateConnectionsWithoutAllocatingIds) {
  PlayerManager players;
  EXPECT_EQ(players.AddPlayer(Net::kInvalidConnection, "invalid", 2), 0u);
  const auto first = players.AddPlayer(70001, "first", 2);
  EXPECT_EQ(players.AddPlayer(70001, "duplicate", 2), 0u);
  EXPECT_EQ(players.GetPlayerCount(), 1u);
  ASSERT_TRUE(players.GetPlayer(first));
  EXPECT_EQ(players.GetPlayer(first)->get().name, "first");
}

TEST(PlayerManagerConnectionTest, DisconnectAndReconnectReplacesOnlyTransportMapping) {
  PlayerManager players;
  const auto first = players.AddPlayer(70001, "first", 1);
  ASSERT_TRUE(players.RemovePlayerByConnection(70001));
  EXPECT_FALSE(players.GetPlayerId(70001));
  EXPECT_FALSE(players.GetConnectionHandle(first));
  const auto reconnected = players.AddPlayer(80001, "reconnected", 1);
  EXPECT_NE(reconnected, 0u);
  EXPECT_FALSE(players.HasConnection(70001));
  ASSERT_TRUE(players.GetConnectionHandle(reconnected));
  EXPECT_EQ(*players.GetConnectionHandle(reconnected), 80001u);
}

TEST(PlayerManagerStateSequenceTest, RejectsZeroDuplicateAndOlderClientSnapshots) {
  PlayerManager::Player player;
  EXPECT_FALSE(player.AcceptClientStateSequence(0));
  EXPECT_TRUE(player.AcceptClientStateSequence(10));
  EXPECT_FALSE(player.AcceptClientStateSequence(10));
  EXPECT_FALSE(player.AcceptClientStateSequence(9));
  EXPECT_TRUE(player.AcceptClientStateSequence(11));
}

TEST(PlayerManagerStateSequenceTest, AcceptsCounterWrapAndRejectsOldAndAmbiguousCounters) {
  PlayerManager::Player player;
  EXPECT_TRUE(player.AcceptClientStateSequence(0xfffffffeu));
  EXPECT_TRUE(player.AcceptClientStateSequence(0xffffffffu));
  EXPECT_TRUE(player.AcceptClientStateSequence(1));
  EXPECT_FALSE(player.AcceptClientStateSequence(0xffffffffu));
  EXPECT_FALSE(player.AcceptClientStateSequence(0x80000001u));
  EXPECT_TRUE(player.AcceptClientStateSequence(2));
}

TEST(PlayerManagerStateSequenceTest, ReusedPlayerSlotStartsNewClientSequenceHistory) {
  PlayerManager players;
  const auto old_id = players.AddPlayer(70001, "first", 1);
  ASSERT_NE(old_id, 0u);
  ASSERT_TRUE(players.GetPlayer(old_id)->get().AcceptClientStateSequence(100));
  ASSERT_TRUE(players.RemovePlayerByConnection(70001));
  const auto new_id = players.AddPlayer(80001, "reconnected", 1);
  ASSERT_EQ(new_id, old_id);
  EXPECT_TRUE(players.GetPlayer(new_id)->get().AcceptClientStateSequence(1));
  EXPECT_FALSE(players.GetPlayer(new_id)->get().AcceptClientStateSequence(1));
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
