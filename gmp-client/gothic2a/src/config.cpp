
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

#pragma warning(disable : 4996 4800)
#include "config.h"

#include <spdlog/spdlog.h>

#include <exception>
#include <filesystem>
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include "renderer/renderer_config.h"
#include "shared/toml_wrapper.h"

using namespace Gothic_II_Addon;

namespace {
constexpr std::string_view kConfigFileName = "GMP_Config.toml";

}  // namespace

Config::Config() {
  config_file_path_ = std::filesystem::current_path() / kConfigFileName;
  LoadConfigFromFile();
}

Config::~Config() {};

void Config::LoadConfigFromFile() {
  DefaultSettings();

  if (!std::filesystem::exists(config_file_path_)) {
    SPDLOG_INFO("GMP configuration not found at {}. Writing defaults.", config_file_path_.string());
    SaveConfigToFile();
    return;
  }

  TomlWrapper toml;
  try {
    toml = TomlWrapper::CreateFromFile(config_file_path_.string());
    is_default_ = false;
  } catch (const std::exception& ex) {
    SPDLOG_INFO("Using default GMP configuration: {}", ex.what());
    return;
  }

  if (auto nickname_opt = toml.GetValue<std::string>("nickname"); nickname_opt) {
    Nickname = nickname_opt->c_str();
  }

  if (auto lang_opt = toml.GetValue<int>("language"); lang_opt) {
    lang = *lang_opt;
  }

  if (std::optional<std::map<std::string, std::int32_t>> window_position = toml.GetValue<std::map<std::string, int>>("window_position")) {
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (auto it = window_position->find("x"); it != window_position->end()) {
      x = it->second;
    }
    if (auto it = window_position->find("y"); it != window_position->end()) {
      y = it->second;
    }
    if (x > 0 && y > 0) {
      window_position_ = WindowPosition{x, y};
    }
  }

  if (std::optional<std::map<std::string, std::int32_t>> console_position = toml.GetValue<std::map<std::string, int>>("console_position")) {
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (auto it = console_position->find("x"); it != console_position->end()) {
      x = it->second;
    }
    if (auto it = console_position->find("y"); it != console_position->end()) {
      y = it->second;
    }
    if (x >= 0 && y >= 0) {
      console_position_ = ConsolePosition{x, y};
    }
  }

  window_always_on_top_ = toml.GetValue<bool>("window_always_on_top", window_always_on_top_);

  if (auto vsync_opt = toml.GetValue<bool>("vsync_enabled"); vsync_opt) {
    vsync_enabled = *vsync_opt;
  }
  // Propagate to renderer config (used by renderers during their init)
  RendererConfig::Instance().vsync_enabled = vsync_enabled;

  // Load renderer type (default: D3D9)
  if (auto renderer_str = toml.GetValue<std::string>("renderer_type"); renderer_str) {
    if (*renderer_str == "D3D7") {
      renderer_type_ = RendererType::D3D7;
    } else if (*renderer_str == "D3D9") {
      renderer_type_ = RendererType::D3D9;
    } else if (*renderer_str == "D3D11") {
      renderer_type_ = RendererType::D3D11;
    }
  }

  // MCP pipe enable flag
  if (auto mcp_opt = toml.GetValue<bool>("mcp_pipe_enabled"); mcp_opt) {
    mcp_pipe_enabled_ = *mcp_opt;
  }

  // Debug console enable flag
  if (auto debug_console_opt = toml.GetValue<bool>("debug_console_enabled"); debug_console_opt) {
    debug_console_enabled_ = *debug_console_opt;
  }

  voice_chat_enabled_ = toml.GetValue<bool>("voice_enabled", voice_chat_enabled_);
  voice_push_to_talk_key_ = toml.GetValue<int>("voice_push_to_talk_key", voice_push_to_talk_key_);
  voice_output_volume_percent_ = toml.GetValue<int>("voice_output_volume", voice_output_volume_percent_);
  if (voice_push_to_talk_key_ <= 0 || voice_push_to_talk_key_ > 255) {
    SPDLOG_WARN("Invalid voice_push_to_talk_key {}; using KEY_K", voice_push_to_talk_key_);
    voice_push_to_talk_key_ = KEY_K;
  }
  voice_output_volume_percent_ = std::clamp(voice_output_volume_percent_, 0, 100);

  // If nickname is empty, the user didn't set up the config yet.
  is_default_ = Nickname.IsEmpty();
}

void Config::DefaultSettings() {
  Nickname.Clear();
  // 0 - polski, 1 - angielski
  lang = 0;
  window_position_.reset();
  console_position_.reset();
  renderer_type_ = RendererType::D3D9;
  mcp_pipe_enabled_ = false;
  debug_console_enabled_ = true;
  voice_chat_enabled_ = true;
  voice_push_to_talk_key_ = KEY_K;
  voice_output_volume_percent_ = 100;
  vsync_enabled = true;
  is_default_ = true;
};

void Config::SaveConfigToFile() {
  TomlWrapper toml;

  toml["nickname"] = Nickname.string();
  toml["language"] = lang;

  if (window_position_) {
    std::unordered_map<std::string, toml::value> window_position_map;
    window_position_map["x"] = toml::value(window_position_->x);
    window_position_map["y"] = toml::value(window_position_->y);
    toml["window_position"] = window_position_map;
  }

  if (console_position_) {
    std::unordered_map<std::string, toml::value> console_position_map;
    console_position_map["x"] = toml::value(console_position_->x);
    console_position_map["y"] = toml::value(console_position_->y);
    toml["console_position"] = console_position_map;
  }

  toml["window_always_on_top"] = toml::value(window_always_on_top_);
  toml["vsync_enabled"] = toml::value(vsync_enabled);
  toml["mcp_pipe_enabled"] = toml::value(mcp_pipe_enabled_);
  toml["debug_console_enabled"] = toml::value(debug_console_enabled_);
  toml["voice_enabled"] = toml::value(voice_chat_enabled_);
  toml["voice_push_to_talk_key"] = toml::value(voice_push_to_talk_key_);
  toml["voice_output_volume"] = toml::value(voice_output_volume_percent_);

  // Save renderer type as string
  std::string renderer_str;
  switch (renderer_type_) {
    case RendererType::D3D7:
      renderer_str = "D3D7";
      break;
    case RendererType::D3D9:
      renderer_str = "D3D9";
      break;
    case RendererType::D3D11:
      renderer_str = "D3D11";
      break;
  }
  toml["renderer_type"] = toml::value(renderer_str);

  toml.Serialize(config_file_path_.string());
  is_default_ = Nickname.IsEmpty();
}

const std::optional<Config::WindowPosition>& Config::GetWindowPosition() const {
  return window_position_;
}

void Config::SetWindowPosition(WindowPosition window_position) {
  window_position_ = window_position;
}

const std::optional<Config::ConsolePosition>& Config::GetConsolePosition() const {
  return console_position_;
}

void Config::SetConsolePosition(ConsolePosition console_position) {
  console_position_ = console_position;
}

bool Config::IsDefault() const {
  return is_default_;
}
