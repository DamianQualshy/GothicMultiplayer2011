
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
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
constexpr DWORD kAttackFinishHookAddress = 0x00751AF0;
constexpr DWORD kDropUnconsciousHookAddress = 0x00735EB0;
constexpr DWORD kStandUpHookAddress = 0x00682B40;
constexpr DWORD kCallOnStateFuncHookAddress = 0x00720870;
constexpr DWORD kAINormalHookAddress = 0x004A4370;
constexpr DWORD kOnDamageAnimHookAddress = 0x00675BD0;
constexpr DWORD kOnDamageHitHookAddress = 0x00666610;
constexpr DWORD kCreateArrowTrailHookAddress = 0x006A0420;
constexpr DWORD kCheckRemoveNpcHookAddress = 0x007792E0;
constexpr DWORD kShootArrowHookAddress = 0x007446B0;

using CastSpellOriginalFn = int(__thiscall*)(oCSpell*);
using DropItemOriginalFn = int(__thiscall*)(oCNpc*, zCVob*);
using TakeItemOriginalFn = int(__thiscall*)(oCNpc*, oCMsgManipulate*);
using UseItemOriginalFn = int(__thiscall*)(oCNpc*, oCMsgManipulate*);
using UseItemToStateOriginalFn = int(__thiscall*)(oCNpc*, oCMsgManipulate*);
using EquipItemOriginalFn = void(__thiscall*)(oCNpc*, oCItem*);
using UnequipItemOriginalFn = void(__thiscall*)(oCNpc*, oCItem*);
using DoDieOriginalFn = void(__thiscall*)(oCNpc*, oCNpc*);
using AttackFinishOriginalFn = int(__thiscall*)(oCNpc*, oCMsgAttack*);
using DropUnconsciousOriginalFn = void(__thiscall*)(oCNpc*, float, oCNpc*);
using StandUpOriginalFn = void(__thiscall*)(oCNpc*, int, int);
using CallOnStateFuncOriginalFn = void(__thiscall*)(oCMobInter*, oCNpc*, int);
using AINormalOriginalFn = void(__thiscall*)(zCAICamera*);
using OnDamageAnimOriginalFn = void(__thiscall*)(oCNpc*, oCNpc::oSDamageDescriptor&);
using OnDamageHitOriginalFn = void(__thiscall*)(oCNpc*, oCNpc::oSDamageDescriptor&);
using CreateArrowTrailOriginalFn = void(__thiscall*)(oCAIArrowBase*, zCVob*);
using CheckRemoveNpcOriginalFn = int(__thiscall*)(oCSpawnManager*, oCNpc*);
using ShootArrowOriginalFn = int(__thiscall*)(oCNpc*, int);

CastSpellOriginalFn g_originalCastSpell = nullptr;
DropItemOriginalFn g_originalDropItem = nullptr;
TakeItemOriginalFn g_originalTakeItem = nullptr;
UseItemOriginalFn g_originalUseItem = nullptr;
UseItemToStateOriginalFn g_originalUseItemToState = nullptr;
EquipItemOriginalFn g_originalEquipItem = nullptr;
UnequipItemOriginalFn g_originalUnequipItem = nullptr;
DoDieOriginalFn g_originalDoDie = nullptr;
AttackFinishOriginalFn g_originalAttackFinish = nullptr;
DropUnconsciousOriginalFn g_originalDropUnconscious = nullptr;
StandUpOriginalFn g_originalStandUp = nullptr;
CallOnStateFuncOriginalFn g_originalCallOnStateFunc = nullptr;
AINormalOriginalFn g_originalAINormal = nullptr;
OnDamageAnimOriginalFn g_originalOnDamageAnim = nullptr;
OnDamageHitOriginalFn g_originalOnDamageHit = nullptr;
CreateArrowTrailOriginalFn g_originalCreateArrowTrail = nullptr;
CheckRemoveNpcOriginalFn g_originalCheckRemoveNpc = nullptr;
ShootArrowOriginalFn g_originalShootArrow = nullptr;
bool g_damageAnimationsEnabled = false;
bool g_munitionTrailEnabled = true;

int GetInventoryAmount(oCNpc* npc, int instance_id) {
  if (npc == nullptr || instance_id <= 0) {
    return 0;
  }

  std::int64_t amount = 0;
  for (auto* entry = npc->inventory2.inventory.GetNextInList(); entry; entry = entry->GetNextInList()) {
    oCItem* item = entry->GetData();
    if (item && item->GetInstance() == instance_id) amount += std::max(0, item->amount);
  }
  return static_cast<int>(std::min<std::int64_t>(amount, std::numeric_limits<std::int32_t>::max()));
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

oCNpc* s_observed_hero = nullptr;
zCModel* s_observed_hero_model = nullptr;
bool s_observed_connected = false;

bool s_suppress_local_equip_events = false;
bool s_suppress_local_lifecycle_events = false;
std::vector<oCNpc*> s_damage_calculation_victims;

struct FinishingMoveObservation {
  oCMsgAttack* message;
  oCNpc* attacker;
  oCNpc* victim;
  bool reported;
};

std::vector<FinishingMoveObservation> s_finishing_move_observations;
FinishingMoveObservation* s_finishing_move_context = nullptr;

struct ScopedFinishingMoveObservation {
  FinishingMoveObservation* previous;
  explicit ScopedFinishingMoveObservation(FinishingMoveObservation& observation)
      : previous(s_finishing_move_context) {
    s_finishing_move_context = &observation;
  }
  ~ScopedFinishingMoveObservation() { s_finishing_move_context = previous; }
};

void ResetLocalActionObservation() {
  s_finishing_move_observations.clear();
  s_observed_hero = nullptr;
  s_observed_hero_model = nullptr;
  s_observed_connected = false;
  if (auto* animations = CActiveAniID::GetInstance()) animations->Reset();
}

void RefreshLocalActionObservation() {
  zCModel* model = player ? player->GetModel() : nullptr;
  const bool connected = NetGame::Instance().IsConnected();
  if (s_observed_hero != player || s_observed_hero_model != model || s_observed_connected != connected) {
    ResetLocalActionObservation();
    s_observed_hero = player;
    s_observed_hero_model = model;
    s_observed_connected = connected;
  }
}

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

void ClearNpcHands(oCNpc* npc) {
  if (!npc) {
    return;
  }

  zCVob* rightVob = npc->GetRightHand();
  zCVob* leftVob = npc->GetLeftHand();
  zSTRING rightSlot(NPC_NODE_RIGHTHAND);
  zSTRING leftSlot(NPC_NODE_LEFTHAND);
  oCItem* rightHand = zDYNAMIC_CAST<oCItem>(npc->RemoveFromSlot(rightSlot, 0, 1));
  oCItem* leftHand = zDYNAMIC_CAST<oCItem>(npc->RemoveFromSlot(leftSlot, 0, 1));
  if (!rightHand) {
    rightHand = zDYNAMIC_CAST<oCItem>(rightVob);
  }
  if (!leftHand) {
    leftHand = zDYNAMIC_CAST<oCItem>(leftVob);
  }
  if (rightHand) {
    rightHand->RemoveVobFromWorld();
  }
  if (leftHand && leftHand != rightHand) {
    leftHand->RemoveVobFromWorld();
  }
}

// Hook: oCNpc::EV_AttackFinish - report each native finishing move once.
int __fastcall OnAttackFinish(oCNpc* npc, void* /*edx*/, oCMsgAttack* message) {
  if (!npc || !message || !message->target) return 1;
  auto* victim = zDYNAMIC_CAST<oCNpc>(message->target);
  auto observed = std::find_if(s_finishing_move_observations.begin(), s_finishing_move_observations.end(),
      [message](const auto& observation) { return observation.message == message; });
  if (observed == s_finishing_move_observations.end()) {
    // Cancelled messages may never receive a final call. Keep their records bounded.
    if (s_finishing_move_observations.size() >= 128) s_finishing_move_observations.erase(s_finishing_move_observations.begin());
    s_finishing_move_observations.push_back({message, npc, victim, false});
    observed = s_finishing_move_observations.end() - 1;
  } else if (!message->IsInUse() || observed->attacker != npc || observed->victim != victim) {
    *observed = {message, npc, victim, false};
  }
  if (observed->reported) {
    // Native death normally clears KO, preventing the kill branch from adding
    // blood and death sounds again while the finishing animation is running.
    auto* model = npc->GetModel();
    auto* animation = model ? model->GetAniFromAniID(message->hitAni) : nullptr;
    const int completed = !animation || !model->IsAniActive(animation);
    if (completed) s_finishing_move_observations.erase(observed);
    return completed;
  }

  // Keep the active context on the stack: nested engine calls can grow the records.
  auto context = *observed;
  ScopedFinishingMoveObservation scope(context);
  const int completed = g_originalAttackFinish ? g_originalAttackFinish(npc, message) : 1;
  observed = std::find_if(s_finishing_move_observations.begin(), s_finishing_move_observations.end(),
      [message](const auto& observation) { return observation.message == message; });
  if (observed != s_finishing_move_observations.end()) {
    if (completed) s_finishing_move_observations.erase(observed);
    else observed->reported = context.reported;
  }
  return completed;
}

// Hook: oCNpc::DoDie - clears hands after death
void __fastcall OnDoDie(oCNpc* thisNpc, void* /*edx*/, oCNpc* attacker) {
  if (std::find(s_damage_calculation_victims.begin(), s_damage_calculation_victims.end(), thisNpc) != s_damage_calculation_victims.end()) return;
  if (!ShouldSuppressLocalLifecycleEvents() && s_finishing_move_context &&
      s_finishing_move_context->attacker == attacker && s_finishing_move_context->victim == thisNpc &&
      NetGame::Instance().ReportFinishingMove(attacker, thisNpc, !s_finishing_move_context->reported)) {
    s_finishing_move_context->reported = true;
    return;
  }
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
  if (std::find(s_damage_calculation_victims.begin(), s_damage_calculation_victims.end(), thisNpc) != s_damage_calculation_victims.end()) return;
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
  if (g_originalStandUp) {
    g_originalStandUp(thisNpc, param1, param2);
  }
  if (thisNpc == player && NetGame::Instance().IsConnected() && !ShouldSuppressLocalLifecycleEvents()) {
    NetGame::Instance().SendPlayerStandUp();
  }
}

// Hook: oCMobInter::CallOnStateFunc - skip SLEEPABIT state to prevent sleep exploit
void __fastcall OnCallOnStateFunc(oCMobInter* mob, void* /*edx*/, oCNpc* npc, int state) {
  if (!mob) return;
  // Skip "SLEEPABIT" to prevent sleep exploit in multiplayer
  if (!mob->onStateFuncName.IsEmpty() && memcmp("SLEEPABIT", mob->onStateFuncName.ToChar(), 9) == 0) {
    return;
  }
  if (g_originalCallOnStateFunc) {
    g_originalCallOnStateFunc(mob, npc, state);
  }
  if (npc == player && NetGame::Instance().IsConnected()) {
    const std::string name = mob->GetObjectName().ToChar();
    if (!name.empty()) {
      const auto position = mob->GetPositionWorld();
      NetGame::Instance().SendMobState(name, glm::vec3(position[VX], position[VY], position[VZ]), state);
    }
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

// Native AI checks the camera's removal radius even when automatic spawning is
// disabled. Network actors stay present until the server streams them out.
int __fastcall OnCheckRemoveNpc(oCSpawnManager* spawn_manager, void* /*edx*/, oCNpc* npc) {
  if (!spawn_manager || !npc) {
    return 0;
  }
  if (NetGame::Instance().GetPlayerIdByNpc(npc).has_value()) {
    return 0;
  }
  // Explicit stream-out uses DeleteNpc directly, so it is not intercepted here.
  // Client-local NPCs and other engine actors retain the original behavior.
  return g_originalCheckRemoveNpc ? g_originalCheckRemoveNpc(spawn_manager, npc) : 0;
}

// Hook: oCNpc::OnDamage_Hit - filter arrow/spell damage from other players
// In multiplayer, damage from other players' arrows/spells should be server-controlled
void __fastcall OnOnDamageHit(oCNpc* thisNpc, void* /*edx*/, oCNpc::oSDamageDescriptor& damageDesc) {
  if (!thisNpc) return;
  const int old_health = thisNpc->attribute[NPC_ATR_HITPOINTS];
  int damage = 0;

  const bool connected = NetGame::Instance().IsConnected();
  const bool attacker_is_local_player = damageDesc.pNpcAttacker == player;
  const bool attacker_is_network_player =
      attacker_is_local_player || NetGame::Instance().GetPlayerIdByNpc(damageDesc.pNpcAttacker).has_value();
  const bool victim_is_network_player = thisNpc == player || NetGame::Instance().GetPlayerIdByNpc(thisNpc).has_value();
  const bool server_controlled_player_damage =
      connected && attacker_is_network_player && victim_is_network_player && damageDesc.pNpcAttacker != thisNpc;

  if (server_controlled_player_damage && !NetGame::Instance().CanReportActorDamage(damageDesc.pNpcAttacker)) {
    // Spectators present the accepted server result. They never execute the
    // native hit gameplay callbacks or calculate another copy of the damage.
    damageDesc.bIsDead = 0;
    damageDesc.bIsUnconscious = 0;
    return;
  }

  if (server_controlled_player_damage) {
    constexpr int kProtectedDamageHealth = 1000000000;
    if (g_originalOnDamageHit) {
      struct DamageCalculation {
        oCNpc* victim;
        int health;
        explicit DamageCalculation(oCNpc* npc) : victim(npc), health(npc->attribute[NPC_ATR_HITPOINTS]) {
          victim->AddRef();
          s_damage_calculation_victims.push_back(victim);
          victim->attribute[NPC_ATR_HITPOINTS] = kProtectedDamageHealth;
        }
        ~DamageCalculation() {
          victim->attribute[NPC_ATR_HITPOINTS] = health;
          s_damage_calculation_victims.pop_back();
          victim->Release();
        }
      } calculation(thisNpc);
      g_originalOnDamageHit(thisNpc, damageDesc);
      damage = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(kProtectedDamageHealth) -
          thisNpc->attribute[NPC_ATR_HITPOINTS], 0, std::numeric_limits<int>::max()));
    }
    damageDesc.bIsDead = 0;
    damageDesc.bIsUnconscious = 0;
  } else if (g_originalOnDamageHit) {
    g_originalOnDamageHit(thisNpc, damageDesc);
    const int new_health = thisNpc->attribute[NPC_ATR_HITPOINTS];
    damage = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(old_health) - new_health,
        0, std::numeric_limits<int>::max()));
  }

  // Report only the damage delta; the server decides whether and how to apply it.
  if (server_controlled_player_damage && connected) {
    if (auto victim_id = NetGame::Instance().GetPlayerIdByNpc(thisNpc); victim_id.has_value()) {
      if (damage > 0) {
        NetGame::Instance().SendActorHit(damageDesc.pNpcAttacker, static_cast<std::uint32_t>(victim_id.value()), damage,
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

// Compare completed native mutations, not message addresses or item classes.
// Engine messages are reused, and two equipped rings can share an instance.
int __fastcall OnDropItem(oCNpc* npc, void* /*unusedEdx*/, zCVob* vob) {
  auto* item = vob ? zDYNAMIC_CAST<oCItem>(vob) : nullptr;
  if (npc != player || !item || !NetGame::Instance().IsConnected()) {
    return g_originalDropItem ? g_originalDropItem(npc, vob) : 0;
  }
  item->AddRef();
  const int instance = item->GetInstance();
  int before = GetInventoryAmount(npc, instance);
  const int amount = std::max(1, item->amount);
  const std::string name = ResolveItemInstanceName(item);
  const glm::vec3 fallback = GetDropFallbackPosition(npc);
  const auto event = EventManager::Instance().DispatchEvent(gmp::gothic::kEventOnDropItemName,
      gmp::gothic::OnDropItemEvent{name, amount});
  if (event.cancelled || GetInventoryAmount(npc, instance) < amount) {
    item->Release();
    return 0;
  }
  before = GetInventoryAmount(npc, instance);
  const int result = g_originalDropItem ? g_originalDropItem(npc, vob) : 0;
  const int removed = std::max(0, before - GetInventoryAmount(npc, instance));
  if (removed > 0) {
    auto position = GetItemPosition(item);
    if (IsZeroVector(position)) position = fallback;
    const auto rotation = GetItemRotation(item);
    const bool physics = item->physicsEnabled != 0;
    NetGame::Instance().SendDropItem(instance, removed, position, rotation, physics);
    item->RemoveVobFromWorld();
  }
  item->Release();
  return result;
}

int __fastcall OnTakeItem(oCNpc* npc, void* /*unusedEdx*/, oCMsgManipulate* msg) {
  auto* item = msg && msg->targetVob ? zDYNAMIC_CAST<oCItem>(msg->targetVob) : nullptr;
  if (npc != player || !item || !NetGame::Instance().IsConnected()) {
    return g_originalTakeItem ? g_originalTakeItem(npc, msg) : 0;
  }
  item->AddRef();
  // Copy before native pickup: stacking can retire the world object/message.
  const std::string name = ResolveItemInstanceName(item);
  const int instance = item->GetInstance();
  const int before = GetInventoryAmount(npc, instance);
  const int offered = std::max(1, item->amount);
  const auto ground = gmp::gothic::ClientItemGroundManager::Instance().GetIdByItem(item);
  const auto* ground_record = ground ? gmp::gothic::ClientItemGroundManager::Instance().GetById(*ground) : nullptr;
  const auto restore_ground = ground_record ? std::optional<gmp::gothic::ClientItemGroundManager::ItemGround>(*ground_record) : std::nullopt;
  const auto event = EventManager::Instance().DispatchEvent(gmp::gothic::kEventOnTakeItemName,
      gmp::gothic::OnTakeItemEvent{name, ground.has_value(), offered, ground});
  if (event.cancelled) { item->Release(); return 0; }
  // A native world item without a managed identity has no server ownership or
  // quantity. Server scripts must import it as managed loot before pickup.
  if (!ground || !item->GetHomeWorld()) { item->Release(); return 0; }
  const auto* current_ground = gmp::gothic::ClientItemGroundManager::Instance().GetById(*ground);
  if (!current_ground || current_ground->item != item) { item->Release(); return 0; }
  const int result = g_originalTakeItem ? g_originalTakeItem(npc, msg) : 0;
  const int gained = std::max(0, GetInventoryAmount(npc, instance) - before);
  if (gained > 0) {
    if (ground) gmp::gothic::ClientItemGroundManager::Instance().DetachItem(*ground);
    if (NetGame::Instance().SendTakeItem(instance, gained, ground) == 0 && restore_ground) {
      const auto& record = *restore_ground;
      gmp::gothic::ClientItemGroundManager::Instance().Upsert(record.id, record.instance, record.amount,
          record.physics_enabled, record.position, record.rotation);
    }
  }
  item->Release();
  return result;
}

int ObserveUseItem(oCNpc* npc, oCMsgManipulate* msg, bool to_state) {
  // UseItem can call UseItemToState. Observe the completed outer operation once,
  // while still allowing another actor's scripted use inside that operation.
  static std::vector<oCNpc*> observed_actors;
  const bool local = npc == player;
  const bool observe = std::find(observed_actors.begin(), observed_actors.end(), npc) == observed_actors.end() &&
                       NetGame::Instance().CanReportActorDamage(npc);
  struct UseObservationScope {
    std::vector<oCNpc*>& actors;
    bool active;
    UseObservationScope(std::vector<oCNpc*>& actors, oCNpc* actor, bool active) : actors(actors), active(active) {
      if (active) actors.push_back(actor);
    }
    ~UseObservationScope() { if (active) actors.pop_back(); }
  } observation_scope(observed_actors, npc, observe);
  const auto actor_id = NetGame::Instance().GetPlayerIdByNpc(npc);
  const std::string name = ResolveItemInstanceName(msg);
  const std::string scheme = ResolveItemSchemeName(msg);
  auto* item = msg && msg->targetVob ? zDYNAMIC_CAST<oCItem>(msg->targetVob) : nullptr;
  int instance = item ? item->GetInstance() : 0;
  if (instance == 0 && !name.empty() && zCParser::GetParser()) instance = zCParser::GetParser()->GetIndex(name.c_str());
  const int before = observe ? GetInventoryAmount(npc, instance) : 0;
  const int health = observe ? npc->GetAttribute(NPC_ATR_HITPOINTS) : 0;
  const int mana = observe ? npc->GetAttribute(NPC_ATR_MANA) : 0;
  auto* mob = msg && msg->targetVob ? zDYNAMIC_CAST<oCMobInter>(msg->targetVob) : nullptr;
  // Hold a reference across native calls; never read the retired message again.
  if (mob) mob->AddRef();
  const int from = mob ? mob->GetState() : 0;
  const int result = to_state ? (g_originalUseItemToState ? g_originalUseItemToState(npc, msg) : 0)
      : (g_originalUseItem ? g_originalUseItem(npc, msg) : 0);
  const int to = mob ? mob->GetState() : 0;
  if (mob) mob->Release();
  if (observe && NetGame::Instance().CanReportActorDamage(npc)) {
    const int consumed = std::max(0, before - GetInventoryAmount(npc, instance));
    const int health_delta = npc->GetAttribute(NPC_ATR_HITPOINTS) - health;
    const int mana_delta = npc->GetAttribute(NPC_ATR_MANA) - mana;
    if (result != 0 || consumed > 0 || health_delta != 0 || mana_delta != 0 || from != to) {
      if (local) EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnUseItemName,
          gmp::gothic::OnUseItemEvent{name, scheme, from, to});
      if (instance > 0 && NetGame::Instance().IsConnected()) {
        NetGame::Instance().SendItemUsePresentation(instance, scheme, consumed, health_delta, mana_delta,
            static_cast<std::uint32_t>(actor_id.value_or(0)));
      }
    }
  }
  return result;
}

int __fastcall OnUseItem(oCNpc* npc, void* /*unusedEdx*/, oCMsgManipulate* msg) { return ObserveUseItem(npc, msg, false); }
int __fastcall OnUseItemToState(oCNpc* npc, void* /*unusedEdx*/, oCMsgManipulate* msg) { return ObserveUseItem(npc, msg, true); }

void __fastcall OnEquipItem(oCNpc* npc, void* /*unusedEdx*/, oCItem* item) {
  if (!item) { if (g_originalEquipItem) g_originalEquipItem(npc, item); return; }
  item->AddRef();
  const bool before = item->HasFlag(ITM_FLAG_ACTIVE) != 0;
  const std::string name = ResolveItemInstanceName(item);
  if (g_originalEquipItem) g_originalEquipItem(npc, item);
  const bool after = item->HasFlag(ITM_FLAG_ACTIVE) != 0;
  item->Release();
  if (npc == player && !ShouldSuppressLocalEquipEvents() && !before && after && !name.empty()) {
    EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnEquipName, gmp::gothic::OnItemEvent{name});
  }
}

void __fastcall OnUnequipItem(oCNpc* npc, void* /*unusedEdx*/, oCItem* item) {
  if (!item) { if (g_originalUnequipItem) g_originalUnequipItem(npc, item); return; }
  item->AddRef();
  const bool before = item->HasFlag(ITM_FLAG_ACTIVE) != 0;
  const std::string name = ResolveItemInstanceName(item);
  if (g_originalUnequipItem) g_originalUnequipItem(npc, item);
  const bool after = item->HasFlag(ITM_FLAG_ACTIVE) != 0;
  item->Release();
  if (npc == player && !ShouldSuppressLocalEquipEvents() && before && !after && !name.empty()) {
    EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnUnequipName, gmp::gothic::OnItemEvent{name});
  }
}

namespace {
class NativeHitBatch {
public:
  explicit NativeHitBatch(bool observe) : client_(observe ? NetGame::Instance().game_client.get() : nullptr) {
    if (client_) client_->BeginHitBatch();
  }
  ~NativeHitBatch() { Finish(false); }
  bool IsCurrent() const { return client_ && NetGame::Instance().game_client.get() == client_; }
  void Finish(bool accepted) {
    if (IsCurrent()) client_->EndHitBatch(accepted);
    client_ = nullptr;
  }
private:
  gmp::client::GameClient* client_;
};
}

int __fastcall OnShootArrow(oCNpc* npc, void* /*unusedEdx*/, int param) {
  const bool observe = NetGame::Instance().CanReportActorDamage(npc);
  NativeHitBatch hits(observe);
  const int mode = npc ? npc->GetWeaponMode() : 0;
  const auto origin = npc ? npc->GetPositionWorld() : zVEC3(0.0f, 0.0f, 0.0f);
  const auto heading = npc ? npc->GetAtVectorWorld() : zVEC3(0.0f, 0.0f, 1.0f);
  const auto target = npc ? NetGame::Instance().GetPlayerIdByNpc(zDYNAMIC_CAST<oCNpc>(npc->GetFocusVob())) : std::nullopt;
  const int result = g_originalShootArrow ? g_originalShootArrow(npc, param) : 0;
  const bool accepted = hits.IsCurrent() && NetGame::Instance().CanReportActorDamage(npc) && result != 0;
  if (accepted) {
    NetGame::Instance().SendShotPresentation(static_cast<std::uint8_t>(mode),
        glm::vec3(origin[VX], origin[VY], origin[VZ]), glm::vec3(heading[VX], heading[VY], heading[VZ]),
        target ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(*target)) : std::nullopt,
        static_cast<std::uint32_t>(NetGame::Instance().GetPlayerIdByNpc(npc).value_or(0)));
  }
  hits.Finish(accepted);
  return result;
}
int __fastcall OnCastSpell(oCSpell* thisSpell) {
  if (!thisSpell) return 0;
  auto* caster = thisSpell->spellCasterNpc;
  const bool local = NetGame::Instance().CanReportActorDamage(caster);
  NativeHitBatch hits(local);
  const auto caster_id = NetGame::Instance().GetPlayerIdByNpc(caster);
  const int spell_id = thisSpell->GetSpellID();
  const auto target_id = NetGame::Instance().GetPlayerIdByNpc(thisSpell->spellTargetNpc);
  int item_instance = 0;
  if (auto* caster = thisSpell->spellCasterNpc) {
    if (auto* book = caster->GetSpellBook()) {
      if (auto* item = book->GetSpellItem(thisSpell)) item_instance = item->GetInstance();
    }
  }
  int result = g_originalCastSpell ? g_originalCastSpell(thisSpell) : 0;
  const bool accepted = hits.IsCurrent() && result != 0 && NetGame::Instance().CanReportActorDamage(caster);
  if (accepted)
    NetGame::Instance().game_client->SendCastSpell(static_cast<std::uint32_t>(target_id.value_or(0)),
        static_cast<std::uint16_t>(spell_id), item_instance,
        static_cast<std::uint32_t>(caster_id.value_or(0)), caster->GetAttribute(NPC_ATR_MANA));
  hits.Finish(accepted);

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
    if (auto original = CreateHook(kShootArrowHookAddress, (DWORD)OnShootArrow)) {
      g_originalShootArrow = reinterpret_cast<ShootArrowOriginalFn>(*original);
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
    if (auto original = CreateHook(kAttackFinishHookAddress, (DWORD)OnAttackFinish)) {
      g_originalAttackFinish = reinterpret_cast<AttackFinishOriginalFn>(*original);
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
    if (auto original = CreateHook(kCheckRemoveNpcHookAddress, (DWORD)OnCheckRemoveNpc)) {
      g_originalCheckRemoveNpc = reinterpret_cast<CheckRemoveNpcOriginalFn>(*original);
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
    SetupExceptionHandler();
    // Initialize language system
    LanguageManager::Instance().LoadLanguages(".\\Multiplayer\\Localization\\", Config::Instance().language);
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
