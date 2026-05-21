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

#include "external_console_commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "CChat.h"
#include "ZenGin/Gothic_II_Addon/API/zParser_Const.h"
#include "ZenGin/zGothicAPI.h"
#include "net_game.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace {

std::vector<std::string> SplitCommandArgs(const std::string& command) {
  std::istringstream stream(command);
  std::vector<std::string> args;
  std::string token;
  while (stream >> token) {
    args.push_back(token);
  }
  return args;
}

std::string BuildWayfileOutputPath(const std::string& world_name) {
  std::filesystem::path world_path(world_name);
  std::string file_stem = world_path.stem().string();
  std::transform(file_stem.begin(), file_stem.end(), file_stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  return (std::filesystem::path("Multiplayer") / (file_stem + ".json")).string();
}

std::string BuildItemsOutputPath() {
  return (std::filesystem::path("Multiplayer") / "items.json").string();
}

std::string ToString(const Gothic_II_Addon::zSTRING& value) {
  const char* text = value.ToChar();
  return text ? std::string(text) : std::string{};
}

std::optional<std::string> ResolveParserSymbolName(Gothic_II_Addon::zCParser* parser, int index) {
  if (!parser || index <= 0) {
    return std::nullopt;
  }

  Gothic_II_Addon::zCPar_Symbol* symbol = parser->GetSymbol(index);
  if (!symbol) {
    return std::nullopt;
  }

  std::string name = ToString(symbol->name);
  if (name.empty()) {
    return std::nullopt;
  }

  return name;
}

nlohmann::ordered_json MakePositionJson(const Gothic_II_Addon::zVEC3& position) {
  return {{"x", position.n[Gothic_II_Addon::VX]}, {"y", position.n[Gothic_II_Addon::VY]}, {"z", position.n[Gothic_II_Addon::VZ]}};
}

float GetAngleDegreesFromDirection(const Gothic_II_Addon::zVEC3& direction) {
  const float x = direction.n[Gothic_II_Addon::VX];
  const float z = direction.n[Gothic_II_Addon::VZ];

  float angle = std::atan2(x, z) * Gothic_II_Addon::DEGREE;
  if (angle < 0.0f) {
    angle += 360.0f;
  }
  return angle;
}

void AddAngle(nlohmann::ordered_json& position_json, const Gothic_II_Addon::zVEC3& direction) {
  position_json["angle"] = GetAngleDegreesFromDirection(direction);
}

void AddWaypointAngle(nlohmann::ordered_json& position_json, Gothic_II_Addon::zCWaypoint* waypoint) {
  if (!waypoint) {
    return;
  }

  if (Gothic_II_Addon::zCVobWaypoint* waypoint_vob = waypoint->GetVob()) {
    AddAngle(position_json, waypoint_vob->GetAtVectorWorld());
    return;
  }

  AddAngle(position_json, waypoint->dir);
}

void CollectFreepointsJson(Gothic_II_Addon::zCWorld* world, std::vector<nlohmann::ordered_json>& freepoints_json) {
  using namespace Gothic_II_Addon;

  if (!world) {
    return;
  }

  zCArray<zCVob*> freepoint_vobs;
  world->SearchVobListByClass(zCVobSpot::classDef, freepoint_vobs, &world->globalVobTree);
  for (int i = 0; i < freepoint_vobs.GetNumInList(); ++i) {
    zCVob* vob = freepoint_vobs[i];
    if (!vob) {
      continue;
    }

    zCVobSpot* freepoint = static_cast<zCVobSpot*>(vob);
    nlohmann::ordered_json freepoint_json;
    freepoint_json["name"] = freepoint->GetObjectName().ToChar();

    nlohmann::ordered_json position_json = MakePositionJson(freepoint->GetPositionWorld());
    AddAngle(position_json, freepoint->GetAtVectorWorld());
    freepoint_json["position"] = std::move(position_json);

    freepoints_json.push_back(std::move(freepoint_json));
  }
}

void SortNamedPositions(std::vector<nlohmann::ordered_json>& entries) {
  std::sort(entries.begin(), entries.end(), [](const nlohmann::ordered_json& left, const nlohmann::ordered_json& right) {
    const std::string left_name = left["name"].get<std::string>();
    const std::string right_name = right["name"].get<std::string>();
    if (left_name != right_name) {
      return left_name < right_name;
    }

    const nlohmann::ordered_json& left_position = left["position"];
    const nlohmann::ordered_json& right_position = right["position"];
    if (left_position["x"].get<float>() != right_position["x"].get<float>()) {
      return left_position["x"].get<float>() < right_position["x"].get<float>();
    }
    if (left_position["y"].get<float>() != right_position["y"].get<float>()) {
      return left_position["y"].get<float>() < right_position["y"].get<float>();
    }
    return left_position["z"].get<float>() < right_position["z"].get<float>();
  });
}

bool CollectWaypointsJson(Gothic_II_Addon::zCWorld* world, std::vector<nlohmann::ordered_json>& waypoints_json) {
  using namespace Gothic_II_Addon;

  if (!world || !world->wayNet) {
    SPDLOG_WARN("Cannot generate wayfile: current world or waynet is not available.");
    return false;
  }

  zCWayNet* way_net = world->wayNet;
  for (zCListSort<zCWaypoint>* waypoint_node = way_net->wplist.next; waypoint_node; waypoint_node = waypoint_node->next) {
    zCWaypoint* waypoint = waypoint_node->data;
    if (!waypoint) {
      continue;
    }

    const zVEC3& position = waypoint->GetPositionWorld();
    nlohmann::ordered_json waypoint_json;
    waypoint_json["name"] = waypoint->GetName().ToChar();
    nlohmann::ordered_json position_json = MakePositionJson(position);
    AddWaypointAngle(position_json, waypoint);
    waypoint_json["position"] = std::move(position_json);
    waypoint_json["connectsTo"] = nlohmann::ordered_json::array();

    std::unordered_set<std::string> connected_names;
    zCList<zCWay>& ways = waypoint->GetWayList();
    for (zCList<zCWay>* way_node = ways.next; way_node; way_node = way_node->next) {
      zCWay* way = way_node->data;
      if (!way) {
        continue;
      }
      zCWaypoint* connected_waypoint = way->GetGoalWaypoint(waypoint);
      if (!connected_waypoint) {
        continue;
      }
      connected_names.insert(connected_waypoint->GetName().ToChar());
    }

    std::vector<std::string> sorted_connections(connected_names.begin(), connected_names.end());
    std::sort(sorted_connections.begin(), sorted_connections.end());
    for (const std::string& connected_name : sorted_connections) {
      waypoint_json["connectsTo"].push_back(connected_name);
    }

    waypoints_json.push_back(std::move(waypoint_json));
  }

  std::sort(waypoints_json.begin(), waypoints_json.end(),
            [](const nlohmann::ordered_json& left, const nlohmann::ordered_json& right) {
              return left["name"].get<std::string>() < right["name"].get<std::string>();
            });

  return true;
}

bool ExportWayfileToJson(std::string& output_path) {
  using namespace Gothic_II_Addon;

  if (!ogame) {
    SPDLOG_WARN("Cannot generate wayfile: game instance is not available.");
    return false;
  }

  oCWorld* world = ogame->GetGameWorld();
  if (!world) {
    SPDLOG_WARN("Cannot generate wayfile: current world is not available.");
    return false;
  }

  const std::string world_name = world->GetWorldFilename().ToChar();
  std::vector<nlohmann::ordered_json> waypoints_json;
  std::vector<nlohmann::ordered_json> freepoints_json;
  if (!CollectWaypointsJson(world, waypoints_json)) {
    return false;
  }

  CollectFreepointsJson(world, freepoints_json);
  SortNamedPositions(freepoints_json);

  nlohmann::ordered_json root;
  root["zen"] = world_name;
  root["waypoints"] = nlohmann::ordered_json::array();
  for (nlohmann::ordered_json& waypoint_json : waypoints_json) {
    root["waypoints"].push_back(std::move(waypoint_json));
  }

  root["freepoints"] = nlohmann::ordered_json::array();
  for (nlohmann::ordered_json& freepoint_json : freepoints_json) {
    root["freepoints"].push_back(std::move(freepoint_json));
  }

  output_path = BuildWayfileOutputPath(world_name);
  std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
  std::ofstream output(output_path, std::ios::binary);
  if (!output.is_open()) {
    SPDLOG_ERROR("Cannot generate wayfile: failed to open output file '{}'.", output_path);
    return false;
  }

  output << root.dump(2);
  output.close();

  SPDLOG_INFO("Wayfile exported to {}", std::filesystem::absolute(output_path).string());
  return true;
}

void AddOptionalInt(nlohmann::ordered_json& json, const char* key, int value) {
  if (value != 0) {
    json[key] = value;
  }
}

void AddOptionalString(nlohmann::ordered_json& json, const char* key, const Gothic_II_Addon::zSTRING& value) {
  std::string text = ToString(value);
  if (!text.empty()) {
    json[key] = std::move(text);
  }
}

void AddDamageJson(nlohmann::ordered_json& item_json, const Gothic_II_Addon::oCItem& item) {
  if (item.damageTotal == 0 && item.damageTypes == 0) {
    return;
  }

  nlohmann::ordered_json damage_json;
  if (item.damageTotal != 0) {
    damage_json["total"] = item.damageTotal;
  }
  if (item.damageTypes != 0) {
    damage_json["types"] = item.damageTypes;
  }
  item_json["damage"] = std::move(damage_json);
}

void AddProtectionsJson(nlohmann::ordered_json& item_json, const Gothic_II_Addon::oCItem& item) {
  nlohmann::ordered_json protections_json = nlohmann::ordered_json::array();
  for (int i = 0; i < Gothic_II_Addon::oEDamageIndex_MAX; ++i) {
    const int value = item.protection[i];
    if (value == 0) {
      continue;
    }

    protections_json.push_back(nlohmann::ordered_json{{"type", i}, {"value", value}});
  }

  if (!protections_json.empty()) {
    item_json["protections"] = std::move(protections_json);
  }
}

void AddConditionsJson(nlohmann::ordered_json& item_json, const Gothic_II_Addon::oCItem& item) {
  nlohmann::ordered_json conditions_json = nlohmann::ordered_json::array();
  for (int i = 0; i < Gothic_II_Addon::ITM_COND_MAX; ++i) {
    const int attribute = item.cond_atr[i];
    const int value = item.cond_value[i];
    if (attribute == 0 || value == 0) {
      continue;
    }

    conditions_json.push_back(nlohmann::ordered_json{{"attribute", attribute}, {"value", value}});
  }

  if (!conditions_json.empty()) {
    item_json["conditions"] = std::move(conditions_json);
  }
}

nlohmann::ordered_json MakeItemJson(Gothic_II_Addon::zCParser* parser, int index, Gothic_II_Addon::oCItem& item) {
  nlohmann::ordered_json item_json;
  item_json["instance"] = ResolveParserSymbolName(parser, index).value_or(ToString(item.GetInstanceName()));
  item_json["index"] = index;
  item_json["mainflag"] = item.mainflag;
  item_json["flags"] = item.flags;

  AddOptionalString(item_json, "visual", item.file);
  AddOptionalInt(item_json, "wear", item.wear);
  AddOptionalInt(item_json, "range", item.range);
  AddOptionalInt(item_json, "value", item.value);
  AddDamageJson(item_json, item);

  if (item.munition != 0) {
    if (std::optional<std::string> munition = ResolveParserSymbolName(parser, item.munition)) {
      item_json["munition"] = *munition;
    } else {
      SPDLOG_WARN("Cannot resolve munition parser index {} for item {}", item.munition, item_json["instance"].get<std::string>());
    }
  }

  AddOptionalInt(item_json, "spell", item.spell);
  AddOptionalString(item_json, "scemename", item.scemeName);
  AddOptionalInt(item_json, "mag_circle", item.mag_circle);
  AddProtectionsJson(item_json, item);
  AddConditionsJson(item_json, item);
  return item_json;
}

bool CollectItemsJson(std::vector<nlohmann::ordered_json>& items_json) {
  using namespace Gothic_II_Addon;

  zCParser* parser = zCParser::GetParser();
  if (!parser || !zfactory) {
    SPDLOG_WARN("Cannot generate items: parser or object factory is not available.");
    return false;
  }

  int item_class_index = parser->GetIndex("C_ITEM");
  if (item_class_index < 0) {
    item_class_index = parser->GetIndex("C_Item");
  }
  if (item_class_index < 0) {
    SPDLOG_WARN("Cannot generate items: C_Item class was not found in the parser.");
    return false;
  }

  std::unordered_set<int> exported_indexes;
  const int symbol_count = parser->symtab.GetNumInList();
  for (int index = 0; index < symbol_count; ++index) {
    zCPar_Symbol* symbol = parser->GetSymbol(index);
    if (!symbol || symbol->type != zPAR_TYPE_INSTANCE || parser->GetBaseClass(symbol) != item_class_index) {
      continue;
    }
    if (!exported_indexes.insert(index).second) {
      continue;
    }

    oCItem* item = zfactory->CreateItem(index);
    if (!item) {
      SPDLOG_WARN("Cannot generate items: failed to create item instance '{}'.", ToString(symbol->name));
      continue;
    }

    item->AddRef();
    items_json.push_back(MakeItemJson(parser, index, *item));
    item->Release();
  }

  std::sort(items_json.begin(), items_json.end(), [](const nlohmann::ordered_json& left, const nlohmann::ordered_json& right) {
    const int left_index = left["index"].get<int>();
    const int right_index = right["index"].get<int>();
    if (left_index != right_index) {
      return left_index < right_index;
    }
    return left["instance"].get<std::string>() < right["instance"].get<std::string>();
  });

  return true;
}

bool ExportItemsToJson(std::string& output_path) {
  std::vector<nlohmann::ordered_json> items_json;
  if (!CollectItemsJson(items_json)) {
    return false;
  }

  nlohmann::ordered_json root;
  root["schema"] = 1;
  root["items"] = nlohmann::ordered_json::array();
  for (nlohmann::ordered_json& item_json : items_json) {
    root["items"].push_back(std::move(item_json));
  }

  output_path = BuildItemsOutputPath();
  std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
  std::ofstream output(output_path, std::ios::binary);
  if (!output.is_open()) {
    SPDLOG_ERROR("Cannot generate items: failed to open output file '{}'.", output_path);
    return false;
  }

  output << root.dump(2);
  output.close();

  SPDLOG_INFO("Item catalog exported to {} ({} items)", std::filesystem::absolute(output_path).string(), items_json.size());
  return true;
}

}  // namespace

void ExecuteExternalConsoleCommand(const char* command) {
  const std::string raw_command = command;
  std::vector<std::string> args = SplitCommandArgs(raw_command);
  if (args.empty()) {
    return;
  }

  std::string cmd = args[0];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (cmd == "help") {
    SPDLOG_INFO("Available commands: help, gmp_test, generate wayfile, generate items");
    return;
  }

  if (cmd == "gmp_test") {
    SPDLOG_INFO("Executing command gmp_test");
    auto& game = NetGame::Instance();
    if (game.task_scheduler) {
      game.task_scheduler->ScheduleOnMainThread([]() {
        CChat::GetInstance()->WriteMessage(NORMAL, true, "Console test command executed.");
      });
    }
    return;
  }

  if (cmd == "generate") {
    if (args.size() != 2) {
      SPDLOG_WARN("Usage: generate wayfile|items");
      return;
    }

    std::string target = args[1];
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (target != "wayfile" && target != "items") {
      SPDLOG_WARN("Unsupported generate target: {}. Supported targets: wayfile, items", args[1]);
      return;
    }

    auto& game = NetGame::Instance();
    if (!game.task_scheduler) {
      SPDLOG_WARN("Cannot execute generate {}: task scheduler is not available.", target);
      return;
    }

    SPDLOG_INFO("Executing command generate {}", target);
    if (target == "wayfile") {
      game.task_scheduler->ScheduleOnMainThread([]() {
        std::string output_path;
        if (!ExportWayfileToJson(output_path)) {
          return;
        }
        const std::string message = "Wayfile exported to " + output_path;
        CChat::GetInstance()->WriteMessage(NORMAL, true, message.c_str());
      });
      return;
    }

    if (target == "items") {
      game.task_scheduler->ScheduleOnMainThread([]() {
        std::string output_path;
        if (!ExportItemsToJson(output_path)) {
          return;
        }
        const std::string message = "Item catalog exported to " + output_path;
        CChat::GetInstance()->WriteMessage(NORMAL, true, message.c_str());
      });
      return;
    }
  }

  SPDLOG_WARN("Unknown debug console command: {}", command);
}
