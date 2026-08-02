/*
MIT License

Copyright (c) 2026 Gothic Multiplayer Team.

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

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <toml.hpp>

#include "sol/sol.hpp"

namespace lua::bindings {

namespace {

constexpr std::string_view kDataRoot = "data";
constexpr std::size_t kMaxTomlBytes = 1024 * 1024;
constexpr std::size_t kMaxEntries = 4096;
constexpr int kMaxDepth = 32;

using TomlValue = toml::ordered_value;

struct TomlPathEntry {
  std::string key;
  std::size_t index = 0;
  bool is_index = false;
};

std::filesystem::path DataRootPath() {
  return std::filesystem::current_path() / std::filesystem::path{kDataRoot};
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

const TomlValue* ResolvePath(const TomlValue& root, const std::vector<TomlPathEntry>& path) {
  const TomlValue* current = &root;
  for (const auto& entry : path) {
    if (entry.is_index) {
      if (!current->is_array()) {
        return nullptr;
      }

      const auto& array = current->as_array();
      if (entry.index >= array.size()) {
        return nullptr;
      }
      current = &array[entry.index];
      continue;
    }

    if (!current->is_table()) {
      return nullptr;
    }

    const auto& table = current->as_table();
    const auto it = table.find(entry.key);
    if (it == table.end()) {
      return nullptr;
    }
    current = &it->second;
  }
  return current;
}

std::string TomlScalarToString(const TomlValue& value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

bool IsIntegralLuaNumber(double value) {
  return std::isfinite(value) && value >= 1.0 && std::floor(value) == value;
}

std::optional<TomlPathEntry> PathEntryFromLuaKey(const sol::object& key) {
  switch (key.get_type()) {
    case sol::type::string:
      return TomlPathEntry{key.as<std::string>(), 0, false};
    case sol::type::number: {
      const double raw_index = key.as<double>();
      if (!IsIntegralLuaNumber(raw_index)) {
        return std::nullopt;
      }
      return TomlPathEntry{{}, static_cast<std::size_t>(raw_index - 1.0), true};
    }
    default:
      return std::nullopt;
  }
}

std::vector<std::string> SplitDottedPath(const std::string& path) {
  std::vector<std::string> segments;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t dot = path.find('.', start);
    const std::size_t end = dot == std::string::npos ? path.size() : dot;
    if (end == start) {
      return {};
    }
    segments.push_back(path.substr(start, end - start));
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  return segments;
}

std::optional<std::vector<TomlPathEntry>> PathFromLuaValue(const sol::object& path) {
  std::vector<TomlPathEntry> result;

  switch (path.get_type()) {
    case sol::type::string: {
      const auto segments = SplitDottedPath(path.as<std::string>());
      if (segments.empty()) {
        return std::nullopt;
      }
      result.reserve(segments.size());
      for (const auto& segment : segments) {
        result.push_back({segment, 0, false});
      }
      return result;
    }
    case sol::type::number: {
      auto entry = PathEntryFromLuaKey(path);
      if (!entry) {
        return std::nullopt;
      }
      result.push_back(std::move(*entry));
      return result;
    }
    case sol::type::table: {
      sol::table table = path.as<sol::table>();
      for (int index = 1;; ++index) {
        sol::object entry_object = table[index];
        if (entry_object.get_type() == sol::type::nil) {
          break;
        }

        auto entry = PathEntryFromLuaKey(entry_object);
        if (!entry) {
          return std::nullopt;
        }
        result.push_back(std::move(*entry));
      }
      if (result.empty()) {
        return std::nullopt;
      }
      return result;
    }
    default:
      return std::nullopt;
  }
}

bool ValidateTomlValue(const TomlValue& value, std::string& error, int depth, std::size_t& entries) {
  if (depth > kMaxDepth) {
    error = "TOML value exceeded maximum depth";
    return false;
  }

  if (++entries > kMaxEntries) {
    error = "TOML entry limit exceeded";
    return false;
  }

  if (value.is_table()) {
    for (const auto& entry : value.as_table()) {
      if (!ValidateTomlValue(entry.second, error, depth + 1, entries)) {
        return false;
      }
    }
    return true;
  }

  if (value.is_array()) {
    for (const auto& entry : value.as_array()) {
      if (!ValidateTomlValue(entry, error, depth + 1, entries)) {
        return false;
      }
    }
  }

  return true;
}

class TomlFile {
public:
  TomlFile(std::shared_ptr<const TomlValue> root, std::vector<TomlPathEntry> path = {})
      : root_(std::move(root)), path_(std::move(path)) {}

  const TomlValue* Value() const {
    if (!root_) {
      return nullptr;
    }
    return ResolvePath(*root_, path_);
  }

  sol::object At(sol::state_view lua, const sol::object& key) const {
    const TomlValue* value = Value();
    if (!value) {
      return sol::make_object(lua, sol::lua_nil);
    }

    auto entry = PathEntryFromLuaKey(key);
    if (!entry) {
      return sol::make_object(lua, sol::lua_nil);
    }

    if (entry->is_index) {
      if (!value->is_array()) {
        return sol::make_object(lua, sol::lua_nil);
      }

      const auto& array = value->as_array();
      if (entry->index >= array.size()) {
        return sol::make_object(lua, sol::lua_nil);
      }
    } else {
      if (!value->is_table()) {
        return sol::make_object(lua, sol::lua_nil);
      }

      const auto& table = value->as_table();
      if (table.find(entry->key) == table.end()) {
        return sol::make_object(lua, sol::lua_nil);
      }
    }

    auto child_path = path_;
    child_path.push_back(std::move(*entry));
    return PathToLuaValue(lua, std::move(child_path));
  }

  bool Has(const sol::object& path) const {
    auto suffix = PathFromLuaValue(path);
    if (!suffix) {
      return false;
    }

    auto full_path = path_;
    full_path.insert(full_path.end(), suffix->begin(), suffix->end());
    return root_ && ResolvePath(*root_, full_path) != nullptr;
  }

  sol::object Get(sol::state_view lua, const sol::object& path) const {
    auto suffix = PathFromLuaValue(path);
    if (!suffix) {
      return sol::make_object(lua, sol::lua_nil);
    }

    auto full_path = path_;
    full_path.insert(full_path.end(), suffix->begin(), suffix->end());
    if (!root_ || ResolvePath(*root_, full_path) == nullptr) {
      return sol::make_object(lua, sol::lua_nil);
    }
    return PathToLuaValue(lua, std::move(full_path));
  }

  sol::object GetOr(sol::state_view lua, const sol::object& path, const sol::object& fallback) const {
    auto suffix = PathFromLuaValue(path);
    if (!suffix) {
      return fallback;
    }

    auto full_path = path_;
    full_path.insert(full_path.end(), suffix->begin(), suffix->end());
    if (!root_ || ResolvePath(*root_, full_path) == nullptr) {
      return fallback;
    }
    return PathToLuaValue(lua, std::move(full_path));
  }

  sol::table Entries(sol::state_view lua) const {
    sol::table entries = lua.create_table();
    const TomlValue* value = Value();
    if (!value) {
      return entries;
    }

    if (value->is_table()) {
      for (const auto& entry : value->as_table()) {
        auto child_path = path_;
        child_path.push_back({entry.first, 0, false});
        entries[entry.first] = PathToLuaValue(lua, std::move(child_path));
      }
      return entries;
    }

    if (value->is_array()) {
      const auto& array = value->as_array();
      for (std::size_t index = 0; index < array.size(); ++index) {
        auto child_path = path_;
        child_path.push_back({{}, index, true});
        entries[static_cast<int>(index + 1)] = PathToLuaValue(lua, std::move(child_path));
      }
    }

    return entries;
  }

private:
  sol::object PathToLuaValue(sol::state_view lua, std::vector<TomlPathEntry> path) const {
    const TomlValue* value = root_ ? ResolvePath(*root_, path) : nullptr;
    if (!value) {
      return sol::make_object(lua, sol::lua_nil);
    }

    if (value->is_table() || value->is_array()) {
      return sol::make_object(lua, TomlFile(root_, std::move(path)));
    }
    if (value->is_boolean()) {
      return sol::make_object(lua, value->as_boolean());
    }
    if (value->is_integer()) {
      return sol::make_object(lua, static_cast<std::int64_t>(value->as_integer()));
    }
    if (value->is_floating()) {
      return sol::make_object(lua, value->as_floating());
    }
    if (value->is_string()) {
      return sol::make_object(lua, toml::get<std::string>(*value));
    }

    return sol::make_object(lua, TomlScalarToString(*value));
  }

  std::shared_ptr<const TomlValue> root_;
  std::vector<TomlPathEntry> path_;
};

std::optional<TomlFile> LoadTomlFile(const std::string& relative_path, std::string& error) {
  auto resolved = ResolveDataPath(relative_path);
  if (!resolved) {
    error = "Invalid TOML path";
    return std::nullopt;
  }

  std::error_code ec;
  if (!std::filesystem::exists(*resolved, ec)) {
    if (ec) {
      error = "Failed to stat TOML file: " + ec.message();
    }
    return std::nullopt;
  }

  const auto size = std::filesystem::file_size(*resolved, ec);
  if (ec) {
    error = "Failed to get TOML file size: " + ec.message();
    return std::nullopt;
  }
  if (size > kMaxTomlBytes) {
    error = "TOML file exceeds size limit";
    return std::nullopt;
  }

  TomlValue data;
  try {
    data = toml::parse<toml::ordered_type_config>(resolved->string());
  } catch (const std::exception& ex) {
    error = std::string("Failed to parse TOML: ") + ex.what();
    return std::nullopt;
  }

  if (!data.is_table()) {
    error = "TOML root must be a table";
    return std::nullopt;
  }

  std::size_t entries = 0;
  if (!ValidateTomlValue(data, error, 0, entries)) {
    return std::nullopt;
  }

  return TomlFile(std::make_shared<TomlValue>(std::move(data)));
}

}  // namespace

/* luagmp (func)
*
* Open a read-only TOML file relative to the server data directory.
*
* @version  0.3.0
* @name     TOML
* @side     server
* @category File
* @param    (string) relative_path  Path under the data directory.
* @return   (TomlFile|nil)          File handle or nil on error.
*
*/
sol::object Function_TOML(const std::string& relative_path, sol::this_state ts) {
  sol::state_view lua(ts);
  std::string error;
  auto file = LoadTomlFile(relative_path, error);
  if (!file) {
    if (!error.empty() && error != "Invalid TOML path") {
      SPDLOG_WARN("TOML open failed for '{}': {}", relative_path, error);
    }
    return sol::make_object(lua, sol::lua_nil);
  }
  return sol::make_object(lua, std::move(*file));
}

void BindToml(sol::state& lua) {
/* luagmp (class)
*
* Read-only TOML document returned by TOML(). Values can be accessed with
* normal field or index syntax, including nested sections. Use get_or when a
* missing value should fall back without replacing configured false.
*
* @version  0.3.0
* @name     TomlFile
* @side     server
* @category File
*
*/
  sol::table methods = lua.create_table();

/* luagmp (method)
*
* Return true when a key or nested path exists. A string path uses dots as
* separators; a table path can be used for dynamic or literal key segments.
*
* @name     has
* @param    (string|table|number) path  Key, dotted path, path table, or array index.
* @return   (bool)                      True if the value exists.
*
*/
  methods.set_function("has", [](const TomlFile& file, const sol::object& path) { return file.Has(path); });

/* luagmp (method)
*
* Return a value by key or nested path. Missing keys return nil.
*
* @name     get
* @param    (string|table|number) path  Key, dotted path, path table, or array index.
* @return   (any|nil)                   Value or nil when missing.
*
*/
  methods.set_function("get", [](const TomlFile& file, const sol::object& path, sol::this_state ts) {
    return file.Get(sol::state_view(ts), path);
  });

/* luagmp (method)
*
* Return a value by key or nested path, or the fallback only when the value is
* missing. This preserves configured false values.
*
* @name     get_or
* @param    (string|table|number) path  Key, dotted path, path table, or array index.
* @param    (any) fallback              Value returned when path is missing.
* @return   (any)                       Configured value or fallback.
*
*/
  methods.set_function("get_or", [](const TomlFile& file, const sol::object& path, const sol::object& fallback, sol::this_state ts) {
    return file.GetOr(sol::state_view(ts), path, fallback);
  });

/* luagmp (method)
*
* Return a Lua table containing the entries of this document, section, or array.
* The returned table is a snapshot; nested TOML sections remain read-only.
*
* @name     entries
* @return   (table) Entries table.
*
*/
  methods.set_function("entries", [](const TomlFile& file, sol::this_state ts) { return file.Entries(sol::state_view(ts)); });

  sol::usertype<TomlFile> toml_type = lua.new_usertype<TomlFile>("TomlFile", sol::no_constructor);
  toml_type[sol::meta_function::index] = [methods](const TomlFile& file, const sol::object& key, sol::this_state ts) -> sol::object {
    sol::state_view lua_state(ts);
    if (key.get_type() == sol::type::string) {
      const std::string key_name = key.as<std::string>();
      sol::object method = methods[key_name];
      if (method.valid() && method.get_type() != sol::type::nil) {
        return method;
      }
    }
    return file.At(lua_state, key);
  };
  toml_type[sol::meta_function::new_index] = [](sol::this_state ts, const TomlFile&, const sol::object&, const sol::object&) -> int {
    return luaL_error(ts, "TomlFile is read-only");
  };
  toml_type[sol::meta_function::pairs] = [](const TomlFile& file, sol::this_state ts) {
    sol::state_view lua_state(ts);
    sol::function next = lua_state["next"];
    return std::make_tuple(next, file.Entries(lua_state), sol::make_object(lua_state, sol::lua_nil));
  };

  lua["TOML"] = Function_TOML;
}

}  // namespace lua::bindings
