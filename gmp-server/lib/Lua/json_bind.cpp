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

#include "Lua/json_bind.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace lua::bindings {

namespace {

constexpr std::string_view kDataRoot = "data";
constexpr std::size_t kMaxJsonBytes = 1024 * 1024;
constexpr std::size_t kMaxEntries = 4096;
constexpr int kMaxDepth = 32;

using Json = NLOHMANN_JSON_NAMESPACE::json;

std::filesystem::path DataRootPath() {
  return std::filesystem::current_path() / std::filesystem::path{kDataRoot};
}

const void* GetLuaIdentity(const sol::object& obj) {
  lua_State* state = obj.lua_state();
  sol::stack::push(state, obj);
  const void* identity = lua_topointer(state, -1);
  lua_pop(state, 1);
  return identity;
}

bool EncodeLuaValue(sol::state_view lua, const sol::object& value, Json& out, std::string& error, int depth,
                    std::unordered_set<const void*>& visited) {
  (void)lua;
  if (depth > kMaxDepth) {
    error = "Lua value exceeded maximum serialization depth";
    return false;
  }

  switch (value.get_type()) {
    case sol::type::nil:
      out = nullptr;
      return true;
    case sol::type::boolean:
      out = value.as<bool>();
      return true;
    case sol::type::number:
      out = value.as<double>();
      return true;
    case sol::type::string:
      out = value.as<std::string>();
      return true;
    case sol::type::table: {
      const void* identity = GetLuaIdentity(value);
      if (identity != nullptr && !visited.insert(identity).second) {
        error = "Lua table contains a cyclic reference";
        return false;
      }

      sol::table table = value.as<sol::table>();
      bool has_string_keys = false;
      bool has_number_keys = false;
      std::size_t max_index = 0;
      std::size_t count = 0;

      for (const auto& kv : table) {
        sol::object key = kv.first;
        if (key.get_type() == sol::type::string) {
          has_string_keys = true;
        } else if (key.get_type() == sol::type::number) {
          has_number_keys = true;
          const double raw = key.as<double>();
          if (raw < 1.0 || std::floor(raw) != raw) {
            error = "Lua table numeric key is not a positive integer";
            if (identity != nullptr) {
              visited.erase(identity);
            }
            return false;
          }
          max_index = std::max(max_index, static_cast<std::size_t>(raw));
        } else {
          error = "Lua table contains unsupported key type";
          if (identity != nullptr) {
            visited.erase(identity);
          }
          return false;
        }
        ++count;
      }

      if (has_string_keys && has_number_keys) {
        error = "Lua table mixes string and numeric keys";
        if (identity != nullptr) {
          visited.erase(identity);
        }
        return false;
      }

      if (has_number_keys) {
        if (count != max_index) {
          error = "Lua array has gaps";
          if (identity != nullptr) {
            visited.erase(identity);
          }
          return false;
        }
        std::vector<Json> array_items(max_index);
        for (const auto& kv : table) {
          const auto index = static_cast<std::size_t>(kv.first.as<double>());
          Json entry;
          if (!EncodeLuaValue(lua, kv.second, entry, error, depth + 1, visited)) {
            if (identity != nullptr) {
              visited.erase(identity);
            }
            return false;
          }
          array_items[index - 1] = std::move(entry);
        }
        Json array = Json::array();
        for (auto& item : array_items) {
          array.push_back(std::move(item));
        }
        out = std::move(array);
      } else {
        Json object = Json::object();
        for (const auto& kv : table) {
          const auto key = kv.first.as<std::string>();
          Json entry;
          if (!EncodeLuaValue(lua, kv.second, entry, error, depth + 1, visited)) {
            if (identity != nullptr) {
              visited.erase(identity);
            }
            return false;
          }
          object[key] = std::move(entry);
        }
        out = std::move(object);
      }

      if (identity != nullptr) {
        visited.erase(identity);
      }
      return true;
    }
    default:
      error = "Unsupported Lua value type";
      return false;
  }
}

bool DecodeJsonValue(sol::state_view lua, const Json& value, sol::object& out, std::string& error, int depth) {
  if (depth > kMaxDepth) {
    error = "JSON value exceeded maximum deserialization depth";
    return false;
  }

  if (value.is_null()) {
    out = sol::make_object(lua, sol::lua_nil);
    return true;
  }
  if (value.is_boolean()) {
    out = sol::make_object(lua, value.get<bool>());
    return true;
  }
  if (value.is_number()) {
    out = sol::make_object(lua, value.get<double>());
    return true;
  }
  if (value.is_string()) {
    out = sol::make_object(lua, value.get<std::string>());
    return true;
  }
  if (value.is_array()) {
    sol::table table = lua.create_table(static_cast<int>(value.size()), 0);
    for (std::size_t i = 0; i < value.size(); ++i) {
      sol::object entry;
      if (!DecodeJsonValue(lua, value[i], entry, error, depth + 1)) {
        return false;
      }
      table[i + 1] = entry;
    }
    out = sol::make_object(lua, table);
    return true;
  }
  if (value.is_object()) {
    sol::table table = lua.create_table();
    for (auto it = value.begin(); it != value.end(); ++it) {
      sol::object entry;
      if (!DecodeJsonValue(lua, it.value(), entry, error, depth + 1)) {
        return false;
      }
      table[it.key()] = entry;
    }
    out = sol::make_object(lua, table);
    return true;
  }

  error = "Unsupported JSON value type";
  return false;
}

bool IsRelativePathSafe(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  for (const auto& part : path) {
    if (part == "..") {
      return false;
    }
  }
  return true;
}

std::optional<std::filesystem::path> ResolveDataPath(const std::string& relative) {
  std::filesystem::path requested(relative);
  if (!IsRelativePathSafe(requested)) {
    return std::nullopt;
  }
  std::filesystem::path normalized = requested.lexically_normal();
  std::filesystem::path root = DataRootPath();
  std::filesystem::path full = (root / normalized).lexically_normal();
  auto full_string = full.generic_string();
  auto root_string = root.lexically_normal().generic_string();
  if (!root_string.empty() && root_string.back() != '/') {
    root_string.push_back('/');
  }
  if (full_string == root.lexically_normal().generic_string()) {
    return full;
  }
  if (full_string.rfind(root_string, 0) != 0) {
    return std::nullopt;
  }
  return full;
}

bool EnsureParentDirectory(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "Failed to create parent directory: " + ec.message();
    return false;
  }
  return true;
}

class JsonFile {
public:
  explicit JsonFile(std::filesystem::path path) : path_(std::move(path)) {}

  bool Load(std::string& error) {
    entries_.clear();
    exists_ = false;

    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
      return true;
    }
    if (ec) {
      error = "Failed to stat JSON file: " + ec.message();
      return false;
    }

    auto size = std::filesystem::file_size(path_, ec);
    if (ec) {
      error = "Failed to get JSON file size: " + ec.message();
      return false;
    }
    if (size > kMaxJsonBytes) {
      error = "JSON file exceeds size limit";
      return false;
    }

    std::ifstream file(path_, std::ios::binary);
    if (!file.good()) {
      error = "Unable to open JSON file";
      return false;
    }

    Json json;
    try {
      file >> json;
    } catch (const nlohmann::json::parse_error& ex) {
      error = std::string("Failed to parse JSON: ") + ex.what();
      return false;
    }

    if (!json.is_object()) {
      error = "JSON root must be an object";
      return false;
    }

    for (auto it = json.begin(); it != json.end(); ++it) {
      if (entries_.size() >= kMaxEntries) {
        error = "JSON entry limit exceeded";
        return false;
      }
      entries_.emplace_back(it.key(), it.value());
    }

    exists_ = true;
    return true;
  }

  bool Save(std::string& error) const {
    if (!EnsureParentDirectory(path_, error)) {
      return false;
    }

    if (entries_.size() > kMaxEntries) {
      error = "JSON entry limit exceeded";
      return false;
    }

    Json json = Json::object();
    for (const auto& entry : entries_) {
      json[entry.key] = entry.value;
    }

    std::string payload = json.dump(2);
    if (payload.size() > kMaxJsonBytes) {
      error = "JSON payload exceeds size limit";
      return false;
    }

    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
      error = "Unable to open JSON file for writing";
      return false;
    }
    file << payload << '\n';
    if (!file.good()) {
      error = "Failed to write JSON file";
      return false;
    }
    return true;
  }

  bool Exists() const {
    return exists_;
  }

  std::size_t EntryCount() const {
    return entries_.size();
  }

  sol::object KeyAt(sol::state_view lua, std::size_t index) const {
    if (index >= entries_.size()) {
      return sol::make_object(lua, sol::lua_nil);
    }
    return sol::make_object(lua, entries_[index].key);
  }

  sol::object GetValue(sol::state_view lua, const std::string& key, std::string& error) const {
    const Entry* entry = FindKey(key);
    if (!entry) {
      return sol::make_object(lua, sol::lua_nil);
    }
    sol::object value;
    if (!DecodeJsonValue(lua, entry->value, value, error, 0)) {
      return sol::make_object(lua, sol::lua_nil);
    }
    return value;
  }

  bool SetValue(sol::state_view lua, const std::string& key, const sol::object& value, std::string& error) {
    Json encoded;
    std::unordered_set<const void*> visited;
    if (!EncodeLuaValue(lua, value, encoded, error, 0, visited)) {
      return false;
    }

    if (Entry* entry = FindKey(key)) {
      entry->value = std::move(encoded);
      return true;
    }

    if (entries_.size() >= kMaxEntries) {
      error = "JSON entry limit exceeded";
      return false;
    }
    entries_.push_back({key, std::move(encoded)});
    return true;
  }

  bool RemoveKey(const std::string& key) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->key == key) {
        entries_.erase(it);
        return true;
      }
    }
    return false;
  }

  void Clear() {
    entries_.clear();
  }

private:
  struct Entry {
    std::string key;
    Json value;
  };

  Entry* FindKey(const std::string& key) {
    for (auto& entry : entries_) {
      if (entry.key == key) {
        return &entry;
      }
    }
    return nullptr;
  }

  const Entry* FindKey(const std::string& key) const {
    for (const auto& entry : entries_) {
      if (entry.key == key) {
        return &entry;
      }
    }
    return nullptr;
  }

  std::filesystem::path path_;
  std::vector<Entry> entries_;
  bool exists_ = false;
};

std::optional<JsonFile> LoadJsonFile(const std::string& relative_path, std::string& error) {
  auto resolved = ResolveDataPath(relative_path);
  if (!resolved) {
    error = "Invalid JSON path";
    return std::nullopt;
  }
  JsonFile file(*resolved);
  if (!file.Load(error)) {
    return std::nullopt;
  }
  return file;
}

}  // namespace

void BindJson(sol::state& lua) {
  sol::usertype<JsonFile> json_type = lua.new_usertype<JsonFile>("JsonFile", sol::no_constructor);
  json_type["key"] = [](const JsonFile& file, int index, sol::this_state ts) {
    sol::state_view lua_state(ts);
    if (index < 0) {
      return sol::make_object(lua_state, sol::lua_nil);
    }
    return file.KeyAt(lua_state, static_cast<std::size_t>(index));
  };
  json_type["len"] = [](const JsonFile& file) { return static_cast<int>(file.EntryCount()); };
  json_type["getItem"] = [](const JsonFile& file, const std::string& key, sol::this_state ts) {
    sol::state_view lua_state(ts);
    std::string error;
    sol::object value = file.GetValue(lua_state, key, error);
    if (!error.empty()) {
      SPDLOG_WARN("JSON getItem failed for key '{}': {}", key, error);
    }
    return value;
  };
  json_type["setItem"] = [](JsonFile& file, const std::string& key, const sol::object& value, sol::this_state ts) {
    sol::state_view lua_state(ts);
    std::string error;
    if (!file.SetValue(lua_state, key, value, error)) {
      SPDLOG_WARN("JSON setItem failed for key '{}': {}", key, error);
      return;
    }
    if (!file.Save(error)) {
      SPDLOG_WARN("JSON save failed for key '{}': {}", key, error);
    }
  };
  json_type["removeItem"] = [](JsonFile& file, const std::string& key) {
    file.RemoveKey(key);
    std::string error;
    if (!file.Save(error)) {
      SPDLOG_WARN("JSON save failed after removeItem for key '{}': {}", key, error);
    }
  };
  json_type["clear"] = [](JsonFile& file) {
    file.Clear();
    std::string error;
    if (!file.Save(error)) {
      SPDLOG_WARN("JSON save failed after clear: {}", error);
    }
  };

  lua["JSON"] = [&lua](const std::string& relative_path) -> sol::object {
    std::string error;
    auto file = LoadJsonFile(relative_path, error);
    if (!file) {
      if (!error.empty() && error != "Invalid JSON path") {
        SPDLOG_WARN("JSON open failed for '{}': {}", relative_path, error);
      }
      return sol::make_object(lua, sol::lua_nil);
    }
    return sol::make_object(lua, std::move(*file));
  };
}

}  // namespace lua::bindings
