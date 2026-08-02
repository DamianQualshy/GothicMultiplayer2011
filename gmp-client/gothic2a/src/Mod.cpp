
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

#include "Mod.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "CActiveAniID.h"
#include "CIngame.h"
#include "CServerList.h"
#include "ExceptionHandler.h"
#include "HooksManager.h"
#include "Interface.h"
#include "dev/dev_tools.h"
#include "ZenGin/zGothicAPI.h"
#include "audio/gothic_music_bridge.h"
#include "config.h"
#include "gmp_core.h"
#include "hooking/AsmPatch.h"
#include "hooking/MemoryPatch.h"
#include "scripting/item_ground.h"
#include "language.h"
#include "main_menu.h"
#include "net_game.h"
#include "patch.h"
#include "scripting/gothic_events.h"
#include "shared/event.h"

#pragma warning(disable : 4996)

using namespace Gothic_II_Addon;

// Legacy global - maintained for compatibility, points to GMPCore-owned instance
// TODO: migrate usages to GMPCore::Instance().GetMainMenu()
CMainMenu* MainMenu = NULL;
extern zCOLOR Red;
extern zCOLOR Normal;
extern CIngame* global_ingame;
zCOLOR Green = zCOLOR(0, 255, 0);
bool MultiplayerLaunched = false;

namespace {

constexpr DWORD kCastSpellHookAddress = 0x00485360;
// Hook DoDropVob, not EV_DropVob, so the dropped item has a world transform.
constexpr DWORD kDropItemHookAddress = 0x00744DD0;
constexpr DWORD kTakeItemHookAddress = 0x007534E0;
constexpr DWORD kUseItemHookAddress = 0x00755620;
constexpr DWORD kUseItemToStateHookAddress = 0x007558F0;
constexpr DWORD kEquipItemHookAddress = 0x007323C0;
constexpr DWORD kUnequipItemHookAddress = 0x007326C0;
constexpr DWORD kDoDieHookAddress = 0x00736760;
constexpr DWORD kDropUnconsciousHookAddress = 0x00735EB0;
constexpr DWORD kStandUpHookAddress = 0x00682B40;
constexpr DWORD kCallOnStateFuncHookAddress = 0x00720870;
constexpr DWORD kAINormalHookAddress = 0x004A4370;
constexpr DWORD kOnDamageAnimHookAddress = 0x00675BD0;
constexpr DWORD kOnDamageHitHookAddress = 0x00666610;
constexpr DWORD kCreateArrowTrailHookAddress = 0x006A0420;

using CastSpellOriginalFn = int(__thiscall*)(oCSpell*);
using DropItemOriginalFn = int(__thiscall*)(oCNpc*, zCVob*);
using TakeItemOriginalFn = int(__thiscall*)(oCNpc*, oCMsgManipulate*);
using UseItemOriginalFn = int(__thiscall*)(oCNpc*, oCMsgManipulate*);
using UseItemToStateOriginalFn = int(__thiscall*)(oCNpc*, oCMsgManipulate*);
using EquipItemOriginalFn = void(__thiscall*)(oCNpc*, oCItem*);
using UnequipItemOriginalFn = void(__thiscall*)(oCNpc*, oCItem*);
using DoDieOriginalFn = void(__thiscall*)(oCNpc*, oCNpc*);
using DropUnconsciousOriginalFn = void(__thiscall*)(oCNpc*, float, oCNpc*);
using StandUpOriginalFn = void(__thiscall*)(oCNpc*, int, int);
using CallOnStateFuncOriginalFn = void(__thiscall*)(oCMobInter*, oCNpc*, int);
using AINormalOriginalFn = void(__thiscall*)(zCAICamera*);
using OnDamageAnimOriginalFn = void(__thiscall*)(oCNpc*, oCNpc::oSDamageDescriptor&);
using OnDamageHitOriginalFn = void(__thiscall*)(oCNpc*, oCNpc::oSDamageDescriptor&);
using CreateArrowTrailOriginalFn = void(__thiscall*)(oCAIArrowBase*, zCVob*);

CastSpellOriginalFn g_originalCastSpell = nullptr;
DropItemOriginalFn g_originalDropItem = nullptr;
TakeItemOriginalFn g_originalTakeItem = nullptr;
UseItemOriginalFn g_originalUseItem = nullptr;
UseItemToStateOriginalFn g_originalUseItemToState = nullptr;
EquipItemOriginalFn g_originalEquipItem = nullptr;
UnequipItemOriginalFn g_originalUnequipItem = nullptr;
DoDieOriginalFn g_originalDoDie = nullptr;
DropUnconsciousOriginalFn g_originalDropUnconscious = nullptr;
StandUpOriginalFn g_originalStandUp = nullptr;
CallOnStateFuncOriginalFn g_originalCallOnStateFunc = nullptr;
AINormalOriginalFn g_originalAINormal = nullptr;
OnDamageAnimOriginalFn g_originalOnDamageAnim = nullptr;
OnDamageHitOriginalFn g_originalOnDamageHit = nullptr;
CreateArrowTrailOriginalFn g_originalCreateArrowTrail = nullptr;
bool g_damageAnimationsEnabled = false;
bool g_munitionTrailEnabled = true;

int GetInventoryAmount(oCNpc* npc, int instance_id) {
  if (npc == nullptr || instance_id <= 0) {
    return 0;
  }

  oCItem* item = npc->inventory2.IsIn(instance_id, 1);
  return item ? std::max(0, item->amount) : 0;
}

}  // namespace

void SetDamageAnimationsEnabled(bool enabled) {
  g_damageAnimationsEnabled = enabled;
}

bool AreDamageAnimationsEnabled() {
  return g_damageAnimationsEnabled;
}

void SetMunitionTrailEnabled(bool enabled) {
  g_munitionTrailEnabled = enabled;
}

bool IsMunitionTrailEnabled() {
  return g_munitionTrailEnabled;
}

// Hook: zCAICamera::AI_Normal - crashfix for null camVob pointer
// The original code dereferences camVob without null check, causing crashes
void __fastcall OnAINormal(zCAICamera* thisCamera, void* /*edx*/) {
  // Check if camVob is valid before calling original
  // AI_Normal dereferences camVob multiple times without null checks
  if (!thisCamera->camVob) {
    return;  // Skip if null - prevents crash
  }
  if (g_originalAINormal) {
    g_originalAINormal(thisCamera);
  }
}

std::string GetItemInstanceNameByIndex(int index) {
  if (index <= 0) {
    return {};
  }
  zCParser* parser = zCParser::GetParser();
  if (!parser) {
    return {};
  }
  zCPar_Symbol* symbol = parser->GetSymbol(index);
  if (!symbol) {
    return {};
  }
  return symbol->name.ToChar();
}

std::string ResolveItemInstanceName(oCItem* item) {
  if (!item) {
    return {};
  }
  return GetItemInstanceNameByIndex(item->GetInstance());
}

std::string ResolveItemInstanceName(oCMsgManipulate* msg) {
  if (!msg) {
    return {};
  }
  if (auto* item = zDYNAMIC_CAST<oCItem>(msg->targetVob)) {
    return ResolveItemInstanceName(item);
  }
  if (!msg->name.IsEmpty()) {
    return msg->name.ToChar();
  }
  return {};
}

std::string ResolveItemSchemeName(oCMsgManipulate* msg) {
  if (!msg || msg->name.IsEmpty()) {
    return {};
  }
  return msg->name.ToChar();
}

glm::vec3 ToGlmVec3(const zVEC3& vec) {
  return glm::vec3(vec[VX], vec[VY], vec[VZ]);
}

bool IsZeroVector(const glm::vec3& vec) {
  return std::abs(vec.x) < 0.001f && std::abs(vec.y) < 0.001f && std::abs(vec.z) < 0.001f;
}

glm::vec3 GetItemPosition(oCItem* item) {
  if (!item) {
    return glm::vec3{0.0f};
  }

  return ToGlmVec3(item->GetNewTrafoObjToWorld().GetTranslation());
}

glm::vec3 GetItemRotation(oCItem* item) {
  if (!item) {
    return glm::vec3{0.0f};
  }

  const zVEC3 euler = item->GetNewTrafoObjToWorld().GetEulerAngles();
  return glm::vec3(-euler[VX] * DEGREE, -euler[VY] * DEGREE, -euler[VZ] * DEGREE);
}

glm::vec3 GetDropFallbackPosition(oCNpc* npc) {
  if (!npc) {
    return glm::vec3{0.0f};
  }

  glm::vec3 position = ToGlmVec3(npc->GetTrafoModelNodeToWorld("ZS_RIGHTHAND").GetTranslation());
  if (IsZeroVector(position)) {
    position = ToGlmVec3(npc->GetPositionWorld());
  }
  return position;
}

std::unordered_set<std::string> s_local_equipped_items;
std::unordered_map<oCNpc*, const oCMsgManipulate*> s_last_take_msg;

struct LastUseState {
  const oCMsgManipulate* msg = nullptr;
  std::string item;
  std::string scheme;
  DWORD tick = 0;
};

std::unordered_map<oCNpc*, LastUseState> s_last_use_state;
bool s_suppress_local_equip_events = false;
bool s_suppress_local_lifecycle_events = false;

void SetSuppressLocalEquipEvents(bool suppress) {
  s_suppress_local_equip_events = suppress;
}

bool ShouldSuppressLocalEquipEvents() {
  return s_suppress_local_equip_events;
}

void SetSuppressLocalLifecycleEvents(bool suppress) {
  s_suppress_local_lifecycle_events = suppress;
}

bool ShouldSuppressLocalLifecycleEvents() {
  return s_suppress_local_lifecycle_events;
}

// Helper to clear items from NPC hands after death/unconscious
void ClearNpcHands(oCNpc* npc) {
  if (!npc) {
    return;
  }

  oCItem* rightHand = npc->GetRightHand() ? zDYNAMIC_CAST<oCItem>(npc->GetRightHand()) : nullptr;
  oCItem* leftHand = npc->GetLeftHand() ? zDYNAMIC_CAST<oCItem>(npc->GetLeftHand()) : nullptr;
  npc->SetRightHand(nullptr);
  npc->SetLeftHand(nullptr);
  if (rightHand) {
    rightHand->RemoveVobFromWorld();
  }
  if (leftHand && leftHand != rightHand) {
    leftHand->RemoveVobFromWorld();
  }
}

// Hook: oCNpc::DoDie - clears hands after death
void __fastcall OnDoDie(oCNpc* thisNpc, void* /*edx*/, oCNpc* attacker) {
  if (g_originalDoDie) {
    g_originalDoDie(thisNpc, attacker);
  }
  ClearNpcHands(thisNpc);
  if (thisNpc == player && NetGame::Instance().IsConnected() && !ShouldSuppressLocalLifecycleEvents()) {
    std::optional<std::uint32_t> killer_id;
    if (auto attacker_id = NetGame::Instance().GetPlayerIdByNpc(attacker); attacker_id.has_value()) {
      killer_id = static_cast<std::uint32_t>(attacker_id.value());
    }
    NetGame::Instance().SendPlayerDeath(killer_id);
  }
}

// Hook: oCNpc::DropUnconscious - clears hands after going unconscious
void __fastcall OnDropUnconscious(oCNpc* thisNpc, void* /*edx*/, float duration, oCNpc* attacker) {
  if (g_originalDropUnconscious) {
    g_originalDropUnconscious(thisNpc, duration, attacker);
  }
  ClearNpcHands(thisNpc);
  if (thisNpc == player && NetGame::Instance().IsConnected() && !ShouldSuppressLocalLifecycleEvents()) {
    std::optional<std::uint32_t> attacker_id;
    if (auto id = NetGame::Instance().GetPlayerIdByNpc(attacker); id.has_value()) {
      attacker_id = static_cast<std::uint32_t>(id.value());
    }
    NetGame::Instance().SendPlayerUnconscious(attacker_id);
  }
}

// Hook: oCNpc::StandUp - notify server when the local player stands up from unconscious
void __fastcall OnStandUp(oCNpc* thisNpc, void* /*edx*/, int param1, int param2) {
  const bool was_unconscious = thisNpc->IsUnconscious();
  if (g_originalStandUp) {
    g_originalStandUp(thisNpc, param1, param2);
  }
  if (thisNpc == player && was_unconscious && NetGame::Instance().IsConnected() && !ShouldSuppressLocalLifecycleEvents()) {
    NetGame::Instance().SendPlayerStandUp();
  }
}

// Hook: oCMobInter::CallOnStateFunc - skip SLEEPABIT state to prevent sleep exploit
void __fastcall OnCallOnStateFunc(oCMobInter* mob, void* /*edx*/, oCNpc* npc, int state) {
  // Skip "SLEEPABIT" to prevent sleep exploit in multiplayer
  if (!mob->onStateFuncName.IsEmpty() && memcmp("SLEEPABIT", mob->onStateFuncName.ToChar(), 9) == 0) {
    return;
  }
  if (g_originalCallOnStateFunc) {
    g_originalCallOnStateFunc(mob, npc, state);
  }
}

// Hook: oCNpc::OnDamage_Anim - script-controlled non-local damage animations
void __fastcall OnOnDamageAnim(oCNpc* thisNpc, void* /*edx*/, oCNpc::oSDamageDescriptor& damageDesc) {
  const int attackerWeaponMode = damageDesc.pNpcAttacker ? damageDesc.pNpcAttacker->GetWeaponMode() : NPC_WEAPON_NONE;
  const bool alwaysAllowMode =
      attackerWeaponMode == NPC_WEAPON_FIST || (attackerWeaponMode >= NPC_WEAPON_BOW && attackerWeaponMode <= NPC_WEAPON_MAG);
  if (thisNpc != player && !g_damageAnimationsEnabled && !alwaysAllowMode) {
    return;
  }
  if (g_originalOnDamageAnim) {
    g_originalOnDamageAnim(thisNpc, damageDesc);
  }
}

// Hook: oCAIArrowBase::CreateTrail - allow scripts to hide arrow/bolt trails
void __fastcall OnCreateArrowTrail(oCAIArrowBase* ai, void* /*edx*/, zCVob* vob) {
  if (!g_munitionTrailEnabled) {
    return;
  }
  if (g_originalCreateArrowTrail) {
    g_originalCreateArrowTrail(ai, vob);
  }
}

// Hook: oCNpc::OnDamage_Hit - filter arrow/spell damage from other players
// In multiplayer, damage from other players' arrows/spells should be server-controlled
void __fastcall OnOnDamageHit(oCNpc* thisNpc, void* /*edx*/, oCNpc::oSDamageDescriptor& damageDesc) {
  // Skip spell damage from non-player attackers (server controls this)
  if (damageDesc.nSpellID > 0 && damageDesc.nSpellID != -1 && player != damageDesc.pNpcAttacker) {
    return;
  }
  // Skip arrow/bolt damage from non-player attackers or self-damage
  if (damageDesc.pItemWeapon) {
    const zSTRING weapon_name = damageDesc.pItemWeapon->GetInstanceName();
    if (weapon_name == "ITRW_ARROW" || weapon_name == "ITRW_BOLT") {
      if (damageDesc.pNpcAttacker && (player != damageDesc.pNpcAttacker || player == thisNpc)) {
        return;
      }
    }
  }
  const int old_health = thisNpc->attribute[NPC_ATR_HITPOINTS];
  int damage = 0;

  const bool connected = NetGame::Instance().IsConnected();
  const bool attacker_is_local_player = damageDesc.pNpcAttacker == player;
  const bool attacker_is_network_player =
      attacker_is_local_player || NetGame::Instance().GetPlayerIdByNpc(damageDesc.pNpcAttacker).has_value();
  const bool victim_is_network_player = thisNpc == player || NetGame::Instance().GetPlayerIdByNpc(thisNpc).has_value();
  const bool server_controlled_player_damage =
      connected && attacker_is_network_player && victim_is_network_player && damageDesc.pNpcAttacker != thisNpc;

  if (server_controlled_player_damage) {
    constexpr int kProtectedDamageHealth = 1000000;
    if (g_originalOnDamageHit) {
      thisNpc->attribute[NPC_ATR_HITPOINTS] = kProtectedDamageHealth;
      g_originalOnDamageHit(thisNpc, damageDesc);
      damage = std::max(0, kProtectedDamageHealth - thisNpc->attribute[NPC_ATR_HITPOINTS]);
      thisNpc->attribute[NPC_ATR_HITPOINTS] = old_health;
    }
    damageDesc.bIsDead = 0;
    damageDesc.bIsUnconscious = 0;
  } else if (g_originalOnDamageHit) {
    g_originalOnDamageHit(thisNpc, damageDesc);
    const int new_health = thisNpc->attribute[NPC_ATR_HITPOINTS];
    damage = std::max(0, old_health - new_health);
  }

  // Report only the damage delta; the server decides whether and how to apply it.
  if (attacker_is_local_player && thisNpc != player && connected) {
    if (auto victim_id = NetGame::Instance().GetPlayerIdByNpc(thisNpc); victim_id.has_value()) {
      if (damage > 0) {
        NetGame::Instance().SendPlayerHit(static_cast<std::uint32_t>(victim_id.value()), damage,
                                          static_cast<std::uint32_t>(damageDesc.enuModeDamage),
                                          damageDesc.bDamageDontKill != 0);
      }
    }
  }
}

char bufferTemp[128];

// Installs a mid-function crashfix for oCNpc::ResetPos using AsmJit
// The original code at 0x006824F6 does: AND [EAX+0xB8], 0xFC
// If EAX (from GetAnictrl) is null, this crashes. We add a null check.
void InstallResetPosCrashfix() {
  using namespace asmjit;
  using namespace asmjit::x86;

  constexpr DWORD kPatchAddress = 0x006824F6;
  constexpr DWORD kContinueAddress = 0x006824FD;  // After the AND instruction
  constexpr DWORD kSkipAddress = 0x006827A9;      // Near function end (cleanup)

  AsmPatch::InstallMidFunctionPatch(kPatchAddress, 7, [](Assembler& a) {
    Label skipLabel = a.newLabel();

    // test eax, eax - check if anictrl is null
    a.test(eax, eax);
    // je skip - if null, skip to function cleanup
    a.je(skipLabel);
    // and byte ptr [eax+0xB8], 0xFC - clear lower 2 bits
    a.and_(byte_ptr(eax, 0xB8), 0xFC);
    // jmp continue - return to normal flow
    a.jmp(kContinueAddress);

    a.bind(skipLabel);
    // jmp cleanup - skip to function end
    a.jmp(kSkipAddress);
  });
}

// Helper function for floor sliding crashfix - checks if address is valid
static bool g_floorSlidingAddressValid = false;
static DWORD g_floorSlidingAddress = 0;

bool __cdecl CheckFloorSlidingAddress(DWORD address) {
  return !IsBadCodePtr(reinterpret_cast<FARPROC>(address));
}

// Installs a mid-function crashfix for zCAIPlayer::CheckFloorSliding
// The original code calls GetCollisionObject twice - between the first null check
// and the second call, the collision data pointer could become null in multiplayer.
// At 0x0050D5C9: MOV EAX, [EAX+0xD0] - gets the collision data pointer
// At 0x0050D5CF: ADD EAX, 0xC then MOV EDX, [EAX] - crashes if pointer was null.
// Fix: Add null check on the collision data pointer loaded at 0x0050D5C9.
void InstallFloorSlidingCrashfix() {
  using namespace asmjit;
  using namespace asmjit::x86;

  // Patch at 0x0050D5C9 where we load [EAX+0xD0]
  // Original: MOV EAX, [EAX+0xD0]  (8B 80 D0 00 00 00) - 6 bytes
  //           ADD EAX, 0xC         (83 C0 0C)          - 3 bytes
  //           MOV EDX, [EAX]       (8B 10)             - 2 bytes
  // Total: 11 bytes we can use
  constexpr DWORD kPatchAddress = 0x0050D5C9;
  constexpr DWORD kContinueAddress = 0x0050D5D4;  // After MOV EDX, [EAX]
  constexpr DWORD kSkipAddress = 0x0050D610;      // Where original null check jumps to

  AsmPatch::InstallMidFunctionPatch(kPatchAddress, 11, [](Assembler& a) {
    Label validLabel = a.newLabel();

    // mov eax, [eax+0xD0] - get collision data pointer (original instruction)
    a.mov(eax, dword_ptr(eax, 0xD0));
    // test eax, eax - null check (the fix!)
    a.test(eax, eax);
    // jnz valid - continue if not null
    a.jnz(validLabel);
    // jmp skip - go to where the original null check would have jumped
    a.jmp(kSkipAddress);

    a.bind(validLabel);
    // add eax, 0xC - original instruction
    a.add(eax, 0xC);
    // mov edx, [eax] - original instruction (now safe)
    a.mov(edx, dword_ptr(eax));
    // jmp continue
    a.jmp(kContinueAddress);
  });
}

bool ShouldEmitMessageOnce(std::unordered_map<oCNpc*, const oCMsgManipulate*>& cache, oCNpc* npc, oCMsgManipulate* msg) {
  if (!npc || !msg) {
    return false;
  }
  auto it = cache.find(npc);
  if (it != cache.end() && it->second == msg) {
    return false;
  }
  cache[npc] = msg;
  return true;
}

const int DROP_ITEM_TIMEOUT = 200;

// DROP & TAKE
int __fastcall OnDropItem(oCNpc* thisNpc, void* /*unusedEdx*/, zCVob* vob) {
  oCItem* item = vob ? zDYNAMIC_CAST<oCItem>(vob) : nullptr;
  if (thisNpc != player || !item) {
    return g_originalDropItem ? g_originalDropItem(thisNpc, vob) : 0;
  }

  static int dropItemTimeout = 0;
  if (global_ingame && dropItemTimeout < GetTickCount()) {
    const auto instance_name = ResolveItemInstanceName(item);
    const auto amount = static_cast<short>(std::max(1, item->amount));
    auto event_result = EventManager::Instance().DispatchEvent(gmp::gothic::kEventOnDropItemName,
                                                               gmp::gothic::OnDropItemEvent{instance_name, amount});
    if (event_result.cancelled) {
      return 0;
    }

    const glm::vec3 fallback_position = GetDropFallbackPosition(thisNpc);
    int result = g_originalDropItem ? g_originalDropItem(thisNpc, vob) : 0;
    glm::vec3 position = GetItemPosition(item);
    if (IsZeroVector(position) && !IsZeroVector(fallback_position)) {
      position = fallback_position;
    }

    NetGame::Instance().SendDropItem(item->GetInstance(), amount, instance_name, position, GetItemRotation(item), item->physicsEnabled != 0);
    item->RemoveVobFromWorld();
    dropItemTimeout = GetTickCount() + DROP_ITEM_TIMEOUT;
    return result;
  }

  return g_originalDropItem ? g_originalDropItem(thisNpc, vob) : 0;
}

int __fastcall OnTakeItem(oCNpc* thisNpc, void* /*unusedEdx*/, oCMsgManipulate* msg) {
  if (thisNpc != player) {
    return g_originalTakeItem ? g_originalTakeItem(thisNpc, msg) : 0;
  }

  oCItem* item = msg && msg->targetVob ? zDYNAMIC_CAST<oCItem>(msg->targetVob) : nullptr;
  if (item && global_ingame && ShouldEmitMessageOnce(s_last_take_msg, thisNpc, msg)) {
    const auto instance_name = ResolveItemInstanceName(item);
    const auto instance_id = item->GetInstance();
    const auto amount = static_cast<short>(std::max(1, item->amount));
    const auto item_ground_id = gmp::gothic::ClientItemGroundManager::Instance().GetIdByItem(item);
    const int previous_amount = item_ground_id.has_value() ? GetInventoryAmount(thisNpc, instance_id) : 0;
    auto event_result =
        EventManager::Instance().DispatchEvent(gmp::gothic::kEventOnTakeItemName,
                                               gmp::gothic::OnTakeItemEvent{instance_name, true, amount, item_ground_id});
    if (event_result.cancelled) {
      return 0;
    }

    int result = g_originalTakeItem ? g_originalTakeItem(thisNpc, msg) : 0;
    if (item_ground_id.has_value()) {
      gmp::gothic::ClientItemGroundManager::Instance().RememberPendingTake(instance_id, amount, previous_amount);
      gmp::gothic::ClientItemGroundManager::Instance().DetachItem(*item_ground_id);
    }
    NetGame::Instance().SendTakeItem(instance_id, amount, instance_name, item_ground_id);
    return result;
  }

  return g_originalTakeItem ? g_originalTakeItem(thisNpc, msg) : 0;
}

bool ShouldEmitUseItem(oCNpc* npc, oCMsgManipulate* msg) {
  if (!npc || !msg) {
    return false;
  }
  auto& state = s_last_use_state[npc];
  if (state.msg == msg) {
    return false;
  }

  const DWORD now = GetTickCount();
  const std::string item = ResolveItemInstanceName(msg);
  const std::string scheme = ResolveItemSchemeName(msg);
  if ((!item.empty() && state.item == item && now - state.tick < 750) ||
      (!scheme.empty() && state.scheme == scheme && now - state.tick < 750)) {
    return false;
  }

  state.msg = msg;
  state.item = item;
  state.scheme = scheme;
  state.tick = now;
  return true;
}

int __fastcall OnUseItem(oCNpc* thisNpc, void* /*unusedEdx*/, oCMsgManipulate* msg) {
  int from_state = 0;
  int to_state = 0;
  oCMobInter* mob = msg ? zDYNAMIC_CAST<oCMobInter>(msg->targetVob) : nullptr;
  if (mob) {
    from_state = mob->GetState();
  }

  int result = g_originalUseItem ? g_originalUseItem(thisNpc, msg) : 0;

  if (mob) {
    to_state = mob->GetState();
  }

  if (thisNpc == player && ShouldEmitUseItem(thisNpc, msg)) {
    EventManager::Instance().TriggerEvent(
        gmp::gothic::kEventOnUseItemName,
        gmp::gothic::OnUseItemEvent{ResolveItemInstanceName(msg), ResolveItemSchemeName(msg), from_state, to_state});
  }

  return result;
}

int __fastcall OnUseItemToState(oCNpc* thisNpc, void* /*unusedEdx*/, oCMsgManipulate* msg) {
  int from_state = 0;
  int to_state = 0;
  oCMobInter* mob = msg ? zDYNAMIC_CAST<oCMobInter>(msg->targetVob) : nullptr;
  if (mob) {
    from_state = mob->GetState();
  }

  int result = g_originalUseItemToState ? g_originalUseItemToState(thisNpc, msg) : 0;

  if (mob) {
    to_state = mob->GetState();
  }

  if (thisNpc == player && ShouldEmitUseItem(thisNpc, msg)) {
    EventManager::Instance().TriggerEvent(
        gmp::gothic::kEventOnUseItemName,
        gmp::gothic::OnUseItemEvent{ResolveItemInstanceName(msg), ResolveItemSchemeName(msg), from_state, to_state});
  }

  return result;
}

void __fastcall OnEquipItem(oCNpc* thisNpc, void* /*unusedEdx*/, oCItem* item) {
  if (g_originalEquipItem) {
    g_originalEquipItem(thisNpc, item);
  }

  if (thisNpc != player || ShouldSuppressLocalEquipEvents()) {
    return;
  }

  const std::string instance = ResolveItemInstanceName(item);
  if (instance.empty()) {
    return;
  }

  if (s_local_equipped_items.insert(instance).second) {
    EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnEquipName, gmp::gothic::OnItemEvent{instance});
  }
}

void __fastcall OnUnequipItem(oCNpc* thisNpc, void* /*unusedEdx*/, oCItem* item) {
  if (g_originalUnequipItem) {
    g_originalUnequipItem(thisNpc, item);
  }

  if (thisNpc != player || ShouldSuppressLocalEquipEvents()) {
    return;
  }

  const std::string instance = ResolveItemInstanceName(item);
  if (instance.empty()) {
    return;
  }

  if (s_local_equipped_items.erase(instance) > 0) {
    EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnUnequipName, gmp::gothic::OnItemEvent{instance});
  }
}

int __fastcall OnCastSpell(oCSpell* thisSpell) {
  int result = g_originalCastSpell ? g_originalCastSpell(thisSpell) : 0;

  if ((DWORD)thisSpell->spellCasterNpc == (DWORD)player) {
    if (global_ingame) {
      if (thisSpell->spellTargetNpc) {
        if (thisSpell->GetSpellID() == 46 && !global_ingame->Shrinker->IsShrinked(thisSpell->spellTargetNpc)) {
          global_ingame->Shrinker->ShrinkNpc(thisSpell->spellTargetNpc);
        }
        NetGame::Instance().SendCastSpell(thisSpell->spellTargetNpc, thisSpell->GetSpellID());
      } else {
        NetGame::Instance().SendCastSpell(0, thisSpell->GetSpellID());
      }
    }
  }

  return result;
}

// Take distance patch - C++ callback for distance check
zSTRING TakeTooFarMessage;
bool __stdcall CheckIfDistanceIsCorrect(oCMsgManipulate* Msg, oCNpc* Npc) {
  if (Npc == player && Msg) {
    if (Msg->targetVob) {
      if (Npc->GetDistanceToVob(*Msg->targetVob) < 240.0f) {
        return true;
      } else if (oCItem* Item = zDYNAMIC_CAST<oCItem>(Msg->targetVob)) {
        sprintf(bufferTemp, "%s %s", Item->name.ToChar(), Language::Instance()[Language::ITEM_TOOFAR].ToChar());
        TakeTooFarMessage = bufferTemp;
        ogame->array_view[oCGame::GAME_VIEW_SCREEN]->PrintTimedCXY(TakeTooFarMessage, 4000.0f, 0);
        return false;
      }
    }
  }
  return true;
}

// Installs the take distance patch using AsmJit
// This patch intercepts item pickup to check if player is close enough
void InstallDistanceTakeFix() {
  using namespace asmjit;
  using namespace asmjit::x86;

  constexpr DWORD kPatchAddress = 0x0074C37C;
  constexpr DWORD kReturnAddress = 0x0074C6C4;
  constexpr DWORD kEvTakeVobAddress = 0x007534E0;  // oCNpc::EV_TakeVob

  AsmPatch::InstallMidFunctionPatch(kPatchAddress, 6, [](Assembler& a) {
    Label skipLabel = a.newLabel();

    // Set up call to CheckIfDistanceIsCorrect(Msg, Npc)
    // At this point: ESI = this (oCNpc*), EBP = oCMsgManipulate*
    a.mov(ecx, esi);  // Npc in ecx (but we push it)
    a.push(ecx);      // Push Npc (2nd arg - stdcall)
    a.push(ebp);      // Push Msg (1st arg - stdcall)
    a.call(reinterpret_cast<uint64_t>(&CheckIfDistanceIsCorrect));
    // stdcall cleans up stack automatically

    // Check result
    a.test(al, al);
    a.je(skipLabel);

    // Distance OK - call EV_TakeVob
    a.push(ebp);      // Push Msg parameter
    a.mov(ecx, esi);  // this pointer
    a.call(kEvTakeVobAddress);
    a.jmp(kReturnAddress);

    a.bind(skipLabel);
    // Distance too far - skip taking the item
    a.jmp(kReturnAddress);
  });
}

void Initialize(void) {
  if (!MultiplayerLaunched) {
    MultiplayerLaunched = true;
    HooksManager* hm = HooksManager::GetInstance();

    // Register task scheduler render hook - processes queued main-thread tasks every frame
    hm->AddHook(HT_RENDER, (DWORD)NetGame::ProcessTaskScheduler);

    CActiveAniID* ani_ptr = new CActiveAniID();
    if (auto original = CreateHook(kCastSpellHookAddress, (DWORD)OnCastSpell)) {
      g_originalCastSpell = reinterpret_cast<CastSpellOriginalFn>(*original);
    }
    if (auto original = CreateHook(kDropItemHookAddress, (DWORD)OnDropItem)) {
      g_originalDropItem = reinterpret_cast<DropItemOriginalFn>(*original);
    }
    if (auto original = CreateHook(kTakeItemHookAddress, (DWORD)OnTakeItem)) {
      g_originalTakeItem = reinterpret_cast<TakeItemOriginalFn>(*original);
    }
    if (auto original = CreateHook(kUseItemHookAddress, (DWORD)OnUseItem)) {
      g_originalUseItem = reinterpret_cast<UseItemOriginalFn>(*original);
    }
    if (auto original = CreateHook(kUseItemToStateHookAddress, (DWORD)OnUseItemToState)) {
      g_originalUseItemToState = reinterpret_cast<UseItemToStateOriginalFn>(*original);
    }
    if (auto original = CreateHook(kEquipItemHookAddress, (DWORD)OnEquipItem)) {
      g_originalEquipItem = reinterpret_cast<EquipItemOriginalFn>(*original);
    }
    if (auto original = CreateHook(kUnequipItemHookAddress, (DWORD)OnUnequipItem)) {
      g_originalUnequipItem = reinterpret_cast<UnequipItemOriginalFn>(*original);
    }
    // oCNpc::DoDie - clear hands after death
    if (auto original = CreateHook(kDoDieHookAddress, (DWORD)OnDoDie)) {
      g_originalDoDie = reinterpret_cast<DoDieOriginalFn>(*original);
    }
    // oCNpc::DropUnconscious - clear hands after going unconscious
    if (auto original = CreateHook(kDropUnconsciousHookAddress, (DWORD)OnDropUnconscious)) {
      g_originalDropUnconscious = reinterpret_cast<DropUnconsciousOriginalFn>(*original);
    }
    // oCNpc::StandUp - notify server when local player stands up
    if (auto original = CreateHook(kStandUpHookAddress, (DWORD)OnStandUp)) {
      g_originalStandUp = reinterpret_cast<StandUpOriginalFn>(*original);
    }
    // oCMobInter::CallOnStateFunc - skip SLEEPABIT to prevent sleep exploit
    if (auto original = CreateHook(kCallOnStateFuncHookAddress, (DWORD)OnCallOnStateFunc)) {
      g_originalCallOnStateFunc = reinterpret_cast<CallOnStateFuncOriginalFn>(*original);
    }
    // zCAICamera::AI_Normal - crashfix for null pointer dereference
    if (auto original = CreateHook(kAINormalHookAddress, (DWORD)OnAINormal)) {
      g_originalAINormal = reinterpret_cast<AINormalOriginalFn>(*original);
    }
    // oCNpc::OnDamage_Anim - script-controlled non-local damage animations
    if (auto original = CreateHook(kOnDamageAnimHookAddress, (DWORD)OnOnDamageAnim)) {
      g_originalOnDamageAnim = reinterpret_cast<OnDamageAnimOriginalFn>(*original);
    }
    // oCNpc::OnDamage_Hit - filter arrow/spell damage from other players
    if (auto original = CreateHook(kOnDamageHitHookAddress, (DWORD)OnOnDamageHit)) {
      g_originalOnDamageHit = reinterpret_cast<OnDamageHitOriginalFn>(*original);
    }
    // oCAIArrowBase::CreateTrail - script-controlled arrow/bolt trail visibility
    if (auto original = CreateHook(kCreateArrowTrailHookAddress, (DWORD)OnCreateArrowTrail)) {
      g_originalCreateArrowTrail = reinterpret_cast<CreateArrowTrailOriginalFn>(*original);
    }
    // Patch for FindMobInter
    EraseMemory(0x00740006, 0x6A, 1);
    EraseMemory(0x00740007, 0x00, 1);
    EraseMemory(0x00740008, 0x8B, 1);
    EraseMemory(0x00740009, 0xCE, 1);
    CallPatch(0x0074000A, 0x00719620, 8);
    // ResetPos CrashFix
    InstallResetPosCrashfix();
    // Take distance patch
    InstallDistanceTakeFix();
    // Floor Sliding Crashfix
    InstallFloorSlidingCrashfix();
    // SetupExceptionHandler();
    // Initialize language system
    LanguageManager::Instance().LoadLanguages(".\\Multiplayer\\Localization\\", Config::Instance().lang);
    // Initialize music bridge for zCOptions integration
    gmp::audio::GothicMusicBridge::Initialize();
    // Initialize GMPCore - the central application owner
    GMPCore::Instance().Initialize();
    MainMenu = GMPCore::Instance().GetMainMenu();  // Legacy compatibility
    Patch::FixSetTime();
    Patch::DisableCheat();
    Patch::DisablePause();
    Patch::FixLights();
    Patch::FixApplyOverlay();
    Patch::EraseCastSecurity();
    Patch::DisableGothicMainMenu();
    Patch::DisableWriteSavegame();
    Patch::DisableChangeSightKeys();
    Patch::ChangeLevelEnabled(false);
    Patch::SetLogScreenEnabled(false);
    Patch::DisableInjection();
  }
}
