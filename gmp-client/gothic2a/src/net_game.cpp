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

#include "net_game.h"

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/std_optional.h>
#include <bitsery/traits/array.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <spdlog/spdlog.h>
#include <wincrypt.h>
#include "hooking/MemoryPatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include "Mod.h"
#include "shared/lua_runtime/lua_value_codec.h"
#include <glm/glm.hpp>
#include <list>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "CChat.h"
#include "CIngame.h"
#include "CActiveAniID.h"
#include "Interface.h"
#include "ZenGin/zGothicAPI.h"
#include "config.h"
#include "scripting/item_ground.h"
#include "language.h"
#include "net_enums.h"
#include "packets.h"
#include "patch.h"
#include "player_name_utils.hpp"
#include "sky_utils.h"
#include "scripting/gothic_bindings.h"
#include "scripting/gothic_events.h"
#include "scripting/lua_draw3d.h"
#include "scripting/lua_sound3d.h"
#include "scripting/process_input.h"
#include "version.h"
#include "shared/event.h"
#include "sol/sol.hpp"

const char* LANG_DIR = ".\\Multiplayer\\Localization\\";

float fWRatio, fHRatio;
extern CIngame* global_ingame;

using namespace Net;

namespace {
oCMenu_Status* GetStatusMenu() {
  zSTRING status_menu_name("MENU_STATUS");
  if (auto* menu = dynamic_cast<oCMenu_Status*>(zCMenu::GetByName(status_menu_name))) {
    return menu;
  }

  zSTRING fallback_name("STATUS");
  return dynamic_cast<oCMenu_Status*>(zCMenu::GetByName(fallback_name));
}

class ScopedLocalLifecycleEventSuppression {
public:
  ScopedLocalLifecycleEventSuppression() : previous_(ShouldSuppressLocalLifecycleEvents()) {
    SetSuppressLocalLifecycleEvents(true);
  }

  ~ScopedLocalLifecycleEventSuppression() {
    SetSuppressLocalLifecycleEvents(previous_);
  }

private:
  bool previous_;
};

std::uint8_t NormalizeLifeState(std::uint8_t life_state, std::int32_t health) {
  switch (life_state) {
    case PLAYER_LIFE_DEAD:
    case PLAYER_LIFE_UNCONSCIOUS:
      return life_state;
    case PLAYER_LIFE_ALIVE:
      return health <= 0 ? PLAYER_LIFE_DEAD : PLAYER_LIFE_ALIVE;
    default:
      break;
  }

  return health <= 0 ? PLAYER_LIFE_DEAD : PLAYER_LIFE_ALIVE;
}

bool IsTerminalLifeState(std::uint8_t life_state) {
  return life_state == PLAYER_LIFE_DEAD || life_state == PLAYER_LIFE_UNCONSCIOUS;
}

void CloseSpellBook(oCNpc* npc) {
  if (!npc) {
    return;
  }

  if (auto* spell_book = npc->GetSpellBook()) {
    spell_book->Close(1);
  }
}

void ClearNpcHandsForLifecycle(oCNpc* npc) {
  if (!npc) {
    return;
  }

  oCItem* right_hand = dynamic_cast<oCItem*>(npc->GetRightHand());
  oCItem* left_hand = dynamic_cast<oCItem*>(npc->GetLeftHand());
  npc->DropAllInHand();
  if (right_hand) {
    right_hand->RemoveVobFromWorld();
  }
  if (left_hand && left_hand != right_hand) {
    left_hand->RemoveVobFromWorld();
  }
}

void SetNpcHealth(oCNpc* npc, int health) {
  if (npc) {
    npc->SetAttribute(NPC_ATR_HITPOINTS, health);
  }
}

int ClampAliveHealth(oCNpc* npc, int health) {
  if (!npc) {
    return std::max(1, health);
  }

  const int max_health = std::max(1, npc->GetAttribute(NPC_ATR_HITPOINTSMAX));
  return std::clamp(health, 1, max_health);
}

void StartWoundedStandTransition(oCNpc* npc) {
  if (!npc) {
    return;
  }

  zCModel* model = npc->GetModel();
  if (!model) {
    return;
  }

  zSTRING wounded_back("S_WOUNDEDB");
  zSTRING wounded_front("S_WOUNDED");
  if (model->IsAnimationActive(wounded_back)) {
    zSTRING transition("T_WOUNDEDB_2_STAND");
    model->StartAnimation(transition);
  } else if (model->IsAnimationActive(wounded_front)) {
    zSTRING transition("T_WOUNDED_2_STAND");
    model->StartAnimation(transition);
  }
}

bool StopModelAnimationById(zCModel* model, std::int16_t animation) {
  if (!model) {
    return false;
  }

  if (animation >= 0) {
    model->StopAni(static_cast<int>(animation));
    return true;
  }

  if (model->numActiveAnis > 0) {
    if (auto* active = model->GetActiveAni(0)) {
      model->StopAni(active);
      return true;
    }
  }

  return false;
}

void TriggerMobTarget(oCMobInter* mob) {
  if (!mob || mob->triggerTarget.IsEmpty() || !ogame || !ogame->GetGameWorld()) {
    return;
  }

  zCMover* mover = static_cast<zCMover*>(ogame->GetGameWorld()->SearchVobByName(mob->triggerTarget));
  if (mover) {
    mover->TriggerMover(mover);
  }
}

oCItem* GetNpcSlotItem(oCNpc* npc, const char* slot_name) {
  if (!npc) {
    return nullptr;
  }

  zSTRING slot(slot_name);
  return npc->GetSlotItem(slot);
}

std::int16_t GetSlotItemInstance(oCNpc* npc, const char* slot_name) {
  if (auto* item = GetNpcSlotItem(npc, slot_name)) {
    return static_cast<std::int16_t>(item->GetInstance());
  }

  return 0;
}

bool IsNetworkItemInstance(std::int32_t instance) {
  return instance > 0;
}

oCItem* CreateNetworkItem(std::int32_t instance) {
  if (!IsNetworkItemInstance(instance) || !zfactory) {
    return nullptr;
  }

  oCItem* item = zfactory->CreateItem(static_cast<int>(instance));
  if (!item) {
    SPDLOG_WARN("Failed to create item instance {}", instance);
  }
  return item;
}

std::int16_t GetCurrentAnimationId() {
  CActiveAniID* active_ani_id = CActiveAniID::GetInstance();
  if (!active_ani_id) {
    return -1;
  }

  return static_cast<std::int16_t>(active_ani_id->GetAniID());
}

std::string GetAnimationNameById(oCNpc* npc, std::int16_t animation_id) {
  if (!npc || animation_id < 0) {
    return {};
  }

  zCModel* model = npc->GetModel();
  zCModelAni* animation = model ? model->GetAniFromAniID(static_cast<int>(animation_id)) : nullptr;
  if (!animation || animation->GetAniName().IsEmpty()) {
    return {};
  }

  return animation->GetAniName().ToChar();
}

std::optional<int> FindParserIndex(const char* instance_name) {
  if (!instance_name || instance_name[0] == '\0') {
    return std::nullopt;
  }

  zCParser* parser = zCParser::GetParser();
  if (!parser) {
    return std::nullopt;
  }

  zSTRING name(instance_name);
  const int index = parser->GetIndex(name);
  if (index < 0) {
    return std::nullopt;
  }

  return index;
}

void ApplyAuthoritativeNpcPosition(oCNpc* npc, const zVEC3& position) {
  if (!npc) {
    return;
  }

  const int coll_det_stat = npc->collDetectionStatic ? 1 : 0;
  const int coll_det_dyn = npc->collDetectionDynamic ? 1 : 0;
  npc->SetCollDet(0);
  npc->SetPositionWorld(position);
  npc->SetCollDetStat(coll_det_stat);
  npc->SetCollDetDyn(coll_det_dyn);
}

bool IsHelmetItem(oCItem* item) {
  return item && (item->wear & ITM_WEAR_HEAD) != 0;
}

bool IsShieldItem(oCItem* item) {
  return item && item->HasFlag(ITM_FLAG_SHIELD);
}

bool IsArmorItem(oCItem* item) {
  return item && ((item->mainflag & ITM_CAT_ARMOR) != 0 || (item->wear & ITM_WEAR_TORSO) != 0);
}

bool IsMeleeWeaponItem(oCItem* item) {
  return item && (item->mainflag & ITM_CAT_NF) != 0 && !IsShieldItem(item);
}

bool IsRangedWeaponItem(oCItem* item) {
  return item && (item->mainflag & ITM_CAT_FF) != 0;
}

bool HasItemFlag(oCItem* item, int item_flag) {
  return item && item->HasFlag(item_flag);
}

std::vector<oCItem*> GetActiveInventoryItems(oCNpc* npc, int item_flag) {
  std::vector<oCItem*> items;
  if (!npc) {
    return items;
  }

  for (zCListSort<oCItem>* entry = npc->inventory2.inventory.GetNextInList(); entry; entry = entry->GetNextInList()) {
    oCItem* item = entry->GetData();
    if (HasItemFlag(item, item_flag) && item->HasFlag(ITM_FLAG_ACTIVE)) {
      items.push_back(item);
    }
  }

  return items;
}

std::int16_t GetEquippedInventoryItemInstance(oCNpc* npc, int item_flag) {
  const auto items = GetActiveInventoryItems(npc, item_flag);
  if (items.empty()) {
    return 0;
  }

  return static_cast<std::int16_t>(items.front()->GetInstance());
}

std::array<std::int16_t, 2> GetEquippedRingInstances(oCNpc* npc, const std::array<std::int16_t, 2>& previous_rings) {
  std::array<std::int16_t, 2> rings{0, 0};
  std::vector<std::int16_t> active_instances;
  for (auto* item : GetActiveInventoryItems(npc, ITM_FLAG_RING)) {
    active_instances.push_back(static_cast<std::int16_t>(item->GetInstance()));
  }

  for (std::size_t i = 0; i < previous_rings.size(); ++i) {
    const auto previous = previous_rings[i];
    if (previous == 0) {
      continue;
    }

    const auto it = std::find(active_instances.begin(), active_instances.end(), previous);
    if (it != active_instances.end()) {
      rings[i] = previous;
      active_instances.erase(it);
    }
  }

  for (const auto instance : active_instances) {
    const auto empty_slot = std::find(rings.begin(), rings.end(), 0);
    if (empty_slot == rings.end()) {
      break;
    }

    *empty_slot = instance;
  }

  return rings;
}

std::int16_t GetActiveSpellItemInstance(oCNpc* npc) {
  if (!npc) {
    return 0;
  }

  const int active_spell_nr = npc->GetActiveSpellNr();
  if (active_spell_nr <= 0) {
    return 0;
  }

  if (auto* item = npc->GetSpellItem(active_spell_nr)) {
    return static_cast<std::int16_t>(item->GetInstance());
  }

  return 0;
}

int GetInventoryAmount(oCNpc* npc, int instance) {
  if (npc == nullptr || instance <= 0) {
    return 0;
  }

  oCItem* item = npc->inventory2.IsIn(instance, 1);
  return item ? std::max(0, item->amount) : 0;
}

oCItem* GetOrCreateInventoryItem(oCNpc* npc, std::int16_t instance, int required_flag) {
  if (!npc || !IsNetworkItemInstance(instance)) {
    return nullptr;
  }

  oCItem* item = npc->inventory2.IsIn(static_cast<int>(instance), 1);
  if (!item) {
    item = CreateNetworkItem(instance);
    if (!item) {
      return nullptr;
    }

    if (!HasItemFlag(item, required_flag)) {
      item->RemoveVobFromWorld();
      return nullptr;
    }

    npc->inventory2.Insert(item);
  } else if (!HasItemFlag(item, required_flag)) {
    return nullptr;
  }

  return item;
}

oCItem* GetOrCreateValidInventoryItem(oCNpc* npc, std::int16_t instance, bool (*is_valid_item)(oCItem*)) {
  if (!npc || !IsNetworkItemInstance(instance) || !is_valid_item) {
    return nullptr;
  }

  oCItem* item = npc->inventory2.IsIn(static_cast<int>(instance), 1);
  if (!item) {
    item = CreateNetworkItem(instance);
    if (!item) {
      return nullptr;
    }

    if (!is_valid_item(item)) {
      item->RemoveVobFromWorld();
      return nullptr;
    }

    npc->inventory2.Insert(item);
  } else if (!is_valid_item(item)) {
    return nullptr;
  }

  return item;
}

void SyncEquippedCombatItem(oCNpc* npc, std::int16_t instance, oCItem* current, bool (*is_valid_item)(oCItem*)) {
  if (!npc) {
    return;
  }

  if (instance == 0 || !IsNetworkItemInstance(instance)) {
    if (current) {
      npc->UnequipItem(current);
    }
    return;
  }

  if (current && current->GetInstance() == static_cast<int>(instance) && (!is_valid_item || is_valid_item(current))) {
    return;
  }

  if (current) {
    npc->UnequipItem(current);
  }

  if (auto* item = GetOrCreateValidInventoryItem(npc, instance, is_valid_item)) {
    npc->Equip(item);
  }
}

void SyncEquippedInventoryItem(oCNpc* npc, std::int16_t instance, int item_flag) {
  if (!npc) {
    return;
  }

  const auto active_items = GetActiveInventoryItems(npc, item_flag);
  for (auto* item : active_items) {
    if (instance == 0 || item->GetInstance() != static_cast<int>(instance)) {
      npc->UnequipItem(item);
    }
  }

  if (instance == 0) {
    return;
  }

  const auto remaining_items = GetActiveInventoryItems(npc, item_flag);
  const auto already_equipped = std::any_of(remaining_items.begin(), remaining_items.end(), [instance](oCItem* item) {
    return item && item->GetInstance() == static_cast<int>(instance);
  });
  if (already_equipped) {
    return;
  }

  if (auto* item = GetOrCreateInventoryItem(npc, instance, item_flag)) {
    npc->Equip(item);
  }
}

void SyncEquippedRingItems(oCNpc* npc, std::int16_t left_instance, std::int16_t right_instance) {
  if (!npc) {
    return;
  }

  std::array<std::int16_t, 2> desired{left_instance, right_instance};
  std::array<bool, 2> matched{false, false};

  const auto active_rings = GetActiveInventoryItems(npc, ITM_FLAG_RING);
  for (auto* item : active_rings) {
    bool keep = false;
    for (std::size_t i = 0; i < desired.size(); ++i) {
      if (!matched[i] && desired[i] != 0 && item->GetInstance() == static_cast<int>(desired[i])) {
        matched[i] = true;
        keep = true;
        break;
      }
    }

    if (!keep) {
      npc->UnequipItem(item);
    }
  }

  for (std::size_t i = 0; i < desired.size(); ++i) {
    if (matched[i] || desired[i] == 0) {
      continue;
    }

    if (auto* item = GetOrCreateInventoryItem(npc, desired[i], ITM_FLAG_RING)) {
      npc->Equip(item);
    }
  }
}

void SyncEquippedSlotItem(oCNpc* npc, std::int16_t instance, const char* slot_name, bool (*is_valid_slot_item)(oCItem*)) {
  if (!npc) {
    return;
  }

  oCItem* current = GetNpcSlotItem(npc, slot_name);
  if (instance == 0) {
    if (current) {
      npc->UnequipItem(current);
    }
    return;
  }

  if (!IsNetworkItemInstance(instance)) {
    return;
  }

  if (current && current->GetInstance() == static_cast<int>(instance)) {
    return;
  }

  if (current) {
    npc->UnequipItem(current);
  }

  oCItem* item = npc->inventory2.IsIn(static_cast<int>(instance), 1);
  if (!item) {
    item = CreateNetworkItem(instance);
    if (!item) {
      return;
    }

    if (!is_valid_slot_item(item)) {
      item->RemoveVobFromWorld();
      return;
    }

    npc->inventory2.Insert(item);
  } else if (!is_valid_slot_item(item)) {
    return;
  }

  npc->Equip(item);
}

struct WorldTimerTickValues {
  float ticks_per_hour;
  float ticks_per_minute;
  float ticks_per_day;
};

WorldTimerTickValues ReadWorldTimerTicks() {
  constexpr DWORD kTicksPerHourAddr = 0x0083E168;
  constexpr DWORD kTicksPerMinuteAddr = 0x00AB3764;
  constexpr DWORD kTicksPerDayAddr = 0x00AB371C;
  return { *reinterpret_cast<float*>(kTicksPerHourAddr),
           *reinterpret_cast<float*>(kTicksPerMinuteAddr),
           *reinterpret_cast<float*>(kTicksPerDayAddr) };
}

void WriteWorldTimerTicks(const WorldTimerTickValues& values) {
  constexpr DWORD kTicksPerHourAddr = 0x0083E168;
  constexpr DWORD kTicksPerMinuteAddr = 0x00AB3764;
  constexpr DWORD kTicksPerDayAddr = 0x00AB371C;
  MemoryPatch::WriteMemory(kTicksPerHourAddr, reinterpret_cast<PBYTE>(const_cast<float*>(&values.ticks_per_hour)), sizeof(float));
  MemoryPatch::WriteMemory(kTicksPerMinuteAddr, reinterpret_cast<PBYTE>(const_cast<float*>(&values.ticks_per_minute)), sizeof(float));
  MemoryPatch::WriteMemory(kTicksPerDayAddr, reinterpret_cast<PBYTE>(const_cast<float*>(&values.ticks_per_day)), sizeof(float));
}

float PositiveOrFallback(float value, float fallback) {
  return value > 0.0f ? value : fallback;
}

void ApplyGameTimeToEngine(const STime& time) {
  if (!ogame) {
    return;
  }

  const int day = static_cast<int>(time.day);
  const int hour = static_cast<int>(time.hour);
  const int min = static_cast<int>(time.min);

  auto* timer = ogame->GetWorldTimer();
  if (!timer) {
    ogame->SetTime(day, hour, min);
    return;
  }

  const auto ticks = ReadWorldTimerTicks();
  const float ticks_per_day = PositiveOrFallback(ticks.ticks_per_day, Gothic_II_Addon::WLD_TICKSPERDAY);
  const float ticks_per_hour = PositiveOrFallback(ticks.ticks_per_hour, Gothic_II_Addon::WLD_TICKSPERHOUR);
  const float ticks_per_minute = PositiveOrFallback(ticks.ticks_per_minute, Gothic_II_Addon::WLD_TICKSPERMIN);
  const float full_time = static_cast<float>(day) * ticks_per_day + static_cast<float>(hour) * ticks_per_hour +
                          static_cast<float>(min) * ticks_per_minute;
  timer->SetFullTime(full_time);
}

void ApplyDayLengthToEngine(float day_length_ms) {
  static std::optional<WorldTimerTickValues> original_ticks;

  if (!original_ticks.has_value()) {
    original_ticks = ReadWorldTimerTicks();
  }

  WorldTimerTickValues current_ticks = ReadWorldTimerTicks();
  const float current_ticks_per_day = current_ticks.ticks_per_day <= 0.0f ? 1.0f : current_ticks.ticks_per_day;
  float percent = 0.0f;
  if (ogame && ogame->GetWorldTimer()) {
    percent = ogame->GetWorldTimer()->worldTime / current_ticks_per_day;
  }

  if (day_length_ms <= 0.0f) {
    if (original_ticks.has_value()) {
      WriteWorldTimerTicks(*original_ticks);
      if (ogame && ogame->GetWorldTimer()) {
        ogame->GetWorldTimer()->worldTime = percent * original_ticks->ticks_per_day;
      }
    }
    return;
  }

  WorldTimerTickValues new_ticks;
  new_ticks.ticks_per_day = day_length_ms;
  new_ticks.ticks_per_hour = day_length_ms / 24.0f;
  new_ticks.ticks_per_minute = new_ticks.ticks_per_hour / 60.0f;
  WriteWorldTimerTicks(new_ticks);

  if (ogame && ogame->GetWorldTimer()) {
    ogame->GetWorldTimer()->worldTime = percent * new_ticks.ticks_per_day;
  }
}

zCOLOR kResourceInfoColor(0, 200, 255, 255);
zCOLOR kResourceErrorColor(255, 0, 0, 255);
zCOLOR kResourceSuccessColor(0, 255, 0, 255);

struct MultiplayerMessageState {
  zCView* view{nullptr};
  int line_index{0};
};

MultiplayerMessageState& GetMultiplayerMessageState() {
  static MultiplayerMessageState state;
  return state;
}

void PrintResourceStatusTimed(const zSTRING& message, float duration_ms, zCOLOR& color) {
  if (!ogame) {
    return;
  }
  auto* view = ogame->array_view[oCGame::GAME_VIEW_SCREEN];
  if (!view) {
    return;
  }
  constexpr int kBaseY = 2800;
  constexpr int kLineHeight = 200;
  constexpr int kMaxLines = 5;
  static int line_index = 0;
  auto& state = GetMultiplayerMessageState();
  if (state.view != view) {
    state.view = view;
    state.line_index = 0;
  }

  const int y = kBaseY + (state.line_index * kLineHeight);
  view->PrintTimedCX(y, message, duration_ms, &color);
  state.line_index = (state.line_index + 1) % kMaxLines;
}

void ClearMultiplayerStatusMessages() {
  if (!ogame) {
    return;
  }
  auto* view = ogame->array_view[oCGame::GAME_VIEW_SCREEN];
  if (!view) {
    return;
  }
  view->ClrPrintwin();
  auto& state = GetMultiplayerMessageState();
  state.view = view;
  state.line_index = 0;
}

std::string BuildMultiplayerStatusMessage() {
  std::string tag = GIT_TAG;
  std::string commit = GIT_COMMIT;
  std::ostringstream message;
  message << "Gothic Multiplayer Classic";
  if (!tag.empty() || !commit.empty()) {
    message << " (";
    if (!tag.empty()) {
      message << tag;
    }
    if (!commit.empty()) {
      if (!tag.empty()) {
        message << " ";
      }
      message << commit;
    }
    message << ")";
  }
  return message.str();
}

}// namespace

NetGame::NetGame() : task_scheduler(nullptr), game_client(nullptr), resource_runtime(nullptr) {
  task_scheduler = std::make_unique<gmp::GothicTaskScheduler>();
  game_client = std::make_unique<gmp::client::GameClient>(*this, *task_scheduler);
  resource_runtime = std::make_unique<ClientResourceRuntime>();
  resource_runtime->SetServerInfoProvider(*game_client);
  resource_runtime->SetResetCallback([]() { gmp::gothic::ResetGothicEvents(); });
  gmp::gothic::BindGothicEvents(resource_runtime->GetLuaState());
  gmp::gothic::BindGothicSpecific(resource_runtime->GetLuaState());
}

void NetGame::Shutdown() {
  EventManager::Instance().Reset();
  pending_local_spawn_position_.reset();
  if (game_client) {
    game_client->Disconnect();
  }
  for (auto* p : players) {
    delete p;
  }
  players.clear();
}

void NetGame::ClearMultiplayerMessages() {
  ClearMultiplayerStatusMessages();
}

void __stdcall NetGame::ProcessTaskScheduler() {
  NetGame& instance = NetGame::Instance();
  if (instance.task_scheduler) {
    instance.task_scheduler->ProcessTasks();
  }
  if (instance.resource_runtime) {
    instance.resource_runtime->ProcessTimers();
  }
  instance.ApplyPendingLocalSpawnPosition();
  if (zinput) {
    gmp::gothic::ProcessInput(zinput);
  }
  gmp::gothic::LuaSound3d::UpdateActiveSounds();
  gmp::gothic::LuaDraw3d::RenderActiveDraws();
  instance.UpdateClientEventState();
  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnRenderName, 0);
}

void NetGame::ApplyPendingLocalSpawnPosition() {
  if (!pending_local_spawn_position_.has_value()) {
    return;
  }

  auto& correction = *pending_local_spawn_position_;
  Gothic2APlayer* local_player = GetPlayerById(correction.player_id);
  if (!local_player || !local_player->GetNpc()) {
    return;
  }

  ApplyAuthoritativeNpcPosition(local_player->GetNpc(), correction.position);
  --correction.remaining_frames;
  if (correction.remaining_frames <= 0) {
    pending_local_spawn_position_.reset();
  }
}

void NetGame::SetDayLengthMs(float day_length_ms) {
  constexpr float kMinDayLengthMs = 10000.0f;
  const float clamped_day_length_ms = std::max(day_length_ms, kMinDayLengthMs);
  if (std::fabs(clamped_day_length_ms - day_length_ms_) > 0.01f) {
    ApplyDayLengthToEngine(clamped_day_length_ms);
    day_length_ms_ = clamped_day_length_ms;
  }
}

float NetGame::GetDayLengthMs() const {
  return day_length_ms_;
}

void NetGame::UpdateClientEventState() {
  if (ogame && ogame->GetWorldTimer()) {
    int hour = 0;
    int min = 0;
    auto* timer = ogame->GetWorldTimer();
    timer->GetTime(hour, min);
    const int day = timer->GetDay();
    GameTimeSnapshot snapshot{day, hour, min};
    if (!last_game_time_.has_value() || snapshot.day != last_game_time_->day || snapshot.hour != last_game_time_->hour ||
        snapshot.min != last_game_time_->min) {
      EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnTimeName, gmp::gothic::OnTimeEvent{day, hour, min});
      last_game_time_ = snapshot;
    }
  }

  if (ogame && ogame->GetGameWorld()) {
    std::string world_name = ogame->GetGameWorld()->GetWorldFilename().ToChar();
    if (!world_name.empty() && world_name != last_world_name_) {
      EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnWorldEnterName, gmp::gothic::OnWorldEnterEvent{world_name});
      SendPlayerWorldEnter(world_name);
      last_world_name_ = world_name;
    }

    gmp::gothic::ClientItemGroundManager::Instance().RefreshPending();
  }

}

bool NetGame::Connect(std::string_view full_address) {
  if (!game_client) {
    return false;
  }

  game_client->ConnectAsync(full_address);
  // Return true to indicate connection attempt started
  // Actual connection status will be reported via callbacks
  return true;
}

void NetGame::RestoreHealth() {
  if (!mp_restore || !IsInGame || !player) {
    return;
  }
  if (last_mp_regen != time(NULL)) {
    last_mp_regen = time(NULL);
    if (player->attribute[NPC_ATR_MANAMAX] != player->attribute[NPC_ATR_MANA]) {
      player->attribute[NPC_ATR_MANA] = player->attribute[NPC_ATR_MANA] + mp_restore;
      if (player->attribute[NPC_ATR_MANA] > player->attribute[NPC_ATR_MANAMAX])
        player->attribute[NPC_ATR_MANA] = player->attribute[NPC_ATR_MANAMAX];
    }
  }
}

void NetGame::HandleNetwork() {
  if (IsConnected()) {
    game_client->HandleNetwork();
  }
}

bool NetGame::IsConnected() {
  return game_client && game_client->IsConnected();
}

Gothic2APlayer* NetGame::GetPlayerById(std::uint64_t player_id) {
  for (auto* player : players) {
    if (!player) {
      continue;
    }

    if (player->base_player().id() == player_id) {
      return player;
    }
  }
  return nullptr;
}

std::optional<std::uint64_t> NetGame::GetPlayerIdByNpc(oCNpc* npc) {
  if (!npc) {
    return std::nullopt;
  }
  for (auto* player : players) {
    if (!player) {
      continue;
    }

    if (player->npc == npc) {
      return player->base_player().id();
    }
  }
  return std::nullopt;
}

void NetGame::ApplyPlayerLifeState(std::uint64_t player_id, std::uint8_t life_state, std::optional<std::uint64_t> actor_id,
                                   bool trigger_event) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  const auto normalized_life_state = NormalizeLifeState(life_state, cplayer ? cplayer->base_player().health() : 0);
  if (!cplayer || !cplayer->GetNpc()) {
    if (trigger_event && normalized_life_state == PLAYER_LIFE_DEAD) {
      EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerDeadName, gmp::gothic::PlayerLifecycleEvent{player_id});
    }
    return;
  }

  oCNpc* npc = cplayer->GetNpc();
  cplayer->base_player().set_life_state(normalized_life_state);

  oCNpc* actor = nullptr;
  if (actor_id.has_value()) {
    if (Gothic2APlayer* actor_player = GetPlayerById(actor_id.value())) {
      actor = actor_player->GetNpc();
    }
  }

  if (normalized_life_state == PLAYER_LIFE_DEAD) {
    const bool was_dead = npc->IsDead();
    cplayer->StopPositionInterpolation();
    cplayer->ClearHandledAnimation();
    cplayer->base_player().set_health(0);
    cplayer->base_player().set_update_health_packet_counter(0);
    CloseSpellBook(npc);

    {
      ScopedLocalLifecycleEventSuppression suppress_lifecycle;
      ClearNpcHandsForLifecycle(npc);
      npc->SetWeaponMode(NPC_WEAPON_NONE);
      if (!was_dead) {
        npc->DoDie(actor);
      }
      SetNpcHealth(npc, 0);
    }

    if (trigger_event) {
      EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerDeadName, gmp::gothic::PlayerLifecycleEvent{player_id});
    }
    return;
  }

  if (normalized_life_state == PLAYER_LIFE_UNCONSCIOUS) {
    cplayer->StopPositionInterpolation();
    cplayer->ClearHandledAnimation();
    cplayer->base_player().set_health(1);
    cplayer->base_player().set_update_health_packet_counter(0);
    CloseSpellBook(npc);

    {
      ScopedLocalLifecycleEventSuppression suppress_lifecycle;
      if (npc->IsDead()) {
        if (cplayer->IsLocalPlayer()) {
          npc->RefreshNpc();
        }
        SetNpcHealth(npc, 1);
        auto pos = npc->GetPositionWorld();
        npc->ResetPos(pos);
      }

      SetNpcHealth(npc, 1);
      npc->SetWeaponMode(NPC_WEAPON_NONE);
      if (!npc->IsUnconscious()) {
        npc->DropUnconscious(0.0f, actor);
      }
      ClearNpcHandsForLifecycle(npc);
    }
    return;
  }

  const int target_health = ClampAliveHealth(npc, cplayer->base_player().health());
  cplayer->base_player().set_health(static_cast<short>(target_health));
  cplayer->base_player().set_update_health_packet_counter(0);

  if (npc->IsDead()) {
    cplayer->RespawnPlayer();
    SetNpcHealth(npc, target_health);
    return;
  }

  if (npc->IsUnconscious()) {
    cplayer->StopPositionInterpolation();
    cplayer->ClearHandledAnimation();
    CloseSpellBook(npc);
    {
      ScopedLocalLifecycleEventSuppression suppress_lifecycle;
      npc->DoDie(nullptr);
      SetNpcHealth(npc, target_health);
      npc->SetBodyState(BS_STAND);
      npc->SetMovLock(0);
      npc->SetWeaponMode(NPC_WEAPON_NONE);
      StartWoundedStandTransition(npc);
      npc->StandUp(0, 1);
      SetNpcHealth(npc, target_health);
    }
    return;
  }

  if (npc->GetAttribute(NPC_ATR_HITPOINTS) != target_health) {
    SetNpcHealth(npc, target_health);
  }
}

void NetGame::JoinGame() {
  if (IsReadyToJoin && game_client && player) {
    HooksManager::GetInstance()->AddHook(HT_RENDER, (DWORD)InterfaceLoop);

    auto sanitized_name = SanitizePlayerName(Config::Instance().Nickname.ToChar());
    player->name[0] = sanitized_name.c_str();

    // Call the new GameClient JoinGame method
    game_client->JoinGame(sanitized_name, sanitized_name, "", 0, "", 0, 0);

    // Set up the local player now that we have the player ID from the server
    CIngame* g = new CIngame();
    // LocalPlayer->id = game_client->player_manager().GetLocalPlayer().id();
    // LocalPlayer->enable = TRUE;
    // LocalPlayer->SetNpc(player);
    // LocalPlayer->hp = static_cast<short>(LocalPlayer->GetHealth());
    // LocalPlayer->update_hp_packet = 0;
    // LocalPlayer->npc->SetMovLock(0);
    // this->players.push_back(LocalPlayer);
    // this->HeroLastHp = player->attribute[NPC_ATR_HITPOINTS];
  }
}

void NetGame::SendMessage(const char* msg) {
  if (!game_client) {
    return;
  }

  if (IsInGame && player != nullptr && game_client && game_client->player_manager().HasLocalPlayer()) {
    UpdatePlayerStats(GetCurrentAnimationId());
  }
  game_client->SendChatMessage(msg);
}

void NetGame::SendCastSpell(oCNpc* Target, short SpellId) {
  if (!game_client) {
    return;
  }

  std::uint32_t target_id = 0;
  if (Target) {
    for (int i = 0; i < (int)players.size(); i++) {
      if (players[i] && players[i]->npc == Target) {
        target_id = players[i]->base_player().id();
        break;
      }
    }
  }
  game_client->SendCastSpell(target_id, static_cast<std::uint16_t>(SpellId));
}

void NetGame::SendDropItem(short instance, short amount, const std::string& instance_name, const glm::vec3& position,
                           const glm::vec3& rotation, bool physics_enabled) {
  if (!game_client) {
    return;
  }

  game_client->SendDropItem(static_cast<std::uint16_t>(instance), static_cast<std::uint16_t>(amount), instance_name, position,
                            rotation, physics_enabled);
}

void NetGame::SendTakeItem(short instance, short amount, const std::string& instance_name, std::optional<std::uint32_t> item_ground_id) {
  if (!game_client) {
    return;
  }

  game_client->SendTakeItem(static_cast<std::uint16_t>(instance), static_cast<std::uint16_t>(amount), instance_name, item_ground_id);
}

void NetGame::SendPlayerWorldEnter(const std::string& world_name) {
  if (game_client && IsConnected()) {
    game_client->SendPlayerWorldEnter(world_name);
  }
}

void NetGame::SendPlayerHit(std::uint32_t victim_id, std::int32_t damage, std::uint32_t damage_type, bool dont_kill) {
  if (!game_client) {
    return;
  }

  game_client->SendPlayerHit(victim_id, damage, damage_type, dont_kill);
}

void NetGame::SendPlayerUnconscious(std::optional<std::uint32_t> attacker_id) {
  if (!game_client) {
    return;
  }

  game_client->SendPlayerUnconscious(attacker_id);
}

void NetGame::SendPlayerStandUp() {
  if (!game_client) {
    return;
  }

  game_client->SendPlayerStandUp();
}

void NetGame::SendPlayerDeath(std::optional<std::uint32_t> killer_id) {
  if (!game_client) {
    return;
  }

  game_client->SendPlayerDeath(killer_id);
}

glm::vec3 Vec3ToGlmVec3(const zVEC3& vec) {
  return glm::vec3(vec[VX], vec[VY], vec[VZ]);
}

std::uint8_t GetHeadDirectionByte(oCNpc* Hero) {
  if (!Hero || !Hero->GetAnictrl()) {
    return static_cast<uint8_t>(Gothic2APlayer::HEAD_NONE);
  }

  zVEC2 HeadVar = zVEC2(Hero->GetAnictrl()->lookTargetx, Hero->GetAnictrl()->lookTargety);
  if (HeadVar[VX] == 0)
    return static_cast<uint8_t>(Gothic2APlayer::HEAD_LEFT);
  else if (HeadVar[VX] == 1)
    return static_cast<uint8_t>(Gothic2APlayer::HEAD_RIGHT);
  else if (HeadVar[VY] == 0)
    return static_cast<uint8_t>(Gothic2APlayer::HEAD_UP);
  else if (HeadVar[VY] == 1)
    return static_cast<uint8_t>(Gothic2APlayer::HEAD_DOWN);

  return 0;
}

void NetGame::UpdatePlayerStats(short anim) {
  if (!game_client || !player) {
    return;
  }

  // Gather player state from the Gothic engine
  PlayerState state;
  state.position = Vec3ToGlmVec3(player->GetPositionWorld());
  state.nrot = Vec3ToGlmVec3(player->GetAtVectorWorld());
  state.left_hand_item_instance = player->GetLeftHand() ? static_cast<short>(player->GetLeftHand()->GetInstance()) : 0;
  state.right_hand_item_instance = player->GetRightHand() ? static_cast<short>(player->GetRightHand()->GetInstance()) : 0;
  state.equipped_armor_instance = player->GetEquippedArmor() ? static_cast<short>(player->GetEquippedArmor()->GetInstance()) : 0;
  state.equipped_helmet_instance = GetSlotItemInstance(player, NPC_NODE_HELMET);
  state.equipped_shield_instance = GetSlotItemInstance(player, NPC_NODE_SHIELD);
  state.equipped_amulet_instance = GetEquippedInventoryItemInstance(player, ITM_FLAG_AMULET);
  state.equipped_belt_instance = GetEquippedInventoryItemInstance(player, ITM_FLAG_BELT);
  std::array<std::int16_t, 2> previous_rings{0, 0};
  if (game_client && game_client->player_manager().HasLocalPlayer()) {
    auto& local_player = game_client->player_manager().GetLocalPlayer();
    previous_rings[0] = static_cast<std::int16_t>(local_player.equipped_ring_left());
    previous_rings[1] = static_cast<std::int16_t>(local_player.equipped_ring_right());
  }
  const auto equipped_rings = GetEquippedRingInstances(player, previous_rings);
  state.equipped_ring_left_instance = equipped_rings[0];
  state.equipped_ring_right_instance = equipped_rings[1];
  state.animation = anim;
  state.animation_name = GetAnimationNameById(player, anim);
  if (state.animation_name.size() > kMaxPlayerAnimationNameLength) {
    state.animation_name.resize(kMaxPlayerAnimationNameLength);
  }
  if (player->IsDead()) {
    state.life_state = PLAYER_LIFE_DEAD;
  } else if (player->IsUnconscious()) {
    state.life_state = PLAYER_LIFE_UNCONSCIOUS;
  } else {
    state.life_state = PLAYER_LIFE_ALIVE;
  }
  state.health_points = static_cast<std::int16_t>(player->attribute[NPC_ATR_HITPOINTS]);
  state.mana_points = static_cast<std::int16_t>(player->attribute[NPC_ATR_MANA]);
  state.weapon_mode = static_cast<uint8_t>(player->GetWeaponMode());
  state.active_spell_nr = player->GetActiveSpellNr() > 0 ? static_cast<uint8_t>(player->GetActiveSpellNr()) : 0;
  state.active_spell_instance = GetActiveSpellItemInstance(player);
  state.head_direction = GetHeadDirectionByte(player);
  state.melee_weapon_instance = player->GetEquippedMeleeWeapon() ? static_cast<short>(player->GetEquippedMeleeWeapon()->GetInstance()) : 0;
  state.ranged_weapon_instance = player->GetEquippedRangedWeapon() ? static_cast<short>(player->GetEquippedRangedWeapon()->GetInstance()) : 0;

  game_client->UpdatePlayerStats(state);
}

void NetGame::SyncGameTime() {
  if (game_client) {
    game_client->SyncGameTime();
  }
}

void NetGame::Disconnect() {
  if (game_client && game_client->IsConnected()) {
    pending_local_spawn_position_.reset();
    IsInGame = false;
    IsReadyToJoin = false;
    if (global_ingame) {
      global_ingame->IgnoreFirstSync = true;
    }
    // if (LocalPlayer) {
    //   LocalPlayer->SetNpcType(Gothic2APlayer::NPC_HUMAN);
    // }
    game_client->Disconnect();
    Gothic2APlayer::DeleteAllPlayers();
    CChat::GetInstance()->ClearChat();
  }

  if (resource_runtime) {
    gmp::gothic::CleanupGothicViews();
    resource_runtime->UnloadResources();
  }
}

// ============================================================================
// EventObserver Implementation
// ============================================================================

void NetGame::OnConnectionStarted() {
  SPDLOG_INFO("Connection attempt started...");
  std::string message = BuildMultiplayerStatusMessage();
  PrintResourceStatusTimed(message.c_str(), 10000.0f, kResourceInfoColor);
}

void NetGame::OnConnected() {
  SPDLOG_INFO("Successfully connected to server");
}

void NetGame::OnConnectionFailed(const std::string& error) {
  SPDLOG_ERROR("Connection failed: {}", error);
  IsReadyToJoin = false;
  // Could show error message to user here
}

void NetGame::OnDisconnected() {
  SPDLOG_INFO("Disconnected from server");
  IsReadyToJoin = false;
  if (resource_runtime) {
    gmp::gothic::CleanupGothicViews();
    resource_runtime->UnloadResources();
  }
}

void NetGame::OnConnectionLost() {
  SPDLOG_WARN("Connection lost");
  Disconnect();
  if (player) {
    auto pos = player->GetPositionWorld();
    player->ResetPos(pos);
  }
  IsInGame = false;
  IsReadyToJoin = false;
  CChat::GetInstance()->WriteMessage(NORMAL, false, zCOLOR(255, 0, 0, 255), "%s", Language::Instance()[Language::DISCONNECTED].ToChar());
}

bool NetGame::RequestResourceDownloadConsent(std::size_t resource_count, std::uint64_t total_bytes) {
  if (resource_count == 0 || total_bytes == 0) {
    return true;
  }

  const double total_mb = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
  SPDLOG_INFO("Server requires downloading {} resource pack(s) ({:.2f} MiB)", resource_count, total_mb);
  std::ostringstream message;
  message << "Server requires downloading " << std::fixed << std::setprecision(2) << total_mb << " MiB of client resources";
  PrintResourceStatusTimed(message.str().c_str(), 10000.0f, kResourceInfoColor);

  return true;
}

void NetGame::OnResourceDownloadProgress(const std::string& resource_name, std::uint64_t downloaded_bytes, std::uint64_t total_bytes) {
  if (total_bytes == 0) {
    return;
  }
  const double percent = (static_cast<double>(downloaded_bytes) / static_cast<double>(total_bytes)) * 100.0;
  SPDLOG_INFO("Downloading resources... {} ({:.2f}% complete)", resource_name, percent);
}

void NetGame::OnResourceDownloadFailed(const std::string& reason) {
  SPDLOG_ERROR("Resource download failed: {}", reason);
  IsReadyToJoin = false;
  std::string message = "Resource download failed: " + reason;
  PrintResourceStatusTimed(message.c_str(), 10000.0f, kResourceErrorColor);
  Disconnect();
}

void NetGame::OnResourcesReady() {
  SPDLOG_INFO("Client signaled that resources are ready; consuming payloads");
  auto payloads = game_client->ConsumeDownloadedResources();

  if (!resource_runtime) {
    SPDLOG_ERROR("Client resource runtime is unavailable; disconnecting");
    OnResourceDownloadFailed("Client resource runtime unavailable");
    return;
  }

  SPDLOG_INFO("Loading {} resource payload(s) into runtime", payloads.size());
  std::string error_message;
  if (game_client->player_manager().HasLocalPlayer()) {
    resource_runtime->GetLuaState()["heroId"] = static_cast<int>(game_client->player_manager().GetLocalPlayer().id());
  } else {
    resource_runtime->GetLuaState()["heroId"] = sol::lua_nil;
  }
  if (!resource_runtime->LoadResources(std::move(payloads), error_message)) {
    if (error_message.empty()) {
      error_message = "Failed to initialize client resources";
    }
    OnResourceDownloadFailed(error_message);
    return;
  }

  SPDLOG_INFO("Client resources ready; player may join");
  SPDLOG_INFO("All required client resources downloaded and loaded");
  IsReadyToJoin = true;
  PrintResourceStatusTimed("Client resources ready. You may join the server.", 10000.0f, kResourceSuccessColor);
  
  auto* view = ogame->array_view[oCGame::GAME_VIEW_SCREEN];
  view->SetFontColor(zCOLOR(255, 255, 255));
}

void NetGame::OnMapChange(const std::string& map_name) {
  if (!ogame || !ogame->GetGameWorld()) {
    return;
  }

  std::string currentMap = ogame->GetGameWorld()->GetWorldFilename().ToChar();

  if (map_name != currentMap) {
    this->map = map_name.c_str();
  } else if (!this->map.IsEmpty()) {
    this->map.Clear();
  }
}

void NetGame::OnGameInfoReceived(std::uint32_t raw_game_time, float day_length_ms, std::uint8_t flags) {
  STime t;
  t.time = raw_game_time;

  if (day_length_ms > 0.0f) {
    SetDayLengthMs(day_length_ms);
  }

  ApplyGameTimeToEngine(t);

  oCGame::s_bUsePotionKeys = flags & 0x01;
  this->ForceHideMap = flags & 0x04;
}

void NetGame::OnSkySettingsReceived(std::uint8_t flags, std::int32_t weather_type,
                                    std::int16_t rain_start_hour, std::int16_t rain_start_min,
                                    std::int16_t rain_stop_hour, std::int16_t rain_stop_min,
                                    float wind_scale, bool dont_rain, float rain_weight, bool render_lightning) {
  if (flags & SKY_SETTING_WEATHER) {
    gmp::gothic::ApplyWeatherType(weather_type);
  }
  if (flags & SKY_SETTING_RAIN_START) {
    gmp::gothic::SetRainStartTime(rain_start_hour, rain_start_min);
  }
  if (flags & SKY_SETTING_RAIN_STOP) {
    gmp::gothic::SetRainStopTime(rain_stop_hour, rain_stop_min);
  }
  if (flags & SKY_SETTING_WIND_SCALE) {
    gmp::gothic::SetWindScale(wind_scale);
  }
  if (flags & SKY_SETTING_RAIN_WEIGHT) {
    gmp::gothic::SetRainWeight(rain_weight, weather_type, render_lightning);
  }
  if (flags & SKY_SETTING_DONT_RAIN) {
    gmp::gothic::SetDontRain(dont_rain);
  }
}

void NetGame::OnLocalPlayerJoined(gmp::client::Player& player) {
  SPDLOG_INFO("Local player registered with id {}", player.id());
}

void NetGame::OnLocalPlayerSpawned(gmp::client::Player& player) {
  if (!::player) {
    SPDLOG_ERROR("Local player spawned by server before Gothic hero exists");
    return;
  }

  this->IsInGame = true;

  Gothic2APlayer* local_player = GetPlayerById(player.id());
  if (local_player == nullptr) {
    local_player = new Gothic2APlayer(player, true);
    players.insert(players.begin(), local_player);
    EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerCreateName, gmp::gothic::PlayerLifecycleEvent{player.id()});
  }

  local_player->SetNpc(::player);
  zVEC3 pos(player.position().x, player.position().y, player.position().z);

  SPDLOG_INFO("Local player spawned at position ({}, {}, {})", player.position().x, player.position().y, player.position().z);
  local_player->StopPositionInterpolation();
  ApplyAuthoritativeNpcPosition(local_player->GetNpc(), pos);
  pending_local_spawn_position_ = PendingLocalSpawnPosition{player.id(), pos, 12};
  local_player->base_player().set_enabled(true);
  ApplyPlayerLifeState(player.id(), player.life_state(), std::nullopt, false);
  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerSpawnName, gmp::gothic::PlayerLifecycleEvent{player.id()});
  if (ogame && ogame->GetGameWorld()) {
    SendPlayerWorldEnter(ogame->GetGameWorld()->GetWorldFilename().ToChar());
  }

#ifndef NDEBUG
  // Spawn Quarhodron NPC near the player
  auto npcIndex = FindParserIndex("NONE_ADDON_111_QUARHODRON");
  if (npcIndex.has_value() && zfactory && ogame && ogame->GetGameWorld()) {
    oCNpc* quarhodron = zfactory->CreateNpc(*npcIndex);
    if (quarhodron) {
      zVEC3 npcPos(pos[VX] + 200.0f, pos[VY], pos[VZ] + 200.0f);
      quarhodron->SetPositionWorld(npcPos);
      ogame->GetGameWorld()->AddVob(quarhodron);
      SPDLOG_INFO("Spawned Quarhodron NPC near local player at ({}, {}, {})", npcPos[VX], npcPos[VY], npcPos[VZ]);
    }
  } else {
    SPDLOG_WARN("Could not find NPC instance NONE_ADDON_111_QUARHODRON");
  }
#endif
}

void NetGame::OnPlayerJoined(gmp::client::Player& new_player) {
  // Remote player metadata registered; actual spawn handled in OnPlayerSpawned.
  (void)new_player;
}

void NetGame::OnPlayerSpawned(gmp::client::Player& new_player) {
  SpawnRemotePlayer(new_player);
  ApplyPlayerLifeState(new_player.id(), new_player.life_state(), std::nullopt, false);
  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerSpawnName, gmp::gothic::PlayerLifecycleEvent{new_player.id()});
}

void NetGame::SpawnRemotePlayer(gmp::client::Player& new_player) {
  if (Gothic2APlayer* existing_player = GetPlayerById(new_player.id())) {
    if (existing_player->GetNpc()) {
      zVEC3 pos(new_player.position().x, new_player.position().y, new_player.position().z);
      existing_player->StopPositionInterpolation();
      existing_player->SetPosition(pos);
      existing_player->base_player().set_enabled(true);
      existing_player->base_player().set_update_health_packet_counter(0);
    }
    return;
  }

  Gothic2APlayer* newhero = new Gothic2APlayer(new_player);
  zVEC3 pos(new_player.position().x, new_player.position().y, new_player.position().z);
  int instance_id = player ? player->GetInstance() : -1;
  if (!new_player.instance().empty()) {
    if (auto parsed_instance_id = FindParserIndex(new_player.instance().c_str()); parsed_instance_id.has_value() && *parsed_instance_id > 0) {
      instance_id = *parsed_instance_id;
    } else {
      SPDLOG_WARN("Remote player '{}' has unknown instance '{}'; using local fallback instance.", new_player.name(), new_player.instance());
    }
  }
  if (!zfactory || instance_id < 0) {
    SPDLOG_ERROR("Failed to resolve remote NPC instance for player '{}'.", new_player.name());
    delete newhero;
    return;
  }
  oCNpc* npc = zfactory->CreateNpc(instance_id);
  if (!npc) {
    SPDLOG_ERROR("Failed to create remote NPC for player '{}' with instance id {}.", new_player.name(), instance_id);
    delete newhero;
    return;
  }
  newhero->SetNpc(npc);
  newhero->npc->startAIState = 0;
  newhero->npc->SetGuild(9);
  newhero->npc->Enable(pos);
  newhero->SetPosition(pos);
  newhero->SetName(new_player.name().c_str());
  (void)new_player;

  SPDLOG_INFO("Player '{}' id {} joined the game.", new_player.name(), new_player.id());
  newhero->base_player().set_enabled(true);
  newhero->base_player().set_update_health_packet_counter(0);
  this->players.push_back(newhero);
}

void NetGame::OnPlayerLeft(std::uint64_t player_id, const std::string& player_name) {
  for (size_t i = 0; i < this->players.size(); i++) {
    if (!this->players[i]) {
      continue;
    }

    if (this->players[i]->base_player().id() == player_id) {
      if (this->players[i]->IsLocalPlayer()) {
        SPDLOG_INFO("Local player '{}' (id {}) was unspawned", this->players[i]->GetName(), player_id);
        pending_local_spawn_position_.reset();
        this->IsInGame = false;
        this->players[i]->base_player().set_has_spawned(false);
        this->players[i]->base_player().set_enabled(false);
        EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerDestroyName, gmp::gothic::PlayerLifecycleEvent{player_id});
        break;
      }

      SPDLOG_INFO("Player '{}' (id {}) left visibility scope", this->players[i]->GetName(), player_id);
      this->players[i]->LeaveGame();
      EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerDestroyName, gmp::gothic::PlayerLifecycleEvent{player_id});
      delete this->players[i];
      this->players.erase(this->players.begin() + i);
      break;
    }
  }
}

void NetGame::OnPlayerNameUpdate(std::uint64_t player_id, const std::string& name) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer) {
    return;
  }

  cplayer->base_player().set_name(name);
  if (!cplayer->GetNpc()) {
    return;
  }

  zSTRING new_name(name.c_str());
  cplayer->SetName(new_name);
}

void NetGame::OnPlayerInstanceUpdate(std::uint64_t player_id, const std::string& instance) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  const auto instance_id = FindParserIndex(instance.c_str());
  if (!instance_id.has_value() || *instance_id <= 0) {
    SPDLOG_WARN("Unknown NPC instance '{}' for player {}", instance, player_id);
    return;
  }

  if (!cplayer->ReplaceNpcInstance(*instance_id)) {
    SPDLOG_WARN("Failed to replace NPC instance '{}' for player {}", instance, player_id);
    return;
  }

  cplayer->base_player().set_instance(instance);
}

void NetGame::OnPlayerColorUpdate(std::uint64_t player_id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->SetNameColor(zCOLOR(r, g, b, 255));
  cplayer->base_player().set_name_color(r, g, b);
}

void NetGame::OnPlayerSkillWeaponUpdate(std::uint64_t player_id, std::int32_t skill_id, std::int32_t percentage) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->GetNpc()->SetHitChance(skill_id, percentage);
  cplayer->base_player().set_weapon_skill(skill_id, percentage);
}

void NetGame::OnPlayerTalentUpdate(std::uint64_t player_id, std::int32_t talent_id, std::int32_t talent_value) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->GetNpc()->SetTalentSkill(talent_id, talent_value);
  cplayer->base_player().set_talent(talent_id, talent_value);
}

void NetGame::OnPlayerVisualUpdate(std::uint64_t player_id, const std::string& body_model, std::int16_t body_texture,
                                   const std::string& head_model, std::int16_t head_texture) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  zSTRING body(body_model.c_str());
  zSTRING head(head_model.c_str());
  cplayer->GetNpc()->SetAdditionalVisuals(body, body_texture, 0, head, head_texture, 0, 0);
  cplayer->base_player().set_body_model(body_model);
  cplayer->base_player().set_body_texture(body_texture);
  cplayer->base_player().set_head_model(head_model);
  cplayer->base_player().set_head_texture(head_texture);
}

void NetGame::OnPlayerFatnessUpdate(std::uint64_t player_id, float fatness) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->GetNpc()->SetFatness(fatness);
  cplayer->base_player().set_fatness(fatness);
}

void NetGame::OnPlayerScaleUpdate(std::uint64_t player_id, const glm::vec3& scale) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->GetNpc()->SetModelScale(zVEC3{scale.x, scale.y, scale.z});
  cplayer->base_player().set_scale(scale);
}

void NetGame::OnPlayerOverlayUpdate(std::uint64_t player_id, const std::string& overlay, bool apply) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  zSTRING overlay_name(overlay.c_str());
  if (apply) {
    cplayer->GetNpc()->ApplyOverlay(overlay_name);
    cplayer->base_player().add_overlay(overlay);
    return;
  }

  if (cplayer->GetNpc()->GetOverlay(overlay_name) != 0) {
    cplayer->GetNpc()->RemoveOverlay(overlay_name);
  }
  cplayer->base_player().remove_overlay(overlay);
}

void NetGame::OnPlayerAnimationPlay(std::uint64_t player_id, std::int16_t animation) {
  if (animation < 0) {
    return;
  }

  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  if (auto* model = cplayer->GetNpc()->GetModel()) {
    if (model->GetAniFromAniID(static_cast<int>(animation))) {
      model->StartAni(static_cast<int>(animation), 0);
    }
  }
}

void NetGame::OnPlayerAnimationStop(std::uint64_t player_id, std::int16_t animation) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  StopModelAnimationById(cplayer->GetNpc()->GetModel(), animation);
}

void NetGame::OnPlayerFaceAnimationPlay(std::uint64_t player_id, const std::string& animation) {
  if (animation.empty()) {
    return;
  }

  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->GetNpc()->StartFaceAni(zSTRING(animation.c_str()), 1.0f, 1.0f);
}

void NetGame::OnPlayerFaceAnimationStop(std::uint64_t player_id, const std::string& animation) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  cplayer->GetNpc()->StopFaceAni(zSTRING(animation.c_str()));
}

void NetGame::OnPlayerGesticulation(std::uint64_t player_id) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  oCNpc* npc = cplayer->GetNpc();
  if (!npc->IsDead() && !npc->IsUnconscious()) {
    npc->StartDialogAni();
  }
}

void NetGame::OnPlayerAttributeUpdate(std::uint64_t player_id, PlayerAttributeId attribute_id, std::int32_t value) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  oCNpc* npc = cplayer->GetNpc();
  const bool is_local = cplayer->IsLocalPlayer();
  const int clamped_value = std::max(0, static_cast<int>(value));

  switch (attribute_id) {
    case ATTR_STRENGTH:
      npc->SetAttribute(NPC_ATR_STRENGTH, clamped_value);
      cplayer->base_player().set_strength(clamped_value);
      break;
    case ATTR_DEXTERITY:
      npc->SetAttribute(NPC_ATR_DEXTERITY, clamped_value);
      cplayer->base_player().set_dexterity(clamped_value);
      break;
    case ATTR_LEVEL:
      npc->level = clamped_value;
      cplayer->base_player().set_level(clamped_value);
      break;
    case ATTR_EXP:
      npc->experience_points = clamped_value;
      cplayer->base_player().set_exp(clamped_value);
      if (is_local) {
        if (auto* status_menu = GetStatusMenu()) {
          status_menu->SetExperience(npc->experience_points, 0, npc->experience_points_next_level);
        }
      }
      break;
    case ATTR_NEXT_LEVEL_EXP:
      npc->experience_points_next_level = clamped_value;
      cplayer->base_player().set_next_level_exp(clamped_value);
      if (is_local) {
        if (auto* status_menu = GetStatusMenu()) {
          status_menu->SetExperience(npc->experience_points, 0, npc->experience_points_next_level);
        }
      }
      break;
    case ATTR_LEARN_POINTS:
      npc->learn_points = static_cast<unsigned long>(clamped_value);
      cplayer->base_player().set_learn_points(clamped_value);
      if (is_local) {
        if (auto* status_menu = GetStatusMenu()) {
          status_menu->SetLearnPoints(npc->learn_points);
        }
      }
      break;
    case ATTR_MAX_HEALTH: {
      npc->SetAttribute(NPC_ATR_HITPOINTSMAX, clamped_value);
      cplayer->base_player().set_max_health(static_cast<std::int16_t>(clamped_value));
      int current_health = npc->GetAttribute(NPC_ATR_HITPOINTS);
      if (current_health > clamped_value) {
        cplayer->base_player().set_health(static_cast<std::int16_t>(clamped_value));
        cplayer->base_player().set_update_health_packet_counter(0);
        if (clamped_value <= 0) {
          ApplyPlayerLifeState(player_id, PLAYER_LIFE_DEAD, std::nullopt, false);
        } else {
          SetNpcHealth(npc, clamped_value);
        }
      }
      break;
    }
    case ATTR_HEALTH: {
      int max_health = npc->GetAttribute(NPC_ATR_HITPOINTSMAX);
      int clamped_health = std::min(clamped_value, std::max(0, max_health));
      cplayer->base_player().set_health(static_cast<std::int16_t>(clamped_health));
      cplayer->base_player().set_update_health_packet_counter(0);
      if (clamped_health <= 0) {
        ApplyPlayerLifeState(player_id, PLAYER_LIFE_DEAD, std::nullopt, false);
        break;
      }

      const auto life_state = cplayer->base_player().life_state();
      if (life_state == PLAYER_LIFE_DEAD) {
        break;
      }
      if (life_state == PLAYER_LIFE_UNCONSCIOUS && clamped_health <= 1) {
        SetNpcHealth(npc, 1);
        break;
      }
      if (npc->IsDead() || npc->IsUnconscious()) {
        ApplyPlayerLifeState(player_id, PLAYER_LIFE_ALIVE, std::nullopt, false);
        break;
      }

      SetNpcHealth(npc, clamped_health);
      break;
    }
    case ATTR_MAX_MANA: {
      npc->SetAttribute(NPC_ATR_MANAMAX, clamped_value);
      cplayer->base_player().set_max_mana(static_cast<std::int16_t>(clamped_value));
      int current_mana = npc->GetAttribute(NPC_ATR_MANA);
      if (current_mana > clamped_value) {
        npc->SetAttribute(NPC_ATR_MANA, clamped_value);
        cplayer->base_player().set_mana(static_cast<std::int16_t>(clamped_value));
      }
      break;
    }
    case ATTR_MANA: {
      int max_mana = npc->GetAttribute(NPC_ATR_MANAMAX);
      int clamped_mana = std::min(clamped_value, max_mana);
      npc->SetAttribute(NPC_ATR_MANA, clamped_mana);
      cplayer->base_player().set_mana(static_cast<std::int16_t>(clamped_mana));
      break;
    }
    default:
      break;
  }
}

void NetGame::OnPlayerWorldUpdate(std::uint64_t player_id, const std::string& world_name, const std::string& start_point) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->IsLocalPlayer() || !ogame) {
    return;
  }

  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnWorldChangeName,
                                        gmp::gothic::OnWorldChangeEvent{world_name, start_point});

  zSTRING z_world(world_name.c_str());
  zSTRING z_start_point(start_point.c_str());
  Patch::ChangeLevelEnabled(true);
  ogame->ChangeLevel(z_world, z_start_point);
  Patch::ChangeLevelEnabled(false);
}

void NetGame::OnPlayerStateUpdate(std::uint64_t player_id, const PlayerState& state) {
  // Use const char* instead of static zSTRING to avoid Gothic allocator issues during shutdown.
  // zSTRING can be constructed from const char* when needed.
  static constexpr const char* Door = "DOOR";
  static constexpr const char* BowSound = "BOWSHOOT";
  static constexpr const char* CrossbowSound = "CROSSBOWSHOOT";
  static constexpr const char* Lever = "LEVER";
  static constexpr const char* Touchplate = "TOUCHPLATE";
  static constexpr const char* VWheel = "VWHEEL";
  static constexpr const char* Arrows = "ITRW_ARROW";
  static constexpr const char* Bolt = "ITRW_BOLT";

  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  // Update position
  zVEC3 pos(state.position.x, state.position.y, state.position.z);
  if (!cplayer->base_player().is_enabled()) {
    cplayer->StopPositionInterpolation();
    cplayer->npc->Enable(pos);
    cplayer->SetPosition(pos);
    cplayer->base_player().set_enabled(true);
  } else {
    cplayer->AnalyzePosition(pos);
  }

  const auto life_state = NormalizeLifeState(state.life_state, state.health_points);
  ApplyPlayerLifeState(player_id, life_state, std::nullopt, false);
  const bool terminal_life_state = IsTerminalLifeState(life_state);

  // Update rotation
  if (!terminal_life_state && !cplayer->npc->IsDead() && !cplayer->npc->IsUnconscious()) {
    const auto& at = state.nrot;
    zVEC3 at_vector(at.x, 0.0f, at.z);
    if (at_vector.Length_Sqr() > 0.000001f) {
      cplayer->npc->SetHeadingYWorld(cplayer->npc->GetPositionWorld() + at_vector);
    }
  }

  auto clear_hand_item = [&](bool right_hand) {
    zCVob* current_vob = right_hand ? cplayer->npc->GetRightHand() : cplayer->npc->GetLeftHand();
    if (!current_vob) {
      return;
    }

    oCItem* removed_item = dynamic_cast<oCItem*>(current_vob);
    oCItem* preserved_item = dynamic_cast<oCItem*>(right_hand ? cplayer->npc->GetLeftHand() : cplayer->npc->GetRightHand());
    if (preserved_item == removed_item) {
      preserved_item = nullptr;
    }
    cplayer->npc->DropAllInHand();
    if (preserved_item) {
      if (right_hand) {
        cplayer->npc->SetLeftHand(preserved_item);
      } else {
        cplayer->npc->SetRightHand(preserved_item);
      }
    }
    if (removed_item) {
      if (right_hand) {
        this->PlayDrawSound(removed_item, cplayer->npc, false);
      }
      removed_item->RemoveVobFromWorld();
    }
  };

  auto sync_hand_item = [&](std::int16_t instance, bool right_hand) {
    zCVob* current_vob = right_hand ? cplayer->npc->GetRightHand() : cplayer->npc->GetLeftHand();
    oCItem* current_item = dynamic_cast<oCItem*>(current_vob);
    if (instance == 0 || !IsNetworkItemInstance(instance)) {
      clear_hand_item(right_hand);
      return;
    }

    if (current_item && current_item->GetInstance() == static_cast<int>(instance)) {
      return;
    }

    clear_hand_item(right_hand);
    oCItem* new_item = CreateNetworkItem(instance);
    if (!new_item) {
      return;
    }

    if (right_hand) {
      cplayer->npc->SetRightHand(new_item);
      this->PlayDrawSound(new_item, cplayer->npc, true);
    } else {
      cplayer->npc->SetLeftHand(new_item);
      this->CheckForSpecialEffects(new_item, cplayer->npc);
    }
  };

  const auto left_hand_item = terminal_life_state ? 0 : state.left_hand_item_instance;
  const auto right_hand_item = terminal_life_state ? 0 : state.right_hand_item_instance;

  // Update hand items
  sync_hand_item(left_hand_item, false);
  sync_hand_item(right_hand_item, true);

  // Update equipped armor
  SyncEquippedCombatItem(cplayer->npc, state.equipped_armor_instance, cplayer->npc->GetEquippedArmor(), IsArmorItem);

  // Update equipped helmet and shield
  SyncEquippedSlotItem(cplayer->npc, state.equipped_helmet_instance, NPC_NODE_HELMET, IsHelmetItem);
  SyncEquippedSlotItem(cplayer->npc, state.equipped_shield_instance, NPC_NODE_SHIELD, IsShieldItem);
  SyncEquippedInventoryItem(cplayer->npc, state.equipped_amulet_instance, ITM_FLAG_AMULET);
  SyncEquippedInventoryItem(cplayer->npc, state.equipped_belt_instance, ITM_FLAG_BELT);
  SyncEquippedRingItems(cplayer->npc, state.equipped_ring_left_instance, state.equipped_ring_right_instance);

  // Update animation
  zCModel* model = cplayer->npc->GetModel();
  std::string animation_name = state.animation_name;
  if (animation_name.size() > kMaxPlayerAnimationNameLength) {
    animation_name.resize(kMaxPlayerAnimationNameLength);
  }
  if (animation_name.empty() && state.animation >= 0 && state.animation < 1400) {
    animation_name = GetAnimationNameById(cplayer->npc, state.animation);
  }
  if (animation_name.empty()) {
    cplayer->ClearHandledAnimation();
  }

  if (model && !animation_name.empty() && !terminal_life_state && !cplayer->npc->IsDead() && !cplayer->npc->IsUnconscious()) {
    zSTRING animation_name_z(animation_name.c_str());
    const bool should_handle_animation_side_effects = cplayer->MarkAnimationHandled(animation_name);

    if (should_handle_animation_side_effects) {
      if (animation_name_z.Search(Door) == 2) {
        if (!cplayer->npc->GetInteractMob()) {
          oCMobLockable* locked_mob = static_cast<oCMobLockable*>(cplayer->npc->FindMobInter(Door));
          if (locked_mob) {
            locked_mob->SetMobBodyState(cplayer->npc);
            locked_mob->AI_UseMobToState(cplayer->npc, !locked_mob->GetState());
          }
        } else {
          cplayer->npc->GetInteractMob()->SetMobBodyState(cplayer->npc);
          cplayer->npc->GetInteractMob()->AI_UseMobToState(cplayer->npc, !cplayer->npc->GetInteractMob()->GetState());
        }
      } else if (cplayer->npc->GetInteractMob()) {
        if (cplayer->npc->GetInteractMob()->GetState() == 0)
          cplayer->npc->GetInteractMob()->SendEndInteraction(cplayer->npc, 0, 1);
        else
          cplayer->npc->GetInteractMob()->SendEndInteraction(cplayer->npc, 1, 0);
      }
    }

    if (!model->IsAnimationActive(animation_name_z)) {
      model->StartAnimation(animation_name_z);
      if (should_handle_animation_side_effects) {
        if (animation_name.compare(0, 11, "T_BOWRELOAD") == 0) {
          oCItem* Bullet = nullptr;
          if (auto arrow_index = FindParserIndex(Arrows)) {
            Bullet = CreateNetworkItem(*arrow_index);
          }
          if (zsound && !this->players.empty() && this->players[0] && this->players[0]->npc) {
            zsound->PlaySound3D(BowSound, this->players[0]->npc, 2);
          }
          if (Bullet) {
            cplayer->npc->SetRightHand(Bullet);
            oCItem* Arrowe = cplayer->npc->inventory2.IsIn(7083, 1);
            if (Arrowe)
              cplayer->npc->inventory2.Remove(7083, 1);
            cplayer->npc->DoShootArrow(1);
          }
        }
        if (animation_name.compare(0, 12, "T_CBOWRELOAD") == 0) {
          oCItem* Bullet = nullptr;
          if (auto bolt_index = FindParserIndex(Bolt)) {
            Bullet = CreateNetworkItem(*bolt_index);
          }
          if (zsound && !this->players.empty() && this->players[0] && this->players[0]->npc) {
            zsound->PlaySound3D(CrossbowSound, this->players[0]->npc, 2);
          }
          if (Bullet) {
            cplayer->npc->SetLeftHand(Bullet);
            oCItem* Bolte = cplayer->npc->inventory2.IsIn(7084, 1);
            if (Bolte)
              cplayer->npc->inventory2.Remove(7084, 1);
            cplayer->npc->DoShootArrow(1);
          }
        }
        if (animation_name.compare(0, 15, "T_LEVER_S0_2_S1") == 0) {
          oCMobInter* LeverSwitch = cplayer->npc->FindMobInter(Lever);
          TriggerMobTarget(LeverSwitch);
        }
        if (animation_name.compare(0, 20, "T_TOUCHPLATE_S0_2_S1") == 0 || animation_name.compare(0, 20, "T_TOUCHPLATE_S1_2_S0") == 0) {
          oCMobInter* LeverSwitch = cplayer->npc->FindMobInter(Touchplate);
          TriggerMobTarget(LeverSwitch);
        }
        if (animation_name.compare(0, 16, "T_VWHEEL_S0_2_S1") == 0) {
          oCMobInter* LeverSwitch = cplayer->npc->FindMobInter(VWheel);
          TriggerMobTarget(LeverSwitch);
        }
      }
    }
  }

  const int target_health = life_state == PLAYER_LIFE_DEAD ? 0 : life_state == PLAYER_LIFE_UNCONSCIOUS ? 1 : ClampAliveHealth(cplayer->npc, state.health_points);
  cplayer->base_player().set_health(static_cast<short>(target_health));
  cplayer->base_player().set_update_health_packet_counter(0);
  if (cplayer->npc->GetAttribute(NPC_ATR_HITPOINTS) != target_health) {
    SetNpcHealth(cplayer->npc, target_health);
  }

  // Update mana
  cplayer->npc->attribute[NPC_ATR_MANA] = static_cast<int>(state.mana_points);

  // Update active spell
  oCMag_Book* spell_book = cplayer->npc->GetSpellBook();
  if (spell_book) {
    BYTE SpellNr = terminal_life_state ? 0 : static_cast<BYTE>(state.active_spell_nr);
    if (SpellNr != cplayer->npc->GetActiveSpellNr() && SpellNr > 0 && SpellNr < 100) {
      for (int s = 0; s < spell_book->GetNoOfSpells(); s++) {
        cplayer->npc->Equip(spell_book->GetSpellItem(s));
      }
      oCItem* SpellItem = cplayer->npc->GetSpellItem((int)SpellNr);
      if (SpellItem) {
        cplayer->npc->Equip(SpellItem);
        spell_book->Open(0);
      }
    } else if (SpellNr == 0 && cplayer->npc->GetActiveSpellNr() > 0) {
      spell_book->Close(1);
    } else if (terminal_life_state || cplayer->npc->IsDead()) {
      spell_book->Close(1);
    }
  }

  // Update weapon mode
  const auto weapon_mode = terminal_life_state ? 0 : state.weapon_mode;
  if ((BYTE)cplayer->npc->GetWeaponMode() != weapon_mode) {
    cplayer->npc->SetWeaponMode(weapon_mode);
  }

  // Update head direction
  if (auto* anictrl = cplayer->npc->GetAnictrl()) {
    switch ((Gothic2APlayer::HeadState)state.head_direction) {
      case Gothic2APlayer::HEAD_NONE:
        anictrl->SetLookAtTarget(0.5f, 0.5f);
        break;
      case Gothic2APlayer::HEAD_LEFT:
        anictrl->SetLookAtTarget(0.0f, 0.5f);
        break;
      case Gothic2APlayer::HEAD_RIGHT:
        anictrl->SetLookAtTarget(1.0f, 0.5f);
        break;
      case Gothic2APlayer::HEAD_UP:
        anictrl->SetLookAtTarget(0.5f, 0.0f);
        break;
      case Gothic2APlayer::HEAD_DOWN:
        anictrl->SetLookAtTarget(0.5f, 1.0f);
        break;
    }
  }

  // Update ranged weapon
  SyncEquippedCombatItem(cplayer->npc, state.ranged_weapon_instance, cplayer->npc->GetEquippedRangedWeapon(), IsRangedWeaponItem);

  // Update melee weapon
  SyncEquippedCombatItem(cplayer->npc, state.melee_weapon_instance, cplayer->npc->GetEquippedMeleeWeapon(), IsMeleeWeaponItem);
}

void NetGame::OnPlayerPositionUpdate(std::uint64_t player_id, float x, float y, float z) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (cplayer && cplayer->GetNpc()) {
    cplayer->StopPositionInterpolation();
    cplayer->SetPosition(x, y, z);
  }
}

void NetGame::OnPlayerDied(std::uint64_t player_id) {
  ApplyPlayerLifeState(player_id, PLAYER_LIFE_DEAD, std::nullopt, true);
}

void NetGame::OnPlayerRespawned(std::uint64_t player_id) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (cplayer && cplayer->GetNpc()) {
    cplayer->base_player().set_life_state(PLAYER_LIFE_ALIVE);
    cplayer->RespawnPlayer();
  }
  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerRespawnName, gmp::gothic::PlayerLifecycleEvent{player_id});
}

void NetGame::OnPlayerUnconscious(std::uint64_t player_id, std::optional<std::uint64_t> attacker_id) {
  ApplyPlayerLifeState(player_id, PLAYER_LIFE_UNCONSCIOUS, attacker_id, false);
}

void NetGame::OnPlayerStandUp(std::uint64_t player_id) {
  ApplyPlayerLifeState(player_id, PLAYER_LIFE_ALIVE, std::nullopt, false);
}

void NetGame::OnPlayerPingUpdate(std::uint64_t player_id, std::int32_t ping) {
  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerChangePingName,
                                        gmp::gothic::OnPlayerPingEvent{player_id, ping});
}

void NetGame::OnItemDropped(std::uint64_t player_id, std::uint16_t item_instance, std::uint16_t amount) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (cplayer && cplayer->GetNpc() && IsNetworkItemInstance(item_instance) && ogame && ogame->GetGameWorld()) {
    oCWorld* world = ogame->GetGameWorld();
    oCItem* NpcDrop = CreateNetworkItem(item_instance);
    if (!NpcDrop) {
      return;
    }
    NpcDrop->amount = amount;
    zVEC3 startPos = cplayer->npc->GetTrafoModelNodeToWorld("ZS_RIGHTHAND").GetTranslation();
    NpcDrop->trafoObjToWorld.SetTranslation(startPos);
    world->AddVob(NpcDrop);
    NpcDrop->SetSleeping(false);
    NpcDrop->SetStaticVob(false);
    NpcDrop->SetPhysicsEnabled(true);
  }
}

void NetGame::OnItemTaken(std::uint64_t player_id, std::uint16_t item_instance) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (cplayer && cplayer->GetNpc() && IsNetworkItemInstance(item_instance) && ogame && ogame->GetGameWorld()) {
    zCListSort<oCItem>* ItemList = ogame->GetGameWorld()->voblist_items;
    if (!ItemList) {
      return;
    }
    for (int i = 0; i < ItemList->GetNumInList(); i++) {
      ItemList = ItemList->GetNextInList();
      if (!ItemList) {
        break;
      }
      oCItem* ItemInList = ItemList->GetData();
      if (ItemInList && ItemInList->GetInstance() == item_instance) {
        if (cplayer->npc->GetDistanceToVob(*ItemInList) < 250.0f) {
          cplayer->npc->DoTakeVob(ItemInList);
          break;
        }
      }
    }
  }
}

void NetGame::OnItemGroundCreate(std::uint32_t item_ground_id, const std::string& item_instance, std::int32_t amount,
                                 bool physics_enabled, const glm::vec3& position, const glm::vec3& rotation) {
  gmp::gothic::ClientItemGroundManager::Instance().Upsert(item_ground_id, item_instance, amount, physics_enabled, position, rotation);
}

void NetGame::OnItemGroundDestroy(std::uint32_t item_ground_id) {
  gmp::gothic::ClientItemGroundManager::Instance().Destroy(item_ground_id, true);
}

void NetGame::OnItemsGroundDestroy() {
  gmp::gothic::ClientItemGroundManager::Instance().Clear(true);
}

void NetGame::OnItemGiven(std::uint64_t player_id, const std::string& item_instance, std::int32_t amount) {
  if (amount <= 0) {
    return;
  }

  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  auto index = FindParserIndex(item_instance.c_str());
  if (!index.has_value()) {
    SPDLOG_WARN("Could not find item instance {}", item_instance);
    return;
  }

  std::int32_t amount_to_add = amount;
  if (cplayer->IsLocalPlayer()) {
    auto pending = gmp::gothic::ClientItemGroundManager::Instance().ConsumePendingTake(*index);
    if (pending.has_value()) {
      const std::int32_t expected_amount = pending->previous_amount + amount;
      const std::int32_t current_amount = GetInventoryAmount(cplayer->npc, *index);
      amount_to_add = std::max<std::int32_t>(0, expected_amount - current_amount);
      if (amount_to_add == 0) {
        return;
      }
    }
  }

  if (oCItem* existing = cplayer->npc->inventory2.IsIn(*index, 1)) {
    existing->amount += amount_to_add;
    return;
  }

  oCItem* item = CreateNetworkItem(*index);
  if (!item) {
    SPDLOG_WARN("Could not create item instance {}", item_instance);
    return;
  }

  item->amount = amount_to_add;
  cplayer->npc->inventory2.Insert(item);
}

void NetGame::OnItemEquipped(std::uint64_t player_id, const std::string& item_instance, std::int16_t slot_id) {
  (void)slot_id;

  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  auto index = FindParserIndex(item_instance.c_str());
  if (!index.has_value()) {
    SPDLOG_WARN("Could not find item instance {}", item_instance);
    return;
  }

  oCItem* item = cplayer->npc->inventory2.IsIn(*index, 1);
  if (!item) {
    item = CreateNetworkItem(*index);
    if (!item) {
      SPDLOG_WARN("Could not create item instance {}", item_instance);
      return;
    }
    item->amount = 1;
    cplayer->npc->inventory2.Insert(item);
  }

  const bool suppress = cplayer->IsLocalPlayer();
  if (suppress) {
    SetSuppressLocalEquipEvents(true);
  }
  cplayer->npc->EquipItem(item);
  if (suppress) {
    SetSuppressLocalEquipEvents(false);
  }
}

void NetGame::OnItemUnequipped(std::uint64_t player_id, const std::string& item_instance) {
  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  auto index = FindParserIndex(item_instance.c_str());
  if (!index.has_value()) {
    SPDLOG_WARN("Could not find item instance {}", item_instance);
    return;
  }

  oCItem* item = cplayer->npc->inventory2.IsIn(*index, 1);
  if (!item) {
    return;
  }

  const bool suppress = cplayer->IsLocalPlayer();
  if (suppress) {
    SetSuppressLocalEquipEvents(true);
  }
  cplayer->npc->UnequipItem(item);
  if (suppress) {
    SetSuppressLocalEquipEvents(false);
  }
}

void NetGame::OnItemRemoved(std::uint64_t player_id, const std::string& item_instance, std::int32_t amount) {
  if (amount <= 0) {
    return;
  }

  Gothic2APlayer* cplayer = GetPlayerById(player_id);
  if (!cplayer || !cplayer->GetNpc()) {
    return;
  }

  auto index = FindParserIndex(item_instance.c_str());
  if (!index.has_value()) {
    SPDLOG_WARN("Could not find item instance {}", item_instance);
    return;
  }

  if (cplayer->IsLocalPlayer()) {
    gmp::gothic::ClientItemGroundManager::Instance().ConsumePendingTake(*index);
  }

  oCItem* item = cplayer->npc->inventory2.IsIn(*index, 1);
  if (!item) {
    return;
  }

  const int remove_amount = std::min(amount, item->amount);
  if (remove_amount <= 0) {
    return;
  }

  cplayer->npc->inventory2.Remove(*index, remove_amount);
}

void NetGame::OnSpellCast(std::uint64_t caster_id, std::uint16_t spell_id) {
  Gothic2APlayer* caster = GetPlayerById(caster_id);
  if (!caster || !caster->GetNpc() || spell_id >= 100) {
    return;
  }

  oCSpell* Spell = new oCSpell();
  Spell->InitValues(spell_id);
  Spell->Setup(caster->GetNpc(), 0, 0);
  this->RunSpellLogic(spell_id, caster->GetNpc(), 0);
  this->RunSpellScript(Spell->GetSpellInstanceName(spell_id).ToChar(), caster->GetNpc());
  Spell->Cast();
}

void NetGame::OnSpellCastOnTarget(std::uint64_t caster_id, std::uint64_t target_id, std::uint16_t spell_id) {
  Gothic2APlayer* caster = GetPlayerById(caster_id);
  Gothic2APlayer* target = GetPlayerById(target_id);

  if (!caster || !caster->GetNpc() || !target || !target->GetNpc() || spell_id >= 100) {
    return;
  }

  oCSpell* Spell = new oCSpell;
  Spell->InitValues(spell_id);
  Spell->Setup(caster->GetNpc(), target->GetNpc(), 0);
  this->RunSpellLogic(spell_id, caster->GetNpc(), target->GetNpc());
  this->RunSpellScript(Spell->GetSpellInstanceName(spell_id).ToChar(), caster->GetNpc());
  Spell->Cast();
}

void NetGame::OnPlayerMessage(std::optional<std::uint64_t> sender_id, std::uint8_t r, std::uint8_t g, std::uint8_t b, const std::string& message) {
  if (sender_id) {
    Gothic2APlayer* sender = GetPlayerById(*sender_id);
    if (sender && sender->GetNpc()) {
      SPDLOG_INFO("Message from player {} ({}): {}", sender->base_player().name(), sender->GetNpc()->GetName().ToChar(), message);
    } else if (sender) {
      SPDLOG_INFO("Message from player {}: {}", sender->base_player().name(), message);
    }
  } else {
    SPDLOG_INFO("System message: {}", message);
  }

  EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnPlayerMessageName, gmp::gothic::OnPlayerMessageEvent{sender_id, r, g, b, message});
}

void NetGame::OnLuaEvent(const std::string& event_name, std::uint32_t source_element, const std::string& payload) {
  if (!resource_runtime) {
    SPDLOG_WARN("Ignoring Lua event '{}' because client resources are not loaded", event_name);
    return;
  }

  auto& lua = resource_runtime->GetLuaState();
  std::vector<sol::object> args;
  std::string error;
  if (!gmp::lua::DecodeLuaArgs(lua, payload, args, error)) {
    SPDLOG_ERROR("Failed to decode Lua event '{}' payload: {}", event_name, error);
    return;
  }

  if (!gmp::gothic::TriggerRemoteEvent(event_name, source_element, args)) {
    SPDLOG_WARN("Lua event '{}' rejected or cancelled", event_name);
  }
}
