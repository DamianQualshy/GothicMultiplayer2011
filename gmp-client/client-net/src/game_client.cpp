
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

#include "game_client.hpp"

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <spdlog/spdlog.h>

#include <cassert>
#include <dylib.hpp>
#include <sstream>

#include "net_enums.h"
#include "packets.h"
#include "znet_client.h"

namespace gmp::client {

using namespace Net;

static Net::NetClient* g_netclient = nullptr;

template <typename TContainer = std::vector<std::uint8_t>, typename Packet>
static void SerializeAndSend(const Packet& packet, Net::PacketPriority priority, Net::PacketReliability reliable) {
  TContainer buffer;
  auto written_size = bitsery::quickSerialization<bitsery::OutputBufferAdapter<TContainer>>(buffer, packet);
  g_netclient->SendPacket(buffer.data(), written_size, reliable, priority);
}

GameClient::GameClient(EventObserver& eventObserver) : event_observer_(eventObserver) {
  assert(g_netclient != nullptr);
  InitPacketHandlers();
  g_netclient->AddPacketHandler(*this);
}

GameClient::~GameClient() {
  assert(g_netclient != nullptr);
  g_netclient->RemovePacketHandler(*this);
}

void GameClient::InitPacketHandlers() {
  packet_handlers_[PT_INITIAL_INFO] = [this](Packet p) { OnInitialInfo(p); };
  packet_handlers_[PT_ACTUAL_STATISTICS] = [this](Packet p) { OnActualStatistics(p); };
  packet_handlers_[PT_MAP_ONLY] = [this](Packet p) { OnMapOnly(p); };
  packet_handlers_[PT_DODIE] = [this](Packet p) { OnDoDie(p); };
  packet_handlers_[PT_RESPAWN] = [this](Packet p) { OnRespawn(p); };
  packet_handlers_[PT_CASTSPELL] = [this](Packet p) { OnCastSpell(p); };
  packet_handlers_[PT_CASTSPELLONTARGET] = [this](Packet p) { OnCastSpellOnTarget(p); };
  packet_handlers_[PT_DROPITEM] = [this](Packet p) { OnDropItem(p); };
  packet_handlers_[PT_TAKEITEM] = [this](Packet p) { OnTakeItem(p); };
  packet_handlers_[PT_WHISPER] = [this](Packet p) { OnWhisper(p); };
  packet_handlers_[PT_MSG] = [this](Packet p) { OnMessage(p); };
  packet_handlers_[PT_SRVMSG] = [this](Packet p) { OnServerMessage(p); };
  packet_handlers_[PT_COMMAND] = [this](Packet p) { OnRcon(p); };
  packet_handlers_[PT_EXISTING_PLAYERS] = [this](Packet p) { OnExistingPlayers(p); };
  packet_handlers_[PT_JOIN_GAME] = [this](Packet p) { OnJoinGame(p); };
  packet_handlers_[PT_GAME_INFO] = [this](Packet p) { OnGameInfo(p); };
  packet_handlers_[PT_LEFT_GAME] = [this](Packet p) { OnLeftGame(p); };
  packet_handlers_[PT_DISCORD_ACTIVITY] = [this](Packet p) { OnDiscordActivity(p); };
  packet_handlers_[Net::ID_DISCONNECTION_NOTIFICATION] = [this](Packet p) { OnDisconnectOrLostConnection(p); };
  packet_handlers_[Net::ID_CONNECTION_LOST] = [this](Packet p) { OnDisconnectOrLostConnection(p); };
}

bool GameClient::Connect(std::string_view full_address) {
  // Extract port number from IP address if present
  std::string host(full_address);
  int port = 0xDEAD;
  size_t pos = host.find_last_of(':');
  if (pos != std::string::npos) {
    std::string portStr = host.substr(pos + 1);
    std::istringstream iss(portStr);
    iss >> port;
    host.erase(pos);
  }

  if (!g_netclient->Connect(host.c_str(), port)) {
    return false;
  }

  server_ip_ = host;
  server_port_ = port;
  return true;
}

void GameClient::Disconnect() {
  if (IsConnected()) {
    is_in_game_ = false;
    g_netclient->Disconnect();
    event_observer_.OnDisconnected();
  }
}

bool GameClient::IsConnected() const {
  return !connection_lost_ && g_netclient->IsConnected();
}

int GameClient::GetPing() {
  return g_netclient->GetPing();
}

void GameClient::HandleNetwork() {
  if (IsConnected()) {
    g_netclient->Pulse();
  }
}

bool GameClient::HandlePacket(unsigned char* data, std::uint32_t size) {
  try {
    SPDLOG_TRACE("Received packet: {}", (int)data[0]);
    auto it = packet_handlers_.find((int)data[0]);
    if (it != packet_handlers_.end()) {
      it->second(Packet{data, size});
    } else {
      SPDLOG_WARN("No handler for packet type: {}", (int)data[0]);
    }
  } catch (std::exception& ex) {
    SPDLOG_ERROR("Exception thrown while handling packet: {}", ex.what());
  }
  return true;
}

// ============================================================================
// Send Methods
// ============================================================================

void GameClient::UpdatePlayerState(Player* player, const PlayerState& state) {
  if (!player) return;
  
  player->set_position(state.position.x, state.position.y, state.position.z);
  player->set_rotation(glm::vec3(state.nrot.x, state.nrot.y, state.nrot.z));
  player->set_left_hand_item(state.left_hand_item_instance);
  player->set_right_hand_item(state.right_hand_item_instance);
  player->set_equipped_armor(state.equipped_armor_instance);
  player->set_animation(state.animation);
  player->set_hp(state.health_points);
  player->set_mana(state.mana_points);
  player->set_weapon_mode(state.weapon_mode);
  player->set_active_spell(state.active_spell_nr);
  player->set_head_direction(state.head_direction);
  player->set_melee_weapon(state.melee_weapon_instance);
  player->set_ranged_weapon(state.ranged_weapon_instance);
}

void GameClient::JoinGame(const std::string& player_name, const std::string& character_name, int head_model, int skin_texture,
                          int face_texture, int walk_style) {
  JoinGamePacket packet;
  packet.packet_type = PT_JOIN_GAME;
  // Position, rotation, and items will need to be filled by the caller or from the local player state
  packet.head_model = head_model;
  packet.skin_texture = skin_texture;
  packet.face_texture = face_texture;
  packet.walk_style = walk_style;
  packet.player_name = player_name;

  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED);
}

void GameClient::SendChatMessage(const std::string& msg) {
  MessagePacket packet;
  packet.packet_type = PT_MSG;
  packet.message = msg;
  SerializeAndSend(packet, MEDIUM_PRIORITY, RELIABLE);
}

void GameClient::SendWhisper(std::uint64_t recipient_id, const std::string& msg) {
  MessagePacket packet;
  packet.packet_type = PT_WHISPER;
  packet.message = msg;
  packet.recipient = recipient_id;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED);
}

void GameClient::SendCommand(const std::string& msg) {
  MessagePacket packet;
  packet.packet_type = PT_COMMAND;
  packet.message = msg;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE_ORDERED);
}

void GameClient::SendCastSpell(std::uint64_t target_id, std::uint16_t spell_id) {
  CastSpellPacket packet;
  packet.spell_id = spell_id;
  packet.packet_type = target_id ? PT_CASTSPELLONTARGET : PT_CASTSPELL;
  if (target_id) {
    packet.target_id = target_id;
  }
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE);
}

void GameClient::SendDropItem(std::uint16_t instance, std::uint16_t amount) {
  DropItemPacket packet;
  packet.packet_type = PT_DROPITEM;
  packet.item_instance = instance;
  packet.item_amount = amount;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE);
}

void GameClient::SendTakeItem(std::uint16_t instance) {
  TakeItemPacket packet;
  packet.packet_type = PT_TAKEITEM;
  packet.item_instance = instance;
  SerializeAndSend(packet, HIGH_PRIORITY, RELIABLE);
}

void GameClient::UpdatePlayerStats(const PlayerState& state) {
  PlayerStateUpdatePacket packet;
  packet.packet_type = PT_ACTUAL_STATISTICS;
  packet.state = state;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE_ORDERED);
}

void GameClient::SendHPDiff(std::uint64_t player_id, std::int16_t diff) {
  HPDiffPacket packet;
  packet.packet_type = PT_HP_DIFF;
  packet.player_id = player_id;
  packet.hp_difference = diff;
  SerializeAndSend(packet, IMMEDIATE_PRIORITY, RELIABLE);
}

void GameClient::SyncGameTime() {
  std::uint8_t data[2] = {PT_GAME_INFO, 0};
  g_netclient->SendPacket(data, 1, RELIABLE, IMMEDIATE_PRIORITY);
}

// ============================================================================
// Packet Handlers
// ============================================================================

void GameClient::OnInitialInfo(Packet p) {
  InitialInfoPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!state.second) {
    SPDLOG_ERROR("Failed to deserialize InitialInfoPacket, error code: {}", static_cast<int>(state.first));
    return;
  }

  auto local_player = player_manager_.CreateLocalPlayer(packet.player_id);
  worlds_.clear();
  worlds_.emplace_back(packet.map_name);
  
  event_observer_.OnMapChange(packet.map_name);
  event_observer_.OnLocalPlayerJoined(*local_player);
}

void GameClient::OnActualStatistics(Packet p) {
  PlayerStateUpdatePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!state.second) {
    SPDLOG_ERROR("Failed to deserialize PlayerStateUpdatePacket");
    return;
  }

  SPDLOG_TRACE("PlayerStateUpdatePacket: {}", packet);

  if (!packet.player_id) {
    SPDLOG_ERROR("PlayerStateUpdatePacket: Player id is null");
    return;
  }

  // Update the Player object with new state
  Player* player = player_manager_.GetPlayer(*packet.player_id);
  if (player) {
    UpdatePlayerState(player, packet.state);
  }

  event_observer_.OnPlayerStateUpdate(*packet.player_id, packet.state);
}

void GameClient::OnMapOnly(Packet p) {
  PlayerPositionUpdatePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.player_id) {
    SPDLOG_ERROR("PlayerPositionUpdatePacket: Player id is null");
    return;
  }

  event_observer_.OnPlayerPositionUpdate(*packet.player_id, packet.position.x, packet.position.z);
}

void GameClient::OnDoDie(Packet p) {
  PlayerDeathInfoPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  event_observer_.OnPlayerDied(packet.player_id);
}

void GameClient::OnRespawn(Packet p) {
  PlayerRespawnInfoPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  event_observer_.OnPlayerRespawned(packet.player_id);
}

void GameClient::OnCastSpell(Packet p) {
  CastSpellPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.caster_id) {
    SPDLOG_ERROR("CastSpellPacket Caster ID is null");
    return;
  }

  event_observer_.OnSpellCast(*packet.caster_id, packet.spell_id);
}

void GameClient::OnCastSpellOnTarget(Packet p) {
  CastSpellPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.caster_id || !packet.target_id) {
    SPDLOG_ERROR("Invalid CastSpellOnTarget packet. No caster or target id.");
    return;
  }

  event_observer_.OnSpellCastOnTarget(*packet.caster_id, *packet.target_id, packet.spell_id);
}

void GameClient::OnDropItem(Packet p) {
  DropItemPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.player_id) {
    SPDLOG_ERROR("Invalid DropItem packet. No player id.");
    return;
  }

  event_observer_.OnItemDropped(*packet.player_id, packet.item_instance, packet.item_amount);
}

void GameClient::OnTakeItem(Packet p) {
  TakeItemPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.player_id) {
    SPDLOG_ERROR("Invalid TakeItem packet. No player id.");
    return;
  }

  event_observer_.OnItemTaken(*packet.player_id, packet.item_instance);
}

void GameClient::OnWhisper(Packet p) {
  MessagePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.sender) {
    SPDLOG_ERROR("Invalid Message packet. No sender id.");
    return;
  }

  Player* sender = player_manager_.GetPlayer(*packet.sender);
  std::string sender_name = sender ? sender->name() : "";
  event_observer_.OnWhisperReceived(*packet.sender, sender_name, packet.message);
}

void GameClient::OnMessage(Packet p) {
  MessagePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.sender) {
    SPDLOG_ERROR("Invalid Message packet. No sender id.");
    return;
  }

  Player* sender = player_manager_.GetPlayer(*packet.sender);
  std::string sender_name = sender ? sender->name() : "";
  SPDLOG_INFO("Message from player {} ({}): {}", sender_name, *packet.sender, packet.message);
  
  event_observer_.OnChatMessage(*packet.sender, sender_name, packet.message);
}

void GameClient::OnServerMessage(Packet p) {
  MessagePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  event_observer_.OnServerMessage(packet.message);
}

void GameClient::OnRcon(Packet p) {
  bool is_admin = (p.data[1] == 0x41);
  std::string response((char*)p.data + 1);
  event_observer_.OnRconResponse(response, is_admin);
}

void GameClient::OnExistingPlayers(Packet p) {
  ExistingPlayersPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  for (const auto& existing_player : packet.existing_players) {
    SPDLOG_INFO("ExistingPlayerPacket packet: {}", existing_player);

    // Create Player object and populate it
    Player* player = player_manager_.CreatePlayer(existing_player.player_id);
    player->set_name(existing_player.player_name);
    player->set_position(existing_player.position.x, existing_player.position.y, existing_player.position.z);
    player->set_left_hand_item(existing_player.left_hand_item_instance);
    player->set_right_hand_item(existing_player.right_hand_item_instance);
    player->set_equipped_armor(existing_player.equipped_armor_instance);
    player->set_selected_class(existing_player.selected_class);
    player->set_head_model(existing_player.head_model);
    player->set_skin_texture(existing_player.skin_texture);
    player->set_face_texture(existing_player.face_texture);
    player->set_walk_style(existing_player.walk_style);

    event_observer_.OnPlayerJoined(*player);
  }
}

void GameClient::OnJoinGame(Packet p) {
  JoinGamePacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  if (!packet.player_id) {
    SPDLOG_ERROR("Invalid JoinGame packet. No player id.");
    return;
  }

  SPDLOG_INFO("JoinGame packet: {}", packet);

  // Create Player object and populate it
  Player* player = player_manager_.CreatePlayer(*packet.player_id);
  player->set_name(packet.player_name);
  player->set_position(packet.position.x, packet.position.y, packet.position.z);
  player->set_left_hand_item(packet.left_hand_item_instance);
  player->set_right_hand_item(packet.right_hand_item_instance);
  player->set_equipped_armor(packet.equipped_armor_instance);
  player->set_selected_class(packet.selected_class);
  player->set_head_model(packet.head_model);
  player->set_skin_texture(packet.skin_texture);
  player->set_face_texture(packet.face_texture);
  player->set_walk_style(packet.walk_style);

  event_observer_.OnPlayerJoined(*player);
}

void GameClient::OnGameInfo(Packet p) {
  GameInfoPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  event_observer_.OnGameInfoReceived(packet.raw_game_time, packet.flags);
}

void GameClient::OnLeftGame(Packet p) {
  DisconnectionInfoPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  // Get player name before removing
  Player* player = player_manager_.GetPlayer(packet.disconnected_id);
  std::string player_name = player ? player->name() : "";
  
  event_observer_.OnPlayerLeft(packet.disconnected_id, player_name);
  
  // Remove from player manager
  player_manager_.RemovePlayer(packet.disconnected_id);
}

void GameClient::OnDiscordActivity(Packet p) {
  DiscordActivityPacket packet;
  using InputAdapter = bitsery::InputBufferAdapter<unsigned char*>;
  auto state = bitsery::quickDeserialization<InputAdapter>({p.data, p.length}, packet);

  SPDLOG_DEBUG("DiscordActivityPacket: {}", packet);

  event_observer_.OnDiscordActivityUpdate(packet.state, packet.details, packet.large_image_key, packet.large_image_text,
                                         packet.small_image_key, packet.small_image_text);
}

void GameClient::OnDisconnectOrLostConnection(Packet p) {
  SPDLOG_WARN("OnDisconnectOrLostConnection, code: {}", p.data[0]);
  connection_lost_ = true;
  is_in_game_ = false;
  event_observer_.OnConnectionLost();
}

void LoadNetworkLibrary() {
  try {
    static dylib lib("znet");
    auto create_net_client_func = lib.get_function<Net::NetClient*()>("CreateNetClient");
    g_netclient = create_net_client_func();
  } catch (std::exception& ex) {
    SPDLOG_ERROR("LoadNetworkLibrary error: {}", ex.what());
    // If loading the network library fails, then GMP will not work.
    std::abort();
  }
  SPDLOG_DEBUG("znet dynamic library loaded: {}", (void*)g_netclient);
}

}  // namespace gmp::client