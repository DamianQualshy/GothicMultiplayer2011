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

#include "external_console_window.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "CChat.h"
#include "ZenGin/zGothicAPI.h"
#include "config.h"
#include "net_game.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace {
static std::unique_ptr<ExternalConsoleWindow> g_instance;
static std::once_flag g_once;
static HWND hwnd = nullptr;

std::vector<std::string> SplitCommandArgs(const std::string& command) {
  std::istringstream stream(command);
  std::vector<std::string> args;
  std::string token;
  while (stream >> token) {
    args.push_back(token);
  }
  return args;
}

std::string BuildWaynetOutputPath(const std::string& world_name) {
  std::filesystem::path world_path(world_name);
  std::string file_stem = world_path.stem().string();
  std::transform(file_stem.begin(), file_stem.end(), file_stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  
  return (std::filesystem::path("Multiplayer") / (file_stem + ".json")).string();
}

bool ExportWaynetToJson(std::string& output_path) {
  using namespace Gothic_II_Addon;

  if (!ogame) {
    SPDLOG_WARN("Cannot generate waynet: game instance is not available.");
    return false;
  }

  zCWorld* world = ogame->GetGameWorld();
  if (!world || !world->wayNet) {
    SPDLOG_WARN("Cannot generate waynet: current world or waynet is not available.");
    return false;
  }

  const std::string world_name = ogame->GetGameWorld()->GetWorldFilename().ToChar();
  std::vector<nlohmann::ordered_json> waypoints_json;

  zCWayNet* way_net = world->wayNet;
  for (zCListSort<zCWaypoint>* waypoint_node = way_net->wplist.next; waypoint_node; waypoint_node = waypoint_node->next) {
    zCWaypoint* waypoint = waypoint_node->data;
    if (!waypoint) {
      continue;
    }

    const zVEC3& position = waypoint->GetPositionWorld();
    nlohmann::ordered_json waypoint_json;
    waypoint_json["name"] = waypoint->GetName().ToChar();
    waypoint_json["position"] = {{"x", position.n[VX]}, {"y", position.n[VY]}, {"z", position.n[VZ]}};
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

  nlohmann::ordered_json root;
  root["zen"] = world_name;
  root["waypoints"] = nlohmann::ordered_json::array();
  for (nlohmann::ordered_json& waypoint_json : waypoints_json) {
     root["waypoints"].push_back(std::move(waypoint_json));
  }

  output_path = BuildWaynetOutputPath(world_name);
  std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
  std::ofstream output(output_path, std::ios::binary);
  if (!output.is_open()) {
    SPDLOG_ERROR("Cannot generate waynet: failed to open output file '{}'.", output_path);
    return false;
  }

  output << root.dump(2);
  output.close();

  SPDLOG_INFO("Waynet exported to {}", std::filesystem::absolute(output_path).string());
  return true;
}

}  // namespace

void ExternalConsoleWindow::Init() {
  std::call_once(g_once, [] { g_instance = std::unique_ptr<ExternalConsoleWindow>(new ExternalConsoleWindow()); });
}

void ExternalConsoleWindow::SavePosition() {
  // Save current console window position to config
  if (hwnd != nullptr) {
    RECT rc{};
    if (::GetWindowRect(hwnd, &rc)) {
      Config::Instance().SetConsolePosition({rc.left, rc.top});
      Config::Instance().SaveConfigToFile();
    }
  }
}

ExternalConsoleWindow::ExternalConsoleWindow() {
  if (EnsureConsoleAvailable()) {
    RedirectStdStreamsToConsole();
    StartInputThread();

    if (auto& opt_pos = Config::Instance().GetConsolePosition(); opt_pos) {
      ::SetWindowPos(hwnd, nullptr, opt_pos->x, opt_pos->y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
}

bool ExternalConsoleWindow::EnsureConsoleAvailable() {
  if (::AllocConsole()) {
    hwnd = ::GetConsoleWindow();
    return true;
  }
  return false;
}

void ExternalConsoleWindow::RedirectStdStreamsToConsole() {
  // Reopen stdout, stderr to console
  FILE* fp;
  // stdout
  freopen_s(&fp, "CONOUT$", "w", stdout);
  setvbuf(stdout, nullptr, _IONBF, 0);
  // stderr
  freopen_s(&fp, "CONOUT$", "w", stderr);
  setvbuf(stderr, nullptr, _IONBF, 0);

  // stdin (optional, but useful for interactive cases)
  freopen_s(&fp, "CONIN$", "r", stdin);
  setvbuf(stdin, nullptr, _IONBF, 0);
}

void ExternalConsoleWindow::StartInputThread() {
  input_thread_ = std::thread([this]() { ProcessConsoleInput(); });
  input_thread_.detach();
}

void ExternalConsoleWindow::ProcessConsoleInput() {
  SPDLOG_INFO("Debug console command input ready. Type 'help' for commands.");
  std::string command;
  while (std::getline(std::cin, command)) {
    command.erase(command.begin(),
                  std::find_if(command.begin(), command.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    command.erase(std::find_if(command.rbegin(), command.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), command.end());
    if (command.empty()) {
      continue;
    }
    ExecuteConsoleCommand(command.c_str());
  }
}

void ExternalConsoleWindow::ExecuteConsoleCommand(const char* command) {
  const std::string raw_command = command;
  std::vector<std::string> args = SplitCommandArgs(raw_command);
  if (args.empty()) {
    return;
  }

  std::string cmd = args[0];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (cmd == "help") {
    SPDLOG_INFO("Available commands: help, gmp_test, generate waynet");
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
      SPDLOG_WARN("Usage: generate waynet");
      return;
    }

    std::string target = args[1];
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (target != "waynet") {
      SPDLOG_WARN("Unsupported generate target: {}. Supported target: waynet", args[1]);
      return;
    }

    auto& game = NetGame::Instance();
    if (!game.task_scheduler) {
      SPDLOG_WARN("Cannot execute generate waynet: task scheduler is not available.");
      return;
    }

    SPDLOG_INFO("Executing command generate waynet");
    game.task_scheduler->ScheduleOnMainThread([]() {
      std::string output_path;
      if (!ExportWaynetToJson(output_path)) {
        return;
      }
      CChat::GetInstance()->WriteMessage(NORMAL, true, ("Waynet exported to " + output_path).c_str());
    });
    return;
  }

  SPDLOG_WARN("Unknown debug console command: {}", command);
}
