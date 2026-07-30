
/*
MIT License

Copyright (c) 2022 Gothic Multiplayer Team.

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

#include "client.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

#include "MessageIdentifiers.h"
#include "RakNetTypes.h"
#include "RakNetStatistics.h"

using namespace RakNet;

#define RAKNET_PASSWORD "YOUR_PASS"

namespace Net {

namespace {
::PacketPriority ToRakNetPacketPriority(Net::PacketPriority packetPriority) {
  switch (packetPriority) {
    case IMMEDIATE_PRIORITY:
      return ::IMMEDIATE_PRIORITY;
    case HIGH_PRIORITY:
      return ::HIGH_PRIORITY;
    case MEDIUM_PRIORITY:
      return ::MEDIUM_PRIORITY;
    case LOW_PRIORITY:
      return ::LOW_PRIORITY;
  }
  return ::MEDIUM_PRIORITY;
}

::PacketReliability ToRakNetPacketReliability(Net::PacketReliability packetPriority) {
  switch (packetPriority) {
    case RELIABLE:
      return ::RELIABLE;
    case RELIABLE_ORDERED:
      return ::RELIABLE_ORDERED;
    case UNRELIABLE_SEQUENCED:
      return ::UNRELIABLE_SEQUENCED;
    case UNRELIABLE:
      return ::UNRELIABLE;
  }
  return ::RELIABLE;
}

bool ShouldClosePeerAfterDispatch(MessageID message) {
  switch (message) {
    case ID_CONNECTION_ATTEMPT_FAILED:
    case ID_ALREADY_CONNECTED:
    case ID_NO_FREE_INCOMING_CONNECTIONS:
    case ID_DISCONNECTION_NOTIFICATION:
    case ID_CONNECTION_LOST:
    case ID_CONNECTION_BANNED:
    case ID_INVALID_PASSWORD:
    case ID_INCOMPATIBLE_PROTOCOL_VERSION:
    case ID_IP_RECENTLY_CONNECTED:
      return true;
    default:
      return false;
  }
}
}  // namespace

bool RakNetClient::Connect(const char* address, std::uint32_t port) {
  if (peer_) {
    Disconnect();
  }

  packetReceived_ = 0;
  peer_ = RakPeerInterface::GetInstance();
  if (!peer_) {
    return false;
  }

  peer_->SetTimeoutTime(1500, UNASSIGNED_SYSTEM_ADDRESS);
  SocketDescriptor socketDescriptor(0, 0);
  socketDescriptor.socketFamily = AF_INET;
  if (peer_->Startup(1, &socketDescriptor, 1) != RAKNET_STARTED) {
    Disconnect();
    return false;
  }

  peer_->SetOccasionalPing(true);
  std::string hostPassword = RAKNET_PASSWORD;
  ConnectionAttemptResult connectionAttemptResult =
      peer_->Connect(address, port, hostPassword.c_str(), hostPassword.size());
  if (connectionAttemptResult != CONNECTION_ATTEMPT_STARTED) {
    Disconnect();
    return false;
  }

  return true;
}

void RakNetClient::Disconnect() {
  if (!peer_) {
    return;
  }

  isConnected_ = false;
  peer_->Shutdown(300, 0, ::IMMEDIATE_PRIORITY);
  RakNet::RakPeerInterface::DestroyInstance(peer_);
  peer_ = nullptr;
}

bool RakNetClient::IsConnected() const {
  return isConnected_;
}

bool RakNetClient::SendPacket(unsigned char* data, std::uint32_t size, PacketReliability packetReliability,
                              PacketPriority packetPriority, std::uint32_t channel) {
  if (!peer_ || !isConnected_) {
    return false;
  }

  // TODO: VALIDATION AND ENCRYPTION.
  const auto ordering_channel = static_cast<char>(std::min<std::uint32_t>(channel, 31));
  peer_->Send(reinterpret_cast<const char*>(data), size, ToRakNetPacketPriority(packetPriority),
              ToRakNetPacketReliability(packetReliability), ordering_channel, serverAddress_, false);
  return true;
}

void RakNetClient::Pulse() {
  if (!peer_) {
    return;
  }

  RakNet::Packet* packet = peer_->Receive();
  while (packet) {
    const MessageID message = packet->data[0];
    const bool close_after_dispatch = ShouldClosePeerAfterDispatch(message);

    if (message == ID_CONNECTION_REQUEST_ACCEPTED) {
      serverAddress_ = packet->systemAddress;
      isConnected_ = true;
    } else if (close_after_dispatch) {
      isConnected_ = false;
    }

    ++packetReceived_;
    std::for_each(packetHandlers_.begin(), packetHandlers_.end(),
                  [packet](auto& handler) { handler->HandlePacket(packet->data, packet->length); });

    peer_->DeallocatePacket(packet);

    if (close_after_dispatch) {
      Disconnect();
      break;
    }

    packet = peer_->Receive();
  }
}

void RakNetClient::AddPacketHandler(PacketHandler& packetHandler) {
  packetHandlers_.insert(&packetHandler);
}

void RakNetClient::RemovePacketHandler(PacketHandler& packetHandler) {
  packetHandlers_.erase(&packetHandler);
}

std::uint32_t RakNetClient::GetPing() const {
  if (!peer_ || !isConnected_) {
    return 0;
  }

  return peer_->GetAveragePing(serverAddress_);
}

NetworkStats RakNetClient::GetNetworkStats() const {
  NetworkStats stats;
  stats.packetReceived = packetReceived_;

  if (!peer_ || !isConnected_) {
    return stats;
  }

  RakNet::RakNetStatistics rak_stats;
  if (!peer_->GetStatistics(0, &rak_stats)) {
    return stats;
  }

  stats.packetlossTotal = rak_stats.packetlossTotal;
  stats.packetlossLastSecond = rak_stats.packetlossLastSecond;
  stats.messagesInResendBuffer = rak_stats.messagesInResendBuffer;
  stats.messageInSendBuffer = rak_stats.messageInSendBuffer[::LOW_PRIORITY] + rak_stats.messageInSendBuffer[::MEDIUM_PRIORITY] +
                              rak_stats.messageInSendBuffer[::HIGH_PRIORITY];
  stats.bytesInResendBuffer = rak_stats.bytesInResendBuffer;
  stats.bytesInSendBuffer = static_cast<std::uint64_t>(rak_stats.bytesInSendBuffer[::LOW_PRIORITY] +
                                                       rak_stats.bytesInSendBuffer[::MEDIUM_PRIORITY] +
                                                       rak_stats.bytesInSendBuffer[::HIGH_PRIORITY]);
  return stats;
}

}  // namespace Net

Net::NetClient* CreateNetClient() {
  return new Net::RakNetClient;
}
