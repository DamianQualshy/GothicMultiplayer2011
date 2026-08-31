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

#include "item_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {

using Json = nlohmann::json;

std::string DescribePath(const std::filesystem::path& path) {
  return std::filesystem::absolute(path).string();
}

const Json& RequireField(const Json& object, const char* field, const std::string& context) {
  const auto it = object.find(field);
  if (it == object.end()) {
    throw std::runtime_error(context + " missing required field '" + field + "'");
  }
  return *it;
}

std::int32_t ReadRequiredInt32(const Json& object, const char* field, const std::string& context) {
  const Json& value = RequireField(object, field, context);
  if (!value.is_number_integer()) {
    throw std::runtime_error(context + " field '" + field + "' must be an integer");
  }
  const auto number = value.get<std::int64_t>();
  if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max()) {
    throw std::runtime_error(context + " field '" + field + "' is outside int32 range");
  }
  return static_cast<std::int32_t>(number);
}

std::optional<std::int32_t> ReadOptionalInt32(const Json& object, const char* field, const std::string& context) {
  const auto it = object.find(field);
  if (it == object.end()) {
    return std::nullopt;
  }
  if (!it->is_number_integer()) {
    throw std::runtime_error(context + " field '" + field + "' must be an integer");
  }
  const auto number = it->get<std::int64_t>();
  if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max()) {
    throw std::runtime_error(context + " field '" + field + "' is outside int32 range");
  }
  return static_cast<std::int32_t>(number);
}

std::string ReadRequiredString(const Json& object, const char* field, const std::string& context) {
  const Json& value = RequireField(object, field, context);
  if (!value.is_string()) {
    throw std::runtime_error(context + " field '" + field + "' must be a string");
  }
  return value.get<std::string>();
}

std::optional<std::string> ReadOptionalString(const Json& object, const char* field, const std::string& context) {
  const auto it = object.find(field);
  if (it == object.end()) {
    return std::nullopt;
  }
  if (!it->is_string()) {
    throw std::runtime_error(context + " field '" + field + "' must be a string");
  }
  return it->get<std::string>();
}

std::vector<ItemRegistry::Protection> ReadProtections(const Json& object, const std::string& context) {
  std::vector<ItemRegistry::Protection> protections;
  const auto it = object.find("protections");
  if (it == object.end()) {
    return protections;
  }
  if (!it->is_array()) {
    throw std::runtime_error(context + " field 'protections' must be an array");
  }

  protections.reserve(it->size());
  for (std::size_t i = 0; i < it->size(); ++i) {
    const Json& entry = (*it)[i];
    const std::string entry_context = context + " protections[" + std::to_string(i) + "]";
    if (!entry.is_object()) {
      throw std::runtime_error(entry_context + " must be an object");
    }
    protections.push_back(ItemRegistry::Protection{ReadRequiredInt32(entry, "type", entry_context),
                                                   ReadRequiredInt32(entry, "value", entry_context)});
  }
  return protections;
}

std::vector<ItemRegistry::Requirement> ReadRequirements(const Json& object, const std::string& context) {
  std::vector<ItemRegistry::Requirement> requirements;
  auto it = object.find("conditions");
  if (it == object.end()) {
    it = object.find("requirements");
  }
  if (it == object.end()) {
    return requirements;
  }
  if (!it->is_array()) {
    throw std::runtime_error(context + " field 'conditions' must be an array");
  }

  requirements.reserve(it->size());
  for (std::size_t i = 0; i < it->size(); ++i) {
    const Json& entry = (*it)[i];
    const std::string entry_context = context + " conditions[" + std::to_string(i) + "]";
    if (!entry.is_object()) {
      throw std::runtime_error(entry_context + " must be an object");
    }
    requirements.push_back(ItemRegistry::Requirement{ReadRequiredInt32(entry, "attribute", entry_context),
                                                     ReadRequiredInt32(entry, "value", entry_context)});
  }
  return requirements;
}

std::optional<ItemRegistry::Damage> ReadDamage(const Json& object, const std::string& context) {
  const auto it = object.find("damage");
  if (it == object.end()) {
    return std::nullopt;
  }
  if (!it->is_object()) {
    throw std::runtime_error(context + " field 'damage' must be an object");
  }

  const auto total = ReadOptionalInt32(*it, "total", context + " damage").value_or(0);
  const auto types = ReadOptionalInt32(*it, "types", context + " damage").value_or(0);
  if (total == 0 && types == 0) {
    throw std::runtime_error(context + " field 'damage' must contain a non-zero 'total' or 'types'");
  }
  return ItemRegistry::Damage{total, types};
}

ItemRegistry::Item ReadItem(const Json& object, std::size_t ordinal) {
  const std::string context = "items[" + std::to_string(ordinal) + "]";
  if (!object.is_object()) {
    throw std::runtime_error(context + " must be an object");
  }

  ItemRegistry::Item item;
  item.instance = ReadRequiredString(object, "instance", context);
  if (item.instance.empty()) {
    throw std::runtime_error(context + " field 'instance' must not be empty");
  }
  if (item.instance.size() > 255) {
    throw std::runtime_error(context + " field 'instance' must not exceed 255 characters");
  }

  item.index = ReadRequiredInt32(object, "index", context);
  if (item.index < 0) {
    throw std::runtime_error(context + " field 'index' must not be negative");
  }

  item.mainflag = ReadRequiredInt32(object, "mainflag", context);
  item.flags = ReadRequiredInt32(object, "flags", context);
  item.visual = ReadOptionalString(object, "visual", context).value_or(std::string{});
  item.wear = ReadOptionalInt32(object, "wear", context);
  item.range = ReadOptionalInt32(object, "range", context);
  item.value = ReadOptionalInt32(object, "value", context);
  item.damage = ReadDamage(object, context);
  item.munition = ReadOptionalString(object, "munition", context);
  item.spell = ReadOptionalInt32(object, "spell", context);
  item.scemename = ReadOptionalString(object, "scemename", context).value_or(std::string{});
  item.mag_circle = ReadOptionalInt32(object, "mag_circle", context);
  item.protections = ReadProtections(object, context);
  item.requirements = ReadRequirements(object, context);
  return item;
}

}  // namespace

bool ItemRegistry::Load(const std::filesystem::path& path) {
  Clear();

  try {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
      SPDLOG_CRITICAL("Required item registry '{}' is missing or cannot be opened.", DescribePath(path));
      return false;
    }

    Json root = Json::parse(input);
    if (!root.is_object()) {
      throw std::runtime_error("root must be an object");
    }

    const Json& items = RequireField(root, "items", "root");
    if (!items.is_array()) {
      throw std::runtime_error("root field 'items' must be an array");
    }
    if (items.empty()) {
      throw std::runtime_error("root field 'items' must not be empty");
    }

    std::unordered_map<std::int32_t, std::string> by_index;
    std::vector<Item> loaded_items;
    loaded_items.reserve(items.size());

    for (std::size_t i = 0; i < items.size(); ++i) {
      Item item = ReadItem(items[i], i);
      const std::string normalized = NormalizeInstanceName(item.instance);
      if (normalized.empty()) {
        throw std::runtime_error("items[" + std::to_string(i) + "] normalized instance is empty");
      }
      if (by_instance_.contains(normalized)) {
        throw std::runtime_error("duplicate item instance '" + item.instance + "'");
      }
      const auto [index_it, inserted] = by_index.emplace(item.index, item.instance);
      if (!inserted) {
        throw std::runtime_error("duplicate item index " + std::to_string(item.index) + " used by '" + index_it->second + "' and '" +
                                 item.instance + "'");
      }

      by_instance_.emplace(normalized, loaded_items.size());
      by_index_.emplace(item.index, loaded_items.size());
      loaded_items.push_back(std::move(item));
    }

    for (const Item& item : loaded_items) {
      if (item.munition.has_value() && !by_instance_.contains(NormalizeInstanceName(*item.munition))) {
        throw std::runtime_error("item '" + item.instance + "' references unknown munition '" + *item.munition + "'");
      }
    }

    items_ = std::move(loaded_items);
    SPDLOG_INFO("Loaded {} item definitions from {}.", items_.size(), DescribePath(path));
    return true;
  } catch (const nlohmann::json::exception& ex) {
    Clear();
    SPDLOG_CRITICAL("Failed to parse required item registry '{}': {}", DescribePath(path), ex.what());
    return false;
  } catch (const std::exception& ex) {
    Clear();
    SPDLOG_CRITICAL("Failed to load required item registry '{}': {}", DescribePath(path), ex.what());
    return false;
  }
}

const ItemRegistry::Item* ItemRegistry::Find(std::string_view instance) const {
  const std::string normalized = NormalizeInstanceName(instance);
  const auto it = by_instance_.find(normalized);
  if (it == by_instance_.end()) {
    return nullptr;
  }
  return &items_[it->second];
}

const ItemRegistry::Item* ItemRegistry::FindByIndex(std::int32_t index) const {
  const auto it = by_index_.find(index);
  if (it == by_index_.end()) {
    return nullptr;
  }
  return &items_[it->second];
}

std::optional<std::string> ItemRegistry::CanonicalizeInstance(std::string_view instance) const {
  const Item* item = Find(instance);
  if (!item) {
    return std::nullopt;
  }
  return item->instance;
}

bool ItemRegistry::Contains(std::string_view instance) const {
  return Find(instance) != nullptr;
}

bool ItemRegistry::ContainsIndex(std::int32_t index) const {
  return FindByIndex(index) != nullptr;
}

std::size_t ItemRegistry::Size() const {
  return items_.size();
}

const std::vector<ItemRegistry::Item>& ItemRegistry::Items() const {
  return items_;
}

std::string ItemRegistry::NormalizeInstanceName(std::string_view instance) {
  std::string normalized(instance);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return normalized;
}

void ItemRegistry::Clear() {
  items_.clear();
  by_instance_.clear();
  by_index_.clear();
}
